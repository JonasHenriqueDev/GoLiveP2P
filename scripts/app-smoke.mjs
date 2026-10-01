import { spawn } from 'node:child_process';
import { resolve } from 'node:path';
import { mkdirSync, writeFileSync } from 'node:fs';
import WebSocket from 'ws';
const work = resolve('work/app-smoke');
mkdirSync(work, { recursive: true });
const app = spawn(
  resolve('node_modules/electron/dist/electron.exe'),
  ['.', '--remote-debugging-port=9223'],
  { cwd: process.cwd(), windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] },
);
app.stdout.on('data', (chunk) =>
  writeFileSync(resolve(work, 'main.log'), chunk, { flag: 'a' }),
);
app.stderr.on('data', (chunk) =>
  writeFileSync(resolve(work, 'stderr.log'), chunk, { flag: 'a' }),
);
app.on('error', (error) => {
  console.error(error);
  process.exitCode = 1;
});
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
let socket;
try {
  let target;
  for (let attempt = 0; attempt < 100; ++attempt) {
    try {
      const targets = await (
        await fetch('http://127.0.0.1:9223/json/list')
      ).json();
      target = targets.find((item) => item.type === 'page');
      if (target) break;
    } catch {
      /* Startup */
    }
    await sleep(200);
  }
  if (!target) throw new Error('Electron page unavailable');
  socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    socket.once('open', resolve);
    socket.once('error', reject);
  });
  let sequence = 0;
  const pending = new Map();
  socket.on('message', (text) => {
    const message = JSON.parse(text);
    if (pending.has(message.id)) {
      const task = pending.get(message.id);
      pending.delete(message.id);
      if (message.error) task.reject(new Error(message.error.message));
      else task.resolve(message.result);
    }
  });
  const request = (method, params = {}) =>
    new Promise((resolve, reject) => {
      const id = ++sequence;
      pending.set(id, { resolve, reject });
      socket.send(JSON.stringify({ id, method, params }));
    });
  const evaluate = async (expression) => {
    const result = await request('Runtime.evaluate', {
      expression,
      awaitPromise: true,
      returnByValue: true,
    });
    if (result.exceptionDetails)
      throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  await sleep(1000);
  const report = {
    system: await evaluate('window.golive.systemInfo()'),
    capabilities: await evaluate(
      "window.golive.mediaRequest('capabilities',{})",
    ),
    initial: await evaluate('document.body.innerText'),
  };
  await evaluate(
    "(()=>{const input=document.querySelector('input[placeholder=\"Ex.: Jonas\"]');Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,'Teste nativo Windows');input.dispatchEvent(new Event('input',{bubbles:true}));})()",
  );
  await sleep(200);
  await evaluate(
    "[...document.querySelectorAll('button')].find(button=>button.textContent==='Criar sala').click()",
  );
  await sleep(1500);
  report.room = await evaluate('document.body.innerText');
  if (!report.room.includes('Pronto para compartilhar'))
    throw new Error('Room creation failed: ' + report.room);
  await evaluate(
    "[...document.querySelectorAll('select')].find(item=>item.value==='wgc').value='dxgi'",
  );
  await evaluate(
    "(()=>{const select=[...document.querySelectorAll('select')].find(item=>item.value==='dxgi');select.dispatchEvent(new Event('change',{bubbles:true}));})()",
  );
  await evaluate(
    "[...document.querySelectorAll('button')].find(button=>button.textContent==='Compartilhar tela').click()",
  );
  await sleep(1000);
  await evaluate(
    "[...document.querySelectorAll('button.source')].find(button=>button.textContent.includes('Monitor inteiro')).click()",
  );
  await sleep(4000);
  report.stream = await evaluate('document.body.innerText');
  report.preview = await evaluate(
    "({loaded:document.querySelector('img.streamVideo')?.naturalWidth>0,width:document.querySelector('img.streamVideo')?.naturalWidth})",
  );
  if (!report.preview.loaded) throw new Error('Native preview did not render');
  const screenshot = await request('Page.captureScreenshot', { format: 'png' });
  writeFileSync(
    resolve(work, 'preview.png'),
    Buffer.from(screenshot.data, 'base64'),
  );
  writeFileSync(resolve(work, 'report.json'), JSON.stringify(report, null, 2));
  console.log('Electron room/native preview verified', report.preview);
  if (process.argv.includes('--keep-open')) {
    console.log('Host remains ready at 100.75.12.74');
    socket.close();
    socket = null;
    app.unref();
    app.stdout.destroy();
    app.stderr.destroy();
  } else {
    await evaluate(
      "[...document.querySelectorAll('button')].find(button=>button.textContent==='Parar transmissão').click()",
    );
    await sleep(300);
    await evaluate(
      "[...document.querySelectorAll('button')].find(button=>button.textContent==='Sair').click()",
    );
  }
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  socket?.close();
  if (!process.argv.includes('--keep-open') || process.exitCode) app.kill();
}
