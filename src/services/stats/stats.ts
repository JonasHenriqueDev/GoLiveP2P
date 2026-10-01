export type StreamStats={resolution:string;fps:number;bitrate:number;rtt:number|null;lost:number;codec:string;ice:string;connection:string};
export class StatsCollector {
 private previous=new Map<string,{bytes:number;time:number}>();
 async collect(pcs:RTCPeerConnection[]):Promise<StreamStats>{
  const result:StreamStats={resolution:'—',fps:0,bitrate:0,rtt:null,lost:0,codec:'—',ice:'—',connection:'—'};
  for(const [index,pc] of pcs.entries()){
   result.ice=pc.iceConnectionState;result.connection=pc.connectionState;
   const reports=await pc.getStats();
   reports.forEach(report=>{
    const item=report as any;
    if(item.type==='codec' && item.mimeType?.startsWith('video/'))result.codec=item.mimeType;
    if(item.type==='candidate-pair' && item.state==='succeeded' && item.currentRoundTripTime!=null)result.rtt=Math.round(item.currentRoundTripTime*1000);
    if(item.type==='outbound-rtp'||item.type==='inbound-rtp'){
     if(item.kind!=='video')return;
     if(item.frameWidth&&item.frameHeight)result.resolution=item.frameWidth+'×'+item.frameHeight;
     result.fps=Math.max(result.fps,Math.round(item.framesPerSecond||0));
     result.lost+=item.packetsLost||0;
     const key=index+':'+item.id;const old=this.previous.get(key);
     if(old&&item.bytesSent!=null)result.bitrate+=Math.max(0,(item.bytesSent-old.bytes)*8/(item.timestamp-old.time)/1000);
     this.previous.set(key,{bytes:item.bytesSent||0,time:item.timestamp});
    }
   });
  }
  result.bitrate=Math.round(result.bitrate*10)/10;return result;
 }
}
