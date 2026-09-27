#pragma once

// "Start with Windows" through HKCU\...\Run: per user, no elevation. Also honours the on/off
// switch in Settings > Apps > Startup (the StartupApproved key).
namespace tbm::autostart {

bool IsEnabled();
// Enabling points the entry at the running executable. Disabling removes every value this
// feature wrote, so nothing is left behind in the registry.
bool SetEnabled(bool enabled);

}  // namespace tbm::autostart
