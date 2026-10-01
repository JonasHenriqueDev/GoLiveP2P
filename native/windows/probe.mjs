import { spawn } from 'node:child_process';
import { join, resolve } from 'node:path';
import { mkdirSync, writeFileSync } from 'node:fs';
import { randomUUID } from 'node:crypto';
import { fileURLToPath } from 'node:url';
const runtime = fileURLToPath(new URL('./runtime/', import.meta.url));
const work = resolve('work/native-probe');
mkdirSync(work, { recursive: true });
const report = {
  tests: [],
  errors: [],
  states: [],
  frames: {},
  runtime: null,
  sources: null,
};
let fixture = null,
  fixtureSource = null,
  fixtureStage = 'first';
if (process.argv.includes('--fixture')) {
  fixture = spawn(resolve('work/native-build/media-fixture.exe'), [], {
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  fixtureSource = await new Promise((resolve, reject) => {
    fixture.on('error', reject);
    fixture.stdout.on('data', (chunk) => {
      const text = chunk.toString();
      console.log('fixture', text.trim());
      if (text.startsWith('{')) resolve(JSON.parse(text.split('\n')[0]).window);
      if (text.includes('covered')) fixtureStage = 'covered';
      if (text.includes('resized')) fixtureStage = 'resized';
    });
  });
}
function engine(name) {
  const child = spawn(join(runtime, 'media-engine.exe'), [], {
    windowsHide: true,
    env: {
      ...process.env,
      PATH: join(runtime, 'bin') + ';' + process.env.PATH,
      GST_PLUGIN_PATH_1_0: '',
      GST_PLUGIN_SYSTEM_PATH_1_0: join(runtime, 'lib/gstreamer-1.0'),
      GST_PLUGIN_SCANNER_1_0: join(
        runtime,
        'libexec/gstreamer-1.0/gst-plugin-scanner.exe',
      ),
      GST_REGISTRY_1_0: join(work, `${name}-registry.bin`),
    },
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  let sequence = 0,
    buffer = '';
  const pending = new Map();
  const api = {
    child,
    signal: () => {},
    ready: null,
    request: null,
    close: () => child.stdin.end(),
  };
  api.ready = new Promise((resolve, reject) => {
    const timeout = setTimeout(
      () => reject(new Error(`${name} startup timeout`)),
      30000,
    );
    child.stdout.on('data', (chunk) => {
      buffer += chunk.toString();
      let end;
      while ((end = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, end);
        buffer = buffer.slice(end + 1);
        const value = JSON.parse(line);
        if (value.event === 'ready') {
          clearTimeout(timeout);
          resolve();
        }
        if (value.event === 'frame') {
          const key = name + ':' + value.peer;
          report.frames[key] = (report.frames[key] || 0) + 1;
          if (report.frames[key] === 1 || fixtureStage !== 'first')
            writeFileSync(
              join(work, `${name}-${fixtureStage}.jpg`),
              Buffer.from(value.jpeg, 'base64'),
            );
        }
        if (value.event === 'audio-state') {
          report.states.push({ name, ...value });
          console.log(name, 'audio', value.active, 'sources', value.sources);
        }
        if (value.event === 'error' || value.event === 'warning') {
          report.errors.push({ name, ...value });
          console.error(name, value.message);
        }
        if (value.event === 'state') {
          report.states.push({ name, ...value });
          console.log(name, value.state);
        }
        if (value.event === 'signal') {
          if (value.sdp)
            writeFileSync(join(work, `${name}-${value.type}.sdp`), value.sdp);
          api.signal(value);
        }
        if (value.id && pending.has(value.id)) {
          const task = pending.get(value.id);
          clearTimeout(task.timer);
          pending.delete(value.id);
          if (value.ok) task.resolve(value.result);
          else task.reject(new Error(value.error));
        }
      }
    });
    child.stderr.on('data', (chunk) =>
      writeFileSync(join(work, `${name}-stderr.log`), chunk, { flag: 'a' }),
    );
    child.on('error', (error) => {
      clearTimeout(timeout);
      reject(error);
    });
    child.on('exit', (code) => {
      clearTimeout(timeout);
      for (const task of pending.values()) {
        clearTimeout(task.timer);
        task.reject(new Error(`${name} exited ${code}`));
      }
      pending.clear();
    });
  });
  api.request = (method, data = {}) =>
    new Promise((resolve, reject) => {
      const id = ++sequence;
      const timer = setTimeout(() => {
        pending.delete(id);
        reject(new Error(`${method} timeout`));
      }, 20000);
      pending.set(id, { resolve, reject, timer });
      child.stdin.write(JSON.stringify({ v: 1, id, method, data }) + '\n');
    });
  return api;
}
const sender = engine('sender'),
  receiver = engine('receiver');
const senderId = randomUUID(),
  receiverId = randomUUID();
let signalChain = Promise.resolve();
sender.signal = (value) => {
  signalChain = signalChain
    .then(() =>
      receiver.request('signal', {
        type: value.type,
        peer: senderId,
        ...(value.sdp ? { sdp: value.sdp } : { candidate: value.candidate }),
      }),
    )
    .catch((error) => report.errors.push({ signal: String(error) }));
};
receiver.signal = (value) => {
  signalChain = signalChain
    .then(() =>
      sender.request('signal', {
        type: value.type,
        peer: receiverId,
        ...(value.sdp ? { sdp: value.sdp } : { candidate: value.candidate }),
      }),
    )
    .catch((error) => report.errors.push({ signal: String(error) }));
};
try {
  await Promise.all([sender.ready, receiver.ready]);
  report.runtime = await sender.request('capabilities');
  console.log(report.runtime);
  report.sources = await sender.request('sources');
  console.log(report.sources);
  report.audioSessions = await sender.request('audio-sessions');
  const selected =
    process.env.GOLIVE_TEST_SOURCE ||
    fixtureSource ||
    report.sources.find((source) => source.kind === 'screen')?.id;
  const settings = {
    source: selected,
    method: process.env.GOLIVE_TEST_METHOD || (fixtureSource ? 'wgc' : 'dxgi'),
    encoder: process.env.GOLIVE_TEST_ENCODER || 'auto',
    width: 1920,
    height: 1080,
    fps: 60,
    bitrate: 6000000,
    audio: process.env.GOLIVE_TEST_AUDIO === '1',
    allowedAudioApps: fixture
      ? [resolve('work/native-build/media-fixture.exe').toLowerCase()]
      : [],
  };
  console.log('capture', await sender.request('start', settings));
  const initial = await sender.request('stats');
  const start = performance.now();
  await sender.request('offer', { peer: receiverId });
  await new Promise((resolve) => setTimeout(resolve, fixture ? 18000 : 12000));
  const stats = await sender.request('stats');
  report.tests.push({
    name: '1080p60 native capture and local native WebRTC receiver',
    settings,
    seconds: (performance.now() - start) / 1000,
    encodedFrames: stats.encodedFrames - initial.encodedFrames,
    stats,
    receiverStats: await receiver.request('stats'),
  });
  await sender.request('bitrate', { bitrate: 2000000 });
  await new Promise((resolve) => setTimeout(resolve, 2000));
  report.tests.push({
    name: 'live bitrate change',
    stats: await sender.request('stats'),
  });
  await sender.request('remove', { peer: receiverId });
  await sender.request('offer', { peer: receiverId });
  await new Promise((resolve) => setTimeout(resolve, 4000));
  report.tests.push({
    name: 'peer recreation',
    stats: await receiver.request('stats'),
  });
  await sender.request('stop');
  await receiver.request('stop');
  report.tests.push({ name: 'stop', stats: await sender.request('stats') });
} catch (error) {
  report.errors.push({ fatal: String(error) });
  console.error(error);
  process.exitCode = 1;
} finally {
  sender.close();
  receiver.close();
  fixture?.kill();
  setTimeout(() => {
    sender.child.kill();
    receiver.child.kill();
  }, 2000).unref();
  writeFileSync(join(work, 'report.json'), JSON.stringify(report, null, 2));
  console.log('Frames', report.frames, 'Report', join(work, 'report.json'));
}
