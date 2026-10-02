#include "thumbnails.hpp"
#include <QTimer>
#include <QGuiApplication>
#include <QScreen>
#include <memory>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif
void installSourceThumbnails(QDialog* dialog, QListWidget* list) {
  for (int i = 0; i < list->count(); ++i) {
    auto item = list->item(i);
    if (!item->data(Qt::UserRole).toString().startsWith("monitor:")) continue;
    for (auto screen : QGuiApplication::screens())
      if (item->text().contains(screen->name())) {
        auto shot =
            screen->grabWindow(0).scaled(180, 110, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!shot.isNull()) item->setIcon(QIcon(shot));
        break;
      }
  }
#ifdef Q_OS_WIN
  struct Entry {
    QListWidgetItem* item;
    HTHUMBNAIL thumbnail;
  };
  auto entries = std::make_shared<std::vector<Entry>>();
  for (int i = 0; i < list->count(); ++i) {
    auto item = list->item(i);
    const auto id = item->data(Qt::UserRole).toString();
    if (!id.startsWith("window:")) continue;
    const auto hwnd = reinterpret_cast<HWND>(quintptr(id.mid(7).toULongLong()));
    HTHUMBNAIL thumb = nullptr;
    if (SUCCEEDED(DwmRegisterThumbnail(reinterpret_cast<HWND>(dialog->winId()), hwnd, &thumb)))
      entries->push_back({item, thumb});
    else
      item->setToolTip(item->toolTip() + "\nPrévia não disponível neste aplicativo");
  }
  QObject::connect(dialog, &QObject::destroyed, [entries] {
    for (auto e : *entries) DwmUnregisterThumbnail(e.thumbnail);
  });
  auto update = [dialog, list, entries] {
    const QRect viewport = list->viewport()->rect();
    for (auto e : *entries) {
      auto cell = list->visualItemRect(e.item);
      DWM_THUMBNAIL_PROPERTIES props{};
      props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY |
                      DWM_TNP_SOURCECLIENTAREAONLY;
      props.opacity = 255;
      props.fSourceClientAreaOnly = TRUE;
      props.fVisible =
          dialog->isVisible() && viewport.contains(QRect(cell.x(), cell.y(), cell.width(), 114));
      auto point = list->viewport()->mapTo(
          dialog, QPoint(cell.x() + (cell.width() - 180) / 2, cell.y() + 4));
      QRect target(point, QSize(180, 110));
      SIZE source{};
      if (SUCCEEDED(DwmQueryThumbnailSourceSize(e.thumbnail, &source)) && source.cx > 0 &&
          source.cy > 0) {
        auto fit = QSize(source.cx, source.cy).scaled(target.size(), Qt::KeepAspectRatio);
        target = QRect(target.center() - QPoint(fit.width() / 2, fit.height() / 2), fit);
      }
      props.rcDestination = {target.left(), target.top(), target.right() + 1, target.bottom() + 1};
      DwmUpdateThumbnailProperties(e.thumbnail, &props);
    }
  };
  auto timer = new QTimer(dialog);
  timer->setInterval(100);
  QObject::connect(timer, &QTimer::timeout, dialog, update);
  timer->start();
  update();
#endif
}
