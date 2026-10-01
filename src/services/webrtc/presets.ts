export const PRESETS = {
  '720p30': { width: 1280, height: 720, frameRate: 30, bitrate: 3_000_000, label: '720p · 30 FPS' },
  '720p60': { width: 1280, height: 720, frameRate: 60, bitrate: 4_000_000, label: '720p · 60 FPS' },
  '1080p30': { width: 1920, height: 1080, frameRate: 30, bitrate: 4_500_000, label: '1080p · 30 FPS' },
  '1080p60': { width: 1920, height: 1080, frameRate: 60, bitrate: 6_000_000, label: '1080p · 60 FPS' },
  '1440p30': { width: 2560, height: 1440, frameRate: 30, bitrate: 8_000_000, label: '1440p · 30 FPS' }
} as const;
export type Quality = keyof typeof PRESETS;
export function preset(quality: Quality) { return PRESETS[quality]; }
