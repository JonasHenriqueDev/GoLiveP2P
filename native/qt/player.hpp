#pragma once
#include <QWidget>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMap>
class VideoView;
class PlayerSurface : public QWidget {
  Q_OBJECT
 public:
  enum Mode { Docked, PictureInPicture, Fullscreen };
  explicit PlayerSurface(QWidget* parent = nullptr);
  ~PlayerSurface() override;
  VideoView* view() const { return video; }
  QLabel* titleLabel() const { return title; }
  QPushButton* action(const QString& name) const { return actions.value(name); }
  void attach(QVBoxLayout* layout);
  void present(Mode mode);
  void toggleFullscreen();
  void togglePip();
  Mode presentation() const { return mode; }
  bool controlsVisible() const { return overlay->isVisible(); }
  void activity();
  void setIdleDelay(int ms) { idle.setInterval(ms); }
 signals:
  void settingsRequested();
  void presentationChanged();

 protected:
  bool eventFilter(QObject*, QEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void closeEvent(QCloseEvent*) override;

 private:
  VideoView* video;
  QWidget *overlay, *bar;
  QLabel* title;
  QTimer idle;
  QVBoxLayout* home = nullptr;
  QMap<QString, QPushButton*> actions;
  Mode mode = Docked, beforeFullscreen = Docked;
};
