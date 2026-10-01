import type { ClientMessage, ServerMessage } from '../../shared/protocol';
export type Quality='1080p60'|'720p30';
const presets={ '1080p60':{width:1920,height:1080,frameRate:60,bitrate:6_000_000},'720p30':{width:1280,height:720,frameRate:30,bitrate:3_000_000} };
export class Mesh {
 private pcs=new Map<string,RTCPeerConnection>();
 private pending=new Map<string,RTCIceCandidateInit[]>();
 stream:MediaStream|null=null;
 onRemote:(stream:MediaStream|null)=>void=()=>{};
 onState:(peer:string,state:string)=>void=()=>{};
 constructor(private send:(message:ClientMessage)=>void){}
 private peer(id:string){
  let pc=this.pcs.get(id);if(pc)return pc;
  pc=new RTCPeerConnection({iceServers:[],iceTransportPolicy:'all'});
  this.pcs.set(id,pc);
  pc.onicecandidate=event=>{if(event.candidate)this.send({type:'ice-candidate',to:id,candidate:{...event.candidate.toJSON(),candidate:event.candidate.candidate}});};
  pc.ontrack=event=>this.onRemote(event.streams[0] || new MediaStream([event.track]));
  pc.onconnectionstatechange=()=>{this.onState(id,pc!.connectionState);if(pc!.connectionState==='failed')pc!.restartIce();};
  return pc;
 }
 async start(stream:MediaStream,peerIds:string[],quality:Quality){
  this.stream=stream;await Promise.all(peerIds.map(id=>this.offer(id,quality)));
 }
 async offer(id:string,quality:Quality){
  if(!this.stream)return;
  this.remove(id);const pc=this.peer(id);
  for(const track of this.stream.getTracks()){const sender=pc.addTrack(track,this.stream);if(track.kind==='video'){const params=sender.getParameters();params.encodings=[{maxBitrate:presets[quality].bitrate,maxFramerate:presets[quality].frameRate}];await sender.setParameters(params).catch(()=>{});}}
  const caps=RTCRtpSender.getCapabilities('video');const h264=caps?.codecs.filter(c=>c.mimeType.toLowerCase()==='video/h264')||[];
  if(h264.length){const transceiver=pc.getTransceivers().find(t=>t.sender.track?.kind==='video');const all=caps!.codecs;transceiver?.setCodecPreferences([...h264,...all.filter(c=>!h264.includes(c))]);}
  const offer=await pc.createOffer();await pc.setLocalDescription(offer);this.send({type:'offer',to:id,sdp:offer.sdp!});console.info('[WebRTC] creating offer',id);
 }
 async handle(message:ServerMessage){
  if(message.type==='offer'){this.remove(message.from);const pc=this.peer(message.from);await pc.setRemoteDescription({type:'offer',sdp:message.sdp});await this.flush(message.from);const answer=await pc.createAnswer();await pc.setLocalDescription(answer);this.send({type:'answer',to:message.from,sdp:answer.sdp!});}
  if(message.type==='answer'){const pc=this.pcs.get(message.from);if(pc){await pc.setRemoteDescription({type:'answer',sdp:message.sdp});await this.flush(message.from);}}
  if(message.type==='ice-candidate'){const pc=this.pcs.get(message.from);if(pc?.remoteDescription)await pc.addIceCandidate(message.candidate).catch(()=>{});else this.pending.set(message.from,[...(this.pending.get(message.from)||[]),message.candidate]);}
 }
 private async flush(id:string){const pc=this.pcs.get(id);for(const candidate of this.pending.get(id)||[])await pc?.addIceCandidate(candidate).catch(()=>{});this.pending.delete(id);}
 get connections(){return [...this.pcs.values()];}
 remove(id:string){this.pcs.get(id)?.close();this.pcs.delete(id);this.pending.delete(id);}
 stop(){this.stream?.getTracks().forEach(t=>t.stop());this.stream=null;for(const id of this.pcs.keys())this.remove(id);this.onRemote(null);}
}
export function preset(quality:Quality){return presets[quality];}
