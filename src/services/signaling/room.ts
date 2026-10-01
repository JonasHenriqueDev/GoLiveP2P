import { randomUUID } from 'node:crypto';
import { WebSocket, WebSocketServer } from 'ws';
import { clientMessage, MAX_PEERS, type Peer, type ServerMessage } from '../../shared/protocol';
export class RoomServer {
 private server:WebSocketServer;
 private members=new Map<WebSocket,Peer>();
 private streamerId:string|null=null;
 constructor(ip:string,port:number){this.server=new WebSocketServer({host:ip,port,maxPayload:220_000});this.server.on('connection',socket=>this.connect(socket));}
 get size(){return this.members.size;}
 close(){for(const socket of this.members.keys())socket.close();this.server.close();}
 private send(socket:WebSocket,msg:ServerMessage){if(socket.readyState===WebSocket.OPEN)socket.send(JSON.stringify(msg));}
 private broadcast(msg:ServerMessage,except?:WebSocket){for(const socket of this.members.keys())if(socket!==except)this.send(socket,msg);}
 private connect(socket:WebSocket){
  if(this.members.size>=MAX_PEERS){this.send(socket,{type:'room-full'});socket.close(1008,'Room full');return;}
  socket.on('message',raw=>{
   let parsed:unknown;try{parsed=JSON.parse(raw.toString());}catch{this.send(socket,{type:'error',message:'JSON inválido'});return;}
   const result=clientMessage.safeParse(parsed);if(!result.success){this.send(socket,{type:'error',message:'Mensagem inválida'});return;}
   const msg=result.data;const self=this.members.get(socket);
   if(msg.type==='join-room'){
    if(self){this.send(socket,{type:'error',message:'Já está na sala'});return;}
    if(this.members.size>=MAX_PEERS){this.send(socket,{type:'room-full'});socket.close();return;}
    const peer={id:randomUUID(),name:msg.name};const peers=[...this.members.values()];this.members.set(socket,peer);
    this.send(socket,{type:'joined-room',self:peer,peers,streamerId:this.streamerId});this.broadcast({type:'user-joined',peer},socket);console.info('[Room] user joined',peer.id);return;
   }
   if(!self){this.send(socket,{type:'error',message:'Entre na sala primeiro'});return;}
   if(msg.type==='start-stream'){
    if(this.streamerId && this.streamerId!==self.id){this.send(socket,{type:'error',message:'Outra pessoa já transmite'});return;}
    this.streamerId=self.id;this.broadcast({type:'start-stream',from:self.id},socket);return;
   }
   if(msg.type==='stop-stream'){if(this.streamerId!==self.id)return;this.streamerId=null;this.broadcast({type:'stop-stream',from:self.id},socket);return;}
   const recipient=[...this.members].find(([,peer])=>peer.id===msg.to);
   if(!recipient){this.send(socket,{type:'error',message:'Destinatário indisponível'});return;}
   if(msg.type==='offer' && self.id!==this.streamerId){this.send(socket,{type:'error',message:'Somente o transmissor pode criar ofertas'});return;}
   if(msg.type==='answer' && recipient[1].id!==this.streamerId){this.send(socket,{type:'error',message:'Resposta inválida'});return;}
   const forwarded=msg.type==='ice-candidate'?{type:msg.type,from:self.id,candidate:msg.candidate}:{type:msg.type,from:self.id,sdp:msg.sdp};
   this.send(recipient[0],forwarded as ServerMessage);
  });
  socket.on('close',()=>{const peer=this.members.get(socket);if(!peer)return;this.members.delete(socket);if(this.streamerId===peer.id){this.streamerId=null;this.broadcast({type:'stop-stream',from:peer.id});}this.broadcast({type:'user-left',peer});console.info('[Room] user left',peer.id);});
 }
}
