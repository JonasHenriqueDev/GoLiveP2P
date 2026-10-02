#include "protocol.hpp"
#include "room.hpp"
#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
class Tests : public QObject {
  Q_OBJECT
 private slots:
  void ips() {
    QVERIFY(Protocol::tailnetIp("100.75.12.74"));
    QVERIFY(!Protocol::tailnetIp("100.1.2.3"));
    QVERIFY(!Protocol::tailnetIp("127.0.0.1"));
    QVERIFY(!Protocol::tailnetIp("100.999.1.1"));
  }
  void media() {
    QJsonObject v{{"source", "window:123"}, {"method", "auto"}, {"encoder", "auto"},
                  {"width", 1920},          {"height", 1080},   {"fps", 60},
                  {"bitrate", 6000000},     {"audio", true}};
    QVERIFY(Protocol::mediaRequest("start", v));
    v["method"] = "dxgi";
    QVERIFY(!Protocol::mediaRequest("start", v));
    v["source"] = "monitor:123";
    QVERIFY(Protocol::mediaRequest("start", v));
    v["method"] = "printwindow";
    QVERIFY(!Protocol::mediaRequest("start", v));
    v["method"] = "auto";
    v["bitrate"] = 100;
    QVERIFY(!Protocol::mediaRequest("start", v));
    QVERIFY(!Protocol::mediaRequest("run-shell", {}));
  }
  void signaling() {
    const QString id = "00000000-0000-4000-8000-000000000001";
    QVERIFY(Protocol::client({{"type", "offer"}, {"to", id}, {"sdp", "v=0"}}));
    QVERIFY(!Protocol::client({{"type", "offer"}, {"to", "bad"}, {"sdp", "v=0"}}));
    QVERIFY(!Protocol::client({{"type", "pcm"}, {"data", "audio"}}));
    QVERIFY(!Protocol::client({{"type", "offer"}, {"to", id}, {"sdp", QString(200001, 'x')}}));
  }
  void results() {
    QVERIFY(!Protocol::mediaResult("stats", QJsonObject{{"peers", QJsonObject()}}));
    QVERIFY(!Protocol::mediaResult(
        "sources",
        QJsonArray{QJsonObject{{"id", "window:0"}, {"kind", "window"}, {"name", "Invalid"}}}));
    QVERIFY(Protocol::mediaResult("stop", QJsonValue(QJsonValue::Null)));
    QVERIFY(!Protocol::mediaResult("stop", QJsonObject()));
    QVERIFY(!Protocol::mediaEvent({{"v", 2}, {"event", "ready"}, {"protocol", 1}}));
  }
  void reconnect() {
    RoomServer server;
    QVERIFY(server.listen("127.0.0.1", 0));
    RoomClient client;
    QSignalSpy messages(&client, &RoomClient::messageReceived);
    client.join("127.0.0.1", "Resume", server.port());
    QTRY_VERIFY(!client.selfId().isEmpty());
    const auto id = client.selfId();
    messages.clear();
    client.reconnect();
    QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 4000);
    QCOMPARE(client.selfId(), id);
    QCOMPARE(server.size(), 1);
    QVERIFY(messages.last()[0].toJsonObject()["resumed"].toBool());
    client.leave();
    QTRY_COMPARE(server.size(), 0);
  }
  void roomRoundtrip() {
    RoomServer server;
    QVERIFY(server.listen("127.0.0.1", 0));
    RoomClient a, b;
    QSignalSpy first(&a, &RoomClient::messageReceived), second(&b, &RoomClient::messageReceived);
    a.join("127.0.0.1", "Host", server.port());
    QTRY_VERIFY_WITH_TIMEOUT(!a.selfId().isEmpty(), 3000);
    b.join("127.0.0.1", "Viewer", server.port());
    QTRY_VERIFY_WITH_TIMEOUT(!b.selfId().isEmpty(), 3000);
    QCOMPARE(server.size(), 2);
    second.clear();
    b.send({{"type", "offer"}, {"to", a.selfId()}, {"sdp", "v=0"}});
    QTRY_VERIFY(!second.isEmpty());
    QCOMPARE(second.last()[0].toJsonObject()["type"].toString(), QString("error"));
    a.send({{"type", "start-stream"}});
    QTRY_COMPARE(second.last()[0].toJsonObject()["type"].toString(), QString("start-stream"));
    a.send({{"type", "offer"}, {"to", b.selfId()}, {"sdp", "v=0"}});
    QTRY_COMPARE(second.last()[0].toJsonObject()["type"].toString(), QString("offer"));
    QCOMPARE(second.last()[0].toJsonObject()["from"].toString(), a.selfId());
    b.leave();
    QTRY_COMPARE(server.size(), 1);
    a.leave();
    QTRY_COMPARE(server.size(), 0);
  }
  void discover() {
    RoomServer server;
    QVERIFY(server.listen("127.0.0.1", 0));
    QNetworkAccessManager manager;
    auto reply = manager.get(
        QNetworkRequest(QUrl("http://127.0.0.1:" + QString::number(server.port()) + "/discover")));
    QSignalSpy finished(reply, &QNetworkReply::finished);
    QVERIFY(finished.wait(3000));
    QCOMPARE(QJsonDocument::fromJson(reply->readAll()).object()["app"].toString(),
             QString("golive-p2p"));
    reply->deleteLater();
  }
  void roomLimit() {
    RoomServer server;
    QVERIFY(server.listen("127.0.0.1", 0));
    std::vector<std::unique_ptr<RoomClient>> clients;
    for (int i = 0; i < 5; ++i) {
      auto c = std::make_unique<RoomClient>();
      c->join("127.0.0.1", "Peer", server.port());
      QTRY_VERIFY(!c->selfId().isEmpty());
      clients.push_back(std::move(c));
    }
    RoomClient extra;
    QSignalSpy messages(&extra, &RoomClient::messageReceived);
    extra.join("127.0.0.1", "Sixth", server.port());
    QTRY_VERIFY(!messages.isEmpty());
    QCOMPARE(messages.first()[0].toJsonObject()["type"].toString(), QString("room-full"));
    QCOMPARE(server.size(), 5);
    for (auto& c : clients) c->leave();
    QTRY_COMPARE(server.size(), 0);
  }
};
QTEST_GUILESS_MAIN(Tests)
#include "tests.moc"
