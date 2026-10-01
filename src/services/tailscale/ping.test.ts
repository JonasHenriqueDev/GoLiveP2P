import { describe, expect, it } from 'vitest';
import { parsePing } from './ping';
describe('Tailscale peer ping', () => {
  it('parses direct and DERP RTT', () => {
    expect(parsePing('pong from pc (100.90.1.2) via 100.90.1.2:41641 in 18ms')).toEqual({ ms: 18, route: 'direct' });
    expect(parsePing('pong from pc (100.90.1.2) via DERP(sao) in 92ms')).toEqual({ ms: 92, route: 'DERP' });
  });
  it('returns unknown when no response exists', () => expect(parsePing('no pong')).toEqual({ ms: null, route: 'unknown' }));
});
