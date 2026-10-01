import { randomBytes, randomUUID } from 'node:crypto';
import { createServer, type Server } from 'node:http';
import { WebSocket, WebSocketServer } from 'ws';
import { clientMessage, MAX_PEERS, RECONNECT_GRACE_MS, type Peer, type ServerMessage } from '../../shared/protocol';

type Member = { peer: Peer; token: string; socket: WebSocket | null; timer: ReturnType<typeof setTimeout> | null };

export class RoomServer {
  private http: Server;
  private server: WebSocketServer;
  private members = new Map<string, Member>();
  private sockets = new Map<WebSocket, Member>();
  private streamerId: string | null = null;
  readonly ready: Promise<void>;

  constructor(ip: string, port: number) {
    this.http = createServer((request, response) => {
      if (request.method === 'GET' && request.url === '/discover') {
        response.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-store' });
        response.end(JSON.stringify({ app: 'golive-p2p', version: 1, participants: this.members.size, full: this.members.size >= MAX_PEERS }));
      } else { response.writeHead(404); response.end(); }
    });
    this.server = new WebSocketServer({ server: this.http, maxPayload: 220_000 });
    this.server.on('connection', (socket, request) => this.connect(socket, request.socket.remoteAddress ?? ''));
    this.ready = new Promise((resolve, reject) => {
      this.http.once('error', reject);
      this.http.listen(port, ip, () => { this.http.off('error', reject); resolve(); });
    });
  }
  get size() { return this.members.size; }
  close() {
    for (const member of this.members.values()) {
      if (member.timer) clearTimeout(member.timer);
      member.socket?.close();
    }
    this.members.clear();
    this.sockets.clear();
    for (const socket of this.server.clients) socket.terminate();
    this.server.close();
    this.http.close();
  }
  private send(socket: WebSocket, msg: ServerMessage) {
    if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify(msg));
  }
  private broadcast(msg: ServerMessage, except?: WebSocket) {
    for (const member of this.members.values()) {
      if (member.socket && member.socket !== except) this.send(member.socket, msg);
    }
  }
  private finish(member: Member) {
    if (member.timer) clearTimeout(member.timer);
    member.timer = null;
    this.members.delete(member.peer.id);
    if (member.socket) this.sockets.delete(member.socket);
    if (this.streamerId === member.peer.id) {
      this.streamerId = null;
      this.broadcast({ type: 'stop-stream', from: member.peer.id });
    }
    this.broadcast({ type: 'user-left', peer: member.peer });
    console.info('[Room] user left', member.peer.id);
  }
  private connect(socket: WebSocket, remoteAddress: string) {
    const joinTimeout = setTimeout(() => socket.close(1008, 'Join timeout'), 10_000);
    socket.on('message', raw => {
      let parsed: unknown;
      try { parsed = JSON.parse(raw.toString()); }
      catch { this.send(socket, { type: 'error', message: 'JSON inválido' }); return; }
      const result = clientMessage.safeParse(parsed);
      if (!result.success) { this.send(socket, { type: 'error', message: 'Mensagem inválida' }); return; }
      const msg = result.data;
      const self = this.sockets.get(socket);
      if (msg.type === 'join-room') {
        if (self) { this.send(socket, { type: 'error', message: 'Já está na sala' }); return; }
        const resumed = msg.resumeToken
          ? [...this.members.values()].find(member => member.token === msg.resumeToken) : undefined;
        if (resumed?.socket) { this.send(socket, { type: 'error', message: 'Sessão já está conectada' }); socket.close(1008); return; }
        if (!resumed && this.members.size >= MAX_PEERS) {
          this.send(socket, { type: 'room-full' }); socket.close(1008); return;
        }
        const member = resumed ?? {
          peer: {
            id: randomUUID(), name: msg.name,
            ip: /^100\.(?:\d{1,3}\.){2}\d{1,3}$/.test(remoteAddress.replace(/^::ffff:/, ''))
              ? remoteAddress.replace(/^::ffff:/, '') : null
          }, token: randomBytes(32).toString('hex'),
          socket: null, timer: null
        };
        if (member.timer) clearTimeout(member.timer);
        member.timer = null;
        member.socket = socket;
        this.members.set(member.peer.id, member);
        this.sockets.set(socket, member);
        clearTimeout(joinTimeout);
        const peers = [...this.members.values()].filter(other => other !== member).map(other => other.peer);
        this.send(socket, { type: 'joined-room', self: member.peer, peers, streamerId: this.streamerId, resumeToken: member.token, resumed: !!resumed });
        this.broadcast({ type: 'user-joined', peer: member.peer }, socket);
        console.info('[Room] user joined', member.peer.id, resumed ? '(resumed)' : '');
        return;
      }
      if (!self) { this.send(socket, { type: 'error', message: 'Entre na sala primeiro' }); return; }
      if (msg.type === 'leave-room') { this.finish(self); socket.close(); return; }
      if (msg.type === 'start-stream') {
        if (this.streamerId && this.streamerId !== self.peer.id) {
          this.send(socket, { type: 'error', message: 'Outra pessoa já transmite' }); return;
        }
        this.streamerId = self.peer.id;
        this.broadcast({ type: 'start-stream', from: self.peer.id }, socket);
        return;
      }
      if (msg.type === 'stop-stream') {
        if (this.streamerId !== self.peer.id) return;
        this.streamerId = null;
        this.broadcast({ type: 'stop-stream', from: self.peer.id }, socket);
        return;
      }
      const recipient = this.members.get(msg.to);
      if (!recipient?.socket) { this.send(socket, { type: 'error', message: 'Destinatário indisponível' }); return; }
      if (msg.type === 'log-report') {
        this.send(recipient.socket, { type: 'log-report', from: self.peer.id, text: msg.text });
        console.info('[Room] log report sent', self.peer.id, recipient.peer.id);
        return;
      }
      if (msg.type === 'offer' && self.peer.id !== this.streamerId) {
        this.send(socket, { type: 'error', message: 'Somente o transmissor pode criar ofertas' }); return;
      }
      if (msg.type === 'answer' && recipient.peer.id !== this.streamerId) {
        this.send(socket, { type: 'error', message: 'Resposta inválida' }); return;
      }
      if (msg.type === 'request-restart') {
        if (recipient.peer.id !== this.streamerId) { this.send(socket, { type: 'error', message: 'Reinício ICE inválido' }); return; }
        this.send(recipient.socket, { type: 'request-restart', from: self.peer.id });
        return;
      }
      if (msg.type === 'ice-candidate' && self.peer.id !== this.streamerId && recipient.peer.id !== this.streamerId) {
        this.send(socket, { type: 'error', message: 'Candidato ICE inválido' }); return;
      }
      const forwarded = msg.type === 'ice-candidate'
        ? { type: msg.type, from: self.peer.id, candidate: msg.candidate }
        : { type: msg.type, from: self.peer.id, sdp: msg.sdp };
      this.send(recipient.socket, forwarded as ServerMessage);
    });
    socket.on('close', () => {
      clearTimeout(joinTimeout);
      const member = this.sockets.get(socket);
      if (!member) return;
      this.sockets.delete(socket);
      member.socket = null;
      member.timer = setTimeout(() => this.finish(member), RECONNECT_GRACE_MS);
    });
  }
}
