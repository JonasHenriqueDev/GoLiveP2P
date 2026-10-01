import type { TailscaleStatus } from '../services/tailscale/status';
declare global {interface Window {golive:{
 platform:string;
 tailscaleStatus:()=>Promise<TailscaleStatus>;
 discoverHosts:()=>Promise<{ip:string;name:string;participants:number;full:boolean}[]>;
 createRoom:()=>Promise<{ip:string;port:number}>;
 closeRoom:()=>Promise<void>;
 listSources:()=>Promise<{id:string;name:string;thumbnail:string}[]>;
 selectSource:(id:string)=>Promise<void>;
}}}
export {};
