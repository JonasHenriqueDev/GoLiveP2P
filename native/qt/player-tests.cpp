#include "player.hpp"
#include "video.hpp"
#include <QtTest>
#include <QMouseEvent>
class PlayerTests : public QObject {
  Q_OBJECT
 private slots:
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
