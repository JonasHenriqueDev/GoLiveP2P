import { z } from 'zod';

export const PORT = 47621;
export const MAX_PEERS = 5;
export const RECONNECT_GRACE_MS = 30_000;
export const idSchema = z.string().uuid();
export const nameSchema = z.string().trim().min(1).max(32);
export const tokenSchema = z.string().regex(/^[a-f0-9]{64}$/);
const sdp = z.string().min(1).max(200_000);
const candidate = z.object({
  candidate: z.string().max(4096),
  sdpMid: z.string().nullable().optional(),
  sdpMLineIndex: z.number().int().nullable().optional(),
  usernameFragment: z.string().nullable().optional()
});
const peerSchema = z.object({ id: idSchema, name: nameSchema });

export const clientMessage = z.discriminatedUnion('type', [
  z.object({ type: z.literal('join-room'), name: nameSchema, resumeToken: tokenSchema.optional() }),
  z.object({ type: z.literal('leave-room') }),
  z.object({ type: z.literal('offer'), to: idSchema, sdp }),
  z.object({ type: z.literal('answer'), to: idSchema, sdp }),
  z.object({ type: z.literal('ice-candidate'), to: idSchema, candidate }),
  z.object({ type: z.literal('request-restart'), to: idSchema }),
  z.object({ type: z.literal('start-stream') }),
  z.object({ type: z.literal('stop-stream') })
]);
export type ClientMessage = z.infer<typeof clientMessage>;
export type Peer = z.infer<typeof peerSchema>;
export type ServerMessage =
  | { type: 'joined-room'; self: Peer; peers: Peer[]; streamerId: string | null; resumeToken: string; resumed: boolean }
  | { type: 'user-joined' | 'user-left'; peer: Peer }
  | { type: 'offer' | 'answer'; from: string; sdp: string }
  | { type: 'ice-candidate'; from: string; candidate: z.infer<typeof candidate> }
  | { type: 'request-restart'; from: string }
  | { type: 'start-stream' | 'stop-stream'; from: string }
  | { type: 'room-full' }
  | { type: 'error'; message: string };

export const serverMessage = z.discriminatedUnion('type', [
  z.object({ type: z.literal('joined-room'), self: peerSchema, peers: z.array(peerSchema), streamerId: idSchema.nullable(), resumeToken: tokenSchema, resumed: z.boolean() }),
  z.object({ type: z.literal('user-joined'), peer: peerSchema }),
  z.object({ type: z.literal('user-left'), peer: peerSchema }),
  z.object({ type: z.literal('offer'), from: idSchema, sdp }),
  z.object({ type: z.literal('answer'), from: idSchema, sdp }),
  z.object({ type: z.literal('ice-candidate'), from: idSchema, candidate }),
  z.object({ type: z.literal('request-restart'), from: idSchema }),
  z.object({ type: z.literal('start-stream'), from: idSchema }),
  z.object({ type: z.literal('stop-stream'), from: idSchema }),
  z.object({ type: z.literal('room-full') }),
  z.object({ type: z.literal('error'), message: z.string() })
]);
