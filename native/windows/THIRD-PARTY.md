# Native runtime dependencies

Pinned SDK: GStreamer 1.26.7 MSVC x64, dynamically linked. Official binaries and matching source: https://gstreamer.freedesktop.org/data/pkg/windows/1.26.7/msvc/ and https://gstreamer.freedesktop.org/src/ .

GStreamer core and most plugins are LGPL-2.1-or-later; libnice is LGPL/MPL; OpenH264 is BSD; nlohmann/json 3.12.0 is MIT. The distribution includes optional plugins with their own licenses, including GPL plugins. Do not publish a runtime before inspecting the complete license inventory, copying the applicable license texts and satisfying source/relinking obligations. Source and dynamic libraries must remain replaceable. H.264 patent licensing requires a separate distribution review.

This is a development runtime, not a completed release license audit. `manifest.json` records every staged dependency and its SHA-256. The release gate must reject missing license inventory.
