#include "player.hpp"
#include "video.hpp"
#include <QtTest>
#include <QMouseEvent>
#include <QBuffer>
class PlayerTests : public QObject {
  Q_OBJECT
 private slots:
  void jpegPresentation() {
    VideoView view;
    view.resize(640, 360);
    view.show();
    QImage source(64, 64, QImage::Format_RGB32);
    source.fill(QColor("#23a55a"));
    QByteArray bytes;
    QBuffer output(&bytes);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(source.save(&output, "JPEG"));
    view.frame(bytes);
    QCOMPARE(view.frames, quint64(1));
    const auto shown = view.grab().toImage().pixelColor(view.rect().center());
    QVERIFY(qAbs(shown.green() - 165) < 6);
    QVERIFY(qAbs(shown.red() - 35) < 6);
    view.frame("invalid JPEG");
    QCOMPARE(view.frames, quint64(1));
  }
  void presentationAndIdleControls() {
    QWidget host;
    auto layout = new QVBoxLayout(&host);
    auto player = new PlayerSurface;
    player->attach(layout);
    host.resize(800, 450);
    host.show();
    player->setIdleDelay(180);
    player->present(PlayerSurface::PictureInPicture);
    QCOMPARE(player->presentation(), PlayerSurface::PictureInPicture);
    QVERIFY(player->isWindow());
    QVERIFY(player->windowFlags() & Qt::WindowStaysOnTopHint);
    player->toggleFullscreen();
    QCOMPARE(player->presentation(), PlayerSurface::Fullscreen);
    QVERIFY(player->isFullScreen());
    QTRY_VERIFY_WITH_TIMEOUT(!player->controlsVisible(), 1500);
    QMouseEvent move(QEvent::MouseMove, QPointF(40, 45), QPointF(40, 45), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(player->view(), &move);
    QTRY_VERIFY(player->controlsVisible());
    player->toggleFullscreen();
    QCOMPARE(player->presentation(), PlayerSurface::PictureInPicture);
    player->close();
    QCOMPARE(player->presentation(), PlayerSurface::Docked);
    QCOMPARE(player->parentWidget(), &host);
    QVERIFY(layout->indexOf(player) >= 0);
    QTest::mouseDClick(player->view(), Qt::LeftButton);
    QCOMPARE(player->presentation(), PlayerSurface::Fullscreen);
    player->present(PlayerSurface::Docked);
  }
};
QTEST_MAIN(PlayerTests)
#include "player-tests.moc"
