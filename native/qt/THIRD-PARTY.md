# GoLive Qt dependencies

Qt 6.8.3 MSVC x64: Qt Core, Gui, Widgets, Network, WebSockets and deployment plugins, dynamically linked. Open-source LGPLv3/GPL licensing applies as specified by each module; Qt Test is a development dependency. Upstream source: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ . Qt license information: https://doc.qt.io/qt-6.8/licensing.html . This application does not statically link Qt or use Qt WebEngine.

Redistributed MSVC runtime comes from the Qt deployment tool / Visual Studio redistributable. GStreamer dependency inventory, licenses and restrictions are in native-media/licenses and native-media/manifest.json. Full audit of optional native plugins remains pending; experimental packages are not a completed migration claim.

Qtbase 6.8.3 source archive SHA-256: `56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80`. Copyright/license notices extracted from the official source archive are included under licenses/qtbase-6.8.3. Qt binaries remain dynamically replaceable.
