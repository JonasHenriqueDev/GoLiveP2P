#pragma once
#include <QJsonObject>
#include <QString>
namespace Protocol {
constexpr int Port = 47621, MaxPeers = 5, GraceMs = 30000;
bool tailnetIp(const QString& ip);
bool uuid(const QString& value);
bool client(const QJsonObject& value);
bool server(const QJsonObject& value);
bool mediaRequest(const QString& method, const QJsonObject& value);
bool mediaEvent(const QJsonObject& value);
bool mediaResult(const QString& method, const QJsonValue& value);
QByteArray json(const QJsonObject& value);
}  // namespace Protocol
