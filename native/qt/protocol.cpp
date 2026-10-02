#include "protocol.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QHostAddress>
#include <QRegularExpression>
#include <cmath>
namespace {
bool text(const QJsonObject& v, const char* key, int max, int min = 1) {
  return v[key].isString() && v[key].toString().size() >= min && v[key].toString().size() <= max;
}
bool integer(const QJsonValue& v, double min, double max) {
  return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() == std::floor(v.toDouble()) &&
         v.toDouble() >= min && v.toDouble() <= max;
}
bool candidate(const QJsonObject& v) {
  return text(v, "candidate", 4096, 0) &&
         (!v.contains("sdpMLineIndex") || v["sdpMLineIndex"].isNull() ||
          integer(v["sdpMLineIndex"], 0, 8)) &&
         (!v.contains("sdpMid") || v["sdpMid"].isNull() || text(v, "sdpMid", 256, 0));
}
bool peer(const QJsonObject& v) {
  return Protocol::uuid(v["id"].toString()) && text(v, "name", 32) &&
         (v["ip"].isNull() || Protocol::tailnetIp(v["ip"].toString()));
}
}  // namespace
bool Protocol::tailnetIp(const QString& ip) {
  QHostAddress address(ip);
  bool ok = false;
  const auto n = address.toIPv4Address(&ok);
  return ok && ip == address.toString() && (n & 0xffc00000u) == 0x64400000u;
}
bool Protocol::uuid(const QString& v) {
  static QRegularExpression re(
      "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
  return re.match(v).hasMatch();
}
QByteArray Protocol::json(const QJsonObject& v) {
  return QJsonDocument(v).toJson(QJsonDocument::Compact);
}
bool Protocol::client(const QJsonObject& v) {
  const auto t = v["type"].toString();
  if (t == "join-room") {
    static QRegularExpression token("^[a-f0-9]{64}$");
    return text(v, "name", 32) && !v["name"].toString().trimmed().isEmpty() &&
           (!v.contains("resumeToken") || token.match(v["resumeToken"].toString()).hasMatch());
  }
  if (t == "leave-room" || t == "start-stream" || t == "stop-stream") return true;
  if (!uuid(v["to"].toString())) return false;
  if (t == "offer" || t == "answer") return text(v, "sdp", 200000);
  if (t == "ice-candidate")
    return v["candidate"].isObject() && candidate(v["candidate"].toObject());
  if (t == "log-report") return text(v, "text", 120000);
  return t == "request-restart";
}
bool Protocol::server(const QJsonObject& v) {
  const auto t = v["type"].toString();
  if (t == "joined-room") {
    if (!peer(v["self"].toObject()) || !v["peers"].isArray() || v["peers"].toArray().size() > 4 ||
        !v["resumed"].isBool() || (!v["streamerId"].isNull() && !uuid(v["streamerId"].toString())))
      return false;
    for (auto p : v["peers"].toArray())
      if (!peer(p.toObject())) return false;
    static QRegularExpression token("^[a-f0-9]{64}$");
    return token.match(v["resumeToken"].toString()).hasMatch();
  }
  if (t == "user-joined" || t == "user-left") return peer(v["peer"].toObject());
  if (t == "room-full") return true;
  if (t == "error") return text(v, "message", 8192);
  QJsonObject mapped = v;
  mapped["to"] = mapped.take("from");
  return client(mapped);
}
bool Protocol::mediaRequest(const QString& method, const QJsonObject& v) {
  if (method == "start") {
    static QRegularExpression source("^(monitor|window):[1-9][0-9]{0,19}$");
    const auto s = v["source"].toString(), m = v["method"].toString(), e = v["encoder"].toString();
    if (!source.match(s).hasMatch() ||
        !QStringList{"auto", "printwindow", "wgc", "dxgi"}.contains(m) ||
        !QStringList{"auto", "software"}.contains(e) || (s.startsWith("window:") && m == "dxgi") ||
        (s.startsWith("monitor:") && m == "printwindow"))
      return false;
    if (!integer(v["width"], 320, 2560) || !integer(v["height"], 180, 1440) ||
        !integer(v["bitrate"], 500000, 20000000) || !(v["fps"] == 30 || v["fps"] == 60) ||
        !v["audio"].isBool())
      return false;
    if (v.contains("allowedAudioApps")) {
      if (!v["allowedAudioApps"].isArray() || v["allowedAudioApps"].toArray().size() > 32)
        return false;
      for (auto a : v["allowedAudioApps"].toArray())
        if (!a.isString() || a.toString().isEmpty() || a.toString().size() > 32768) return false;
    }
    return true;
  }
  if (method == "offer" || method == "remove") return uuid(v["peer"].toString());
  if (method == "signal") {
    QJsonObject mapped = v;
    mapped["to"] = mapped.take("peer");
    return client(mapped) &&
           QStringList{"offer", "answer", "ice-candidate"}.contains(v["type"].toString());
  }
  if (method == "bitrate") return integer(v["bitrate"], 500000, 20000000);
  return QStringList{"capabilities", "sources", "audio-sessions", "stats", "stop"}.contains(
             method) &&
         v.isEmpty();
}
bool Protocol::mediaEvent(const QJsonObject& v) {
  if (v["v"] != 1) return false;
  const auto e = v["event"].toString();
  if (e == "ready") return v["protocol"] == 1;
  if (e == "audio-state") return v["active"].isBool() && integer(v["sources"], 0, 1024);
  if (e == "frame")
    return (v["peer"] == "local" || uuid(v["peer"].toString())) && text(v, "jpeg", 2000000);
  if (e == "state")
    return uuid(v["peer"].toString()) &&
           QStringList{"new", "connecting", "connected", "disconnected", "failed", "closed"}
               .contains(v["state"].toString());
  if (e == "signal") {
    QJsonObject mapped = v;
    mapped["to"] = mapped.take("peer");
    return client(mapped) &&
           QStringList{"offer", "answer", "ice-candidate"}.contains(v["type"].toString());
  }
  return (e == "warning" || e == "error") && text(v, "message", 8192);
}
bool Protocol::mediaResult(const QString& method, const QJsonValue& value) {
  const auto v = value.toObject();
  if (method == "capabilities")
    return value.isObject() && text(v, "runtime", 256) && v["capture"].isObject() &&
           v["webrtc"].isBool() && v["nvenc"].isBool() && v["openh264"].isBool();
  if (method == "start")
    return value.isObject() && text(v, "encoder", 256) &&
           QStringList{"printwindow", "wgc", "dxgi"}.contains(v["method"].toString()) &&
           v["audio"].isBool() && v["borderRemovalVerified"] == false;
  if (method == "sources" || method == "audio-sessions") {
    if (!value.isArray() || value.toArray().size() > 1024) return false;
    for (auto item : value.toArray()) {
      if (!item.isObject()) return false;
      auto source = item.toObject();
      if (method == "sources") {
        static QRegularExpression id("^(monitor|window):[1-9][0-9]{0,19}$");
        if (!id.match(source["id"].toString()).hasMatch() || !text(source, "name", 4096) ||
            !QStringList{"screen", "window"}.contains(source["kind"].toString()))
          return false;
      } else if (!integer(source["pid"], 1, 4294967295.) || !text(source, "name", 4096) ||
                 !text(source, "image", 32768, 0) || !source["allowed"].isBool())
        return false;
    }
    return true;
  }
  if (method == "stats") {
    if (!value.isObject() || !integer(v["encodedFrames"], 0, 9007199254740991.) ||
        !v["peers"].isObject())
      return false;
    auto peers = v["peers"].toObject();
    if (peers.size() > 4) return false;
    for (auto it = peers.begin(); it != peers.end(); ++it) {
      auto p = it.value().toObject();
      if (!uuid(it.key()) || !p["raw"].isObject() ||
          !integer(p["receivedFrames"], 0, 9007199254740991.) || !integer(p["connection"], 0, 5) ||
          !integer(p["ice"], 0, 6))
        return false;
    }
    return true;
  }
  return value.isNull();
}
