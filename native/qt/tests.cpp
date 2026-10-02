#include "protocol.hpp"
#include "engine.hpp"
#include <QTemporaryDir>
#include <QFile>
#include "room.hpp"
#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
class Tests : public QObject {
  Q_OBJECT
 private slots:
  void earlySignalingSurvivesSlowMediaStartup() {
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
#ifdef Q_OS_WIN
    const auto executable = QString(".exe");
#else
    const auto executable = QString();
#endif
    QVERIFY(QFile::copy(QCoreApplication::applicationDirPath() + "/engine-fixture" + executable,
                        runtime.path() + "/media-engine" + executable));
    EngineProcess engine(runtime.path());
    QSignalSpy errors(&engine, &EngineProcess::error);
    QSignalSpy ready(&engine, &EngineProcess::ready);
    engine.start();
    bool completed = false;
    QString reason;
    engine.request(
        "signal",
        {{"type", "offer"}, {"peer", "11111111-1111-4111-8111-111111111111"}, {"sdp", "v=0\r\n"}},
        [&](QJsonValue, QString error) {
          completed = true;
          reason = error;
        });
    QTest::qWait(80);
    QVERIFY(!completed);
    QVERIFY(errors.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
    QVERIFY2(reason.isEmpty(), qPrintable(reason));
    QCOMPARE(ready.count(), 1);
    QVERIFY(errors.isEmpty());
    engine.shutdown();
  }

  void cancellationBeforeMediaReady() {
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
#ifdef Q_OS_WIN
    const auto suffix = QString(".exe");
#else
    const auto suffix = QString();
#endif
    QVERIFY(QFile::copy(QCoreApplication::applicationDirPath() + "/engine-fixture" + suffix,
                        runtime.path() + "/media-engine" + suffix));
    EngineProcess engine(runtime.path());
    QSignalSpy ready(&engine, &EngineProcess::ready);
    engine.start();
    const auto peer = QString("11111111-1111-4111-8111-111111111111");
    int callbacks = 0;
    QString reason;
    engine.request("signal", {{"type", "offer"}, {"peer", peer}, {"sdp", "v=0\r\n"}},
                   [&](QJsonValue, QString error) {
                     ++callbacks;
                     reason = error;
                   });
    engine.request("remove", {{"peer", peer}});
    QCOMPARE(callbacks, 1);
    QCOMPARE(reason, QString("Pedido cancelado"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QTest::qWait(80);
    QCOMPARE(callbacks, 1);
    engine.shutdown();
  }
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
