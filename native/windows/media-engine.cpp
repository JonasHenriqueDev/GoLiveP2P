// GoLive native media process. stdout is reserved for versioned NDJSON IPC.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GST_USE_UNSTABLE_API
#include <audiopolicy.h>
#include <dwmapi.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/video/video-event.h>
#include <gst/webrtc/webrtc.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "vendor/json.hpp"
using Json = nlohmann::json;
static std::mutex outputMutex;
static void emit(Json value) {
  value["v"] = 1;
  std::lock_guard<std::mutex> lock(outputMutex);
  std::cout << value.dump() << '\n' << std::flush;
}
static std::string utf8(const std::wstring& text) {
  int n =
      WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n, nullptr, nullptr);
  return out;
}
static Json sources() {
  Json out = Json::array();
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR handle, HDC, LPRECT, LPARAM data) -> BOOL {
        auto& list = *reinterpret_cast<Json*>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(handle, &info))
          list.push_back({{"id", "monitor:" + std::to_string((uintptr_t)handle)},
                          {"kind", "screen"},
                          {"name", utf8(info.szDevice)},
                          {"thumbnail", ""}});
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  EnumWindows(
      [](HWND handle, LPARAM data) -> BOOL {
        if (!IsWindowVisible(handle) || GetWindow(handle, GW_OWNER) ||
            !GetWindowTextLengthW(handle))
          return TRUE;
        DWORD cloaked = 0;
        DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        wchar_t title[1024]{};
        GetWindowTextW(handle, title, 1024);
        DWORD pid = 0;
        GetWindowThreadProcessId(handle, &pid);
        reinterpret_cast<Json*>(data)->push_back(
            {{"id", "window:" + std::to_string((uintptr_t)handle)},
             {"kind", "window"},
             {"name", utf8(title)},
             {"pid", pid},
             {"thumbnail", ""}});
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  return out;
}
static GstElement* parse(const std::string& text) {
  GError* error = nullptr;
  GstElement* pipeline = gst_parse_launch(text.c_str(), &error);
  if (error) {
    std::string message = error->message;
    g_error_free(error);
    if (pipeline) gst_object_unref(pipeline);
    throw std::runtime_error(message);
  }
  if (!pipeline) throw std::runtime_error("Pipeline unavailable");
  return pipeline;
}
static bool available(const char* name) {
  auto factory = gst_element_factory_find(name);
  if (!factory) return false;
  gst_object_unref(factory);
  return true;
}
static Json structureJson(const GstStructure* structure) {
  Json out = Json::object();
  if (!structure) return out;
  for (int i = 0; i < gst_structure_n_fields(structure); ++i) {
    auto name = gst_structure_nth_field_name(structure, i);
    auto value = gst_structure_get_value(structure, name);
    if (!value) continue;
    if (GST_VALUE_HOLDS_STRUCTURE(value))
      out[name] = structureJson(gst_value_get_structure(value));
    else if (G_VALUE_HOLDS_STRING(value))
      out[name] = g_value_get_string(value) ? g_value_get_string(value) : "";
    else if (G_VALUE_HOLDS_UINT64(value))
      out[name] = g_value_get_uint64(value);
    else if (G_VALUE_HOLDS_INT64(value))
      out[name] = g_value_get_int64(value);
    else if (G_VALUE_HOLDS_UINT(value))
      out[name] = g_value_get_uint(value);
    else if (G_VALUE_HOLDS_INT(value))
      out[name] = g_value_get_int(value);
    else if (G_VALUE_HOLDS_DOUBLE(value))
      out[name] = g_value_get_double(value);
    else if (G_VALUE_HOLDS_BOOLEAN(value))
      out[name] = (bool)g_value_get_boolean(value);
    else if (G_VALUE_HOLDS_ENUM(value)) {
      auto klass = (GEnumClass*)g_type_class_ref(G_VALUE_TYPE(value));
      auto item = g_enum_get_value(klass, g_value_get_enum(value));
      out[name] = item ? item->value_nick : "unknown";
      g_type_class_unref(klass);
    }
  }
  return out;
}
#include "process-audio.hpp"
#include "window-source.hpp"
struct Peer {
  std::string id;
  GstElement *pipeline = nullptr, *rtc = nullptr, *video = nullptr, *audio = nullptr;
  bool remoteSet = false;
  std::vector<std::pair<unsigned, std::string>> candidates;
  std::atomic<bool> closing{false};
  std::atomic<unsigned> receivedFrames{0};
  std::atomic<int> width{0}, height{0};
  std::atomic<double> audioRms{0};
  std::atomic<uint64_t> audioFrames{0};
  uint64_t sentAudioFrames = 0;
  ~Peer() {
    closing = true;
    if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
    if (video) gst_object_unref(video);
    if (audio) gst_object_unref(audio);
    if (rtc) gst_object_unref(rtc);
    if (pipeline) gst_object_unref(pipeline);
  }
};
class Engine {
  GstElement *capture = nullptr, *encoder = nullptr;
  std::unique_ptr<WindowSource> windowSource;
  std::map<std::string, std::unique_ptr<Peer>> peers;
  std::map<std::string, std::vector<std::pair<unsigned, std::string>>> earlyCandidates;
  std::mutex peerMutex;
  Json settings = Json::object();
  std::atomic<unsigned> encodedFrames{0};
  bool software = false;
  bool audioEnabled = false;
  GstSample* keyframe = nullptr;
  std::atomic<bool> audioRunning{false};
  std::atomic<unsigned> audioSources{0};
  std::atomic<double> capturedAudioRms{0};
  std::atomic<uint64_t> nonSilentAudioFrames{0};
  std::thread audioWorker;
  static GstFlowReturn frame(GstAppSink* sink, gpointer data) {
    auto peer = static_cast<Peer*>(data);
    auto sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_EOS;
    auto buffer = gst_sample_get_buffer(sample);
    GstMapInfo map{};
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      gchar* base64 = g_base64_encode(map.data, map.size);
      emit({{"event", "frame"}, {"peer", peer ? peer->id : "local"}, {"jpeg", base64}});
      g_free(base64);
      gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }
  static GstFlowReturn audioMeter(GstAppSink* sink, gpointer data) {
    auto peer = static_cast<Peer*>(data);
    auto sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_EOS;
    auto buffer = gst_sample_get_buffer(sample);
    GstMapInfo map{};
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      auto samples = reinterpret_cast<const float*>(map.data);
      auto count = map.size / sizeof(float);
      double sum = 0;
      for (size_t i = 0; i < count; ++i) sum += samples[i] * samples[i];
      peer->audioRms = count ? std::sqrt(sum / count) : 0;
      peer->audioFrames += count / 2;
      gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }
  static GstFlowReturn encoded(GstAppSink* sink, gpointer data) {
    auto self = static_cast<Engine*>(data);
    auto sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_EOS;
    auto buffer = gst_sample_get_buffer(sample);
    ++self->encodedFrames;
    {
      std::lock_guard<std::mutex> lock(self->peerMutex);
      if (!GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
        if (self->keyframe) gst_sample_unref(self->keyframe);
        self->keyframe = gst_sample_ref(sample);
      }
      for (auto& [id, peer] : self->peers)
        if (peer->video && !peer->closing) {
          gst_app_src_set_caps(GST_APP_SRC(peer->video), gst_sample_get_caps(sample));
          auto copy = gst_buffer_copy(buffer);
          // Each receiver has its own clock and transport. Let appsrc timestamp at arrival.
          GST_BUFFER_PTS(copy) = GST_CLOCK_TIME_NONE;
          GST_BUFFER_DTS(copy) = GST_CLOCK_TIME_NONE;
          gst_app_src_push_buffer(GST_APP_SRC(peer->video), copy);
        }
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }
  static void ice(GstElement*, guint index, gchar* candidate, gpointer data) {
    auto peer = static_cast<Peer*>(data);
    if (!peer->closing)
      emit({{"event", "signal"},
            {"peer", peer->id},
            {"type", "ice-candidate"},
            {"candidate", {{"candidate", candidate}, {"sdpMLineIndex", index}}}});
  }
  static void state(GObject* rtc, GParamSpec*, gpointer data) {
    auto peer = static_cast<Peer*>(data);
    gint connection = 0;
    g_object_get(rtc, "connection-state", &connection, nullptr);
    const char* names[] = {"new", "connecting", "connected", "disconnected", "failed", "closed"};
    if (!peer->closing)
      emit(
          {{"event", "state"}, {"peer", peer->id}, {"state", names[std::clamp(connection, 0, 5)]}});
  }
  static void decoded(GstElement*, GstPad* pad, gpointer data) {
    auto peer = static_cast<Peer*>(data);
    auto caps = gst_pad_get_current_caps(pad);
    if (!caps) return;
    auto structure = gst_caps_get_structure(caps, 0);
    const char* name = gst_structure_get_name(structure);
    std::string chain;
    if (g_str_has_prefix(name, "video/x-raw")) {
      int width = 0, height = 0;
      gst_structure_get_int(structure, "width", &width);
      gst_structure_get_int(structure, "height", &height);
      peer->width = width;
      peer->height = height;
      gst_pad_add_probe(
          pad, GST_PAD_PROBE_TYPE_BUFFER,
          [](GstPad*, GstPadProbeInfo*, gpointer user) -> GstPadProbeReturn {
            ++static_cast<Peer*>(user)->receivedFrames;
            return GST_PAD_PROBE_OK;
          },
          peer, nullptr);
      chain =
          "d3d11download ! video/x-raw ! queue max-size-buffers=2 leaky=downstream ! videoconvert "
          "! videoscale add-borders=true ! "
          "video/x-raw,width=1280,height=720,pixel-aspect-ratio=1/1 ! videorate drop-only=true ! "
          "video/x-raw,framerate=30/1 ! jpegenc quality=85 ! appsink name=display "
          "emit-signals=true sync=false max-buffers=1 drop=true";
    } else if (g_str_has_prefix(name, "audio/x-raw"))
      chain =
          "queue ! audioconvert ! audioresample ! "
          "audio/x-raw,format=F32LE,channels=2,rate=48000,layout=interleaved ! tee name=sound "
          "sound. ! queue ! audioconvert ! wasapi2sink sync=true sound. ! queue ! appsink "
          "name=meter emit-signals=true sync=false max-buffers=2 drop=true";
    gst_caps_unref(caps);
    if (chain.empty() || peer->closing) return;
    GError* error = nullptr;
    auto bin = gst_parse_bin_from_description(chain.c_str(), TRUE, &error);
    if (error) {
      emit({{"event", "error"}, {"message", error->message}, {"peer", peer->id}});
      g_error_free(error);
      if (bin) gst_object_unref(bin);
      return;
    }
    auto sink = gst_bin_get_by_name(GST_BIN(bin), "display");
    if (sink) {
      g_signal_connect(sink, "new-sample", G_CALLBACK(frame), peer);
      gst_object_unref(sink);
    }
    auto meter = gst_bin_get_by_name(GST_BIN(bin), "meter");
    if (meter) {
      g_signal_connect(meter, "new-sample", G_CALLBACK(audioMeter), peer);
      gst_object_unref(meter);
    }
    gst_bin_add(GST_BIN(peer->pipeline), bin);
    auto target = gst_element_get_static_pad(bin, "sink");
    if (gst_pad_link(pad, target) != GST_PAD_LINK_OK)
      emit({{"event", "error"}, {"peer", peer->id}, {"message", "Decode pad link failed"}});
    gst_object_unref(target);
    gst_element_sync_state_with_parent(bin);
  }
  static void incoming(GstElement*, GstPad* pad, gpointer data) {
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) return;
    auto peer = static_cast<Peer*>(data);
    if (peer->closing) return;
    auto decoder = gst_element_factory_make("decodebin", nullptr);
    g_signal_connect(decoder, "pad-added", G_CALLBACK(decoded), peer);
    gst_bin_add(GST_BIN(peer->pipeline), decoder);
    auto sink = gst_element_get_static_pad(decoder, "sink");
    gst_pad_link(pad, sink);
    gst_object_unref(sink);
    gst_element_sync_state_with_parent(decoder);
  }
  void description(Peer& peer, bool offer) {
    auto promise = gst_promise_new();
    g_signal_emit_by_name(peer.rtc, offer ? "create-offer" : "create-answer", nullptr, promise);
    if (gst_promise_wait(promise) != GST_PROMISE_RESULT_REPLIED) {
      gst_promise_unref(promise);
      throw std::runtime_error("SDP creation failed");
    }
    auto reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* sdp = nullptr;
    if (!reply ||
        !gst_structure_get(reply, offer ? "offer" : "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
                           &sdp, nullptr) ||
        !sdp || !sdp->sdp) {
      gst_promise_unref(promise);
      throw std::runtime_error("SDP missing");
    }
    auto local = gst_promise_new();
    g_signal_emit_by_name(peer.rtc, "set-local-description", sdp, local);
    gst_promise_wait(local);
    gst_promise_unref(local);
    auto text = gst_sdp_message_as_text(sdp->sdp);
    emit({{"event", "signal"},
          {"peer", peer.id},
          {"type", offer ? "offer" : "answer"},
          {"sdp", text}});
    g_free(text);
    gst_webrtc_session_description_free(sdp);
    gst_promise_unref(promise);
  }
  Peer& createPeer(const std::string& id, bool sender) {
    auto queued = std::move(earlyCandidates[id]);
    earlyCandidates.erase(id);
    if (peers.count(id)) remove(id);
    if (peers.size() >= 4) throw std::runtime_error("Maximum four receivers");
    auto peer = std::make_unique<Peer>();
    peer->id = id;
    peer->candidates = std::move(queued);
    std::string chain = "webrtcbin name=rtc bundle-policy=max-bundle latency=40";
    if (sender)
      chain +=
          " appsrc name=video is-live=true format=time do-timestamp=true max-buffers=3 "
          "leaky-type=downstream ! queue max-size-buffers=3 leaky=downstream ! h264parse ! "
          "rtph264pay config-interval=-1 pt=96 aggregate-mode=zero-latency ! "
          "application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000 ! rtc.";
    if (sender && audioEnabled)
      chain +=
          " appsrc name=audio is-live=true format=time do-timestamp=false max-buffers=10 "
          "leaky-type=downstream "
          "caps=\"audio/x-raw,format=S16LE,rate=48000,channels=2,layout=interleaved\" ! queue "
          "max-size-time=100000000 leaky=downstream ! audioconvert ! audioresample ! opusenc "
          "bitrate=128000 audio-type=restricted-lowdelay frame-size=10 ! rtpopuspay pt=97 ! "
          "application/x-rtp,media=audio,encoding-name=OPUS,payload=97,clock-rate=48000 ! rtc.";
    auto parsed = parse(chain);
    if (GST_IS_PIPELINE(parsed))
      peer->pipeline = parsed;
    else {
      peer->pipeline = gst_pipeline_new(nullptr);
      gst_bin_add(GST_BIN(peer->pipeline), parsed);
    }
    peer->rtc = gst_bin_get_by_name(GST_BIN(peer->pipeline), "rtc");
    if (!peer->rtc) throw std::runtime_error("WebRTC element missing from pipeline");
    peer->video = gst_bin_get_by_name(GST_BIN(peer->pipeline), "video");
    peer->audio = gst_bin_get_by_name(GST_BIN(peer->pipeline), "audio");
    if (peer->audio) {
      auto buffer = gst_buffer_new_allocate(nullptr, 1920, nullptr);
      gst_buffer_memset(buffer, 0, 0, 1920);
      GST_BUFFER_PTS(buffer) = 0;
      GST_BUFFER_DURATION(buffer) = 10 * GST_MSECOND;
      peer->sentAudioFrames = 480;
      gst_app_src_push_buffer(GST_APP_SRC(peer->audio), buffer);
    }
    if (sender && peer->video) {
      std::lock_guard<std::mutex> lock(peerMutex);
      if (keyframe) {
        gst_app_src_set_caps(GST_APP_SRC(peer->video), gst_sample_get_caps(keyframe));
        auto buffer = gst_buffer_copy(gst_sample_get_buffer(keyframe));
        GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_NONE;
        GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
        gst_app_src_push_buffer(GST_APP_SRC(peer->video), buffer);
      }
    }
    g_signal_connect(peer->rtc, "on-ice-candidate", G_CALLBACK(ice), peer.get());
    g_signal_connect(peer->rtc, "notify::connection-state", G_CALLBACK(state), peer.get());
    g_signal_connect(peer->rtc, "pad-added", G_CALLBACK(incoming), peer.get());
    auto ptr = peer.get();
    {
      std::lock_guard<std::mutex> lock(peerMutex);
      peers[id] = std::move(peer);
    }
    if (gst_element_set_state(ptr->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      remove(id);
      throw std::runtime_error("WebRTC pipeline failed to start");
    }
    return *ptr;
  }

 public:
  ~Engine() { stop(); }
  void remove(const std::string& id) {
    earlyCandidates.erase(id);
    std::unique_ptr<Peer> old;
    {
      std::lock_guard<std::mutex> lock(peerMutex);
      auto it = peers.find(id);
      if (it != peers.end()) {
        old = std::move(it->second);
        peers.erase(it);
      }
    }
  }
  void stop() {
    windowSource.reset();
    earlyCandidates.clear();
    audioRunning = false;
    if (audioWorker.joinable()) audioWorker.join();
    audioSources = 0;
    capturedAudioRms = 0;
    if (capture) {
      gst_element_set_state(capture, GST_STATE_NULL);
      gst_object_unref(capture);
      capture = nullptr;
    }
    if (encoder) {
      gst_object_unref(encoder);
      encoder = nullptr;
    }
    std::map<std::string, std::unique_ptr<Peer>> old;
    {
      std::lock_guard<std::mutex> lock(peerMutex);
      old.swap(peers);
    }
    if (keyframe) {
      gst_sample_unref(keyframe);
      keyframe = nullptr;
    }
  }
  Json start(const Json& config) {
    stop();
    settings = config;
    encodedFrames = 0;
    nonSilentAudioFrames = 0;
    audioEnabled = config.value("audio", false);
    const auto source = config.at("source").get<std::string>();
    auto method = config.at("method").get<std::string>();
    const bool automatic = method == "auto";
    auto list = sources();
    if (std::none_of(list.begin(), list.end(), [&](auto& item) { return item["id"] == source; }))
      throw std::runtime_error("Source no longer exists");
    bool window = source.rfind("window:", 0) == 0;
    process_audio::Process selectedOwner{0, 0, L"", L"", 0};
    if (window && audioEnabled) {
      DWORD ownerPid = 0;
      GetWindowThreadProcessId(reinterpret_cast<HWND>(std::stoull(source.substr(7))), &ownerPid);
      const auto processes = process_audio::snapshot();
      if (const auto owner = process_audio::find(ownerPid, processes)) selectedOwner = *owner;
    }
    if (automatic) method = window ? "printwindow" : "dxgi";
    if (method != "wgc" && method != "dxgi" && method != "printwindow")
      throw std::runtime_error("Invalid capture method");
    if (window && method == "dxgi")
      throw std::runtime_error("DXGI captures monitors only; it cannot capture covered windows");
    if (!window && method == "printwindow")
      throw std::runtime_error("PrintWindow captures windows only");
    int width = config.at("width"), height = config.at("height"), fps = config.at("fps"),
        bitrate = config.at("bitrate");
    if (width < 320 || width > 2560 || height < 180 || height > 1440 || (fps != 30 && fps != 60) ||
        bitrate < 500000 || bitrate > 20000000)
      throw std::runtime_error("Invalid media settings");
    std::string handle = source.substr(source.find(':') + 1);
    std::string captureSpec = "d3d11screencapturesrc capture-api=" + method +
                              " show-cursor=true show-border=false " +
                              (window ? "window-handle=" : "monitor-handle=") + handle;
    if (method == "printwindow")
      captureSpec =
          "appsrc name=windowframes is-live=true format=time do-timestamp=true block=false "
          "max-buffers=2 leaky-type=downstream caps=video/x-raw,format=BGRx,width=" +
          std::to_string(width) + ",height=" + std::to_string(height) +
          ",pixel-aspect-ratio=1/1,framerate=" + std::to_string(fps) + "/1 ! d3d11upload";
    // NVIDIA path keeps scaling/format conversion on D3D11. Software is a real fallback.
    software =
        config.value("encoder", std::string("auto")) == "software" || !available("nvd3d11h264enc");
    std::string encode =
        software ? "videoconvert ! video/x-raw,format=I420 ! openh264enc name=encoder "
                   "rate-control=bitrate usage-type=screen complexity=low bitrate=" +
                       std::to_string(bitrate) + " max-bitrate=" + std::to_string(bitrate) +
                       " gop-size=" + std::to_string(fps)
                 : "d3d11upload ! d3d11convert ! video/x-raw(memory:D3D11Memory),format=NV12 ! "
                   "nvd3d11h264enc name=encoder bitrate=" +
                       std::to_string(bitrate / 1000) +
                       " max-bitrate=" + std::to_string(bitrate / 1000) +
                       " gop-size=" + std::to_string(fps) +
                       " bframes=0 zerolatency=true tune=ultra-low-latency";
    auto chain =
        captureSpec + " ! video/x-raw(memory:D3D11Memory),framerate=" + std::to_string(fps) +
        "/1 ! d3d11convert ! video/x-raw(memory:D3D11Memory),width=" + std::to_string(width) +
        ",height=" + std::to_string(height) +
        " ! d3d11download ! video/x-raw ! tee name=split split. ! queue max-size-buffers=2 "
        "leaky=downstream ! " +
        encode +
        " ! h264parse config-interval=-1 ! "
        "video/x-h264,stream-format=byte-stream,alignment=au,profile=constrained-baseline ! "
        "appsink name=encoded emit-signals=true sync=false max-buffers=2 drop=true split. ! queue "
        "max-size-buffers=1 leaky=downstream ! videorate drop-only=true ! "
        "video/x-raw,framerate=15/1 ! videoscale add-borders=true ! "
        "video/x-raw,width=960,height=540,pixel-aspect-ratio=1/1 ! videoconvert ! jpegenc "
        "quality=75 ! appsink name=preview emit-signals=true sync=false max-buffers=1 drop=true";
    try {
      capture = parse(chain);
    } catch (const std::exception& error) {
      if (software) throw;
      emit({{"event", "warning"},
            {"peer", "local"},
            {"message",
             std::string("Hardware pipeline unavailable; retrying OpenH264: ") + error.what()}});
      auto fallback = config;
      fallback["encoder"] = "software";
      return start(fallback);
    }
    encoder = gst_bin_get_by_name(GST_BIN(capture), "encoder");
    auto output = gst_bin_get_by_name(GST_BIN(capture), "encoded");
    auto preview = gst_bin_get_by_name(GST_BIN(capture), "preview");
    g_signal_connect(output, "new-sample", G_CALLBACK(encoded), this);
    g_signal_connect(preview, "new-sample", G_CALLBACK(frame), nullptr);
    gst_object_unref(output);
    gst_object_unref(preview);
    if (gst_element_set_state(capture, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      bool retry = !software;
      stop();
      if (retry) {
        emit({{"event", "warning"},
              {"peer", "local"},
              {"message", "Hardware initialization failed; retrying OpenH264"}});
        auto fallback = config;
        fallback["encoder"] = "software";
        return start(fallback);
      }
      throw std::runtime_error("Native capture failed");
    }
    if (method == "printwindow") {
      auto raw = gst_bin_get_by_name(GST_BIN(capture), "windowframes");
      windowSource = std::make_unique<WindowSource>();
      try {
        windowSource->start(raw, reinterpret_cast<HWND>(std::stoull(handle)), width, height, fps);
      } catch (...) {
        gst_object_unref(raw);
        stop();
        throw;
      }
      gst_object_unref(raw);
    }
    for (int attempt = 0; attempt < 500 && encodedFrames == 0; ++attempt) {
      if (windowSource && windowSource->unhealthy()) break;
      Sleep(10);
    }
    if (encodedFrames == 0) {
      auto bus = gst_element_get_bus(capture);
      auto message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
      std::string reason = "Capture did not deliver encoded video within five seconds";
      if (message) {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        if (error) {
          reason = error->message;
          g_error_free(error);
        }
        g_free(debug);
        gst_message_unref(message);
      }
      gst_object_unref(bus);
      bool captureFailed = windowSource && windowSource->unhealthy();
      bool retry = !software;
      stop();
      if (automatic && method == "printwindow" && captureFailed) {
        emit({{"event", "warning"},
              {"peer", "local"},
              {"message",
               "PrintWindow unavailable for this application; using WGC, which can show the "
               "capture border"}});
        auto fallback = config;
        fallback["method"] = "wgc";
        return start(fallback);
      }
      if (captureFailed) throw std::runtime_error("Native window capture failed or exceeded its deadline");
      if (retry) {
        emit({{"event", "warning"},
              {"peer", "local"},
              {"message", "Hardware encoder failed; retrying OpenH264: " + reason}});
        auto fallback = config;
        fallback["encoder"] = "software";
        return start(fallback);
      }
      throw std::runtime_error(reason);
    }
    if (audioEnabled) {
      audioRunning = true;
      audioWorker = std::thread([this, config, window, selectedOwner] {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment)) {
          emit({{"event", "audio-state"}, {"active", false}, {"sources", 0}});
          emit({{"event", "warning"},
                {"peer", "local"},
                {"message", "Audio COM initialization failed; video continues"}});
          return;
        }
        try {
          process_audio::Mixer mixer;
          mixer.start(config, window ? &selectedOwner : nullptr);
          // An audio packet represents exactly 480 samples at 48 kHz. Use a sample clock
          // for RTP timestamps instead of timestamping each buffer by thread wake-up time.
          process_audio::ScopedHandle timer(CreateWaitableTimerExW(
              nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS));
          process_audio::ScopedHandle fallbackTimer(timer.get() ? nullptr : CreateWaitableTimerW(nullptr, FALSE, nullptr));
          HANDLE clockTimer = timer.get() ? timer.get() : fallbackTimer.get();
          LARGE_INTEGER due{};
          due.QuadPart = -100000;
          if (!clockTimer || !SetWaitableTimer(clockTimer, &due, 10, nullptr, nullptr, FALSE))
            throw std::runtime_error("Audio scheduling clock unavailable");
          size_t previous = SIZE_MAX;
          ULONGLONG lastMeter = 0;
          double peakRms = 0;
          while (audioRunning) {
            auto samples = mixer.packet();
            auto count = mixer.active();
            double energy = 0;
            for (auto sample : samples) {
              const double normalized = sample / 32768.0;
              energy += normalized * normalized;
            }
            const double rms = samples.empty() ? 0 : std::sqrt(energy / samples.size());
            if (rms > 0.0001) nonSilentAudioFrames += samples.size() / 2;
            peakRms = std::max(peakRms, rms);
            audioSources = static_cast<unsigned>(count);
            capturedAudioRms = rms;
            const auto now = GetTickCount64();
            if (count != previous || now - lastMeter >= 1000) {
              previous = count;
              lastMeter = now;
              emit({{"event", "audio-state"}, {"active", count > 0}, {"sources", count},
                    {"rms", peakRms}, {"nonSilentFrames", nonSilentAudioFrames.load()}});
              peakRms = 0;
            }
            {
              std::lock_guard<std::mutex> lock(peerMutex);
              for (auto& [id, peer] : peers)
                if (peer->audio) {
                  auto buffer = gst_buffer_new_allocate(nullptr, samples.size() * 2, nullptr);
                  if (!buffer) throw std::runtime_error("Audio buffer allocation failed");
                  gst_buffer_fill(buffer, 0, samples.data(), samples.size() * 2);
                  GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(peer->sentAudioFrames, GST_SECOND, 48000);
                  GST_BUFFER_DURATION(buffer) = 10 * GST_MSECOND;
                  peer->sentAudioFrames += samples.size() / 2;
                  gst_app_src_push_buffer(GST_APP_SRC(peer->audio), buffer);
                }
            }
            if (WaitForSingleObject(clockTimer, 100) != WAIT_OBJECT_0)
              throw std::runtime_error("Audio scheduling wait failed");
          }
          mixer.stop();
        } catch (const std::exception& error) {
          emit({{"event", "warning"},
                {"peer", "local"},
                {"message", std::string("Audio blocked; video continues: ") + error.what()}});
          emit({{"event", "audio-state"}, {"active", false}, {"sources", 0}});
        } catch (...) {
          emit({{"event", "warning"},
                {"peer", "local"},
                {"message", "Audio blocked after unexpected failure; video continues"}});
          emit({{"event", "audio-state"}, {"active", false}, {"sources", 0}});
        }
        audioSources = 0;
        capturedAudioRms = 0;
        CoUninitialize();
      });
    }
    return {{"encoder", software ? "openh264" : "nvenc-d3d11"},
            {"method", method},
            {"borderRemovalVerified", false},
            {"audio", false}};
  }
  void offer(const std::string& id) {
    if (!capture) throw std::runtime_error("Capture not started");
    auto& peer = createPeer(id, true);
    // Codec caps must reach the WebRTC sink before constructing SDP.
    auto pad = gst_element_get_static_pad(peer.rtc, "sink_0");
    bool ready = false;
    for (int i = 0; i < 300 && !ready; ++i) {
      auto caps = pad ? gst_pad_get_current_caps(pad) : nullptr;
      if (caps) {
        ready = true;
        gst_caps_unref(caps);
      } else
        Sleep(10);
    }
    if (pad) gst_object_unref(pad);
    if (!ready) {
      remove(id);
      throw std::runtime_error("RTP caps negotiation timed out");
    }
    if (peer.audio) {
      auto audioPad = gst_element_get_static_pad(peer.rtc, "sink_1");
      bool audioReady = false;
      for (int i = 0; i < 100; ++i) {
        auto caps = audioPad ? gst_pad_get_current_caps(audioPad) : nullptr;
        if (caps) {
          audioReady = true;
          gst_caps_unref(caps);
          break;
        }
        Sleep(10);
      }
      if (audioPad) gst_object_unref(audioPad);
      if (!audioReady) {
        remove(id);
        throw std::runtime_error("Audio RTP caps negotiation timed out");
      }
    }
    description(peer, true);
  }
  void signal(const Json& message) {
    auto id = message.at("peer").get<std::string>(), type = message.at("type").get<std::string>();
    if (type == "offer") {
      auto& peer = createPeer(id, false);
      setRemote(peer, message.at("sdp"), true);
      description(peer, false);
    } else if (type == "answer") {
      auto it = peers.find(id);
      if (it == peers.end()) throw std::runtime_error("Unknown answer peer");
      setRemote(*it->second, message.at("sdp"), false);
    } else if (type == "ice-candidate") {
      auto it = peers.find(id);
      auto c = message.at("candidate");
      auto candidate = c.at("candidate").get<std::string>();
      unsigned index = c.contains("sdpMLineIndex") && c["sdpMLineIndex"].is_number_unsigned()
                           ? c["sdpMLineIndex"].get<unsigned>()
                           : 0;
      if (candidate.size() > 4096 || index > 8) throw std::runtime_error("Invalid ICE candidate");
      if (it == peers.end()) {
        if (!earlyCandidates.count(id) && earlyCandidates.size() >= 4)
          throw std::runtime_error("Early ICE peer limit");
        auto& queued = earlyCandidates[id];
        if (queued.size() >= 128) throw std::runtime_error("Early ICE queue full");
        queued.emplace_back(index, candidate);
        return;
      }
      auto& peer = *it->second;
      if (peer.remoteSet)
        g_signal_emit_by_name(peer.rtc, "add-ice-candidate", index, candidate.c_str());
      else {
        if (peer.candidates.size() >= 128) throw std::runtime_error("ICE queue full");
        peer.candidates.emplace_back(index, candidate);
      }
    } else
      throw std::runtime_error("Unknown signal type");
  }
  void setRemote(Peer& peer, const std::string& text, bool offer) {
    if (text.empty() || text.size() > 200000) throw std::runtime_error("Invalid SDP size");
    GstSDPMessage* sdp = nullptr;
    gst_sdp_message_new(&sdp);
    if (gst_sdp_message_parse_buffer(reinterpret_cast<const guint8*>(text.data()),
                                     (guint)text.size(), sdp) != GST_SDP_OK) {
      gst_sdp_message_free(sdp);
      throw std::runtime_error("Invalid SDP");
    }
    auto description = gst_webrtc_session_description_new(
        offer ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
    auto promise = gst_promise_new();
    g_signal_emit_by_name(peer.rtc, "set-remote-description", description, promise);
    gst_promise_wait(promise);
    auto reply = gst_promise_get_reply(promise);
    bool failed = reply && gst_structure_has_field(reply, "error");
    gst_promise_unref(promise);
    gst_webrtc_session_description_free(description);
    if (failed) throw std::runtime_error("Remote SDP rejected");
    peer.remoteSet = true;
    for (auto& [index, candidate] : peer.candidates)
      g_signal_emit_by_name(peer.rtc, "add-ice-candidate", index, candidate.c_str());
    peer.candidates.clear();
  }
  void bitrate(unsigned value) {
    if (value < 500000 || value > 20000000) throw std::runtime_error("Invalid bitrate");
    if (encoder)
      g_object_set(encoder, "bitrate", software ? value : value / 1000, "max-bitrate",
                   software ? value : value / 1000, nullptr);
    settings["bitrate"] = value;
  }
  void pcm(const std::string& data) {
    if (data.size() > 262144) throw std::runtime_error("PCM packet too large");
    gsize length = 0;
    auto bytes = g_base64_decode(data.c_str(), &length);
    if (!length || length % 4) {
      g_free(bytes);
      throw std::runtime_error("Unaligned PCM packet");
    }
    std::lock_guard<std::mutex> lock(peerMutex);
    for (auto& [id, peer] : peers)
      if (peer->audio) {
        auto buffer = gst_buffer_new_allocate(nullptr, length, nullptr);
        if (!buffer) {
          g_free(bytes);
          throw std::runtime_error("PCM buffer allocation failed");
        }
        gst_buffer_fill(buffer, 0, bytes, length);
        GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(peer->sentAudioFrames, GST_SECOND, 48000);
        GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(length / 4, GST_SECOND, 48000);
        peer->sentAudioFrames += length / 4;
        gst_app_src_push_buffer(GST_APP_SRC(peer->audio), buffer);
      }
    g_free(bytes);
  }
  void poll() {
    if (windowSource && windowSource->unhealthy()) {
      emit({{"event", "error"}, {"peer", "local"},
            {"message", "Window capture stopped: application closed, minimized or unresponsive"}});
      stop();
    }
    std::vector<std::pair<GstElement*, std::string>> pipelines;
    if (capture) pipelines.emplace_back(capture, "local");
    for (auto& [id, peer] : peers) pipelines.emplace_back(peer->pipeline, id);
    for (auto& [pipeline, id] : pipelines) {
      auto bus = gst_element_get_bus(pipeline);
      while (
          auto message = gst_bus_pop_filtered(
              bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING | GST_MESSAGE_EOS))) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS)
          emit({{"event", "error"}, {"peer", id}, {"message", "Media source ended"}});
        else {
          GError* error = nullptr;
          gchar* debug = nullptr;
          if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
            gst_message_parse_error(message, &error, &debug);
          else
            gst_message_parse_warning(message, &error, &debug);
          emit({{"event", GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR ? "error" : "warning"},
                {"peer", id},
                {"message", error ? std::string(error->message) +
                                        (debug ? std::string(" [") + debug + "]" : "")
                                  : "Pipeline error"}});
          if (error) g_error_free(error);
          g_free(debug);
        }
        gst_message_unref(message);
      }
      gst_object_unref(bus);
    }
  }
  Json stats() {
    Json out = Json::object();
    for (auto& [id, peer] : peers) {
      auto promise = gst_promise_new();
      g_signal_emit_by_name(peer->rtc, "get-stats", nullptr, promise);
      gst_promise_wait(promise);
      auto reply = gst_promise_get_reply(promise);
      int connection = 0, iceState = 0;
      g_object_get(peer->rtc, "connection-state", &connection, "ice-connection-state", &iceState,
                   nullptr);
      out[id] = {{"raw", structureJson(reply)},
                 {"connection", connection},
                 {"ice", iceState},
                 {"audioRms", peer->audioRms.load()},
                 {"audioFrames", peer->audioFrames.load()},
                 {"receivedFrames", peer->receivedFrames.load()},
                 {"width", peer->width.load()},
                 {"height", peer->height.load()}};
      gst_promise_unref(promise);
    }
    return {{"peers", out},
            {"audio", {{"sources", audioSources.load()}, {"rms", capturedAudioRms.load()},
                       {"nonSilentFrames", nonSilentAudioFrames.load()}}},
            {"encodedFrames", encodedFrames.load()},
            {"width", settings.value("width", 0)},
            {"height", settings.value("height", 0)}};
  }
  Json command(const Json& request) {
    if (request.at("v") != 1 || !request.at("id").is_number_unsigned())
      throw std::runtime_error("Invalid IPC version or id");
    auto method = request.at("method").get<std::string>();
    auto data = request.value("data", Json::object());
    if (method == "sources") return sources();
    if (method == "audio-sessions") return process_audio::sessions();
    if (method == "policy-test") {
      using process_audio::Process;
      std::vector<Process> list = {{2000001, 0, L"player.exe", L"c:\\player.exe", 100},
                                   {2000002, 2000001, L"decoder.exe", L"c:\\decoder.exe", 200}};
      auto expect = [](bool pass) {
        if (!pass) throw std::runtime_error("Native audio policy regression");
      };
      expect(process_audio::safe(2000001, list));
      list[1].name = L"discord.exe";
      expect(!process_audio::safe(2000001, list));
      list[1].name = L"chrome.exe";
      expect(process_audio::safe(2000001, list));
      list[1].name = L"svchost.exe";
      expect(!process_audio::safe(2000001, list));
      list[1].name = L"decoder.exe";
      list[1].image = L"";
      expect(!process_audio::safe(2000001, list));
      list[1].created = 0;
      expect(!process_audio::safe(2000001, list));
      list[1].created = 200;
      list[1].image = L"c:\\decoder.exe";
      expect(process_audio::identityMatches(2000001, 100, list));
      list[0].created = 300;
      expect(!process_audio::identityMatches(2000001, 100, list));
      expect(!process_audio::descendant(2000002, 2000001, list));
      return {{"passed", 9}};
    }
    if (method == "capabilities")
      return {{"runtime", gst_version_string()},
              {"capture",
               {{"wgc", available("d3d11screencapturesrc")},
                {"dxgi", available("d3d11screencapturesrc")}}},
              {"nvenc", available("nvd3d11h264enc")},
              {"openh264", available("openh264enc")},
              {"webrtc", available("webrtcbin") && available("nicesrc")}};
    if (method == "start") return start(data);
    if (method == "offer") {
      offer(data.at("peer"));
      return nullptr;
    }
    if (method == "signal") {
      signal(data);
      return nullptr;
    }
    if (method == "remove") {
      remove(data.at("peer"));
      return nullptr;
    }
    if (method == "stop") {
      stop();
      return nullptr;
    }
    if (method == "bitrate") {
      bitrate(data.at("bitrate"));
      return nullptr;
    }
    if (method == "stats") return stats();
    if (method == "pcm") {
      pcm(data.at("data"));
      return nullptr;
    }
    throw std::runtime_error("Unknown IPC method");
  }
};
struct Input {
  std::mutex mutex;
  std::vector<std::string> lines;
  std::atomic<bool> eof{false};
};
int main(int argc, char** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(apartment)) {
    std::cerr << "COM initialization failed\n";
    return 1;
  }
  gst_init(&argc, &argv);
  Input input;
  std::thread reader([&] {
    std::string line;
    char c;
    while (std::cin.get(c)) {
      if (c == '\n') {
        std::lock_guard<std::mutex> lock(input.mutex);
        if (input.lines.size() < 128) input.lines.push_back(std::move(line));
        line.clear();
      } else {
        line += c;
        if (line.size() > 1048576) {
          emit({{"event", "error"}, {"message", "IPC line too large"}});
          input.eof = true;
          return;
        }
      }
    }
    input.eof = true;
  });
  Engine engine;
  emit({{"event", "ready"}, {"protocol", 1}});
  while (!input.eof) {
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    std::vector<std::string> lines;
    {
      std::lock_guard<std::mutex> lock(input.mutex);
      lines.swap(input.lines);
    }
    for (auto& line : lines) {
      Json request;
      try {
        request = Json::parse(line);
        auto result = engine.command(request);
        emit({{"id", request.at("id")}, {"ok", true}, {"result", result}});
      } catch (const std::exception& e) {
        emit({{"id", request.is_object() ? request.value("id", Json(nullptr)) : Json(nullptr)},
              {"ok", false},
              {"error", e.what()}});
      }
    }
    engine.poll();
    Sleep(5);
  }
  engine.stop();
  reader.join();
  CoUninitialize();
  return 0;
}
