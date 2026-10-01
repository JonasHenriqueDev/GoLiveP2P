import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { join, resolve } from 'node:path';
const runtime = fileURLToPath(new URL('./runtime/', import.meta.url));
const child = spawn(join(runtime, 'media-engine.exe'), [], {
  windowsHide: true,
  env: {
    ...process.env,
    PATH: join(runtime, 'bin') + ';' + process.env.PATH,
    GST_PLUGIN_PATH_1_0: '',
    GST_PLUGIN_SYSTEM_PATH_1_0: join(runtime, 'lib/gstreamer-1.0'),
    GST_REGISTRY_1_0: resolve('work/policy-registry.bin'),
  },
  stdio: ['pipe', 'pipe', 'pipe'],
});
const timer = setTimeout(() => {
  child.kill();
  process.exitCode = 1;
}, 20000);
let text = '';
child.on('error', (error) => {
  clearTimeout(timer);
  console.error(error);
  process.exitCode = 1;
});
child.stdout.on('data', (chunk) => {
  text += chunk;
  let end;
  while ((end = text.indexOf('\n')) >= 0) {
    const message = JSON.parse(text.slice(0, end));
    text = text.slice(end + 1);
    if (message.event === 'ready')
      child.stdin.write(
        JSON.stringify({ v: 1, id: 1, method: 'policy-test', data: {} }) + '\n',
      );
    if (message.id === 1) {
      clearTimeout(timer);
      if (!message.ok || message.result.passed !== 5) process.exitCode = 1;
      console.log(message);
      child.stdin.end();
    }
  }
});
child.stderr.on('data', (chunk) => process.stderr.write(chunk));
