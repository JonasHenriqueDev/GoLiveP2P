import {describe,it,expect} from 'vitest';
import {clientMessage,nameSchema} from './protocol';
describe('signaling protocol',()=>{
 it('validates names',()=>{expect(nameSchema.safeParse(' Jonas ').success).toBe(true);expect(nameSchema.safeParse('').success).toBe(false);});
 it('rejects spoofed identity and arbitrary messages',()=>{
  expect(clientMessage.safeParse({type:'offer',from:'attacker',to:'bad',sdp:'x'}).success).toBe(false);
  expect(clientMessage.safeParse({type:'delete-room'}).success).toBe(false);
 });
 it('bounds log reports and validates their recipient',()=>{
  const to='00000000-0000-4000-8000-000000000001';
  expect(clientMessage.safeParse({type:'log-report',to,text:'diagnostic'}).success).toBe(true);
  expect(clientMessage.safeParse({type:'log-report',to,text:'x'.repeat(120001)}).success).toBe(false);
  expect(clientMessage.safeParse({type:'log-report',to:'other',text:'diagnostic'}).success).toBe(false);
 });
 it('accepts valid join',()=>expect(clientMessage.safeParse({type:'join-room',name:'Jonas'}).success).toBe(true));
});
