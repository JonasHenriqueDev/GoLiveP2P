#!/usr/bin/env bash
set -euo pipefail
: "${QT_ROOT:?Set QT_ROOT to the Qt 6.8 SDK directory}"
cmake -S native/qt -B work/qt-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT"
cmake --build work/qt-linux
ctest --test-dir work/qt-linux --output-on-failure
bash native/qt/package-linux.sh "$QT_ROOT" work/qt-linux
