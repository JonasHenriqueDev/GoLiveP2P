import { app, dialog } from 'electron';
import { appendFileSync, existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { arch, release, version } from 'node:os';
import { inspect } from 'node:util';
import { LOG_LIMIT } from '../shared/protocol';

type Level = 'info' | 'warn' | 'error';
let file: string | null = null;
const recent: string[] = [];
function safe(value: unknown): string {
  if (value instanceof Error) return value.stack || value.message;
  if (typeof value === 'string') return value;
  if (Array.isArray(value)) return value.map(safe).join(' ');
  return inspect(value, { depth: 3, breakLength: Infinity, maxArrayLength: 20 });
}
export function systemInfo() {
  return { name: version(), release: release(), arch: arch() };
}
export function record(level: Level, scope: string, message: unknown, details?: unknown) {
  const line = [new Date().toISOString(), level.toUpperCase(), '[' + scope.slice(0, 40) + ']', safe(message), details == null ? '' : safe(details)]
    .join(' ').replace(/[\r\n]+/g, ' ').slice(0, 4000);
  recent.push(line);
  if (recent.length > 1000) recent.shift();
  if (!file) return;
  try {
    if (existsSync(file) && statSync(file).size > 2_000_000) {
      writeFileSync(file + '.previous', readFileSync(file));
      writeFileSync(file, '');
    }
    appendFileSync(file, line + '\n', 'utf8');
  } catch { /* Logging must never break streaming. */ }
}
export function setupLogs() {
  const directory = join(app.getPath('userData'), 'logs');
  mkdirSync(directory, { recursive: true });
  file = join(directory, 'golive.log');
  const original = { info: console.info, warn: console.warn, error: console.error };
  for (const level of ['info', 'warn', 'error'] as const) {
    console[level] = (...args: unknown[]) => { original[level](...args); record(level, 'Main', args[0], args.slice(1)); };
  }
  process.on('uncaughtException', error => { record('error', 'Main', error); app.exit(1); });
  process.on('unhandledRejection', error => record('error', 'Main', error));
  record('info', 'App', 'Started', { version: app.getVersion(), platform: process.platform, os: systemInfo() });
}
export function reportText(): string {
  let lines = recent;
  try { if (file) lines = readFileSync(file, 'utf8').split('\n'); } catch { /* Use in-memory records. */ }
  const os = systemInfo();
  const header = 'GoLive P2P diagnostics\nVersion: ' + app.getVersion() + '\nPlatform: ' + process.platform + '\nOS: ' + os.name + ' (' + os.release + '; ' + os.arch + ')\n\n';
  const body = Buffer.from(lines.join('\n'), 'utf8');
  return header + body.subarray(Math.max(0, body.length - (LOG_LIMIT - 256))).toString('utf8');
}
export async function saveReport(text: string, defaultName = 'golive-log.txt'): Promise<boolean> {
  if (typeof text !== 'string' || text.length > LOG_LIMIT) throw new Error('Log inválido');
  const result = await dialog.showSaveDialog({ defaultPath: defaultName, filters: [{ name: 'Text', extensions: ['txt'] }] });
  if (result.canceled || !result.filePath) return false;
  writeFileSync(result.filePath, text, 'utf8');
  return true;
}
