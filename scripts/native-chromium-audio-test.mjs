// Windows integration test: real process audio + C++ WebRTC -> the Linux Mesh
// receiver running in Chromium. Only the dedicated fixture is captured.
// Prerequisites: npm ci && npm run native:build
// Optional regression check: --receiver-ref=v0.5.0-native.1
import { spawn, execFileSync } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { createServer } from 'node:http';
import {
  mkdirSync,
  readFileSync,
  writeFileSync,
  existsSync,
  copyFileSync,
} from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import ts from 'typescript';
import WebSocket from 'ws';

if (process.platform !== 'win32')
  throw new Error('The native sender requires Windows');
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const reference = process.argv
  .find((value) => value.startsWith('--receiver-ref='))
  ?.split('=')[1];
const chromeSource = process.argv.includes('--chrome-source');
const unrelatedChrome = process.argv.includes('--unrelated-chrome');
const blockedFixture = process.argv.includes('--blocked-fixture');
if (reference && !/^[A-Za-z0-9_./-]+$/.test(reference))
  throw new Error('Invalid receiver Git reference');
const work = join(
  root,
  'work/native-chromium-audio',
  new Date().toISOString().replace(/[:.]/g, '-'),
);
// Also exercise the exact DLLs and executable extracted from the installer.
const runtimeArgument = process.argv.find((value) => value.startsWith('--runtime='));
const runtime = runtimeArgument
  ? resolve(runtimeArgument.slice('--runtime='.length))
  : join(root, 'native/windows/runtime');
const fixturePath = join(root, 'work/native-build/media-fixture.exe');
mkdirSync(work, { recursive: true });
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const report = {
  sourceKind: chromeSource
    ? 'Chrome with simultaneous unrelated fixture tone'
    : unrelatedChrome
      ? 'Win32 fixture with simultaneous unrelated Chrome tone'
      : 'Win32 fixture',
  receiverRef: reference || 'working tree',
  runtime,
  errors: [],
  audioStates: [],
  states: [],
  samples: [],
  nativeFrames: 0,
};
const children = [];
let fixture,
  native,
  browser,
  socket,
  server,
  signalChain = Promise.resolve();
let requestSequence = 0;
const pending = new Map();
const receiverId = randomUUID();
const senderId = randomUUID();

function childProcess(executable, args, options = {}) {
  const child = spawn(executable, args, {
    cwd: root,
    windowsHide: true,
    stdio: ['pipe', 'pipe', 'pipe'],
    ...options,
  });
  children.push(child);
  child.stderr.on('data', (data) =>
    writeFileSync(
      join(work, `${options.logName || 'process'}-stderr.log`),
      data,
      { flag: 'a' },
    ),
  );
  child.on('error', (error) =>
    report.errors.push({ process: executable, error: String(error) }),
  );
  child.on('exit', (code, signal) => {
    report.processExits ??= [];
    report.processExits.push({ process: executable, code, signal });
  });
  return child;
}

function engineRequest(method, data = {}) {
  return new Promise((resolve, reject) => {
    const id = ++requestSequence;
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`Native ${method} timed out`));
    }, 20000);
    pending.set(id, { resolve, reject, timer });
    native.stdin.write(JSON.stringify({ v: 1, id, method, data }) + '\n');
  });
}

let cdpSequence = 0;
const cdpPending = new Map();
function cdp(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++cdpSequence;
    const timer = setTimeout(() => {
      cdpPending.delete(id);
      reject(new Error(`CDP ${method} timed out`));
    }, 15000);
    cdpPending.set(id, { resolve, reject, timer });
    socket.send(JSON.stringify({ id, method, params }));
  });
}
async function evaluate(expression) {
  const result = await cdp('Runtime.evaluate', {
    expression,
    awaitPromise: true,
    returnByValue: true,
  });
  if (result.exceptionDetails)
    throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}

async function sample() {
  await signalChain;
  const messages = await evaluate('window.test.drain()');
  for (const message of messages) {
    if (message.sdp) {
      writeFileSync(join(work, `chromium-${message.type}.sdp`), message.sdp);
      report.answer = message.sdp;
    }
    if (message.type === 'answer' || message.type === 'ice-candidate')
      await engineRequest('signal', {
        peer: receiverId,
        type: message.type,
        ...(message.sdp
          ? { sdp: message.sdp }
          : { candidate: message.candidate }),
      });
    else report.errors.push({ unexpectedSignal: message });
  }
  report.samples.push(await evaluate('window.test.stats()'));
}

try {
  for (const path of [fixturePath, join(runtime, 'media-engine.exe')])
    if (!existsSync(path))
      throw new Error(`Missing ${path}; run npm run native:build`);

  const assets = new Map();
  for (const name of ['mesh', 'presets']) {
    const relative = `src/services/webrtc/${name}.ts`;
    const source = reference
      ? execFileSync('git', ['show', `${reference}:${relative}`], {
          cwd: root,
          encoding: 'utf8',
        })
      : readFileSync(join(root, relative), 'utf8');
    const output = ts
      .transpileModule(source, {
        compilerOptions: {
          target: ts.ScriptTarget.ES2022,
          module: ts.ModuleKind.ES2022,
        },
      })
      .outputText.replaceAll("'./presets'", "'./presets.js'")
      .replaceAll('"./presets"', '"./presets.js"');
    assets.set(`/${name}.js`, { type: 'text/javascript', content: output });
  }
  const renderer = `<!doctype html><meta charset="utf-8"><title>GoLive audio integration</title><video autoplay playsinline width="960" height="540"></video><script type="module">
import { Mesh } from './mesh.js';
const video = document.querySelector('video');
const context = new AudioContext({sampleRate:48000});
const analyser = context.createAnalyser(), gain = context.createGain();
analyser.fftSize=2048; gain.gain.value=0; analyser.connect(gain).connect(context.destination);
const outbox=[], changes=[], states=[], errors=[]; let priorStream=null,maxRms=0,source=null,max440=0,max660=0;
const mesh = new Mesh(message=>outbox.push(message));
mesh.onRemote=stream=>{changes.push({time:performance.now(),tracks:stream?.getTracks().map(t=>({id:t.id,kind:t.kind,muted:t.muted,readyState:t.readyState}))});if(stream!==priorStream){video.srcObject=stream;priorStream=stream;source?.disconnect();source=null;}if(!source&&video.srcObject?.getAudioTracks().length){source=context.createMediaStreamSource(video.srcObject);source.connect(analyser);}video.play().catch(error=>errors.push(String(error)));};
mesh.onState=(peer,state)=>states.push({peer,state});
const buffer = new Float32Array(analyser.fftSize);
const spectrum = new Float32Array(analyser.frequencyBinCount);
setInterval(()=>{analyser.getFloatTimeDomainData(buffer);maxRms=Math.max(maxRms,Math.sqrt(buffer.reduce((sum,value)=>sum+value*value,0)/buffer.length));analyser.getFloatFrequencyData(spectrum);const band=hz=>{const index=Math.round(hz*analyser.fftSize/context.sampleRate);return Math.max(...Array.from(spectrum.slice(index-1,index+2),db=>10**(db/20)));};max440=Math.max(max440,band(440));max660=Math.max(max660,band(660));},20);
await context.resume();
window.test={receive:message=>mesh.handle(message),drain:()=>outbox.splice(0),stop:()=>mesh.stop(),frame:()=>{const canvas=document.createElement('canvas');canvas.width=video.videoWidth;canvas.height=video.videoHeight;canvas.getContext('2d').drawImage(video,0,0);return canvas.toDataURL('image/png').split(',')[1];},stats:async()=>{
const inbound=[], connections=[];
for(const [peer,pc] of mesh.connections){connections.push({peer,state:pc.connectionState,ice:pc.iceConnectionState});for(const stat of (await pc.getStats()).values())if(stat.type==='inbound-rtp'||stat.type==='codec')inbound.push(stat);}
const quality=video.getVideoPlaybackQuality();
return {time:performance.now(),connections,inbound,changes,states,errors,maxRms,spectrum:{tone440:max440,tone660:max660},audioContext:context.state,video:{width:video.videoWidth,height:video.videoHeight,decoded:quality.totalVideoFrames,dropped:quality.droppedVideoFrames,paused:video.paused,muted:video.muted,volume:video.volume,tracks:video.srcObject?.getTracks().map(t=>({kind:t.kind,enabled:t.enabled,muted:t.muted,readyState:t.readyState}))}};}};
</script>`;
  assets.set('/', { type: 'text/html', content: renderer });
  assets.set('/chrome-source', {
    type: 'text/html',
    content: `<!doctype html><title>GoLive exclusive Chrome audio test</title><h1>GoLive: Chrome 660 Hz</h1><p>Only this application's audio should be transmitted; the unrelated fixture plays 440 Hz.</p><script>const context=new AudioContext();const oscillator=context.createOscillator(),gain=context.createGain();oscillator.frequency.value=660;gain.gain.value=0.04;oscillator.connect(gain).connect(context.destination);oscillator.start();context.resume();</script>`,
  });
  server = createServer((request, response) => {
    const asset = assets.get(request.url);
    if (!asset) {
      response.writeHead(404).end();
      return;
    }
    response
      .writeHead(200, {
        'Content-Type': asset.type,
        'Cache-Control': 'no-store',
      })
      .end(asset.content);
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const url = `http://127.0.0.1:${server.address().port}/`;
  const mainPath = join(work, 'electron-main.cjs');
  writeFileSync(
    mainPath,
    `const {app,BrowserWindow}=require('electron');
app.setPath('userData',${JSON.stringify(join(work, 'profile'))});
app.commandLine.appendSwitch('autoplay-policy','no-user-gesture-required');
app.commandLine.appendSwitch('remote-debugging-port','0');
let window;app.whenReady().then(()=>{window=new BrowserWindow({show:false,width:1000,height:650,webPreferences:{nodeIntegration:false,contextIsolation:true,sandbox:true,backgroundThrottling:false}});window.webContents.setAudioMuted(true);window.loadURL(${JSON.stringify(url)});});
app.on('window-all-closed',()=>app.quit());`,
  );
  browser = childProcess(
    join(root, 'node_modules/electron/dist/electron.exe'),
    [mainPath],
    { logName: 'electron' },
  );
  const portPath = join(work, 'profile/DevToolsActivePort');
  let target;
  for (let attempt = 0; attempt < 100; ++attempt) {
    if (browser.exitCode !== null)
      throw new Error(`Electron exited ${browser.exitCode}`);
    if (existsSync(portPath)) {
      const port = readFileSync(portPath, 'utf8').split('\n')[0];
      try {
        target = (
          await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()
        ).find((item) => item.type === 'page' && item.url === url);
      } catch {
        /* Startup */
      }
      if (target) break;
    }
    await sleep(100);
  }
  if (!target) throw new Error('Isolated Electron receiver unavailable');
  socket = new WebSocket(target.webSocketDebuggerUrl);
  socket.on('message', (text) => {
    const value = JSON.parse(text);
    const task = cdpPending.get(value.id);
    if (!task) return;
    clearTimeout(task.timer);
    cdpPending.delete(value.id);
    if (value.error) task.reject(new Error(value.error.message));
    else task.resolve(value.result);
  });
  await new Promise((resolve, reject) => {
    socket.once('open', resolve);
    socket.once('error', reject);
  });
  for (let attempt = 0; attempt < 100; ++attempt) {
    if (await evaluate('Boolean(window.test)')) break;
    if (attempt === 99) throw new Error('Receiver page did not initialize');
    await sleep(100);
  }
  report.chromium = await evaluate('navigator.userAgent');

  native = childProcess(join(runtime, 'media-engine.exe'), [], {
    logName: 'native',
    env: {
      ...process.env,
      PATH: join(runtime, 'bin') + ';' + process.env.PATH,
      GST_PLUGIN_PATH_1_0: '',
      GST_PLUGIN_SYSTEM_PATH_1_0: join(runtime, 'lib/gstreamer-1.0'),
      GST_PLUGIN_SCANNER_1_0: join(
        runtime,
        'libexec/gstreamer-1.0/gst-plugin-scanner.exe',
      ),
      GST_REGISTRY_1_0: join(work, 'gst-registry.bin'),
    },
  });
  let buffer = '';
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(
      () => reject(new Error('Native startup timed out')),
      30000,
    );
    native.once('error', reject);
    native.stdout.on('data', (chunk) => {
      buffer += chunk.toString();
      let end;
      while ((end = buffer.indexOf('\n')) >= 0) {
        const value = JSON.parse(buffer.slice(0, end));
        buffer = buffer.slice(end + 1);
        if (value.event === 'ready') {
          clearTimeout(timeout);
          resolve();
        }
        if (value.event === 'frame') report.nativeFrames++;
        if (value.event === 'audio-state') report.audioStates.push(value);
        if (value.event === 'state') report.states.push(value);
        if (value.event === 'warning' || value.event === 'error')
          report.errors.push(value);
        if (value.event === 'signal') {
          if (value.sdp) {
            report.offer = value.sdp;
            writeFileSync(join(work, `native-${value.type}.sdp`), value.sdp);
          }
          const message = {
            type: value.type,
            from: senderId,
            ...(value.sdp
              ? { sdp: value.sdp }
              : { candidate: value.candidate }),
          };
          signalChain = signalChain
            .then(() =>
              evaluate(`window.test.receive(${JSON.stringify(message)})`),
            )
            .catch((error) => report.errors.push({ signal: String(error) }));
        }
        const task = pending.get(value.id);
        if (task) {
          clearTimeout(task.timer);
          pending.delete(value.id);
          if (value.ok) task.resolve(value.result);
          else task.reject(new Error(value.error));
        }
      }
    });
    native.once('exit', (code) => {
      clearTimeout(timeout);
      reject(new Error(`Native exited ${code}`));
      for (const task of pending.values()) {
        clearTimeout(task.timer);
        task.reject(new Error(`Native exited ${code}`));
      }
      pending.clear();
    });
  });
  report.runtime = await engineRequest('capabilities');
  let selectedFixture = fixturePath;
  if (blockedFixture) {
    selectedFixture = join(work, 'Discord.exe');
    copyFileSync(fixturePath, selectedFixture);
    report.simulation =
      'Dedicated Win32 fixture renamed Discord.exe; not the real Discord application';
  }
  fixture = childProcess(selectedFixture, [], { logName: 'fixture' });
  report.fixture = await new Promise((resolve, reject) => {
    let buffer = '';
    const timeout = setTimeout(
      () => reject(new Error('Fixture timed out')),
      5000,
    );
    fixture.once('error', reject);
    fixture.stdout.on('data', (chunk) => {
      buffer += chunk.toString();
      if (buffer.includes('\n')) {
        clearTimeout(timeout);
        resolve(JSON.parse(buffer.split('\n')[0]));
      }
    });
  });
  report.settings = {
    source: report.fixture.window,
    method: 'printwindow',
    encoder: 'auto',
    width: 1280,
    height: 720,
    fps: 30,
    bitrate: 4000000,
    audio: true,
    allowedAudioApps: [],
  };
  if (chromeSource || unrelatedChrome) {
    const chromePath =
      process.env.GOLIVE_CHROME_PATH ||
      'C:/Program Files/Google/Chrome/Application/chrome.exe';
    if (!existsSync(chromePath)) throw new Error('Chrome executable not found');
    childProcess(
      chromePath,
      [
        `--user-data-dir=${join(work, 'chrome-source-profile')}`,
        '--no-first-run',
        '--no-default-browser-check',
        '--autoplay-policy=no-user-gesture-required',
        '--window-position=80,80',
        '--window-size=1000,700',
        `--app=${url}chrome-source`,
      ],
      { logName: 'chrome-source', windowsHide: false },
    );
    for (let attempt = 0; attempt < 100; ++attempt) {
      const sources = await engineRequest('sources');
      report.lastSources = sources;
      const selected = sources.find((source) =>
        source.name.includes('GoLive exclusive Chrome audio test'),
      );
      if (selected) {
        if (chromeSource) report.settings.source = selected.id;
        report.chromeSource = selected;
        break;
      }
      await sleep(100);
    }
    if (!report.chromeSource)
      throw new Error('Chrome audio source window not found');
  }
  report.capture = await engineRequest('start', report.settings);
  await engineRequest('offer', { peer: receiverId });
  for (let attempt = 0; attempt < 36; ++attempt) {
    await sample();
    await sleep(500);
  }
  report.nativeStats = await engineRequest('stats');
  report.final = await evaluate('window.test.stats()');
  const screenshot = await evaluate('window.test.frame()');
  writeFileSync(join(work, 'receiver.png'), Buffer.from(screenshot, 'base64'));
  const audio = report.final.inbound.find(
    (stat) => stat.type === 'inbound-rtp' && stat.kind === 'audio',
  );
  const video = report.final.inbound.find(
    (stat) => stat.type === 'inbound-rtp' && stat.kind === 'video',
  );
  if (
    !report.offer?.includes('m=audio ') ||
    !report.answer?.includes('m=audio ')
  )
    throw new Error('Audio missing from negotiated SDP');
  if (!blockedFixture && !report.audioStates.some((state) => state.active))
    throw new Error('Native fixture audio never became active');
  if (
    !audio?.packetsReceived ||
    (!blockedFixture && !(audio.totalAudioEnergy > 0))
  )
    throw new Error('Chromium received no nonzero audio RTP');
  if (!video?.framesDecoded || !report.final.video.decoded)
    throw new Error('Chromium did not decode/display video');
  for (const kind of ['audio', 'video'])
    if (!report.final.video.tracks?.some((track) => track.kind === kind))
      throw new Error(`Playback element lost the ${kind} track`);
  if (!blockedFixture && !(report.final.maxRms > 0.0001))
    throw new Error('Playback audio PCM is silent');
  if (
    chromeSource &&
    !(report.final.spectrum.tone660 > report.final.spectrum.tone440 * 20)
  )
    throw new Error(
      'Chrome tone was absent or unrelated fixture audio leaked into capture',
    );
  if (
    unrelatedChrome &&
    !(report.final.spectrum.tone440 > report.final.spectrum.tone660 * 20)
  )
    throw new Error(
      'Fixture tone was absent or unrelated Chrome audio leaked into capture',
    );
  if (
    blockedFixture &&
    (report.audioStates.some((state) => state.active) ||
      report.final.maxRms > 0.0001)
  )
    throw new Error('Blocked fixture audio leaked while video should continue');
  if (report.errors.some((error) => error.event === 'error' || error.signal))
    throw new Error('Native signaling or pipeline errors');
  report.passed = true;
  console.log(
    JSON.stringify(
      {
        passed: true,
        receiverRef: report.receiverRef,
        audioPackets: audio.packetsReceived,
        audioEnergy: audio.totalAudioEnergy,
        playbackRms: report.final.maxRms,
        decodedVideoFrames: video.framesDecoded,
        displayedVideoFrames: report.final.video.decoded,
        report: join(work, 'report.json'),
      },
      null,
      2,
    ),
  );
} catch (error) {
  report.passed = false;
  report.errors.push({ fatal: String(error) });
  console.error(error);
  process.exitCode = 1;
} finally {
  if (native?.exitCode === null) await engineRequest('stop').catch(() => {});
  if (socket?.readyState === WebSocket.OPEN)
    await evaluate('window.test.stop()').catch(() => {});
  if (socket?.readyState === WebSocket.OPEN)
    await cdp('Browser.close').catch(() => {});
  socket?.close();
  server?.closeAllConnections();
  server?.close();
  for (const task of cdpPending.values()) {
    clearTimeout(task.timer);
    task.reject(new Error('Test finished'));
  }
  cdpPending.clear();
  for (const child of children) child.stdin.end();
  fixture?.kill();
  await sleep(1000);
  for (const child of children) if (child.exitCode === null) child.kill();
  writeFileSync(join(work, 'report.json'), JSON.stringify(report, null, 2));
  console.log('Integration report:', join(work, 'report.json'));
}
