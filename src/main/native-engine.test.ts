import { EventEmitter } from 'node:events';
import { PassThrough } from 'node:stream';
import { describe, expect, it, vi } from 'vitest';
import { NativeEngine, JsonLines, MAX_NATIVE_LINE } from './native-engine';
function fake() {
  const child = Object.assign(new EventEmitter(), {
    stdin: new PassThrough(),
    stdout: new PassThrough(),
    stderr: new PassThrough(),
    kill: vi.fn(),
  });
  const launch = vi.fn(() => child);
  const engine = new NativeEngine('C:/native', 'C:/cache', launch as never);
  return { child, launch, engine };
}
describe('native process supervision', () => {
  it('decodes fragmented frames and rejects oversized unterminated input', () => {
    const decoder = new JsonLines();
    expect(decoder.accept('{"a":')).toEqual([]);
    expect(decoder.accept('1}\n{"b":2}\n')).toEqual(['{"a":1}', '{"b":2}']);
    expect(() => decoder.accept('x'.repeat(MAX_NATIVE_LINE + 1))).toThrow(
      /limit/,
    );
  });
  it('validates before launch and never forwards arbitrary commands', async () => {
    const { engine, launch } = fake();
    await expect(
      engine.request('offer', { peer: 'invalid' }),
    ).rejects.toThrow();
    expect(launch).not.toHaveBeenCalled();
  });
  it('correlates replies and rejects all pending work when the process exits', async () => {
    const { engine, child } = fake();
    const first = engine.request('sources', {});
    child.stdout.write('{"v":1,"event":"ready","protocol":1}\n');
    await new Promise((resolve) => setTimeout(resolve, 0));
    child.stdout.write('{"v":1,"id":1,"ok":true,"result":[]}\n');
    expect(await first).toEqual([]);
    const second = engine.request('stats', {});
    const rejected = expect(second).rejects.toThrow(/exited/);
    await new Promise((resolve) => setTimeout(resolve, 0));
    child.emit('exit', 9, null);
    await rejected;
  });
  it('rejects malformed native results instead of exposing them to React', async () => {
    const { engine, child } = fake();
    const pending = engine.request('sources', {});
    const rejected = expect(pending).rejects.toThrow(/Invalid native result/);
    child.stdout.write('{"v":1,"event":"ready","protocol":1}\n');
    await new Promise((resolve) => setTimeout(resolve, 0));
    child.stdout.write('{"v":1,"id":1,"ok":true,"result":"unexpected"}\n');
    await rejected;
    engine.shutdown();
    child.emit('exit', 0);
  });
});
