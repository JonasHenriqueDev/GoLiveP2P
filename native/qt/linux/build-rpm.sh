#!/usr/bin/env bash
set -euo pipefail
archive="${1:?Pass the verified Qt Linux tarball}"
repo="$(pwd)"
top="$repo/work/qt-rpmbuild"
mkdir -p "$top"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS} release
cp "$archive" "$top/SOURCES/GoLive-P2P-0.6.0-qt.3-linux-x64.tar.gz"
cp native/qt/linux/golive-p2p.desktop "$top/SOURCES/"
cp native/qt/assets/golive-icon-256.png "$top/SOURCES/golive-p2p.png"
rpmbuild -bb --define "_topdir $top" native/qt/linux/golive-p2p.spec
rpm_path="$(find "$top/RPMS/x86_64" -name '*.rpm' -print -quit)"
test -n "$rpm_path"
cp "$rpm_path" release/GoLive-P2P-0.6.0-qt.3-fedora-x86_64.rpm
sha256sum release/GoLive-P2P-0.6.0-qt.3-fedora-x86_64.rpm > release/SHA256SUMS-rpm-0.6.0-qt.3.txt
