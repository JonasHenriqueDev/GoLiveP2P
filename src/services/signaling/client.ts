import { clientMessage, serverMessage, type ClientMessage, type ServerMessage, PORT } from '../../shared/protocol';
export class SignalingClient {
 private socket:WebSocket|null=null;
 onMessage:(message:ServerMessage)=>void=()=>{};
 onState:(state:string)=>void=()=>{};
 async connect(ip:string,name:string){
  this.close();
  return new Promise<void>((resolve,reject)=>{
   const socket=new WebSocket('ws://'+ip+':'+PORT);this.socket=socket;
   const timeout=setTimeout(()=>{socket.close();reject(new Error('Tempo esgotado ao conectar'))},8000);
   socket.onopen=()=>{clearTimeout(timeout);this.onState('connected');this.send({type:'join-room',name});resolve();};
   socket.onerror=()=>{clearTimeout(timeout);reject(new Error('Falha de conexão com o host'))};
   socket.onclose=()=>{clearTimeout(timeout);this.onState('disconnected');};
   socket.onmessage=event=>{try{const parsed=serverMessage.safeParse(JSON.parse(event.data));if(parsed.success)this.onMessage(parsed.data);}catch{console.warn('[Signaling] invalid server message');}};
  });
 }
 send(message:ClientMessage){if(!clientMessage.safeParse(message).success)throw new Error('Mensagem inválida');if(this.socket?.readyState===WebSocket.OPEN)this.socket.send(JSON.stringify(message));}
 close(){this.socket?.close();this.socket=null;}
}
