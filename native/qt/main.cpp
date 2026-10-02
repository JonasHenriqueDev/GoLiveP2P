#include "window.hpp"
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QLockFile>
#include <QIcon>
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setApplicationName("GoLive P2P");
  app.setOrganizationName("GoLiveP2P");
  app.setApplicationVersion(GOLIVE_VERSION);
  app.setWindowIcon(QIcon(":/golive-icon.png"));
  app.setDesktopFileName("golive-p2p");
  auto args = app.arguments();
  QString runtime = QCoreApplication::applicationDirPath() + "/native-media";
  for (auto a : args)
    if (a.startsWith("--runtime=")) runtime = QDir(a.mid(10)).absolutePath();
  MainWindow window(runtime);
  window.show();
  window.automate(args);
  return app.exec();
}
