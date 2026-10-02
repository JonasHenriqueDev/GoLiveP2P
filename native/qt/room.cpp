#include "room.hpp"
#include "protocol.hpp"
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QRandomGenerator>
#include <QDateTime>
RoomServer::RoomServer(QObject* parent)
    : QObject(parent), ws("GoLive P2P", QWebSocketServer::NonSecureMode, this) {
  connect(&tcp, &QTcpServer::newConnection, this, [this] {
    while (tcp.hasPendingConnections()) {
      auto socket = tcp.nextPendingConnection();
      socket->setReadBufferSize(16384);
      auto timeout = new QTimer(socket);
      timeout->setSingleShot(true);
      timeout->start(10000);
      connect(timeout, &QTimer::timeout, socket, &QTcpSocket::abort);
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      auto route = [this, socket, timeout] {
        auto header = socket->peek(16385);
        if (header.size() > 16384) {
          socket->abort();
          return;
        }
        if (!header.contains("\r\n\r\n")) return;
        disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
        timeout->stop();
        if (header.startsWith("GET /discover ")) {
          auto body = Protocol::json({{"app", "golive-p2p"},
                                      {"version", 1},
                                      {"participants", size()},
                                      {"full", size() >= Protocol::MaxPeers}});
          socket->readAll();
          socket->write(
              "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: "
              "no-store\r\nConnection: close\r\nContent-Length: " +
              QByteArray::number(body.size()) + "\r\n\r\n" + body);
          socket->disconnectFromHost();
        } else if (header.startsWith("GET / ") && header.toLower().contains("upgrade: websocket")) {
          socket->setReadBufferSize(0);
          ws.handleConnection(socket);
        } else {
          socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
          socket->disconnectFromHost();
        }
      };
      connect(socket, &QTcpSocket::readyRead, this, route);
      route();
    }
  });
  connect(&ws, &QWebSocketServer::newConnection, this, [this] {
    while (ws.hasPendingConnections()) connection(ws.nextPendingConnection());
  });
}
RoomServer::~RoomServer() { close(); }
bool RoomServer::listen(const QString& ip, quint16 port) {
  close();
  return tcp.listen(QHostAddress(ip), port);
}
void RoomServer::close() {
  tcp.close();
  auto all = sockets.keys();
  members.clear();
  sockets.clear();
  streamer.clear();
  for (auto s : all) {
    disconnect(s, nullptr, this, nullptr);
    s->close();
    s->deleteLater();
  }
  ws.close();
}
void RoomServer::send(QWebSocket* socket, const QJsonObject& value) {
  if (socket && socket->state() == QAbstractSocket::ConnectedState)
    socket->sendTextMessage(QString::fromUtf8(Protocol::json(value)));
}
void RoomServer::broadcast(const QJsonObject& value, QWebSocket* except) {
  for (auto m : members)
    if (m.socket && m.socket != except) send(m.socket, value);
}
void RoomServer::finish(const QString& id) {
  if (!members.contains(id)) return;
  auto member = members.take(id);
  if (member.timer) {
    member.timer->stop();
    member.timer->deleteLater();
  }
  if (member.socket) sockets.remove(member.socket);
  if (streamer == id) {
    streamer.clear();
    broadcast({{"type", "stop-stream"}, {"from", id}});
  }
  broadcast({{"type", "user-left"}, {"peer", member.peer}});
  emit log("Participante saiu: " + member.peer["name"].toString());
}
void RoomServer::connection(QWebSocket* socket) {
  socket->setMaxAllowedIncomingMessageSize(220000);
  socket->setMaxAllowedIncomingFrameSize(220000);
  auto timeout = new QTimer(socket);
  timeout->setSingleShot(true);
  timeout->start(10000);
  connect(timeout, &QTimer::timeout, this, [this, socket] {
    if (!sockets.contains(socket))
      socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "Join timeout");
  });
  connect(socket, &QWebSocket::textMessageReceived, this,
          [this, socket](const QString& text) { message(socket, text); });
  connect(socket, &QWebSocket::binaryMessageReceived, this, [socket](const QByteArray&) {
    socket->close(QWebSocketProtocol::CloseCodeDatatypeNotSupported, "Somente sinalização JSON");
  });
  connect(socket, &QWebSocket::disconnected, this, [this, socket] {
    auto id = sockets.take(socket);
    if (members.contains(id)) {
      auto& m = members[id];
      m.socket = nullptr;
      auto timer = new QTimer(this);
      m.timer = timer;
      timer->setSingleShot(true);
      connect(timer, &QTimer::timeout, this, [this, id] { finish(id); });
      timer->start(Protocol::GraceMs);
    }
    socket->deleteLater();
  });
}
void RoomServer::message(QWebSocket* socket, const QString& text) {
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(text.toUtf8(), &error);
  const auto v = doc.object();
  auto reject = [this, socket](const QString& why) {
    send(socket, {{"type", "error"}, {"message", why}});
  };
  if (error.error != QJsonParseError::NoError || !doc.isObject() || !Protocol::client(v)) {
    reject("Mensagem inválida");
    return;
  }
  const auto type = v["type"].toString(), id = sockets.value(socket);
  if (type == "join-room") {
    if (!id.isEmpty()) {
      reject("Já está na sala");
      return;
    }
    QString resumed;
    const auto token = v["resumeToken"].toString();
    if (!token.isEmpty())
      for (auto it = members.begin(); it != members.end(); ++it)
        if (it->token == token) {
          resumed = it.key();
          break;
        }
    if (!resumed.isEmpty() && members[resumed].socket) {
      reject("Sessão já conectada");
      socket->close();
      return;
    }
    if (resumed.isEmpty() && size() >= Protocol::MaxPeers) {
      send(socket, {{"type", "room-full"}});
      socket->close();
      return;
    }
    const auto next =
        resumed.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : resumed;
    if (resumed.isEmpty()) {
      QByteArray secret;
      for (int i = 0; i < 8; ++i) {
        auto r = QRandomGenerator::system()->generate();
        secret.append(reinterpret_cast<const char*>(&r), sizeof(r));
      }
      const auto ip = socket->peerAddress().toString().remove("::ffff:");
      members[next] = {
          {{"id", next},
           {"name", v["name"].toString().trimmed()},
           {"ip", Protocol::tailnetIp(ip) ? QJsonValue(ip) : QJsonValue(QJsonValue::Null)}},
          QString::fromLatin1(secret.toHex()),
          socket,
          nullptr};
    }
    auto& m = members[next];
    if (m.timer) {
      m.timer->stop();
      m.timer->deleteLater();
      m.timer = nullptr;
    }
    m.socket = socket;
    sockets[socket] = next;
    QJsonArray peers;
    for (auto it = members.begin(); it != members.end(); ++it)
      if (it.key() != next) peers.append(it->peer);
    send(socket,
         {{"type", "joined-room"},
          {"self", m.peer},
          {"peers", peers},
          {"streamerId", streamer.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(streamer)},
          {"resumeToken", m.token},
          {"resumed", !resumed.isEmpty()}});
    broadcast({{"type", "user-joined"}, {"peer", m.peer}}, socket);
    emit log("Participante entrou: " + m.peer["name"].toString());
    return;
  }
  if (id.isEmpty()) {
    reject("Entre na sala primeiro");
    return;
  }
  if (type == "leave-room") {
    finish(id);
    socket->close();
    return;
  }
  if (type == "start-stream") {
    if (!streamer.isEmpty() && streamer != id) {
      reject("Outra pessoa já transmite");
      return;
    }
    streamer = id;
    broadcast({{"type", type}, {"from", id}}, socket);
    return;
  }
  if (type == "stop-stream") {
    if (streamer == id) {
      streamer.clear();
      broadcast({{"type", type}, {"from", id}}, socket);
    }
    return;
  }
  const auto to = v["to"].toString();
  if (!members.contains(to) || !members[to].socket) {
    reject("Destinatário indisponível");
    return;
  }
  if ((type == "offer" && streamer != id) ||
      ((type == "answer" || type == "request-restart") && streamer != to) ||
      (type == "ice-candidate" && streamer != id && streamer != to)) {
    reject("Sinalização fora da transmissão registrada");
    return;
  }
  auto forwarded = v;
  forwarded.remove("to");
  forwarded["from"] = id;
  send(members[to].socket, forwarded);
}
RoomClient::RoomClient(QObject* parent) : QObject(parent) {
  socket.setMaxAllowedIncomingMessageSize(220000);
  socket.setMaxAllowedIncomingFrameSize(220000);
  retry.setSingleShot(true);
  connect(&retry, &QTimer::timeout, this,
          [this] { socket.open(QUrl("ws://" + ip_ + ":" + QString::number(port_))); });
  connect(&socket, &QWebSocket::connected, this, [this] {
    QJsonObject v{{"type", "join-room"}, {"name", name_}};
    if (!token.isEmpty()) v["resumeToken"] = token;
    send(v);
    emit status("Entrando na sala…");
  });
  connect(&socket, &QWebSocket::textMessageReceived, this, [this](const QString& text) {
    auto doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject() || !Protocol::server(doc.object())) {
      emit error("Resposta inválida da sala");
      return;
    }
    const auto v = doc.object();
    if (v["type"] == "joined-room") {
      token = v["resumeToken"].toString();
      self = v["self"].toObject()["id"].toString();
      attempts = 0;
      disconnectedAt = 0;
      emit status("Conectado à sala");
    }
    if (v["type"] == "room-full") {
      leaving = true;
      emit error("Sala cheia (máximo de cinco participantes)");
    }
    emit messageReceived(v);
  });
  connect(&socket, &QWebSocket::disconnected, this, [this] {
    if (leaving) return;
    if (!disconnectedAt) disconnectedAt = QDateTime::currentMSecsSinceEpoch();
    if (QDateTime::currentMSecsSinceEpoch() - disconnectedAt > Protocol::GraceMs) {
      leaving = true;
      emit error("Reconexão expirou; entre novamente na sala");
      return;
    }
    emit status("Reconectando…");
    retry.start(qMin(3000, 500 * (++attempts)));
  });
  connect(&socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    if (!leaving) {
      emit error(socket.errorString());
      if (!retry.isActive()) retry.start(1000);
    }
  });
}
void RoomClient::join(const QString& ip, const QString& name, quint16 port) {
  leave();
  ip_ = ip;
  name_ = name.trimmed().left(32);
  port_ = port;
  token.clear();
  self.clear();
  leaving = false;
  attempts = 0;
  disconnectedAt = 0;
  socket.open(QUrl("ws://" + ip + ":" + QString::number(port)));
}
void RoomClient::leave() {
  leaving = true;
  retry.stop();
  if (socket.state() == QAbstractSocket::ConnectedState) {
    send({{"type", "leave-room"}});
    socket.flush();
    // Let the server process leave-room and close the connection. A close
    // control frame can otherwise overtake queued application frames in Qt.
    QTimer::singleShot(1000, this, [this] {
      if (leaving) socket.close();
    });
  } else {
    socket.close();
  }
  token.clear();
  self.clear();
}
void RoomClient::send(const QJsonObject& value) {
  if (!Protocol::client(value)) {
    emit error("Pedido de sinalização inválido");
    return;
  }
  if (socket.state() != QAbstractSocket::ConnectedState) {
    emit error("Sinalização desconectada");
    return;
  }
  socket.sendTextMessage(QString::fromUtf8(Protocol::json(value)));
}
