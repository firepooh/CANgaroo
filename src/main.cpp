/*
  Copyright (c) 2015, 2016 Hubert Denkmair <hubert@denkmair.de>
  Copyright (c) 2026 Schildkroet

  This file is part of cangaroo.

  cangaroo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  cangaroo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cangaroo.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "mainwindow.h"

#include <algorithm>

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QStyleFactory>
#include <QTranslator>

#ifdef REMOTE_CONTROL
#include <memory>

#include "core/Backend.h"
#include "remote/RemoteServer.h"
#endif


#if defined(__linux__)
// Returns true if a Qt plugin whose file name contains `needle` exists in the
// given plugin sub-directory (e.g. "platformthemes", "wayland-decoration-client").
static bool hasQtPlugin(const QString &subdir, const QString &needle)
{
    QDir dir(QLibraryInfo::path(QLibraryInfo::PluginsPath) + QLatin1Char('/') + subdir);
    const auto entries = dir.entryList(QDir::Files);
    return std::any_of(entries.begin(), entries.end(),
        [&](const QString &f) { return f.contains(needle, Qt::CaseInsensitive); });
}

// On a GNOME/Wayland session, default to a native Qt platform theme and (if
// available) native Adwaita window decorations so the app looks native out of
// the box. Only pick values whose plugin is actually installed, and only when
// the variable is unset, so user/packager overrides win and we never request a
// missing plugin (which would log a "decoration not found" warning).
// Must run before the QApplication is constructed (env is read during init).
static void setupNativeEnvironmentDefaults()
{
    const QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE");
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");

    const bool isWayland = sessionType.compare("wayland", Qt::CaseInsensitive) == 0;
    const bool isGnome = desktop.contains("GNOME", Qt::CaseInsensitive);

    if (!isWayland || !isGnome)
        return;

    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME"))
    {
        if (hasQtPlugin("platformthemes", "gnomeplatform"))
            qputenv("QT_QPA_PLATFORMTHEME", "gnome");
        else if (hasQtPlugin("platformthemes", "gtk3"))
            qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    }

    // Only request the Adwaita Qt decoration if its plugin is present; otherwise
    // leave it to Qt's default decoration / libdecor.
    if (!qEnvironmentVariableIsSet("QT_WAYLAND_DECORATION")
        && hasQtPlugin("wayland-decoration-client", "adwaita"))
    {
        qputenv("QT_WAYLAND_DECORATION", "adwaita");
    }
}
#endif


int main(int argc, char *argv[])
{
#if defined(__linux__)
    setupNativeEnvironmentDefaults();
#endif

    QApplication a(argc, argv);

    QCoreApplication::setOrganizationName("CANgaroo/Schildkroet");
    QCoreApplication::setOrganizationDomain("CANgaroo/Schildkroet");
    QCoreApplication::setApplicationName("CANgaroo");

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption workspaceOption("workspace", "Open the workspace <file> at startup.", "file");
    parser.addOption(workspaceOption);
#ifdef REMOTE_CONTROL
    const QCommandLineOption remoteOption("remote", "Enable the remote control endpoint on 127.0.0.1.");
    const QCommandLineOption remotePortOption("remote-port", "Remote control TCP <port> (default 52790, 0 = any).", "port", "52790");
    const QCommandLineOption remoteTokenOption("remote-token", "Remote control <token> (default: random, see the discovery file).", "token");
    const QCommandLineOption remoteAllowTxOption("remote-allow-tx", "Allow remote clients to transmit frames.");
    const QCommandLineOption remoteBufferOption("remote-buffer", "Frames kept for remote clients (default 100000).", "frames", "100000");
    parser.addOptions({ remoteOption, remotePortOption, remoteTokenOption, remoteAllowTxOption, remoteBufferOption });
#endif
    parser.process(a);

    MainWindow w;
    if(w.isMaximizedWindow())
    {
        w.showMaximized();
    }
    else
    {
        w.show();
    }

    if (parser.isSet(workspaceOption))
    {
        w.loadWorkspaceFromFile(parser.value(workspaceOption));
    }

#ifdef REMOTE_CONTROL
    // Declared after w, so it is destroyed before the main window and its drivers.
    std::unique_ptr<RemoteServer> remote;
    if (parser.isSet(remoteOption) || parser.isSet(remotePortOption))
    {
        RemoteServer::Options options;
        options.port = static_cast<quint16>(parser.value(remotePortOption).toUInt());
        options.token = parser.value(remoteTokenOption);
        if (options.token.isEmpty()) { options.token = qEnvironmentVariable("CANGAROO_REMOTE_TOKEN"); }
        options.allowTx = parser.isSet(remoteAllowTxOption);
        options.frameLogCapacity = std::max(1000, parser.value(remoteBufferOption).toInt());

        remote = std::make_unique<RemoteServer>(Backend::instance(), options);
        remote->start();
    }
#endif

    return a.exec();
}
