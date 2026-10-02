#include "audio-fifo.hpp"
#include "audio-pacer.hpp"
#include <iostream>
#include <stdexcept>
static void expect(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
  try {
    AudioFifo burst;
    std::vector<int16_t> pending;
    unsigned next = 3, pattern = 0, silent = 0;
    const unsigned intervals[]{3, 1, 4, 2, 1, 3, 2};
    for (unsigned tick = 0; tick < 2000; ++tick) {
      pending.insert(pending.end(), 960, 1200);
      const bool delivered = tick == next;
      if (delivered) { burst.push(pending.data(), pending.size() / 2, false); pending.clear(); next += intervals[pattern++ % 7]; }
      std::vector<int32_t> output(960);
      burst.mix(output);
      if (tick > 30 && output[100] == 0) ++silent;
    }
    expect(silent == 0 && burst.underruns == 0, "Capture burst inserted silence");
    AudioFifo drift;
    size_t maximum = 0;
    for (unsigned tick = 0; tick < 30000; ++tick) {
      const size_t frames = 480 + (tick % 2 == 0 ? 1 : 0) + (tick % 5 == 0 ? 1 : 0);
      std::vector<int16_t> samples(frames * 2, 1400);
      drift.push(samples.data(), frames, false);
      std::vector<int32_t> output(960); drift.mix(output);
      maximum = std::max(maximum, drift.frames());
    }
    expect(drift.underruns == 0 && drift.trimmedFrames == 0 && maximum < 12000, "Audio clock drift accumulated latency");
    drift.clear();
    std::vector<int32_t> blocked(960); drift.mix(blocked);
    expect(std::all_of(blocked.begin(), blocked.end(), [](auto v) { return v == 0; }), "Unsafe source retained buffered audio");
    AudioPacer clock;
    uint64_t emitted = 0, now = 0;
    for (unsigned tick = 0; tick < 3000; ++tick) {
      now += tick % 17 == 0 ? 45000 : 7000;
      emitted += clock.due(now);
      expect(emitted == now / 10000 + 1, "Coalesced scheduler wake lost samples");
    }
    now += 300000; emitted += clock.due(now);
    expect(clock.skippedPackets > 0 && emitted + clock.skippedPackets == now / 10000 + 1, "Unbounded scheduler catch-up");
    std::cout << "{\"captureBurstSilentPackets\":" << silent
              << ",\"driftUnderruns\":" << drift.underruns << ",\"driftTrimmedFrames\":" << drift.trimmedFrames
              << ",\"driftMaximumBufferedMs\":" << maximum / 48.0 << ",\"boundedScheduling\":true,\"unsafeClear\":true}\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
