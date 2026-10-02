#include "player.hpp"
#include "video.hpp"
#include "icons.hpp"
#include <QApplication>
#include <QHBoxLayout>
#include <QEvent>
#include <QCloseEvent>
#include <QShortcut>
#include <QScreen>
#include <QGuiApplication>
#include <QMouseEvent>
PlayerSurface::PlayerSurface(QWidget* parent) : QWidget(parent) {
  setObjectName("player");
  setMinimumSize(336, 190);
  setWindowTitle("GoLive P2P · vídeo");
  video = new VideoView(this);
  video->setMinimumSize(0, 0);
  overlay = new QWidget(this);
  overlay->setObjectName("playerOverlay");
  overlay->setStyleSheet("QWidget#playerOverlay{background:transparent}");
  auto layout = new QVBoxLayout(overlay);
  layout->setContentsMargins(16, 14, 16, 14);
  title = new QLabel("Aguardando transmissão");
  title->setObjectName("playerTitle");
  title->setStyleSheet(
      "background:rgba(17,18,20,190);border-radius:6px;padding:7px 10px;color:#dbdee1");
  auto heading = new QHBoxLayout;
  heading->addWidget(title);
  heading->addStretch();
  layout->addLayout(heading);
  layout->addStretch();
  bar = new QWidget;
  bar->setObjectName("playerBar");
  bar->setStyleSheet(
      "QWidget#playerBar{background:rgba(32,34,38,235);border:1px solid "
      "#36373d;border-radius:16px} "
      "QPushButton{background:#36373d;border:0;border-radius:18px;padding:0} "
      "QPushButton:hover{background:#4a4d55} QPushButton:checked{background:#5865f2} "
      "QPushButton#hangup{background:#da373c} QPushButton#hangup:hover{background:#f23f43}");
  auto buttons = new QHBoxLayout(bar);
  buttons->setContentsMargins(8, 7, 8, 7);
  buttons->setSpacing(8);
  for (auto name : {"share", "settings", "pip", "fullscreen", "dock", "stop", "hangup"}) {
    auto button = new QPushButton;
    button->setObjectName(name);
    button->setIcon(actionIcon(name));
    button->setIconSize(QSize(21, 21));
    button->setFixedSize(36, 36);
    button->setCursor(Qt::PointingHandCursor);
    actions[name] = button;
    buttons->addWidget(button);
  }
  const QMap<QString, QString> labels{{"share", "Compartilhar / trocar janela ou monitor"},
                                      {"settings", "Qualidade e opções da transmissão"},
                                      {"pip", "Picture in picture"},
                                      {"fullscreen", "Tela cheia (duplo clique)"},
                                      {"dock", "Voltar ao aplicativo"},
                                      {"stop", "Parar compartilhamento"},
                                      {"hangup", "Sair da sala"}};
  for (auto it = labels.begin(); it != labels.end(); ++it) {
    actions[it.key()]->setToolTip(it.value());
    actions[it.key()]->setAccessibleName(it.value());
  }
  actions["dock"]->hide();
  actions["stop"]->hide();
  auto centered = new QHBoxLayout;
  centered->addStretch();
  centered->addWidget(bar);
  centered->addStretch();
  layout->addLayout(centered);
  idle.setSingleShot(true);
  idle.setInterval(2500);
  connect(&idle, &QTimer::timeout, this, [this] {
    if (QApplication::activePopupWidget() || QApplication::activeModalWidget()) {
      idle.start();
      return;
    }
    overlay->hide();
    video->setCursor(Qt::BlankCursor);
    setCursor(Qt::BlankCursor);
  });
  connect(actions["fullscreen"], &QPushButton::clicked, this, &PlayerSurface::toggleFullscreen);
  connect(actions["pip"], &QPushButton::clicked, this, &PlayerSurface::togglePip);
  connect(actions["dock"], &QPushButton::clicked, this, [this] { present(Docked); });
  connect(actions["settings"], &QPushButton::clicked, this, &PlayerSurface::settingsRequested);
  connect(video, &VideoView::doubleClicked, this, &PlayerSurface::toggleFullscreen);
  auto escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape, &QShortcut::activated, this, [this] {
    if (mode != Docked) present(Docked);
  });
  setMouseTracking(true);
  video->setMouseTracking(true);
  overlay->setMouseTracking(true);
  for (auto child : findChildren<QWidget*>()) child->setMouseTracking(true);
  qApp->installEventFilter(this);
}
PlayerSurface::~PlayerSurface() { qApp->removeEventFilter(this); }
void PlayerSurface::attach(QVBoxLayout* layout) {
  home = layout;
  home->addWidget(this, 1);
}
void PlayerSurface::activity() {
  overlay->show();
  overlay->raise();
  video->unsetCursor();
  unsetCursor();
  idle.start();
}
bool PlayerSurface::eventFilter(QObject* object, QEvent* event) {
  const auto widget = qobject_cast<QWidget*>(object);
  if ((widget == video || widget == overlay || widget == title || widget == this) &&
      event->type() == QEvent::MouseButtonDblClick &&
      static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
    toggleFullscreen();
    return true;
  }
  if ((widget == this || (widget && isAncestorOf(widget))) &&
      (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress ||
       event->type() == QEvent::KeyPress))
    activity();
  return QWidget::eventFilter(object, event);
}
void PlayerSurface::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  video->setGeometry(rect());
  overlay->setGeometry(rect());
}
void PlayerSurface::present(Mode next) {
  if (!home || next == mode) {
    activity();
    return;
  }
  hide();
  if (mode == Docked) home->removeWidget(this);
  mode = next;
  if (mode == Docked) {
    setParent(home->parentWidget(), Qt::Widget);
    home->addWidget(this, 1);
    show();
  } else {
    const auto flags = mode == Fullscreen ? Qt::Window | Qt::FramelessWindowHint
                                          : Qt::Tool | Qt::WindowStaysOnTopHint |
                                                Qt::WindowTitleHint | Qt::WindowCloseButtonHint;
    setParent(nullptr, flags);
    if (mode == Fullscreen)
      showFullScreen();
    else {
      showNormal();
      resize(480, 270);
      const auto available = screen()->availableGeometry();
      move(available.right() - width() - 24, available.bottom() - height() - 44);
      show();
    }
  }
  actions["dock"]->setVisible(mode != Docked);
  activity();
  emit presentationChanged();
}
void PlayerSurface::toggleFullscreen() {
  if (mode == Fullscreen)
    present(beforeFullscreen);
  else {
    beforeFullscreen = mode;
    present(Fullscreen);
  }
}
void PlayerSurface::togglePip() { present(mode == PictureInPicture ? Docked : PictureInPicture); }
void PlayerSurface::closeEvent(QCloseEvent* event) {
  if (mode != Docked) {
    event->ignore();
    present(Docked);
  } else
    QWidget::closeEvent(event);
}
