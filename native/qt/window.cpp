#include "window.hpp"
#include "protocol.hpp"
#include "updater.hpp"
#include "thumbnails.hpp"
#include <QApplication>
#include <QBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QSettings>
#include <QPainter>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollArea>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QDateTime>
#include <QSysInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QDir>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif
namespace {
QString tailscale() {
  auto path = QStandardPaths::findExecutable("tailscale");
#ifdef Q_OS_WIN
  if (path.isEmpty()) path = qEnvironmentVariable("ProgramFiles") + "/Tailscale/tailscale.exe";
#endif
  return path;
}
void command(QObject* owner, const QStringList& args, std::function<void(QByteArray)> callback) {
  auto p = new QProcess(owner);
#ifdef Q_OS_WIN
  p->setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* a) { a->flags |= 0x08000000; });
#endif
  QObject::connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), owner,
                   [p, callback](int exit, QProcess::ExitStatus status) {
                     callback(exit == 0 && status == QProcess::NormalExit
                                  ? p->readAllStandardOutput()
                                  : QByteArray());
                     p->deleteLater();
                   });
  QObject::connect(p, &QProcess::errorOccurred, owner, [p, callback](QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart) {
      callback({});
      p->deleteLater();
    }
  });
  QTimer::singleShot(6000, p, [p] {
    if (p->state() != QProcess::NotRunning) p->kill();
  });
  p->start(tailscale(), args);
}
QPushButton* button(const QString& title, const QString& object, QWidget* parent) {
  auto b = new QPushButton(title, parent);
  b->setObjectName(object);
  b->setCursor(Qt::PointingHandCursor);
  return b;
}
}  // namespace
VideoView::VideoView(QWidget* parent) : QWidget(parent) {
  setMinimumSize(320, 180);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setObjectName("video");
}
void VideoView::frame(const QByteArray& bytes) {
  auto next = QImage::fromData(bytes, "JPEG");
  if (next.isNull()) return;
  image = std::move(next);
  ++frames;
  update();
}
void VideoView::clear() {
  image = {};
  update();
}
void VideoView::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), QColor("#080e1b"));
  if (image.isNull()) {
    p.setPen(QColor("#8b9bb4"));
    p.drawText(rect(), Qt::AlignCenter,
               "Escolha uma janela ou monitor para compartilhar\nou aguarde a transmissão de um "
               "participante.");
    return;
  }
  auto size = image.size().scaled(this->size(), Qt::KeepAspectRatio);
  QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.drawImage(target, image);
}
void VideoView::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) emit doubleClicked();
}
MainWindow::MainWindow(const QString& runtime, QWidget* parent)
    : QMainWindow(parent), engine(runtime, this), server(this), client(this), network(this) {
  setWindowTitle("GoLive P2P · " + QString(GOLIVE_VERSION) + " · Qt");
  resize(1180, 760);
  setMinimumSize(760, 520);
  QSettings prefs;
  setStyleSheet(
      "QWidget{background:#111a2b;color:#e8eef9;font-family:'Segoe UI';font-size:13px} "
      "QLabel#title{font-size:25px;font-weight:600} "
      "QLineEdit,QComboBox,QDoubleSpinBox{background:#1b2940;border:1px solid "
      "#35465f;border-radius:6px;padding:8px} QPushButton{background:#253850;border:1px solid "
      "#405577;border-radius:6px;padding:9px 14px} QPushButton:hover{background:#304b6b} "
      "QPushButton:disabled{color:#75839a;background:#182437} "
      "QPushButton#share,QPushButton#create{background:#2265cf;border-color:#4384ea} "
      "QGroupBox{border:1px solid #2e415e;border-radius:8px;margin-top:12px;padding-top:12px} "
      "QGroupBox::title{subcontrol-origin:margin;left:12px} "
      "QListWidget,QPlainTextEdit{background:#0d1626;border:1px solid #2e415e;border-radius:6px} "
      "QCheckBox{spacing:8px} QSplitter::handle{background:#20304a}");
  auto central = new QWidget(this);
  setCentralWidget(central);
  auto base = new QVBoxLayout(central);
  auto top = new QHBoxLayout;
  auto title = new QLabel("◉  GoLive P2P");
  title->setObjectName("title");
  top->addWidget(title);
  top->addStretch();
  connection = new QLabel("Conferindo Tailscale…");
  top->addWidget(connection);
  base->addLayout(top);
  lobby = new QWidget;
  auto lobbyLayout = new QVBoxLayout(lobby);
  lobbyLayout->addStretch();
  auto welcome = new QLabel("Compartilhe sua tela. Direto para seu grupo.");
  welcome->setAlignment(Qt::AlignCenter);
  welcome->setStyleSheet("font-size:27px;font-weight:600");
  lobbyLayout->addWidget(welcome);
  auto caption = new QLabel("Até cinco participantes • vídeo e áudio P2P pela sua tailnet");
  caption->setAlignment(Qt::AlignCenter);
  lobbyLayout->addWidget(caption);
  auto form = new QFormLayout;
  name = new QLineEdit(prefs.value("name", "Jonas").toString());
  name->setMaxLength(32);
  name->setObjectName("name");
  host = new QLineEdit(prefs.value("host", "100.94.54.119").toString());
  host->setPlaceholderText("100.x.x.x");
  host->setObjectName("host");
  form->addRow("Seu nome", name);
  form->addRow("IP do host", host);
  auto formContainer = new QWidget;
  formContainer->setMaximumWidth(510);
  formContainer->setLayout(form);
  auto aligned = new QHBoxLayout;
  aligned->addStretch();
  aligned->addWidget(formContainer);
  aligned->addStretch();
  lobbyLayout->addLayout(aligned);
  auto actions = new QHBoxLayout;
  actions->addStretch();
  create = button("Criar sala", "create", lobby);
  join = button("Entrar na sala", "join", lobby);
  discover = button("Procurar salas", "discover", lobby);
  actions->addWidget(create);
  actions->addWidget(join);
  actions->addWidget(discover);
  actions->addStretch();
  lobbyLayout->addLayout(actions);
  lobbyLayout->addStretch();
  base->addWidget(lobby, 1);
  room = new QWidget;
  auto roomLayout = new QHBoxLayout(room);
  roomLayout->setContentsMargins(0, 0, 0, 0);
  splitter = new QSplitter;
  roomLayout->addWidget(splitter);
  side = new QWidget;
  auto sidebar = new QVBoxLayout(side);
  people = new QListWidget;
  people->setMinimumWidth(195);
  sidebar->addWidget(new QLabel("Participantes"));
  sidebar->addWidget(people, 1);
  leave = button("Sair da sala", "leave", side);
  sidebar->addWidget(leave);
  splitter->addWidget(side);
  auto main = new QWidget;
  auto content = new QVBoxLayout(main);
  streamInfo = new QLabel("Pronto para compartilhar");
  streamInfo->setWordWrap(true);
  content->addWidget(streamInfo);
  video = new VideoView;
  content->addWidget(video, 1);
  audioInfo = new QLabel("Áudio: desativado");
  statsInfo = new QLabel("Estatísticas aparecerão durante a transmissão");
  statsInfo->setWordWrap(true);
  content->addWidget(audioInfo);
  content->addWidget(statsInfo);
  controls = new QWidget;
  auto capture = new QGridLayout(controls);
  quality = new QComboBox;
  quality->addItems(
      {"720p · 30 FPS", "720p · 60 FPS", "1080p · 30 FPS", "1080p · 60 FPS", "1440p · 30 FPS"});
  quality->setCurrentIndex(prefs.value("quality", 0).toInt());
  quality->setObjectName("quality");
  bitrate = new QDoubleSpinBox;
  bitrate->setRange(0.5, 20);
  bitrate->setSingleStep(0.5);
  bitrate->setValue(prefs.value("bitrate", 6).toDouble());
  bitrate->setSuffix(" Mbps por espectador");
  audio = new QCheckBox("Transmitir áudio do aplicativo");
  audio->setChecked(true);
  audio->setObjectName("audio");
  audio->setToolTip(
      "Janela: aplicativo e subprocessos. Navegadores podem incluir outras abas. Monitor: somente "
      "aplicativos marcados. Origens não identificadas são bloqueadas.");
  method = new QComboBox;
  method->addItem("Captura automática", "auto");
  method->addItem("PrintWindow · janela sem borda", "printwindow");
  method->addItem("Windows Graphics Capture", "wgc");
  method->addItem("DXGI · monitor", "dxgi");
  method->setToolTip(
      "WGC pode exibir borda no Windows 10. PrintWindow depende do aplicativo e preserva janelas "
      "cobertas quando suportado.");
  capture->addWidget(quality, 0, 0);
  capture->addWidget(bitrate, 0, 1);
  capture->addWidget(audio, 1, 0, 1, 2);
  auto advanced = button("Opções avançadas", "advanced", controls);
  capture->addWidget(advanced, 2, 0);
  capture->addWidget(method, 2, 1);
  method->hide();
  connect(advanced, &QPushButton::clicked, this,
          [this] { method->setVisible(!method->isVisible()); });
  share = button("Escolher janela ou monitor", "share", controls);
  stop = button("Parar transmissão", "stop", controls);
  stop->hide();
  capture->addWidget(share, 3, 0, 1, 2);
  capture->addWidget(stop, 4, 0, 1, 2);
  content->addWidget(controls);
  splitter->addWidget(main);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({225, 900});
  room->hide();
  base->addWidget(room, 1);
  auto logActions = new QHBoxLayout;
  auto save = button("Salvar log", "saveLog", central);
  auto send = button("Enviar log a participante", "sendLog", central);
  auto show = button("Diagnóstico", "showLog", central);
  logActions->addWidget(save);
  logActions->addWidget(send);
  logActions->addStretch();
  logActions->addWidget(show);
  base->addLayout(logActions);
  logs = new QPlainTextEdit;
  logs->setReadOnly(true);
  logs->setMaximumBlockCount(1500);
  logs->setMaximumHeight(150);
  logs->hide();
  base->addWidget(logs);
  connect(save, &QPushButton::clicked, this, &MainWindow::saveLog);
  connect(send, &QPushButton::clicked, this, &MainWindow::sendLog);
  connect(show, &QPushButton::clicked, this, [this] { logs->setVisible(!logs->isVisible()); });
  connect(create, &QPushButton::clicked, this, &MainWindow::createRoom);
  connect(join, &QPushButton::clicked, this, [this] { joinRoom(host->text()); });
  connect(discover, &QPushButton::clicked, this, &MainWindow::discoverRooms);
  connect(leave, &QPushButton::clicked, this, &MainWindow::leaveRoom);
  connect(share, &QPushButton::clicked, this, &MainWindow::selectSource);
  connect(stop, &QPushButton::clicked, this, &MainWindow::stopStream);
  connect(video, &VideoView::doubleClicked, this, &MainWindow::toggleFullscreen);
  connect(&server, &RoomServer::log, this, &MainWindow::log);
  connect(&client, &RoomClient::messageReceived, this, &MainWindow::message);
  connect(&client, &RoomClient::status, this, [this](const QString& s) {
    connection->setText(s);
    log(s);
  });
  connect(&client, &RoomClient::error, this, [this](const QString& s) {
    log(s);
    connection->setText(s);
  });
  connect(&engine, &EngineProcess::eventReceived, this, &MainWindow::media);
  connect(&engine, &EngineProcess::error, this, [this](const QString& s) {
    log(s);
    if (transmitting && !engine.isReady()) {
      transmitting = false;
      client.send({{"type", "stop-stream"}});
      share->show();
      stop->hide();
      audioInfo->setText("Áudio: motor indisponível");
    }
  });
  connect(&engine, &EngineProcess::ready, this, [this] {
    log("Motor C++ pronto");
    if (!testSource.isEmpty() && !client.selfId().isEmpty() && !transmitting && !starting)
      startSource(testSource);
  });
  connect(bitrate, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
    QSettings().setValue("bitrate", value);
    if (transmitting) {
      settings["bitrate"] = int(value * 1000000);
      engine.request("bitrate", {{"bitrate", int(value * 1000000)}});
    }
  });
  connect(quality, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
    QSettings().setValue("quality", i);
    if (transmitting) {
      auto source = selectedSource;
      auto apps = settings["allowedAudioApps"].toArray();
      stopStream();
      startSource(source, apps);
    }
  });
  auto tsTimer = new QTimer(this);
  tsTimer->setInterval(10000);
  connect(tsTimer, &QTimer::timeout, this, &MainWindow::refreshTailnet);
  tsTimer->start();
  auto timer = new QTimer(this);
  timer->setInterval(1000);
  connect(timer, &QTimer::timeout, this, &MainWindow::collect);
  timer->start();
  auto pingTimer = new QTimer(this);
  pingTimer->setInterval(12000);
  connect(pingTimer, &QTimer::timeout, this, &MainWindow::ping);
  pingTimer->start();
  updater = new Updater(this);
  connect(updater, &Updater::status, this, &MainWindow::log);
  connect(updater, &Updater::ready, this, [this] {
    auto update = button("Reiniciar e atualizar", "update", centralWidget());
    centralWidget()->layout()->addWidget(update);
    connect(update, &QPushButton::clicked, this, [this] {
      if (client.selfId().isEmpty())
        updater->install();
      else
        QMessageBox::information(this, "Atualização",
                                 "Saia da sala antes de instalar a atualização.");
    });
  });
  log("GoLive " + QString(GOLIVE_VERSION) + " | Qt " + qVersion() + " | " +
      QSysInfo::prettyProductName());
#ifndef Q_OS_WIN
  create->hide();share->hide();controls->hide();
#endif
  refreshTailnet();
  engine.start();
  QTimer::singleShot(3000, updater, &Updater::check);
}
MainWindow::~MainWindow() {
  closed = true;
  report();
  client.leave();
  server.close();
  engine.shutdown();
}
void MainWindow::log(const QString& text) {
  logs->appendPlainText(QDateTime::currentDateTime().toString(Qt::ISODate) + " " + text.left(8192));
}
void MainWindow::refreshTailnet() {
  command(this, {"status", "--json"}, [this](QByteArray raw) {
    auto state = QJsonDocument::fromJson(raw).object();
    QString ip;
    for (auto entry : state["TailscaleIPs"].toArray())
      if (Protocol::tailnetIp(entry.toString())) {
        ip = entry.toString();
        break;
      }
    tailIp = state["BackendState"] == "Running" ? ip : QString();
    create->setEnabled(!tailIp.isEmpty());
    join->setEnabled(!tailIp.isEmpty());
    discover->setEnabled(!tailIp.isEmpty());
    if (client.selfId().isEmpty())
      connection->setText(tailIp.isEmpty() ? "Tailscale desconectado"
                                           : "Tailscale conectado · " + tailIp);
  });
}
void MainWindow::createRoom() {
#ifndef Q_OS_WIN
  log("Linux é cliente de recepção nesta etapa");return;
#endif
  if (tailIp.isEmpty()) {
    log("Conecte o Tailscale para criar sala");
    return;
  }
  if (!server.listen(tailIp,roomPort)) {
    log("Não foi possível abrir a porta 47621; outra sala pode estar aberta");
    return;
  }
  joinRoom(tailIp);
}
void MainWindow::joinRoom(const QString& ip) {
  if (!Protocol::tailnetIp(ip) || name->text().trimmed().isEmpty()) {
    log("Informe nome e IP válido da tailnet (100.64.0.0/10)");
    return;
  }
  QSettings prefs;
  prefs.setValue("name", name->text().trimmed());
  prefs.setValue("host", ip);
  host->setText(ip);
  client.join(ip, name->text(),roomPort);
}
void MainWindow::leaveRoom() {
  stopStream();
  client.leave();
  updater->sessionActive(false);
  server.close();
  engine.request("stop");
  peers.clear();
  pendingIce.clear();
  mediaPeers.clear();
  streamer.clear();
  video->clear();
  room->hide();
  lobby->show();
  refreshTailnet();
  if (fullscreen) toggleFullscreen();
}
void MainWindow::refreshPeople() {
  people->clear();
  for (auto it = peers.begin(); it != peers.end(); ++it) {
    const auto p = it.value();
    auto item =
        new QListWidgetItem(p["name"].toString() + (it.key() == client.selfId() ? " (Você)" : "") +
                                (it.key() == streamer ? " · transmitindo" : "") +
                                (p.contains("ping") ? "\n" + p["ping"].toString() : ""),
                            people);
    item->setData(Qt::UserRole, it.key());
    item->setToolTip(p["ip"].toString());
  }
  if (!transmitting) share->setEnabled(streamer.isEmpty() && engine.isReady());
}
void MainWindow::message(const QJsonObject& v) {
  auto t = v["type"].toString(), from = v["from"].toString();
  if (t == "joined-room") {
    updater->sessionActive(true);
    const auto old = client.selfId();
    Q_UNUSED(old);
    peers.clear();
    auto self = v["self"].toObject();
    peers[self["id"].toString()] = self;
    for (auto value : v["peers"].toArray()) {
      auto p = value.toObject();
      peers[p["id"].toString()] = p;
    }
    streamer = v["streamerId"].toString();
    lobby->hide();
    room->show();
    refreshPeople();
    ping();
    if (transmitting) {
      for (auto id : peers.keys())
        if (id != client.selfId()) offer(id);
    } else if (!testSource.isEmpty() && engine.isReady() && !starting)
      startSource(testSource);
  } else if (t == "user-joined") {
    auto p = v["peer"].toObject();
    peers[p["id"].toString()] = p;
    refreshPeople();
    if (transmitting) offer(p["id"].toString());
  } else if (t == "user-left") {
    auto id = v["peer"].toObject()["id"].toString();
    peers.remove(id);
    removePeer(id);
    refreshPeople();
  } else if (t == "start-stream") {
    streamer = from;
    streamInfo->setText("Recebendo transmissão de " + peers[from]["name"].toString());
    refreshPeople();
  } else if (t == "stop-stream") {
    removePeer(from);
    streamer.clear();
    video->clear();
    audioInfo->setText("Áudio: desativado");
    streamInfo->setText("Pronto para compartilhar");
    refreshPeople();
  } else if (t == "offer" || t == "answer") {
    if (t == "offer" && from != streamer) {
      log("Oferta recusada: origem não é o transmissor");
      return;
    }
    mediaPeers.insert(from);
    engine.request("signal", {{"type", t}, {"peer", from}, {"sdp", v["sdp"]}},
                   [this, from](QJsonValue, QString e) {
                     if (!e.isEmpty()) return;
                     for (auto ice : pendingIce.take(from))
                       engine.request(
                           "signal",
                           {{"type", "ice-candidate"}, {"peer", from}, {"candidate", ice}});
                   });
  } else if (t == "ice-candidate") {
    if (mediaPeers.contains(from))
      engine.request("signal", {{"type", t}, {"peer", from}, {"candidate", v["candidate"]}});
    else {
      auto& queue = pendingIce[from];
      if (queue.size() < 128)
        queue.append(v["candidate"]);
      else
        log("Fila de candidatos ICE excedida");
    }
  } else if (t == "request-restart" && transmitting)
    offer(from);
  else if (t == "error")
    log(v["message"].toString());
  else if (t == "log-report") {
    if (QMessageBox::question(this, "Log recebido",
                              "Salvar o log enviado por " + peers[from]["name"].toString() + "?") ==
        QMessageBox::Yes) {
      auto path = QFileDialog::getSaveFileName(this, "Salvar log recebido", "golive-log.txt",
                                               "Texto (*.txt)");
      if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) file.write(v["text"].toString().toUtf8());
      }
    }
  }
}
void MainWindow::media(const QJsonObject& v) {
  const auto e = v["event"].toString(), peer = v["peer"].toString();
  if (e == "frame") {
    if (peer == "local") {
      if (transmitting) {
        video->frame(QByteArray::fromBase64(v["jpeg"].toString().toLatin1()));
        ++previewFrames;
      }
    } else if (peer == streamer)
      video->frame(QByteArray::fromBase64(v["jpeg"].toString().toLatin1()));
  } else if (e == "signal") {
    QJsonObject signal{{"type", v["type"]}, {"to", peer}};
    if (v.contains("sdp")) signal["sdp"] = v["sdp"];
    if (v.contains("candidate")) signal["candidate"] = v["candidate"];
    client.send(signal);
  } else if (e == "audio-state") {
    audioActive = v["active"].toBool();
    lastAudioRms = v["rms"].toDouble();
    audioInfo->setText(audioActive
                           ? (lastAudioRms > 0.0001 ? "Áudio do aplicativo: som detectado"
                                                    : "Áudio do aplicativo: capturando · silêncio")
                           : "Áudio: desativado ou bloqueado");
    log("Áudio: fontes=" + QString::number(v["sources"].toInt()) +
        " RMS=" + QString::number(lastAudioRms, 'g', 4));
  } else if (e == "state") {
    log("WebRTC " + peer + ": " + v["state"].toString());
    if (v["state"] == "connected") restarting.remove(peer);
    if (v["state"] == "failed" && !restarting.contains(peer)) {
      restarting.insert(peer);
      if (transmitting)
        offer(peer);
      else
        client.send({{"type", "request-restart"}, {"to", peer}});
    }
  } else if (e == "warning" || e == "error") {
    log(v["message"].toString());
    if (e == "error" && peer == "local" && transmitting) stopStream();
  }
}
QJsonObject MainWindow::captureSettings(const QString& source, const QJsonArray& allowed) {
  const int preset = quality->currentIndex();
  const int width = preset >= 4   ? 2560
                    : preset >= 2 ? 1920
                                  : 1280,
            height = preset >= 4   ? 1440
                     : preset >= 2 ? 1080
                                   : 720,
            fps = (preset == 1 || preset == 3) ? 60 : 30;
  return {{"source", source},
          {"method", method->currentData().toString()},
          {"encoder", "auto"},
          {"width", width},
          {"height", height},
          {"fps", fps},
          {"bitrate", int(bitrate->value() * 1000000)},
          {"audio", audio->isChecked()},
          {"allowedAudioApps", allowed}};
}
void MainWindow::startSource(const QString& source, const QJsonArray& allowed) {
#ifndef Q_OS_WIN
  Q_UNUSED(source);Q_UNUSED(allowed);log("Linux é cliente de recepção nesta etapa");return;
#endif
  if (starting || transmitting || client.selfId().isEmpty() ||
      (!streamer.isEmpty() && streamer != client.selfId()))
    return;
  if (!engine.isReady()) {
    engine.start();
    log("Aguarde o motor iniciar");
    return;
  }
  settings = captureSettings(source, allowed);
  if (!Protocol::mediaRequest("start", settings)) {
    log("Método incompatível com a fonte escolhida");
    return;
  }
  starting = true;
  share->setEnabled(false);
  engine.request("start", settings, [this, source](QJsonValue result, QString error) {
    starting = false;
    if (!error.isEmpty()) {
      refreshPeople();
      return;
    }
    selectedSource = source;
    encoder = result.toObject()["encoder"].toString();
    transmitting = true;
    streamer = client.selfId();
    client.send({{"type", "start-stream"}});
    streamInfo->setText(
        "Sua transmissão · " +
        (encoder.contains("nv", Qt::CaseInsensitive) ? QString("GPU · ") : QString("CPU · ")) +
        encoder + " · " + result.toObject()["method"].toString());
    share->hide();
    stop->show();
    refreshPeople();
    for (auto id : peers.keys())
      if (id != client.selfId()) offer(id);
  });
}
void MainWindow::offer(const QString& id) {
  if (!transmitting) return;
  mediaPeers.insert(id);
  engine.request("offer", {{"peer", id}});
}
void MainWindow::removePeer(const QString& id) {
  if (mediaPeers.remove(id)) engine.request("remove", {{"peer", id}});
  pendingIce.remove(id);
  restarting.remove(id);
}
void MainWindow::stopStream() {
  if (!transmitting && !starting) return;
  transmitting = false;
  starting = false;
  client.send({{"type", "stop-stream"}});
  engine.request("stop");
  mediaPeers.clear();
  pendingIce.clear();
  restarting.clear();
  streamer.clear();
  video->clear();
  share->show();
  stop->hide();
  audioInfo->setText("Áudio: desativado");
  streamInfo->setText("Pronto para compartilhar");
  refreshPeople();
}
void MainWindow::selectSource() {
  if (!engine.isReady()) {
    engine.start();
    return;
  }
  engine.request("sources", {}, [this](QJsonValue value, QString error) {
    if (!error.isEmpty()) return;
    auto dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Escolha o que compartilhar");
    dialog->resize(800, 560);
    auto layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(
        "Escolha uma janela ou monitor. O áudio da janela segue o aplicativo selecionado."));
    auto list = new QListWidget;
    list->setObjectName("sources");
    list->setIconSize(QSize(180, 110));
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);
    list->setSpacing(8);
    for (auto item : value.toArray()) {
      auto source = item.toObject();
#ifdef Q_OS_WIN
      if (source["pid"].toInt() == int(GetCurrentProcessId())) continue;
#endif
      auto row = new QListWidgetItem(
          source["name"].toString() + "\n" +
              (source["kind"] == "screen" ? "Monitor inteiro" : "Aplicativo / janela"),
          list);
      row->setData(Qt::UserRole, source["id"].toString());
      row->setSizeHint(QSize(230, 155));
      row->setToolTip(source["name"].toString());
      QPixmap picture(180, 110);
      picture.fill(QColor("#1b2940"));
      QPainter painter(&picture);
      painter.setPen(QColor("#98b8e8"));
      painter.drawText(picture.rect(), Qt::AlignCenter,
                       source["kind"] == "screen" ? "▣ Monitor" : "▤ Janela");
      painter.end();
      row->setIcon(QIcon(picture));
    }
    layout->addWidget(list, 1);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Compartilhar");
    layout->addWidget(buttons);
    auto launch = [this, dialog, list] {
      if (!list->currentItem()) return;
      const auto id = list->currentItem()->data(Qt::UserRole).toString();
      if (id.startsWith("monitor:") && audio->isChecked()) {
        engine.request(
            "audio-sessions", {}, [this, dialog, id](QJsonValue sessions, QString error) {
              if (!error.isEmpty()) return;
              QDialog allowed(this);
              allowed.setWindowTitle("Áudio do monitor · aplicativos permitidos");
              auto box = new QVBoxLayout(&allowed);
              box->addWidget(
                  new QLabel("Marque somente os aplicativos cujo áudio deseja transmitir."));
              auto apps = new QListWidget;
              box->addWidget(apps);
              QSet<QString> seen;
              for (auto s : sessions.toArray()) {
                auto v = s.toObject();
                const auto image = v["image"].toString();
                if (seen.contains(image)) continue;
                seen.insert(image);
                auto item = new QListWidgetItem(
                    v["name"].toString() + (v["allowed"].toBool() ? "" : " · bloqueado"), apps);
                item->setData(Qt::UserRole, image);
                item->setToolTip(v["reason"].toString());
                item->setFlags(v["allowed"].toBool() ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable
                                                     : Qt::NoItemFlags);
                item->setCheckState(Qt::Unchecked);
              }
              auto accept = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
              box->addWidget(accept);
              connect(accept, &QDialogButtonBox::accepted, &allowed, &QDialog::accept);
              connect(accept, &QDialogButtonBox::rejected, &allowed, &QDialog::reject);
              allowed.resize(550, 350);
              if (allowed.exec() == QDialog::Accepted) {
                QJsonArray images;
                for (int i = 0; i < apps->count(); ++i)
                  if (apps->item(i)->checkState() == Qt::Checked)
                    images.append(apps->item(i)->data(Qt::UserRole).toString());
                dialog->accept();
                startSource(id, images);
              }
            });
      } else {
        dialog->accept();
        startSource(id);
      }
    };
    connect(buttons, &QDialogButtonBox::accepted, this, launch);
    connect(list, &QListWidget::itemDoubleClicked, this, [launch](QListWidgetItem*) { launch(); });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    dialog->show();
    installSourceThumbnails(dialog, list);
    if (!testReport.isEmpty()) QTimer::singleShot(1200, dialog, [this, dialog] {
      QGuiApplication::primaryScreen()->grabWindow(dialog->winId()).save(testReport + ".picker.png");
    });
  });
}
void MainWindow::discoverRooms() {
  discover->setEnabled(false);
  command(this, {"status", "--json"}, [this](QByteArray raw) {
    auto peers = QJsonDocument::fromJson(raw).object()["Peer"].toObject();
    auto dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Salas na tailnet");
    auto layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel("Consultando dispositivos online…"));
    auto list = new QListWidget;
    layout->addWidget(list);
    dialog->resize(480, 320);
    connect(list, &QListWidget::itemDoubleClicked, this, [this, dialog](QListWidgetItem* item) {
      joinRoom(item->data(Qt::UserRole).toString());
      dialog->accept();
    });
    dialog->show();
    int count = 0;
    for (auto entry : peers) {
      auto peer = entry.toObject();
      if (!peer["Online"].toBool()) continue;
      for (auto ip : peer["TailscaleIPs"].toArray()) {
        if (!Protocol::tailnetIp(ip.toString()) || ++count > 100) continue;
        QNetworkRequest req(QUrl("http://" + ip.toString() + ":47621/discover"));
        req.setTransferTimeout(1200);
        auto reply = network.get(req);
        connect(reply, &QNetworkReply::finished, dialog, [reply, list, peer, ip] {
          auto bytes = reply->read(4097);
          auto v = QJsonDocument::fromJson(bytes).object();
          if (bytes.size() <= 4096 && v["app"] == "golive-p2p") {
            auto item =
                new QListWidgetItem(peer["HostName"].toString() + " · " + ip.toString() + " · " +
                                        QString::number(v["participants"].toInt()) + "/5",
                                    list);
            item->setData(Qt::UserRole, ip.toString());
            if (v["full"].toBool()) item->setFlags(Qt::NoItemFlags);
          }
          reply->deleteLater();
        });
      }
    }
    discover->setEnabled(true);
  });
}
void MainWindow::ping() {
  for (auto id : peers.keys()) {
    const auto ip = peers[id]["ip"].toString();
    if (id == client.selfId() || !Protocol::tailnetIp(ip)) continue;
    command(this, {"ping", "--c=1", "--timeout=3s", ip}, [this, id](QByteArray raw) {
      if (!peers.contains(id)) return;
      const auto text = QString::fromUtf8(raw);
      auto match = QRegularExpression("\\bin\\s+([\\d.]+)\\s*ms\\b").match(text);
      peers[id]["ping"] = match.hasMatch()
                              ? match.captured(1) + " ms · " +
                                    (text.contains("DERP", Qt::CaseInsensitive) ? "DERP" : "direto")
                              : "ping indisponível";
      refreshPeople();
    });
  }
}
void MainWindow::collect() {
  if (statsBusy || !engine.isReady() || (client.selfId().isEmpty() && !transmitting)) return;
  statsBusy = true;
  engine.request("stats", {}, [this](QJsonValue value, QString error) {
    statsBusy = false;
    if (!error.isEmpty()) return;
    lastStats = value.toObject();
    if (!testReport.isEmpty()) statsSamples.append(lastStats);
    const auto audio = lastStats["audio"].toObject();
    if (transmitting) lastAudioRms = audio["rms"].toDouble();
    const auto peers = lastStats["peers"].toObject();
    QStringList lines;
    for (auto it = peers.begin(); it != peers.end(); ++it) {
      auto p = it.value().toObject();
      auto raw = p["raw"].toObject();
      double bytes = 0, rtt = -1;
      int lost = 0;
      for (auto entry : raw) {
        auto stat = entry.toObject();
        if (stat["kind"] == "video" &&
            stat["type"] == (transmitting ? "outbound-rtp" : "inbound-rtp")) {
          bytes += stat[transmitting ? "bytes-sent" : "bytes-received"].toDouble();
          lost += stat["packets-lost"].toInt();
        }
        if (stat["type"] == "candidate-pair" && stat.contains("current-round-trip-time"))
          rtt = stat["current-round-trip-time"].toDouble() * 1000;
      }
      lines.append(
          QString("%1 · H264 · %2×%3 · %4 quadros · %5 MB · RTT %6 ms · perdas %7")
              .arg(this->peers.value(it.key())["name"].toString(it.key().left(8)))
              .arg(transmitting ? lastStats["width"].toInt() : p["width"].toInt())
              .arg(transmitting ? lastStats["height"].toInt() : p["height"].toInt())
              .arg(transmitting ? lastStats["encodedFrames"].toInt() : p["receivedFrames"].toInt())
              .arg(bytes / 1000000, 0, 'f', 2)
              .arg(rtt < 0 ? QString("—") : QString::number(rtt, 'f', 0))
              .arg(lost));
      if (!transmitting)
        audioInfo->setText(p["audioRms"].toDouble() > 0.0001 ? "Áudio recebido: som detectado"
                                                             : "Áudio recebido: silêncio");
    }
    statsInfo->setText(lines.isEmpty() ? "Prévia local · " + QString::number(previewFrames) +
                                             " quadros · aguardando espectadores"
                                       : lines.join('\n'));
  });
}
void MainWindow::saveLog() {
  auto path =
      QFileDialog::getSaveFileName(this, "Salvar diagnóstico", "golive-log.txt", "Texto (*.txt)");
  if (path.isEmpty()) return;
  QFile file(path);
  if (file.open(QIODevice::WriteOnly)) file.write(logs->toPlainText().toUtf8());
}
void MainWindow::sendLog() {
  auto item = people->currentItem();
  if (!item || item->data(Qt::UserRole).toString() == client.selfId()) {
    log("Selecione outro participante para enviar o log");
    return;
  }
  client.send({{"type", "log-report"},
               {"to", item->data(Qt::UserRole).toString()},
               {"text", logs->toPlainText().right(120000)}});
}
void MainWindow::toggleFullscreen() {
  fullscreen = !fullscreen;
  side->setVisible(!fullscreen);
  controls->setVisible(!fullscreen);
  if (fullscreen)
    showFullScreen();
  else
    showNormal();
}
void MainWindow::report() {
  if (testReport.isEmpty()) return;
  if(isVisible()) QGuiApplication::primaryScreen()->grabWindow(winId()).save(testReport + ".png");
  QFile file(testReport);
  if (file.open(QIODevice::WriteOnly))
    file.write(QJsonDocument(QJsonObject{{"version", GOLIVE_VERSION},
                                         {"ui", "Qt Widgets"},
                                         {"tailnetIp", tailIp},
                                         {"self", client.selfId()},
                                         {"transmitting", transmitting},
                                         {"encoder", encoder},
                                         {"displayedFrames", double(video->frames)},
                                         {"previewFrames", double(previewFrames)},
                                         {"audioActive", audioActive},
                                         {"audioRms", lastAudioRms},
                                         {"stats", lastStats},
                                         {"samples", statsSamples},
                                         {"logs", logs->toPlainText()}})
                   .toJson());
}
void MainWindow::automate(const QStringList& args) {
  auto value = [args](const QString& key) {
    for (auto a : args)
      if (a.startsWith(key + "=")) return a.mid(key.size() + 1);
    return QString();
  };
  testReport = value("--report");
  testSource = value("--source");
  bool portValid=false;const auto specifiedPort=value("--port").toUInt(&portValid);
  if(portValid && specifiedPort>0 && specifiedPort<=65535)roomPort=quint16(specifiedPort);
  if (args.contains("--picker-test")) {
    auto timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this, timer] { if(engine.isReady()){timer->stop();selectSource();} });
    timer->start();
  }
  if (args.contains("--no-audio")) audio->setChecked(false);
  if (args.contains("--1080p60")) quality->setCurrentIndex(3);
  auto ip = value("--connect");
  if (args.contains("--host") || !ip.isEmpty()) {
    auto timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this, timer, ip] {
      if (tailIp.isEmpty() || !engine.isReady()) return;
      timer->stop();
      if (ip.isEmpty())
        createRoom();
      else
        joinRoom(ip);
    });
    timer->start();
  }
  const auto duration = value("--duration").toInt();
  if (duration > 0)
    QTimer::singleShot(qMin(duration, 300) * 1000, this, [this] {
      report();
      close();
    });
}
