// A deliberately slow media process exercises early signaling across real IPC.
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QThread::msleep(350);
  std::cout << "{\"v\":1,\"event\":\"ready\",\"protocol\":1}\n" << std::flush;
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
    const QJsonObject response{
        {"v", 1}, {"id", request["id"]}, {"ok", true}, {"result", QJsonValue::Null}};
    std::cout << QJsonDocument(response).toJson(QJsonDocument::Compact).constData() << '\n'
              << std::flush;
  }
  return 0;
}
