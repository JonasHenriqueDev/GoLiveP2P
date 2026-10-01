import { z } from 'zod';
export const PORT = 47621;
export const MAX_PEERS = 5;
export const idSchema = z.string().uuid();
export const nameSchema = z.string().trim().min(1).max(32);
const sdp = z.string().min(1).max(200_000);
const candidate = z.object({candidate:z.string().max(4096),sdpMid:z.string().nullable().optional(),sdpMLineIndex:z.number().int().nullable().optional(),usernameFragment:z.string().nullable().optional()});
export const clientMessage = z.discriminatedUnion('type',[
 z.object({type:z.literal('join-room'),name:nameSchema}),
 z.object({type:z.literal('offer'),to:idSchema,sdp}),
 z.object({type:z.literal('answer'),to:idSchema,sdp}),
 z.object({type:z.literal('ice-candidate'),to:idSchema,candidate}),
 z.object({type:z.literal('start-stream')}),
 z.object({type:z.literal('stop-stream')})
]);
export type ClientMessage=z.infer<typeof clientMessage>;
export type Peer={id:string;name:string};
export type ServerMessage=
 |{type:'joined-room';self:Peer;peers:Peer[];streamerId:string|null}
 |{type:'user-joined'|'user-left';peer:Peer}
 |{type:'offer'|'answer';from:string;sdp:string}
 |{type:'ice-candidate';from:string;candidate:z.infer<typeof candidate>}
 |{type:'start-stream'|'stop-stream';from:string}
 |{type:'room-full'}
 |{type:'error';message:string};
export const serverMessage=z.discriminatedUnion('type',[
 z.object({type:z.literal('joined-room'),self:z.object({id:idSchema,name:nameSchema}),peers:z.array(z.object({id:idSchema,name:nameSchema})),streamerId:idSchema.nullable()}),
 z.object({type:z.literal('user-joined'),peer:z.object({id:idSchema,name:nameSchema})}),
 z.object({type:z.literal('user-left'),peer:z.object({id:idSchema,name:nameSchema})}),
 z.object({type:z.literal('offer'),from:idSchema,sdp}),z.object({type:z.literal('answer'),from:idSchema,sdp}),
 z.object({type:z.literal('ice-candidate'),from:idSchema,candidate}),
 z.object({type:z.literal('start-stream'),from:idSchema}),z.object({type:z.literal('stop-stream'),from:idSchema}),
 z.object({type:z.literal('room-full')}),z.object({type:z.literal('error'),message:z.string()})
]);
