#pragma once

#include <QString>
#include <QtGlobal>

// Name of the per-user local socket used to forward a second launch (for example
// clicking the app icon while SnapLingo already lives in the tray) to the running
// instance. Kept in one header so main.cpp and App.cpp cannot drift apart.
namespace AppIpc
{
inline QString serverName()
{
    return QStringLiteral("SnapLingo.Instance.") + qEnvironmentVariable("USERNAME");
}
} // namespace AppIpc
