#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

// Single-source stereo FIFO. The owner must clear it as soon as attribution fails.
// A small capture reserve absorbs WASAPI bursts independently of network jitter.
class AudioFifo {
  std::deque<int16_t> samples;
  double position = 0, rate = 1;
  bool primed = false;
 public:
  static constexpr size_t ReserveFrames = 3840; // 80 ms at 48 kHz
  static constexpr size_t CapacityFrames = 14400; // bounded to 300 ms
  uint64_t underruns = 0, trimmedFrames = 0, renderedFrames = 0;
  size_t frames() const { return samples.size() / 2; }
  void clear() { samples.clear(); position = 0; rate = 1; primed = false; }
  void push(const int16_t* data, size_t frames, bool silent) {
    for (size_t i = 0; i < frames * 2; ++i) samples.push_back(silent ? 0 : data[i]);
    if (this->frames() > CapacityFrames) {
      const auto discard = this->frames() - ReserveFrames;
      for (size_t i = 0; i < discard * 2; ++i) samples.pop_front();
      trimmedFrames += discard;
      position = 0;
    }
  }
  void mix(std::vector<int32_t>& output) {
    const auto wanted = output.size() / 2;
    if (!primed) {
      if (frames() < ReserveFrames) return;
      primed = true;
    }
    // Correct clock drift gradually; dropping complete packets produces clicks.
    const auto error = (static_cast<double>(frames()) - ReserveFrames) / ReserveFrames;
    const double desired = 1 + std::clamp(error * 0.004, -0.004, 0.004);
    rate += (desired - rate) * 0.02;
    if (frames() < static_cast<size_t>(std::ceil(position + wanted * rate)) + 1) {
      ++underruns;
      primed = false;
      position = 0;
      return;
    }
    for (size_t frame = 0; frame < wanted; ++frame) {
      const auto index = static_cast<size_t>(position);
      const auto fraction = position - index;
      for (size_t channel = 0; channel < 2; ++channel) {
        const auto a = samples[index * 2 + channel], b = samples[(index + 1) * 2 + channel];
        output[frame * 2 + channel] += static_cast<int32_t>(std::lround(a + (b - a) * fraction));
      }
      position += rate;
    }
    const auto consumed = static_cast<size_t>(position);
    for (size_t i = 0; i < consumed * 2; ++i) samples.pop_front();
    position -= consumed;
    renderedFrames += wanted;
  }
};
