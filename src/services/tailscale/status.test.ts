import {describe,it,expect} from 'vitest';
import {parseTailscaleStatus,tailscaleCommands} from './status';
describe('Tailscale status',()=>{
 it('finds connected IPv4',()=>expect(parseTailscaleStatus('{"BackendState":"Running"}','100.101.2.3\n').ip).toBe('100.101.2.3'));
 it('rejects stopped backend',()=>expect(parseTailscaleStatus('{"BackendState":"Stopped"}','100.101.2.3').connected).toBe(false));
 it('handles malformed JSON',()=>expect(parseTailscaleStatus('bad','').connected).toBe(false));
 it('finds the standard Windows installation when absent from PATH',()=>{
  expect(tailscaleCommands('win32',{ProgramFiles:'C:\\Program Files'})).toContain('C:\\Program Files\\Tailscale\\tailscale.exe');
 });
});
