#pragma once
#include <algorithm>
#include <cstdint>
// Absolute sample deadlines, independent of how many timer wakes Windows delivers.
class AudioPacer {
  uint64_t next = 0;
 public:
  uint64_t skippedPackets = 0, maxLatenessUs = 0;
  unsigned due(uint64_t now) {
    if (now < next) return 0;
    maxLatenessUs = std::max(maxLatenessUs, now - next);
    auto packets = (now - next) / 10000 + 1;
    if (packets > 8) { skippedPackets += packets - 8; next += (packets - 8) * 10000; packets = 8; }
    next += packets * 10000;
    return static_cast<unsigned>(packets);
  }
  uint64_t waitUs(uint64_t now) const { return next > now ? next - now : 1; }
};
