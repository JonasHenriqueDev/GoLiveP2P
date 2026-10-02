#include "video.hpp"
#include <QPainter>
#include <QMouseEvent>
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
