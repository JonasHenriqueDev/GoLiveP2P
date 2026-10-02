#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QFile>
#include <QCryptographicHash>
#include <functional>
class Updater : public QObject {
  Q_OBJECT
 public:
  explicit Updater(QObject* parent = nullptr);
  void check();
  void install();
  void sessionActive(bool active);
 signals:
  void status(QString);
  void ready();

 private:
  QNetworkAccessManager network;
  QString downloaded;
  bool busy = false, checking = false;
  void download(const QJsonObject& asset);
};
