#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QMap>
#include <functional>
class EngineProcess : public QObject {
  Q_OBJECT
 public:
  using Callback = std::function<void(QJsonValue, QString)>;
  explicit EngineProcess(const QString& runtime, QObject* parent = nullptr);
  ~EngineProcess() override;
  void start();
  void shutdown();
  void request(const QString& method, const QJsonObject& data = {}, Callback callback = {});
  bool isReady() const { return ready_; }
 signals:
  void ready();
  void eventReceived(QJsonObject);
  void error(QString);

 private:
  struct Pending {
    Callback callback;
    qint64 deadline;
  };
  QProcess process;
  QString runtime_;
  QByteArray buffer;
  QMap<int, Pending> pending;
  int nextId = 0;
  bool ready_ = false, closing = false;
  void fail(const QString& reason);
  void read();
};
