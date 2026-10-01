import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { join } from 'node:path';
import {
  nativeEvent,
  nativeRequests,
  nativeResponse,
  nativeResults,
  type NativeData,
  type NativeEvent,
  type NativeMethod,
  type NativeResults,
} from '../shared/native-media';

export const MAX_NATIVE_LINE = 3_000_000;
export class JsonLines {
  private buffer = '';
  accept(chunk: string): string[] {
    this.buffer += chunk;
    const lines: string[] = [];
    let end: number;
    while ((end = this.buffer.indexOf('\n')) >= 0) {
      if (end > MAX_NATIVE_LINE)
        throw new Error('Native IPC frame exceeded limit');
      lines.push(this.buffer.slice(0, end));
      this.buffer = this.buffer.slice(end + 1);
    }
    if (this.buffer.length > MAX_NATIVE_LINE)
      throw new Error('Native IPC frame exceeded limit');
    return lines;
  }
}
type Pending = {
  method: NativeMethod;
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
};
export class NativeEngine {
  private child: ChildProcessWithoutNullStreams | null = null;
  private pending = new Map<number, Pending>();
  private sequence = 0;
  private ready: Promise<void> | null = null;
  private stopping = false;
  onEvent: (event: NativeEvent) => void = () => {};
  constructor(
    private directory: string,
    private cache: string,
    private launch = spawn,
  ) {}
  private async ensureStarted() {
    if (this.ready) return this.ready;
    this.stopping = false;
    const child = this.launch(join(this.directory, 'media-engine.exe'), [], {
      windowsHide: true,
      stdio: ['pipe', 'pipe', 'pipe'],
      env: {
        ...process.env,
        PATH: join(this.directory, 'bin') + ';' + process.env.PATH,
        GST_PLUGIN_PATH_1_0: '',
        GST_PLUGIN_SYSTEM_PATH_1_0: join(
          this.directory,
          'lib',
          'gstreamer-1.0',
        ),
        GST_PLUGIN_SCANNER_1_0: join(
          this.directory,
          'libexec',
          'gstreamer-1.0',
          'gst-plugin-scanner.exe',
        ),
        GST_REGISTRY_1_0: join(this.cache, 'golive-gst-registry.bin'),
      },
    });
    this.child = child;
    const lines = new JsonLines();
    this.ready = new Promise<void>((resolve, reject) => {
      const timer = setTimeout(() => {
        reject(new Error('Native engine startup timeout'));
        this.shutdown();
      }, 20_000);
      child.stdout.setEncoding('utf8');
      child.stdout.on('data', (chunk: string) => {
        try {
          for (const line of lines.accept(chunk)) {
            const value: unknown = JSON.parse(line);
            const event = nativeEvent.safeParse(value);
            if (event.success) {
              if (event.data.event === 'ready') {
                clearTimeout(timer);
                resolve();
              }
              this.onEvent(event.data);
            } else {
              const response = nativeResponse.parse(value);
              if (response.id === null)
                throw new Error(
                  response.ok ? 'Invalid response' : response.error,
                );
              const pending = this.pending.get(response.id);
              if (!pending) continue;
              clearTimeout(pending.timer);
              this.pending.delete(response.id);
              if (!response.ok) pending.reject(new Error(response.error));
              else {
                const parsed = nativeResults[pending.method].safeParse(
                  response.result,
                );
                if (parsed.success) pending.resolve(parsed.data);
                else
                  pending.reject(
                    new Error('Invalid native result: ' + parsed.error.message),
                  );
              }
            }
          }
        } catch (error) {
          reject(error);
          this.onEvent({
            v: 1,
            event: 'error',
            message: 'Native protocol failure: ' + String(error),
          });
          this.shutdown();
        }
      });
      child.stderr.setEncoding('utf8');
      child.stderr.on('data', (text: string) =>
        console.warn('[Native]', text.slice(0, 4000)),
      );
      child.on('error', (error) => {
        clearTimeout(timer);
        reject(error);
        this.shutdown();
      });
      child.on('exit', (code, signal) => {
        clearTimeout(timer);
        const error = new Error(`Native engine exited (${code ?? signal})`);
        reject(error);
        for (const pending of this.pending.values()) {
          clearTimeout(pending.timer);
          pending.reject(error);
        }
        this.pending.clear();
        if (this.child === child) {
          this.child = null;
          this.ready = null;
        }
        if (!this.stopping)
          this.onEvent({
            v: 1,
            event: 'error',
            peer: 'local',
            message: error.message,
          });
      });
    });
    return this.ready;
  }
  async request<M extends NativeMethod>(
    method: M,
    data: NativeData<M>,
  ): Promise<NativeResults[M]> {
    const parsed = nativeRequests[method].parse(data);
    await this.ensureStarted();
    if (this.pending.size >= 128) throw new Error('Native IPC queue full');
    const id = ++this.sequence;
    return new Promise<NativeResults[M]>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Native request timed out: ${method}`));
        this.shutdown();
      }, 15_000);
      this.pending.set(id, {
        method,
        timer,
        resolve: (value) => resolve(value as NativeResults[M]),
        reject,
      });
      this.child!.stdin.write(
        JSON.stringify({ v: 1, id, method, data: parsed }) + '\n',
        (error) => {
          if (error) {
            clearTimeout(timer);
            this.pending.delete(id);
            reject(error);
          }
        },
      );
    });
  }
  shutdown() {
    this.stopping = true;
    const child = this.child;
    this.child = null;
    this.ready = null;
    child?.stdin.end();
    // EOF is the graceful exit path. The deadline also covers stalled driver calls.
    if (child) {
      const timer = setTimeout(() => child.kill(), 2000);
      timer.unref();
      child.once('exit', () => clearTimeout(timer));
    }
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(new Error('Native engine stopped'));
    }
    this.pending.clear();
  }
}
