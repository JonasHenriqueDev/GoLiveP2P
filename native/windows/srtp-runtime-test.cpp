#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <atomic>
#include <iostream>
#include <stdexcept>

// Fixed public test material only; this key is never used by a real peer.
static constexpr const char* Key = "012345678901234567890123456789012345678901234567890123456789";
static GstCaps* keyRequest(GstElement*, guint, gpointer) {
  return gst_caps_from_string(
      "application/x-srtp,srtp-key=(buffer)012345678901234567890123456789012345678901234567890123456789,"
      "srtp-cipher=(string)aes-128-icm,srtp-auth=(string)hmac-sha1-80,"
      "srtcp-cipher=(string)aes-128-icm,srtcp-auth=(string)hmac-sha1-80");
}
static GstFlowReturn received(GstAppSink* sink, gpointer user) {
  auto sample = gst_app_sink_pull_sample(sink);
  if (!sample) return GST_FLOW_EOS;
  auto count = static_cast<std::atomic<unsigned>*>(user);
  ++*count;
  gst_sample_unref(sample);
  return GST_FLOW_OK;
}
int main(int argc, char** argv) {
  gst_init(&argc, &argv);
  GstElement* pipeline = nullptr;
  try {
    const std::string chain =
        std::string("audiotestsrc num-buffers=50 samplesperbuffer=960 ! audio/x-raw,rate=48000,channels=2 ! ") +
        "opusenc frame-size=20 ! rtpopuspay pt=97 ! srtpenc key=" + Key +
        " ! srtpdec name=decrypt ! rtpopusdepay ! opusdec ! appsink name=output emit-signals=true sync=false";
    GError* error = nullptr;
    pipeline = gst_parse_launch(chain.c_str(), &error);
    if (error) {
      std::string message = error->message;
      g_error_free(error);
      throw std::runtime_error(message);
    }
    if (!pipeline) throw std::runtime_error("SRTP pipeline unavailable");
    auto decrypt = gst_bin_get_by_name(GST_BIN(pipeline), "decrypt");
    auto output = gst_bin_get_by_name(GST_BIN(pipeline), "output");
    std::atomic<unsigned> packets{0};
    g_signal_connect(decrypt, "request-key", G_CALLBACK(keyRequest), nullptr);
    g_signal_connect(output, "new-sample", G_CALLBACK(received), &packets);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    auto bus = gst_element_get_bus(pipeline);
    auto message = gst_bus_timed_pop_filtered(bus, 8 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool passed = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS && packets >= 50;
    if (message) gst_message_unref(message);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(decrypt);
    gst_object_unref(output);
    gst_object_unref(pipeline);
    pipeline = nullptr;
    std::cout << "{\"passed\":" << (passed ? "true" : "false")
              << ",\"decryptedAudioPackets\":" << packets << "}\n";
    return passed ? 0 : 1;
  } catch (const std::exception& error) {
    if (pipeline) { gst_element_set_state(pipeline, GST_STATE_NULL); gst_object_unref(pipeline); }
    std::cerr << error.what() << '\n';
    return 1;
  }
}
