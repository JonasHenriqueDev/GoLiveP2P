import type { TailscaleStatus } from '../services/tailscale/status';
declare global {interface Window {golive:{
 tailscaleStatus:()=>Promise<TailscaleStatus>;
 createRoom:()=>Promise<{ip:string;port:number}>;
 closeRoom:()=>Promise<void>;
 listSources:()=>Promise<{id:string;name:string;thumbnail:string}[]>;
 selectSource:(id:string)=>Promise<void>;
}}}
export {};
