import WebSocket from 'ws';
import { writeFileSync } from 'node:fs';
const pages = await (await fetch('http://127.0.0.1:9223/json/list')).json();
const socket = new WebSocket(
  pages.find((page) => page.type === 'page').webSocketDebuggerUrl,
);
await new Promise((resolve) => socket.once('open', resolve));
let next = 0;
const pending = new Map();
socket.on('message', (text) => {
  const message = JSON.parse(text);
  const task = pending.get(message.id);
  if (task) {
    pending.delete(message.id);
    if (message.error) task.reject(message.error);
    else task.resolve(message.result);
  }
});
const request = (method, params = {}) =>
  new Promise((resolve, reject) => {
    const id = ++next;
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
  });
const evaluate = async (expression) => {
  const result = await request('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
  });
  if (result.exceptionDetails)
    throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
};
try {
  const report = { tests: [] };
  if (!process.argv.includes('--stats-only'))
    await evaluate(
      'document.fullscreenElement ? document.exitFullscreen() : Promise.resolve()',
    );
  for (const expected of process.argv.includes('--stats-only')
    ? []
    : [true, false]) {
    const point = await evaluate(
      "(()=>{const r=document.querySelector('.streamStage').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()",
    );
    for (const clickCount of [1, 2]) {
      await request('Input.dispatchMouseEvent', {
        type: 'mousePressed',
        ...point,
        button: 'left',
        clickCount,
      });
      await request('Input.dispatchMouseEvent', {
        type: 'mouseReleased',
        ...point,
        button: 'left',
        clickCount,
      });
    }
    await new Promise((resolve) => setTimeout(resolve, 500));
    const actual = await evaluate('!!document.fullscreenElement');
    report.tests.push({ doubleClickFullscreen: actual, expected });
    if (actual !== expected) throw new Error('Double-click fullscreen failed');
  }
  report.stats = await evaluate("window.golive.mediaRequest('stats',{})");
  report.screen = await evaluate('document.body.innerText');
  writeFileSync('work/app-smoke/live-ui.json', JSON.stringify(report, null, 2));
  console.log(
    process.argv.includes('--stats-only')
      ? 'Live native connection statistics saved'
      : 'Double-click enter/exit fullscreen verified',
  );
} finally {
  socket.close();
}
