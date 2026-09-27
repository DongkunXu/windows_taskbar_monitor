#pragma once

#include <windows.h>

namespace tbm {

// Private messages posted to the App window.
inline constexpr UINT kMsgSample = WM_APP + 1;           // A new sample is ready.
inline constexpr UINT kMsgOverlayLost = WM_APP + 2;      // The overlay window was destroyed.
inline constexpr UINT kMsgOpenTaskManager = WM_APP + 3;  // Overlay left-click.
inline constexpr UINT kMsgShowMenu = WM_APP + 4;  // Overlay right-click; lParam = screen point.

}  // namespace tbm
