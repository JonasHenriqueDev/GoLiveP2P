#!/usr/bin/env bash
set -euo pipefail
qt_root="$1"
build="$2"
stage="release/qt-linux"
mkdir -p "$stage/native-media/lib/gstreamer-1.0" "$stage/lib" "$stage/plugins/platforms" "$stage/plugins/tls" "$stage/licenses"
cp "$build/GoLive P2P" "$stage/GoLive P2P"
cp "$build/media-engine" "$stage/native-media/"
for module in Core Gui Widgets Network WebSockets; do cp -L "$qt_root/lib/libQt6${module}.so.6" "$stage/lib/"; done
cp "$qt_root/plugins/platforms/libqxcb.so" "$qt_root/plugins/platforms/libqoffscreen.so" "$stage/plugins/platforms/"
cp "$qt_root/plugins/tls/"*.so "$stage/plugins/tls/"
# GStreamer loads plugins dynamically. Bundle the distro plugin set and resolve
# dependencies of those plugins as well as the executable (not just ldd of UI).
cp /usr/lib/x86_64-linux-gnu/gstreamer-1.0/*.so "$stage/native-media/lib/gstreamer-1.0/"
cp /usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner "$stage/native-media/"
export LD_LIBRARY_PATH="$stage/lib:$LD_LIBRARY_PATH"
python3 - "$stage" <<'PY'
import pathlib, subprocess, re, shutil, sys
root=pathlib.Path(sys.argv[1]); todo=[root/'GoLive P2P',root/'native-media/media-engine',root/'native-media/gst-plugin-scanner',*root.glob('plugins/**/*.so'),*root.glob('native-media/lib/gstreamer-1.0/*.so'),*root.glob('lib/*.so*')]
seen=set()
while todo:
    file=todo.pop()
    if str(file) in seen: continue
    seen.add(str(file))
    result=subprocess.run(['ldd',str(file)],capture_output=True,text=True)
    for name,path in re.findall(r'\s+(\S+) => (/[\S]+)',result.stdout):
        # glibc and the ELF loader must come from the receiving system.
        if name.startswith(('libc.so','libm.so','libpthread.so','libdl.so','librt.so','libresolv.so')): continue
        target=root/'lib'/name
        if not target.exists(): shutil.copy2(path,target);todo.append(target)
    if 'not found' in result.stdout: raise SystemExit(result.stdout)
PY
cp native/qt/THIRD-PARTY.md "$stage/licenses/"
# Include the distro copyright files for the bundled multimedia packages.
for folder in /usr/share/doc/libqt* /usr/share/doc/libgst* /usr/share/doc/gstreamer* /usr/share/doc/libav* /usr/share/doc/libnice*; do
  if [ -f "$folder/copyright" ]; then cp "$folder/copyright" "$stage/licenses/$(basename "$folder")-copyright"; fi
done
cat > "$stage/golive" <<'SH'
#!/usr/bin/env bash
set -e
HERE="$(cd -- "$(dirname -- "$0")" && pwd)"
export LD_LIBRARY_PATH="$HERE/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$HERE/plugins"
export GST_PLUGIN_PATH="$HERE/native-media/lib/gstreamer-1.0"
export GST_PLUGIN_SYSTEM_PATH="$GST_PLUGIN_PATH"
export GST_PLUGIN_SCANNER="$HERE/native-media/gst-plugin-scanner"
exec "$HERE/GoLive P2P" "$@"
SH
chmod +x "$stage/golive"
printf '[Paths]\nPlugins=plugins\nLibraries=lib\n' > "$stage/qt.conf"
tar -czf release/GoLive-P2P-0.6.0-qt.1-linux-x64.tar.gz -C release qt-linux
sha256sum release/GoLive-P2P-0.6.0-qt.1-linux-x64.tar.gz > release/SHA256SUMS-linux-0.6.0-qt.1.txt
