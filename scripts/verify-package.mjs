import { readFileSync, existsSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { join, resolve } from 'node:path';
const root = resolve(process.argv[2] || 'release/win-unpacked');
const native = join(root, 'resources/native-media');
const manifest = JSON.parse(
  readFileSync(join(native, 'manifest.json'), 'utf8').replace(/^\uFEFF/, ''),
);
for (const item of manifest) {
  const path = resolve(native, item.path);
  if (!path.startsWith(native + '\\') && !path.startsWith(native + '/'))
    throw new Error('Invalid manifest path');
  const data = readFileSync(path);
  if (
    data.length !== item.bytes ||
    createHash('sha256').update(data).digest('hex') !== item.sha256
  )
    throw new Error('Packaged native hash mismatch: ' + item.path);
}
for (const relative of [
  'resources/app.asar',
  'resources/app-update.yml',
  'resources/audio-capture.exe',
  'resources/native-media/media-engine.exe',
  'resources/native-media/lib/gstreamer-1.0/gstwebrtc.dll',
  'resources/native-media/lib/gstreamer-1.0/gstnice.dll',
  'resources/native-media/licenses/sdk/gstreamer-1.0/README-LICENSE-INFO.txt',
])
  if (!existsSync(join(root, relative)))
    throw new Error('Required package file missing: ' + relative);
console.log(
  `Packaged runtime verified: ${manifest.length} files and updater configuration.`,
);
