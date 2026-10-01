import { describe, expect, it } from 'vitest';
import { normalizeBitrate } from './mesh';
describe('bitrate limit', () => {
  it('accepts user values and clamps the safe range', () => {
    expect(normalizeBitrate(5.5)).toBe(5_500_000);
    expect(normalizeBitrate(0)).toBe(500_000);
    expect(normalizeBitrate(50)).toBe(20_000_000);
    expect(() => normalizeBitrate(NaN)).toThrow();
  });
});
