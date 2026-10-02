#pragma once
#include <gst/gst.h>

namespace ReceiveBuffer {
// Shared RTP latency preserves the audio/video receive budget. 40 ms is too
// aggressive for short tailnet delivery bursts; 120 ms allows 12 Opus packets
// at the current 10 ms packetization without the default 200 ms network delay.
constexpr guint JitterMs = 120;
constexpr guint SenderJitterMs = 40;
inline void configure(GstElement* rtc, bool sender) {
  g_object_set(rtc, "latency", sender ? SenderJitterMs : JitterMs, nullptr);
}
}  // namespace ReceiveBuffer
