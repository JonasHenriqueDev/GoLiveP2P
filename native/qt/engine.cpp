#include "engine.hpp"
#include "protocol.hpp"
#include <QProcessEnvironment>
#include <QDir>
#include <QJsonDocument>
#include <QDateTime>
#include <QTimer>
#include <QStandardPaths>
EngineProcess::EngineProcess(const QString& runtime, QObject* parent)
    : QObject(parent), runtime_(QDir(runtime).absolutePath()) {
  connect(&process, &QProcess::readyReadStandardOutput, this, &EngineProcess::read);
  connect(&process, &QProcess::readyReadStandardError, this, [this] {
    const auto output = process.readAllStandardError();
    if (output.contains("ERROR")) emit error(QString::fromUtf8(output.left(8192)));
  });
  connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
    if (!closing) fail(process.errorString());
  });
  connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int, QProcess::ExitStatus) {
            if (!closing) fail("Motor de mídia encerrou; inicie novamente a transmissão.");
          });
  auto timer = new QTimer(this);
  timer->setInterval(250);
  connect(timer, &QTimer::timeout, this, [this] {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (auto id : pending.keys())
      if (pending[id].deadline < now) {
        auto p = pending.take(id);
        if (p.callback) p.callback({}, "Prazo do IPC excedido");
        emit error("Prazo do IPC excedido");
      }
  });
  timer->start();
}
EngineProcess::~EngineProcess() { shutdown(); }
void EngineProcess::start() {
  if (process.state() != QProcess::NotRunning) return;
  closing = false;
  ready_ = false;
  buffer.clear();
  auto env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
  env.insert("PATH", runtime_ + "/bin;" + env.value("PATH"));
  env.insert("GST_PLUGIN_PATH", runtime_ + "/lib/gstreamer-1.0");
  env.insert("GST_PLUGIN_SYSTEM_PATH", runtime_ + "/lib/gstreamer-1.0");
  env.insert("GST_PLUGIN_SCANNER", runtime_ + "/libexec/gstreamer-1.0/gst-plugin-scanner.exe");
#else
  const auto plugins = runtime_ + "/lib/gstreamer-1.0";
  if (QDir(plugins).exists()) {
    env.insert("GST_PLUGIN_PATH", plugins);
    env.insert("GST_PLUGIN_SYSTEM_PATH", plugins);
    env.insert("LD_LIBRARY_PATH", runtime_ + "/lib:" + env.value("LD_LIBRARY_PATH"));
    env.insert("GST_PLUGIN_SCANNER", runtime_ + "/gst-plugin-scanner");
  }
#endif
  env.insert("GST_REGISTRY",
             QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/gst-registry.bin");
  QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
  process.setProcessEnvironment(env);
  process.setWorkingDirectory(runtime_);
#ifdef Q_OS_WIN
  process.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) { args->flags |= 0x08000000; });
#endif
#ifdef Q_OS_WIN
  process.start(runtime_ + "/media-engine.exe");
#else
  process.start(runtime_ + "/media-engine");
#endif
  QTimer::singleShot(20000, this, [this] {
    if (!ready_ && process.state() != QProcess::NotRunning) {
      fail("Motor não iniciou em 20 segundos");
      process.kill();
    }
  });
}
void EngineProcess::shutdown() {
  closing = true;
  ready_ = false;
  if (process.state() != QProcess::NotRunning) {
    process.closeWriteChannel();
    if (!process.waitForFinished(2000)) {
      process.kill();
      process.waitForFinished(2000);
    }
  }
  fail("Motor encerrado");
}
void EngineProcess::fail(const QString& reason) {
  ready_ = false;
  auto all = std::move(pending);
  pending.clear();
  for (auto p : all)
    if (p.callback) p.callback({}, reason);
  if (!closing) emit error(reason);
}
void EngineProcess::request(const QString& method, const QJsonObject& data, Callback callback) {
  if (!ready_ || pending.size() >= 128 || !Protocol::mediaRequest(method, data)) {
    const QString e = !ready_ ? "Motor indisponível" : "Pedido IPC inválido ou fila cheia";
    if (callback) callback({}, e);
    emit error(e);
    return;
  }
  const int id = ++nextId;
  pending[id] = {method, callback, QDateTime::currentMSecsSinceEpoch() + 20000};
  auto bytes = Protocol::json({{"v", 1}, {"id", id}, {"method", method}, {"data", data}}) + '\n';
  if (process.write(bytes) != bytes.size()) {
    auto p = pending.take(id);
    if (p.callback) p.callback({}, "Falha ao escrever IPC");
    emit error("Falha ao escrever IPC");
  }
}
void EngineProcess::read() {
  buffer += process.readAllStandardOutput();
  for (;;) {
    const auto at = buffer.indexOf('\n');
    if (at < 0) break;
    if (at > 3000000) {
      process.kill();
      fail("Resposta IPC excessiva");
      return;
    }
    auto line = buffer.left(at);
    buffer.remove(0, at + 1);
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(line, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
      process.kill();
      fail("JSON inválido do motor");
      return;
    }
    const auto v = doc.object();
    if (v["v"] != 1) {
      process.kill();
      fail("Versão IPC inválida");
      return;
    }
    if (v.contains("id")) {
      int id = v["id"].toInt(-1);
      if (!pending.contains(id)) continue;
      auto task = pending.take(id);
      if (!v["ok"].isBool()) {
        if (task.callback) task.callback({}, "Resposta IPC inválida");
        fail("Resposta IPC inválida");
        return;
      }
      const auto reason = v["ok"].toBool() ? QString() : v["error"].toString("Falha nativa");
      if (reason.isEmpty() && !Protocol::mediaResult(task.method, v["result"])) {
        if (task.callback) task.callback({}, "Resultado IPC inválido");
        process.kill();
        fail("Resultado IPC inválido");
        return;
      }
      if (task.callback) task.callback(v["result"], reason);
      if (!reason.isEmpty()) emit error(reason);
    } else {
      if (!Protocol::mediaEvent(v)) {
        process.kill();
        fail("Evento IPC inválido");
        return;
      }
      if (v["event"] == "ready") {
        ready_ = true;
        emit ready();
      } else
        emit eventReceived(v);
    }
  }
  if (buffer.size() > 3000000) {
    process.kill();
    fail("Linha IPC excessiva");
  }
}
