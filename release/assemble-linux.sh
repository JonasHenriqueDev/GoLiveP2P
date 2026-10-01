#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
cat GoLive-P2P-0.2.0.AppImage.part00 GoLive-P2P-0.2.0.AppImage.part01 > GoLive-P2P-0.2.0.AppImage
printf '%s  %s\n' 'c8dd87c177a728bebc397ee7bfe06b526861ec17a85918753979648f2601e07a' 'GoLive-P2P-0.2.0.AppImage' | sha256sum --check
chmod +x GoLive-P2P-0.2.0.AppImage
