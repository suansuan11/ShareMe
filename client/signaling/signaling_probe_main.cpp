#include "qt_signaling_client.hpp"
#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTimer>
#include <iostream>
int main(int argc, char** argv) { QCoreApplication app(argc, argv); QCommandLineParser parser; parser.addHelpOption(); QCommandLineOption server(QStringList{"s","server"}, "WebSocket URL", "url"); QCommandLineOption role(QStringList{"r","role"}, "host or viewer", "role"); QCommandLineOption room(QStringList{"room"}, "Room ID for viewer", "room"); parser.addOption(server); parser.addOption(role); parser.addOption(room); parser.process(app); if (!parser.isSet(server) || !parser.isSet(role)) return 2; QtSignalingClient client; QObject::connect(&client, &QtSignalingClient::roomReady, &app, [&](const QString& id) { std::cout << id.toStdString() << std::endl; app.exit(0); }); QObject::connect(&client, &QtSignalingClient::failed, &app, [&](const QString&) { app.exit(1); }); client.connectTo(QUrl(parser.value(server))); QObject::connect(&client, &QtSignalingClient::statusChanged, &app, [&](const QString& state) { if (state == "connected") { if (parser.value(role) == "host") client.createRoom(); else client.joinRoom(parser.value(room)); } }); QTimer::singleShot(5000, &app, [&] { app.exit(1); }); return app.exec(); }
