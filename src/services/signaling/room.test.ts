import { afterEach, describe, expect, it } from 'vitest';
import { WebSocket } from 'ws';
import { RoomServer } from './room';
import { MAX_PEERS } from '../../shared/protocol';

let room: RoomServer | undefined;
let sockets: WebSocket[] = [];
const port = 48000 + Math.floor(Math.random() * 1000);
async function connect(name: string, resumeToken?: string) {
  const socket = new WebSocket('ws://127.0.0.1:' + port);
  sockets.push(socket);
  const first = new Promise<any>((resolve, reject) => {
    socket.once('message', data => resolve(JSON.parse(data.toString())));
    socket.once('error', reject);
  });
  await new Promise<void>((resolve, reject) => {
    socket.once('open', resolve);
    socket.once('error', reject);
  });
  socket.send(JSON.stringify({ type: 'join-room', name, ...(resumeToken ? { resumeToken } : {}) }));
  return { socket, first: await first };
}
async function next(socket: WebSocket, type: string) {
  return new Promise<any>(resolve => {
    const listener = (data: Buffer) => {
      const message = JSON.parse(data.toString());
      if (message.type === type) { socket.off('message', listener); resolve(message); }
    };
    socket.on('message', listener);
  });
}
afterEach(() => {
  room?.close(); room = undefined;
  sockets.forEach(socket => socket.terminate()); sockets = [];
});
describe('room', () => {
  it('joins, broadcasts and leaves explicitly', async () => {
    room = new RoomServer('127.0.0.1', port); await room.ready;
    const first = await connect('A'); expect(first.first.type).toBe('joined-room');
    const joined = next(first.socket, 'user-joined');
    const second = await connect('B'); expect(second.first.peers).toHaveLength(1);
    expect((await joined).peer.id).toBe(second.first.self.id);
    const left = next(first.socket, 'user-left');
    second.socket.send(JSON.stringify({ type: 'leave-room' }));
    expect((await left).peer.id).toBe(second.first.self.id);
    expect(room.size).toBe(1);
  });
  it('enforces five participants, including reserved sessions', async () => {
    room = new RoomServer('127.0.0.1', port); await room.ready;
    const users = [];
    for (let i = 0; i < MAX_PEERS; i++) users.push(await connect('P' + i));
    users[0].socket.terminate();
    await new Promise(resolve => setTimeout(resolve, 20));
    expect((await connect('extra')).first.type).toBe('room-full');
    expect(room.size).toBe(MAX_PEERS);
  });
  it('resumes the same ID and rejects duplicate active sessions', async () => {
    room = new RoomServer('127.0.0.1', port); await room.ready;
    const first = await connect('A');
    const duplicate = await connect('A', first.first.resumeToken);
    expect(duplicate.first.type).toBe('error');
    first.socket.terminate();
    await new Promise(resolve => setTimeout(resolve, 20));
    const resumed = await connect('A', first.first.resumeToken);
    expect(resumed.first.type).toBe('joined-room');
    expect(resumed.first.resumed).toBe(true);
    expect(resumed.first.self.id).toBe(first.first.self.id);
    expect(room.size).toBe(1);
  });
  it('rejects offers from viewers and spoofs from disconnected sockets', async () => {
    room = new RoomServer('127.0.0.1', port); await room.ready;
    const host = await connect('Host');
    const viewer = await connect('Viewer');
    const error = next(viewer.socket, 'error');
    viewer.socket.send(JSON.stringify({ type: 'offer', to: host.first.self.id, sdp: 'fake' }));
    expect((await error).message).toMatch(/Somente o transmissor/);
  });
  it('advertises a discoverable room over the Tailscale-bound HTTP endpoint', async () => {
    room = new RoomServer('127.0.0.1', port); await room.ready;
    const response = await fetch('http://127.0.0.1:' + port + '/discover');
    expect(response.status).toBe(200);
    expect(await response.json()).toMatchObject({ app: 'golive-p2p', participants: 0, full: false });
  });
  it('delivers a bounded log report with the authenticated sender ID', async () => {
    room = new RoomServer('127.0.0.1', port); await room.ready;
    const sender = await connect('Sender');
    const recipient = await connect('Recipient');
    const received = next(recipient.socket, 'log-report');
    sender.socket.send(JSON.stringify({ type: 'log-report', to: recipient.first.self.id, text: 'diagnostic' }));
    expect(await received).toMatchObject({ from: sender.first.self.id, text: 'diagnostic' });
  });
});
