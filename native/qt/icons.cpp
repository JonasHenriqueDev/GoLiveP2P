#include "icons.hpp"
#include <QPainter>
#include <QPainterPath>
QIcon actionIcon(const QString& name) {
  QPixmap pix(48, 48);
  pix.fill(Qt::transparent);
  QPainter p(&pix);
  p.setRenderHint(QPainter::Antialiasing);
  p.scale(2, 2);
  p.setPen(QPen(QColor("#e3e5e8"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  if (name == "hangup") {
    QPainterPath phone;
    phone.moveTo(3, 15);
    phone.cubicTo(3, 6, 21, 6, 21, 15);
    phone.lineTo(16, 15);
    phone.lineTo(16, 12);
    phone.cubicTo(14, 11, 10, 11, 8, 12);
    phone.lineTo(8, 15);
    phone.closeSubpath();
    p.fillPath(phone, QColor("#ffffff"));
    p.drawPath(phone);
  } else if (name == "fullscreen") {
    p.drawLines(QVector<QLineF>{{4, 9, 4, 4},
                                {4, 4, 9, 4},
                                {15, 4, 20, 4},
                                {20, 4, 20, 9},
                                {20, 15, 20, 20},
                                {20, 20, 15, 20},
                                {9, 20, 4, 20},
                                {4, 20, 4, 15}});
  } else if (name == "pip") {
    p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
    p.fillRect(QRectF(12, 11, 7, 6), QColor("#e3e5e8"));
  } else if (name == "share") {
    p.drawRoundedRect(QRectF(3, 4, 18, 13), 2, 2);
    p.drawLine(8, 21, 16, 21);
    p.drawLine(12, 17, 12, 21);
    p.drawLine(12, 13, 12, 7);
    p.drawLine(8, 10, 12, 6);
    p.drawLine(12, 6, 16, 10);
  } else if (name == "stop") {
    p.fillRect(QRectF(6, 6, 12, 12), QColor("#ffffff"));
  } else if (name == "settings") {
    for (int i = 0; i < 8; ++i) {
      p.save();
      p.translate(12, 12);
      p.rotate(i * 45);
      p.drawLine(0, -7, 0, -10);
      p.restore();
    }
    p.drawEllipse(QPointF(12, 12), 6, 6);
    p.drawEllipse(QPointF(12, 12), 2, 2);
  } else if (name == "bug") {
    p.drawEllipse(QRectF(8, 4, 8, 5));
    p.drawRoundedRect(QRectF(7, 8, 10, 12), 5, 5);
    p.drawLine(12, 9, 12, 19);
    for (int y : {9, 13, 17}) {
      p.drawLine(3, y - 1, 7, y);
      p.drawLine(17, y, 21, y - 1);
    }
    p.drawLine(9, 4, 7, 2);
    p.drawLine(15, 4, 17, 2);
  } else if (name == "dock") {
    p.drawRect(QRectF(3, 4, 18, 16));
    p.drawLine(8, 4, 8, 20);
    p.drawLine(12, 12, 18, 12);
    p.drawLine(12, 12, 15, 9);
    p.drawLine(12, 12, 15, 15);
  }
  return QIcon(pix);
}
