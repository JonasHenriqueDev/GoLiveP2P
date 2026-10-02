#include "receive-buffer.hpp"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

struct Result {
  guint64 pushed = 0, lost = 0, late = 0;
};
static GstFlowReturn sample(GstAppSink* sink, gpointer user) {
  auto value = gst_app_sink_pull_sample(sink);
  if (!value) return GST_FLOW_EOS;
  ++*static_cast<std::atomic<unsigned>*>(user);
  gst_sample_unref(value);
  return GST_FLOW_OK;
}
static GstBuffer* packet(unsigned sequence) {
  // RTP header, SSRC=1, payload type 97, 48 kHz / 10 ms Opus timing.
  guint8 data[15] = {0x80, 97, guint8(sequence >> 8), guint8(sequence), 0, 0, 0, 0,
                     0, 0, 0, 1, 0xf8, 0xff, 0xfe};
  const auto timestamp = sequence * 480;
  for (int i = 0; i < 4; ++i) data[4 + i] = guint8(timestamp >> (24 - 8 * i));
  auto buffer = gst_buffer_new_allocate(nullptr, sizeof(data), nullptr);
  gst_buffer_fill(buffer, 0, data, sizeof(data));
  return buffer;
}
static Result run(guint latency, bool burst) {
  GError* error = nullptr;
  auto pipeline = gst_parse_launch(
      "appsrc name=in is-live=true format=time do-timestamp=true "
      "caps=\"application/x-rtp,media=audio,encoding-name=OPUS,payload=97,clock-rate=48000\" "
      "! rtpjitterbuffer name=jb do-lost=true ! "
      "appsink name=out emit-signals=true sync=false async=false", &error);
  if (!pipeline || error) throw std::runtime_error(error ? error->message : "Pipeline failed");
  auto input = gst_bin_get_by_name(GST_BIN(pipeline), "in");
  auto jitter = gst_bin_get_by_name(GST_BIN(pipeline), "jb");
  auto output = gst_bin_get_by_name(GST_BIN(pipeline), "out");
  std::atomic<unsigned> count{0};
  g_object_set(jitter, "latency", latency, nullptr);
  g_signal_connect(output, "new-sample", G_CALLBACK(sample), &count);
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  auto start = std::chrono::steady_clock::now();
  std::vector<unsigned> held;
  for (unsigned i = 0; i < 120; ++i) {
    std::this_thread::sleep_until(start + std::chrono::milliseconds(i * 10));
    // Delay three packets up to 80 ms while subsequent packets keep arriving.
    // Reordering reveals the gap to RTP and exercises the real loss deadline.
    if (burst && (i % 40 >= 15 && i % 40 < 18)) {
      held.push_back(i);
      continue;
    }
    if (i % 40 == 23) {
      for (auto sequence : held)
        gst_app_src_push_buffer(GST_APP_SRC(input), packet(sequence));
      held.clear();
    }
    gst_app_src_push_buffer(GST_APP_SRC(input), packet(i));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(latency + 150));
  GstStructure* stats = nullptr;
  g_object_get(jitter, "stats", &stats, nullptr);
  Result result;
  if (!stats) throw std::runtime_error("Jitterbuffer statistics missing");
  gst_structure_get_uint64(stats, "num-pushed", &result.pushed);
  gst_structure_get_uint64(stats, "num-lost", &result.lost);
  gst_structure_get_uint64(stats, "num-late", &result.late);
  gst_structure_free(stats);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  if (count != result.pushed) throw std::runtime_error("Output/statistics mismatch");
  gst_object_unref(input); gst_object_unref(jitter); gst_object_unref(output);
  gst_object_unref(pipeline);
  return result;
}
int main(int argc, char** argv) {
  gst_init(&argc, &argv);
  try {
    auto rtc = gst_element_factory_make("webrtcbin", nullptr);
    if (!rtc) throw std::runtime_error("WebRTC unavailable");
    ReceiveBuffer::configure(rtc, false);
    guint latency = 0;
    g_object_get(rtc, "latency", &latency, nullptr);
    if (latency != ReceiveBuffer::JitterMs) throw std::runtime_error("Receiver budget failed");
    ReceiveBuffer::configure(rtc, true);
    g_object_get(rtc, "latency", &latency, nullptr);
    if (latency != ReceiveBuffer::SenderJitterMs) throw std::runtime_error("Sender budget failed");
    gst_object_unref(rtc);
    auto normal = run(ReceiveBuffer::JitterMs, false);
    auto old = run(ReceiveBuffer::SenderJitterMs, true);
    auto fixed = run(ReceiveBuffer::JitterMs, true);
    std::cout << "{\"normal\": " << normal.pushed << ", \"old40ms\": {\"pushed\": "
              << old.pushed << ", \"lost\": " << old.lost << ", \"late\": " << old.late
              << "}, \"new120ms\": {\"pushed\": " << fixed.pushed << ", \"lost\": "
              << fixed.lost << ", \"late\": " << fixed.late << "}}" << std::endl;
    if (normal.pushed != 120 || old.pushed >= 120 || old.lost == 0 || fixed.pushed != 120 ||
        fixed.lost || fixed.late)
      throw std::runtime_error("Delayed RTP recovery regression");
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  return 0;
}
