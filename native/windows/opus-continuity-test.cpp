#include "receive-buffer.hpp"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
struct Bridge { GstElement* input = nullptr; unsigned packets = 0, dropped = 0; std::atomic<uint64_t> frames{0}, quietFrames{0}; };
static GstFlowReturn forward(GstAppSink* sink, gpointer user) {
  auto bridge = static_cast<Bridge*>(user);
  auto sample = gst_app_sink_pull_sample(sink);
  if (!sample) return GST_FLOW_EOS;
  const auto index = bridge->packets++;
  if (index % 17 == 7) ++bridge->dropped;
  else {
    auto packet = gst_buffer_copy(gst_sample_get_buffer(sample));
    GST_BUFFER_PTS(packet) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DTS(packet) = GST_CLOCK_TIME_NONE;
    gst_app_src_push_buffer(GST_APP_SRC(bridge->input), packet);
  }
  gst_sample_unref(sample); return GST_FLOW_OK;
}
static GstFlowReturn measure(GstAppSink* sink, gpointer user) {
  auto bridge = static_cast<Bridge*>(user);
  auto sample = gst_app_sink_pull_sample(sink);
  if (!sample) return GST_FLOW_EOS;
  GstMapInfo map{};
  auto buffer = gst_sample_get_buffer(sample);
  if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
    auto data = reinterpret_cast<const int16_t*>(map.data);
    double energy = 0;
    for (size_t i = 0; i < map.size / 2; ++i) energy += static_cast<double>(data[i]) * data[i];
    bridge->frames += map.size / 4;
    if (energy / std::max<size_t>(1, map.size / 2) < 100) bridge->quietFrames += map.size / 4;
    gst_buffer_unmap(buffer, &map);
  }
  gst_sample_unref(sample); return GST_FLOW_OK;
}
static GstElement* pipeline(const char* text) {
  GError* error = nullptr; auto value = gst_parse_launch(text, &error);
  if (error || !value) throw std::runtime_error(error ? error->message : "Pipeline unavailable");
  return value;
}
int main(int argc, char** argv) {
  gst_init(&argc, &argv);
  try {
    auto receiver = pipeline("appsrc name=input is-live=true format=time do-timestamp=true caps=\"application/x-rtp,media=audio,encoding-name=OPUS,payload=97,clock-rate=48000\" ! rtpjitterbuffer name=jitter do-lost=true ! rtpopusdepay ! opusdec name=decoder plc=true use-inband-fec=true ! audioconvert ! audio/x-raw,format=S16LE,rate=48000,channels=2 ! appsink name=output emit-signals=true sync=false async=false");
    auto sender = pipeline("audiotestsrc samplesperbuffer=960 num-buffers=100 is-live=true volume=0.2 ! audio/x-raw,rate=48000,channels=2 ! audioconvert ! opusenc bitrate=128000 audio-type=generic frame-size=20 inband-fec=true packet-loss-percentage=10 ! rtpopuspay pt=97 ! appsink name=packets emit-signals=true sync=false async=false");
    Bridge bridge;
    bridge.input = gst_bin_get_by_name(GST_BIN(receiver), "input");
    auto output = gst_bin_get_by_name(GST_BIN(receiver), "output");
    auto packets = gst_bin_get_by_name(GST_BIN(sender), "packets");
    auto jitter = gst_bin_get_by_name(GST_BIN(receiver), "jitter");
    auto decoder = gst_bin_get_by_name(GST_BIN(receiver), "decoder");
    g_object_set(jitter, "latency", ReceiveBuffer::JitterMs, nullptr);
    g_signal_connect(output, "new-sample", G_CALLBACK(measure), &bridge);
    g_signal_connect(packets, "new-sample", G_CALLBACK(forward), &bridge);
    gst_element_set_state(receiver, GST_STATE_PLAYING);
    gst_element_set_state(sender, GST_STATE_PLAYING);
    auto bus = gst_element_get_bus(sender);
    auto result = gst_bus_timed_pop_filtered(bus, 8 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (!result || GST_MESSAGE_TYPE(result) == GST_MESSAGE_ERROR) throw std::runtime_error("Opus encoder failed/timed out");
    gst_message_unref(result);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    GstStructure *decStats = nullptr, *jitterStats = nullptr;
    g_object_get(decoder, "stats", &decStats, nullptr); g_object_get(jitter, "stats", &jitterStats, nullptr);
    guint64 plc = 0, lost = 0;
    if (decStats) gst_structure_get_uint64(decStats, "plc-num-samples", &plc);
    if (jitterStats) gst_structure_get_uint64(jitterStats, "num-lost", &lost);
    std::cout << "{\"packets\":" << bridge.packets << ",\"intentionallyDropped\":" << bridge.dropped << ",\"rtpLost\":" << lost << ",\"decodedFrames\":" << bridge.frames << ",\"quietFrames\":" << bridge.quietFrames << ",\"concealedSamples\":" << plc << "}\n";
    const bool passed = bridge.dropped >= 5 && lost == bridge.dropped && plc > 0 && bridge.frames >= 95000 && bridge.quietFrames < 1920;
    gst_element_set_state(sender, GST_STATE_NULL); gst_element_set_state(receiver, GST_STATE_NULL);
    if (decStats) gst_structure_free(decStats); if (jitterStats) gst_structure_free(jitterStats);
    gst_object_unref(bridge.input); gst_object_unref(output); gst_object_unref(packets); gst_object_unref(jitter); gst_object_unref(decoder); gst_object_unref(bus); gst_object_unref(sender); gst_object_unref(receiver);
    if (!passed) throw std::runtime_error("Opus packet-loss continuity regression");
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
