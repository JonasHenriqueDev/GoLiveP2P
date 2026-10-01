import { z } from 'zod';
import { idSchema } from './protocol';

export const sourceSchema = z.object({
  id: z.string().regex(/^(monitor|window):[1-9][0-9]{0,19}$/),
  name: z.string().max(4096),
  kind: z.enum(['screen', 'window']),
  thumbnail: z.string().max(2_000_000),
  pid: z.number().int().positive().optional(),
});
export const captureSettings = z
  .strictObject({
    source: sourceSchema.shape.id,
    method: z.enum(['wgc', 'dxgi']),
    encoder: z.enum(['auto', 'software']),
    width: z.number().int().min(320).max(2560),
    height: z.number().int().min(180).max(1440),
    fps: z.union([z.literal(30), z.literal(60)]),
    bitrate: z.number().int().min(500_000).max(20_000_000),
    audio: z.boolean(),
    allowedAudioApps: z.array(z.string().min(1).max(32768)).max(32).optional(),
  })
  .refine(
    (value) => !value.source.startsWith('window:') || value.method === 'wgc',
    'DXGI só captura monitores',
  );
const candidate = z.strictObject({
  candidate: z.string().max(4096),
  sdpMLineIndex: z.number().int().min(0).max(8).nullable().optional(),
  sdpMid: z.string().nullable().optional(),
  usernameFragment: z.string().nullable().optional(),
});
export const nativeSignal = z.discriminatedUnion('type', [
  z.strictObject({
    type: z.literal('offer'),
    peer: idSchema,
    sdp: z.string().min(1).max(200_000),
  }),
  z.strictObject({
    type: z.literal('answer'),
    peer: idSchema,
    sdp: z.string().min(1).max(200_000),
  }),
  z.strictObject({
    type: z.literal('ice-candidate'),
    peer: idSchema,
    candidate,
  }),
]);
export const nativeRequests = {
  capabilities: z.strictObject({}),
  sources: z.strictObject({}),
  start: captureSettings,
  offer: z.strictObject({ peer: idSchema }),
  signal: nativeSignal,
  remove: z.strictObject({ peer: idSchema }),
  stop: z.strictObject({}),
  bitrate: z.strictObject({
    bitrate: z.number().int().min(500_000).max(20_000_000),
  }),
  stats: z.strictObject({}),
  pcm: z.strictObject({
    data: z
      .string()
      .regex(/^[A-Za-z0-9+/]*={0,2}$/)
      .max(262144),
  }),
  'audio-sessions': z.strictObject({}),
};
export type NativeMethod = keyof typeof nativeRequests;
export type NativeData<M extends NativeMethod> = z.infer<
  (typeof nativeRequests)[M]
>;
export type CaptureSettings = z.infer<typeof captureSettings>;
export type NativeSource = z.infer<typeof sourceSchema>;
export const capabilitiesSchema = z.object({
  runtime: z.string(),
  capture: z.object({ wgc: z.boolean(), dxgi: z.boolean() }),
  nvenc: z.boolean(),
  openh264: z.boolean(),
  webrtc: z.boolean(),
});
export const nativeStatsSchema = z.object({
  encodedFrames: z.number().int().nonnegative(),
  width: z.number().int(),
  height: z.number().int(),
  peers: z.record(
    idSchema,
    z.object({
      raw: z.record(z.string(), z.unknown()),
      receivedFrames: z.number().int().nonnegative(),
      width: z.number().int(),
      height: z.number().int(),
      connection: z.number().int().min(0).max(5),
      ice: z.number().int().min(0).max(6),
    }),
  ),
});
export const startResultSchema = z.object({
  encoder: z.string(),
  method: z.enum(['wgc', 'dxgi']),
  borderRemovalVerified: z.literal(false),
  audio: z.boolean(),
});
export type NativeResults = {
  capabilities: z.infer<typeof capabilitiesSchema>;
  sources: NativeSource[];
  start: z.infer<typeof startResultSchema>;
  stats: z.infer<typeof nativeStatsSchema>;
  offer: null;
  signal: null;
  remove: null;
  stop: null;
  bitrate: null;
  pcm: null;
  'audio-sessions': {
    pid: number;
    name: string;
    image: string;
    allowed: boolean;
    reason: string;
  }[];
};
export const nativeResults = {
  capabilities: capabilitiesSchema,
  sources: z.array(sourceSchema).max(1024),
  start: startResultSchema,
  stats: nativeStatsSchema,
  offer: z.null(),
  signal: z.null(),
  remove: z.null(),
  stop: z.null(),
  bitrate: z.null(),
  pcm: z.null(),
  'audio-sessions': z
    .array(
      z.object({
        pid: z.number().int().positive(),
        name: z.string(),
        image: z.string(),
        allowed: z.boolean(),
        reason: z.string(),
      }),
    )
    .max(1024),
};
export const nativeEvent = z.discriminatedUnion('event', [
  z.strictObject({
    v: z.literal(1),
    event: z.literal('ready'),
    protocol: z.literal(1),
  }),
  z.strictObject({
    v: z.literal(1),
    event: z.literal('audio-state'),
    active: z.boolean(),
    sources: z.number().int().nonnegative(),
  }),
  z.strictObject({
    v: z.literal(1),
    event: z.literal('frame'),
    peer: z.union([idSchema, z.literal('local')]),
    jpeg: z
      .string()
      .regex(/^[A-Za-z0-9+/]+={0,2}$/)
      .max(2_000_000),
  }),
  z.strictObject({
    v: z.literal(1),
    event: z.literal('state'),
    peer: idSchema,
    state: z.enum([
      'new',
      'connecting',
      'connected',
      'disconnected',
      'failed',
      'closed',
    ]),
  }),
  z
    .strictObject({
      v: z.literal(1),
      event: z.literal('signal'),
      peer: idSchema,
      type: z.enum(['offer', 'answer', 'ice-candidate']),
      sdp: z.string().max(200_000).optional(),
      candidate: candidate.optional(),
    })
    .refine(
      (value) =>
        value.type === 'ice-candidate'
          ? !!value.candidate && !value.sdp
          : !!value.sdp && !value.candidate,
      'Native signal payload does not match type',
    ),
  z.strictObject({
    v: z.literal(1),
    event: z.literal('error'),
    peer: z.union([idSchema, z.literal('local')]).optional(),
    message: z.string().max(16384),
  }),
  z.strictObject({
    v: z.literal(1),
    event: z.literal('warning'),
    peer: z.union([idSchema, z.literal('local')]).optional(),
    message: z.string().max(16384),
  }),
]);
export type NativeEvent = z.infer<typeof nativeEvent>;
export const nativeResponse = z.discriminatedUnion('ok', [
  z.strictObject({
    v: z.literal(1),
    id: z.number().int().positive(),
    ok: z.literal(true),
    result: z.unknown(),
  }),
  z.strictObject({
    v: z.literal(1),
    id: z.number().int().positive().nullable(),
    ok: z.literal(false),
    error: z.string().max(16384),
  }),
]);
