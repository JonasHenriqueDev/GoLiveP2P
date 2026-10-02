#pragma once
#include <QWidget>
#include <QImage>
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
