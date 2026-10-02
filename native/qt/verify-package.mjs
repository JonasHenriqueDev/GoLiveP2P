import {readFileSync,readdirSync,statSync,existsSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve,join,sep} from 'node:path';
const root=resolve(process.argv[2]||'release/qt-unpacked');
const manifest=JSON.parse(readFileSync(join(root,'package-manifest.json'),'utf8').replace(/^\uFEFF/,''));
for(const entry of manifest){const path=resolve(root,entry.path);if(!path.startsWith(root+sep))throw Error('Invalid manifest path');const bytes=readFileSync(path);if(bytes.length!==entry.bytes||createHash('sha256').update(bytes).digest('hex')!==entry.sha256)throw Error('Package mismatch: '+entry.path);}
for(const file of ['GoLive P2P.exe','Qt6Core.dll','Qt6Widgets.dll','Qt6WebSockets.dll','platforms/qwindows.dll','tls/qschannelbackend.dll','vcruntime140.dll','msvcp140.dll','native-media/media-engine.exe','native-media/window-capture.exe','licenses/LGPL-3.0-only.txt'])if(!existsSync(join(root,file)))throw Error('Missing '+file);
function check(folder){for(const name of readdirSync(folder)){const path=join(folder,name);if(statSync(path).isDirectory())check(path);else if(/^(electron\.exe|app\.asar|chrome_.*|snapshot_blob\.bin)$/i.test(name))throw Error('Unexpected Electron runtime: '+path);}}
check(root);console.log('Qt-only package verified: '+manifest.length+' files, hashes and required runtime.');
