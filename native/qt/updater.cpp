#include "updater.hpp"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QVersionNumber>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QProcess>
#include <QTimer>
Updater::Updater(QObject* parent) : QObject(parent), network(this) {}
void Updater::sessionActive(bool active) {
  busy = active;
  if (!busy && !downloaded.isEmpty())
    QTimer::singleShot(1500, this, [this] {
      if (!busy) install();
    });
}
void Updater::check() {
  if (checking) return;
  checking = true;
  QNetworkRequest request(
      QUrl("https://api.github.com/repos/JonasHenriqueDev/GoLiveP2P/releases?per_page=20"));
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", "GoLive-Qt/" GOLIVE_VERSION);
  request.setTransferTimeout(15000);
  auto reply = network.get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    checking = false;
    if (reply->error() != QNetworkReply::NoError) {
      emit status("Atualização indisponível: " + reply->errorString());
      reply->deleteLater();
      return;
    }
    auto body = reply->read(1048577);
    reply->deleteLater();
    if (body.size() > 1048576) return;
    const auto releases = QJsonDocument::fromJson(body).array();
    for (auto entry : releases) {
      auto release = entry.toObject();
      const auto tag = release["tag_name"].toString();
      if (release["draft"].toBool() || !tag.contains("-qt.")) continue;
      const auto candidate = QVersionNumber::fromString(tag.mid(1).replace("-qt.", "."));
      const auto current = QVersionNumber::fromString(QString(GOLIVE_VERSION).replace("-qt.", "."));
      if (candidate <= current) continue;
      for (auto a : release["assets"].toArray()) {
        auto asset = a.toObject();
        if (asset["name"] == "GoLive-P2P-Setup-" + tag.mid(1) + ".exe") {
          download(asset);
          return;
        }
      }
    }
    emit status("Interface Qt está atualizada");
  });
}
void Updater::download(const QJsonObject& asset) {
  const QUrl url(asset["browser_download_url"].toString());
  const auto digest = asset["digest"].toString();
  const auto size = qint64(asset["size"].toDouble());
  if (url.scheme() != "https" || url.host() != "github.com" ||
      !url.path().startsWith("/JonasHenriqueDev/GoLiveP2P/releases/download/") ||
      !digest.startsWith("sha256:") || digest.size() != 71 || size < 1000000 || size > 1500000000) {
    emit status("Atualização recusada: executável sem origem/tamanho/checksum esperado");
    return;
  }
  const auto folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  QDir().mkpath(folder);
  auto file = new QFile(folder + "/update-installer.exe.part", this);
  if (!file->open(QIODevice::WriteOnly)) {
    file->deleteLater();
    return;
  }
  auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
  QNetworkRequest req(url);
  req.setTransferTimeout(30000);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  auto reply = network.get(req);
  connect(reply, &QNetworkReply::readyRead, this, [reply, file, hash, size] {
    auto data = reply->readAll();
    if (file->size() + data.size() > size || file->write(data) != data.size()) {
      reply->abort();
      return;
    }
    hash->addData(data);
  });
  connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
    if (total > 0)
      emit status("Baixando atualização: " + QString::number(100 * done / total) + "%");
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, file, hash, digest, size] {
    const bool ok = reply->error() == QNetworkReply::NoError && file->size() == size &&
                    QString::fromLatin1(hash->result().toHex()) == digest.mid(7);
    auto path = file->fileName();
    file->close();
    file->deleteLater();
    reply->deleteLater();
    if (!ok) {
      QFile::remove(path);
      emit status("Atualização rejeitada: download/checksum falhou");
      return;
    }
    downloaded = path.left(path.size() - 5);
    QFile::remove(downloaded);
    if (!QFile::rename(path, downloaded)) {
      downloaded.clear();
      emit status("Não foi possível preparar atualização");
      return;
    }
    emit status("Atualização conferida; instalação aguarda saída da sala");
    emit ready();
    if (!busy)
      QTimer::singleShot(1500, this, [this] {
        if (!busy) install();
      });
  });
}
void Updater::install() {
  if (busy || downloaded.isEmpty()) return;
#ifdef Q_OS_WIN
  if (QProcess::startDetached(downloaded, {"/S"})) {
    downloaded.clear();
    QApplication::quit();
  } else
    emit status("Não foi possível iniciar instalador");
#else
  emit status("Atualização Linux manual nesta etapa");
#endif
}
