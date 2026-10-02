#pragma once
#include <QObject>
#include <QTcpServer>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QTimer>
class RoomServer : public QObject {
  Q_OBJECT
 public:
  explicit RoomServer(QObject* parent = nullptr);
  ~RoomServer() override;
  bool listen(const QString& ip, quint16 port = 47621);
  void close();
  quint16 port() const { return tcp.serverPort(); }
  int size() const { return members.size(); }
 signals:
  void log(QString);

 private:
  struct Member {
    QJsonObject peer;
    QString token;
    QPointer<QWebSocket> socket;
    QPointer<QTimer> timer;
  };
  QTcpServer tcp;
  QWebSocketServer ws;
  QMap<QString, Member> members;
  QMap<QWebSocket*, QString> sockets;
  QString streamer;
  void connection(QWebSocket*);
  void message(QWebSocket*, const QString&);
  void finish(const QString&);
  void send(QWebSocket*, const QJsonObject&);
  void broadcast(const QJsonObject&, QWebSocket* except = nullptr);
};
class RoomClient : public QObject {
  Q_OBJECT
 public:
  explicit RoomClient(QObject* parent = nullptr);
  void join(const QString& ip, const QString& name, quint16 port = 47621);
  void leave();
  void reconnect();
  void send(const QJsonObject&);
  QString selfId() const { return self; }
 signals:
  void messageReceived(QJsonObject);
  void status(QString);
  void error(QString);

 private:
  QWebSocket socket;
  QTimer retry;
  QString ip_, name_, token, self;
  quint16 port_ = 47621;
  bool leaving = true;
  int attempts = 0;
  qint64 disconnectedAt = 0;
};
