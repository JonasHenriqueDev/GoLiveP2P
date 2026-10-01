import { spawn, execFileSync } from 'node:child_process';
import { once } from 'node:events';
import { join, resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const runtime = resolve('native/windows/runtime');
const fixture = spawn(resolve('work/native-build/media-fixture.exe'), [], {
  windowsHide: true,
});
const engine = spawn(join(runtime, 'media-engine.exe'), [], {
  windowsHide: true,
  env: {
    ...process.env,
    PATH: join(runtime, 'bin') + ';' + process.env.PATH,
    GST_PLUGIN_PATH_1_0: '',
    GST_PLUGIN_SYSTEM_PATH_1_0: join(runtime, 'lib/gstreamer-1.0'),
    GST_REGISTRY_1_0: resolve('work/lifecycle-registry.bin'),
  },
});
let sequence = 0,
  buffer = '';
const pending = new Map();
const deadline = setTimeout(() => {
  engine.kill();
  fixture.kill();
  process.exitCode = 1;
}, 25000);
let readyResolve;
let failureResolve;
const failed = new Promise((resolve) => {
  failureResolve = resolve;
});
const ready = new Promise((resolve) => {
  readyResolve = resolve;
});
engine.stdout.on('data', (chunk) => {
  buffer += chunk;
  let end;
  while ((end = buffer.indexOf('\n')) >= 0) {
    const message = JSON.parse(buffer.slice(0, end));
    buffer = buffer.slice(end + 1);
    if (message.event === 'ready') readyResolve();
    if (message.event === 'error' && message.peer === 'local')
      failureResolve(message);
    if (pending.has(message.id)) {
      pending.get(message.id)(message);
      pending.delete(message.id);
    }
  }
});
engine.stderr.resume();
fixture.stderr.resume();
function request(method, data = {}) {
  return new Promise((resolve) => {
    const id = ++sequence;
    pending.set(id, resolve);
    engine.stdin.write(JSON.stringify({ v: 1, id, method, data }) + '\n');
  });
}
try {
  const [chunk] = await once(fixture.stdout, 'data');
  const source = JSON.parse(chunk.toString().split('\n')[0]).window;
  await ready;
  const capture = await request('start', {
    source,
    method: 'printwindow',
    encoder: 'auto',
    width: 1920,
    height: 1080,
    fps: 60,
    bitrate: 6000000,
    audio: false,
    allowedAudioApps: [],
  });
  assert.equal(capture.ok, true);
  const started = Date.now();
  const frozen = process.argv.includes('--freeze-helper');
  const action = frozen
    ? `Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class CaptureTestThreads { [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint access, bool inherit, uint id); [DllImport("kernel32.dll")] public static extern uint SuspendThread(IntPtr handle); [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle); }'; $ownedHelper | ForEach-Object { (Get-Process -Id $_.ProcessId).Threads | ForEach-Object { $threadHandle = [CaptureTestThreads]::OpenThread(2, $false, $_.Id); if ($threadHandle -eq [IntPtr]::Zero) { throw 'Cannot suspend test helper' }; try { if ([CaptureTestThreads]::SuspendThread($threadHandle) -eq [uint32]::MaxValue) { throw 'Suspend failed' } } finally { [void][CaptureTestThreads]::CloseHandle($threadHandle) } } }`
    : '$ownedHelper | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }';
  execFileSync(
    'powershell.exe',
    [
      '-NoProfile',
      '-Command',
      `$ownedHelper = Get-CimInstance Win32_Process -Filter "ParentProcessId=${engine.pid} AND Name='window-capture.exe'"; if (!$ownedHelper) { throw 'Owned helper missing' }; ${action}`,
    ],
    { windowsHide: true },
  );
  const failure = await failed;
  const elapsedMs = Date.now() - started;
  assert.ok(elapsedMs < 8000, 'Capture timeout must keep IPC responsive');
  assert.equal(
    (await request('capabilities')).ok,
    true,
    'Engine must remain responsive',
  );
  assert.equal((await request('stop')).ok, true);
  const restarted = await request('start', {
    source,
    method: 'printwindow',
    encoder: 'auto',
    width: 1920,
    height: 1080,
    fps: 60,
    bitrate: 6000000,
    audio: false,
    allowedAudioApps: [],
  });
  assert.equal(restarted.ok, true, 'Capture must restart after helper failure');
  assert.equal((await request('stop')).ok, true);
  const report = {
    scenario: frozen
      ? 'owned capture helper threads suspended'
      : 'unexpected exit of owned window capture helper',
    elapsedMs,
    capture,
    failure,
    engineResponsive: true,
    captureRestarted: true,
  };
  writeFileSync(
    frozen
      ? 'work/native-results/window-frozen-helper.json'
      : 'work/native-results/window-hang.json',
    JSON.stringify(report, null, 2),
  );
  console.log(report);
} finally {
  clearTimeout(deadline);
  fixture.kill();
  engine.stdin.end();
}
