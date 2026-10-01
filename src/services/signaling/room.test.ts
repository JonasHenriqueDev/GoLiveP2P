import {afterEach,describe,it,expect} from 'vitest';
import {WebSocket} from 'ws';
import {RoomServer} from './room';
import {MAX_PEERS} from '../../shared/protocol';
let room:RoomServer|undefined;
let sockets:WebSocket[]=[];
const port=48000+Math.floor(Math.random()*1000);
async function join(name:string){
 const socket=new WebSocket('ws://127.0.0.1:'+port);sockets.push(socket);
 return new Promise<any>((resolve,reject)=>{
  socket.once('message',data=>resolve(JSON.parse(data.toString())));
  socket.once('open',()=>socket.send(JSON.stringify({type:'join-room',name})));
  socket.once('error',reject);
 });
}
afterEach(()=>{sockets.forEach(s=>s.terminate());sockets=[];room?.close();room=undefined});
describe('room',()=>{
 it('joins, broadcasts and leaves',async()=>{
  room=new RoomServer('127.0.0.1',port);
  const first=await join('A');expect(first.type).toBe('joined-room');
  const second=await join('B');expect(second.peers).toHaveLength(1);
  expect(room.size).toBe(2);sockets[1].close();
  await new Promise<void>(resolve=>{const check=(data:Buffer)=>{if(JSON.parse(data.toString()).type==='user-left'){sockets[0].off('message',check);resolve();}};sockets[0].on('message',check);});
  expect(room.size).toBe(1);
 });
 it('enforces five participants',async()=>{
  room=new RoomServer('127.0.0.1',port);
  for(let i=0;i<MAX_PEERS;i++)expect((await join('P'+i)).type).toBe('joined-room');
  expect((await join('extra')).type).toBe('room-full');
  expect(room.size).toBe(MAX_PEERS);
 });
});
