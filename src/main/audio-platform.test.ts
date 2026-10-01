import { describe, expect, it } from 'vitest';
import { canTryProcessLoopback } from './audio-platform';

describe('Windows process-loopback eligibility', () => {
  it('tries updated Windows 10 and Windows 11 builds', () => {
    expect(canTryProcessLoopback('win32', '10.0.19045')).toBe(true);
    expect(canTryProcessLoopback('win32', '10.0.22631')).toBe(true);
  });
  it('rejects older or malformed systems', () => {
    expect(canTryProcessLoopback('win32', '10.0.18363')).toBe(false);
    expect(canTryProcessLoopback('win32', 'unknown')).toBe(false);
    expect(canTryProcessLoopback('linux', '6.8.0')).toBe(false);
  });
});
