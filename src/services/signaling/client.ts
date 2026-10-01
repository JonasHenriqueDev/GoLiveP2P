import { clientMessage, serverMessage, type ClientMessage, type ServerMessage, PORT } from '../../shared/protocol';

export class SignalingClient {
  private socket: WebSocket | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private generation = 0;
  private token: string | null = null;
  private address = '';
  private name = '';
  private attempts = 0;
  onMessage: (message: ServerMessage) => void = () => {};
  onState: (state: string) => void = () => {};

  async connect(ip: string, name: string): Promise<void> {
    this.close();
    this.address = ip;
    this.name = name;
    this.attempts = 0;
    const generation = this.generation;
    await this.open(generation);
  }
  private open(generation: number): Promise<void> {
    return new Promise((resolve, reject) => {
      if (generation !== this.generation) return reject(new Error('Conexão cancelada'));
      const socket = new WebSocket('ws://' + this.address + ':' + PORT);
      this.socket = socket;
      let settled = false;
      const fail = (error: Error) => {
        if (!settled) { settled = true; reject(error); }
      };
      const timeout = setTimeout(() => { socket.close(); fail(new Error('Tempo esgotado ao conectar')); }, 8000);
      socket.onopen = () => {
        if (generation !== this.generation) { socket.close(); return; }
        this.send({ type: 'join-room', name: this.name, ...(this.token ? { resumeToken: this.token } : {}) });
      };
      socket.onerror = () => fail(new Error('Falha de conexão com o host'));
      socket.onclose = () => {
        clearTimeout(timeout);
        if (generation !== this.generation) return;
        if (!settled) fail(new Error('Conexão encerrada pelo host'));
        if (this.token) this.scheduleReconnect(generation);
        else this.onState('disconnected');
      };
      socket.onmessage = event => {
        try {
          const parsed = serverMessage.safeParse(JSON.parse(event.data));
          if (!parsed.success) return;
          const message = parsed.data;
          if (message.type === 'joined-room') {
            clearTimeout(timeout);
            this.token = message.resumeToken;
            this.attempts = 0;
            this.onState('connected');
            if (!settled) { settled = true; resolve(); }
          }
          if ((message.type === 'room-full' || message.type === 'error') && !settled) {
            fail(new Error(message.type === 'room-full' ? 'Sala cheia' : message.message));
            this.onMessage(message);
            this.close();
            return;
          }
          this.onMessage(message);
        } catch { console.warn('[Signaling] invalid server message'); }
      };
    });
  }
  private scheduleReconnect(generation: number) {
    if (generation !== this.generation) return;
    if (this.attempts >= 6) { this.onState('disconnected'); return; }
    this.attempts++;
    this.onState('reconnecting');
    const delay = Math.min(1000 * 2 ** (this.attempts - 1), 6000);
    this.timer = setTimeout(() => {
      this.timer = null;
      void this.open(generation).catch(() => {});
    }, delay);
  }
  send(message: ClientMessage) {
    if (!clientMessage.safeParse(message).success) throw new Error('Mensagem inválida');
    if (this.socket?.readyState === WebSocket.OPEN) this.socket.send(JSON.stringify(message));
  }
  close() {
    this.generation++;
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
    this.token = null;
    if (this.socket?.readyState === WebSocket.OPEN) this.send({ type: 'leave-room' });
    this.socket?.close();
    this.socket = null;
    this.onState('disconnected');
  }
}
