#pragma once
#include "engine.hpp"
#include "room.hpp"
#include <QMainWindow>
#include <QImage>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QNetworkAccessManager>
#include <QJsonArray>
#include <QSplitter>
class Updater;
class VideoView : public QWidget {
  Q_OBJECT
 public:
  explicit VideoView(QWidget* parent = nullptr);
  void frame(const QByteArray& jpeg);
  void clear();
  quint64 frames = 0;
 signals:
  void doubleClicked();

 protected:
  void paintEvent(QPaintEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;

 private:
  QImage image;
};
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(const QString& runtime, QWidget* parent = nullptr);
  ~MainWindow() override;
  void automate(const QStringList& args);

 private:
  EngineProcess engine;
  RoomServer server;
  RoomClient client;
  QNetworkAccessManager network;
  Updater* updater = nullptr;
  QLineEdit *name, *host;
  QPushButton *create, *join, *discover, *share, *stop, *leave;
  QLabel *connection, *streamInfo, *audioInfo, *statsInfo;
  QComboBox *quality, *method;
  QDoubleSpinBox* bitrate;
  QCheckBox* audio;
  QListWidget* people;
  QPlainTextEdit* logs;
  VideoView* video;
  QWidget *lobby, *room, *side, *controls;
  QSplitter* splitter;
  QString tailIp, streamer, encoder, selectedSource, testReport, testSource;
  quint16 roomPort=47621;
  QJsonObject settings, lastStats;
  QMap<QString, QJsonObject> peers;
  QSet<QString> mediaPeers, restarting;
  QMap<QString, QJsonArray> pendingIce;
  QJsonArray statsSamples;
  bool transmitting = false, starting = false, fullscreen = false, closed = false,
       statsBusy = false;
  quint64 previewFrames = 0;
  double lastAudioRms = 0;
  bool audioActive = false;
  void log(const QString&);
  void refreshTailnet();
  void createRoom();
  void joinRoom(const QString&);
  void leaveRoom();
  void message(const QJsonObject&);
  void media(const QJsonObject&);
  void refreshPeople();
  void selectSource();
  void startSource(const QString&, const QJsonArray& allowed = {});
  void offer(const QString&);
  void removePeer(const QString&);
  void stopStream();
  void discoverRooms();
  void ping();
  void collect();
  void saveLog();
  void sendLog();
  void toggleFullscreen();
  void report();
  QJsonObject captureSettings(const QString&, const QJsonArray&);
};
