#pragma once

// Remix of Harkinian: the logo screen of the boot sequence (Ship logo -> this logo -> Nintendo 64 logo).
// The boot code (CustomLogoTitle) only says how visible the logo is; the logo itself is drawn with ImGui.

#ifdef __cplusplus
extern "C" {
#endif

// 0 = nothing, 255 = fully shown. Called every frame of the boot logo states.
void RemixBootLogo_SetAlpha(int alpha);

#ifdef __cplusplus
}
#include <memory>
namespace Ship {
class GuiWindow;
}
std::shared_ptr<Ship::GuiWindow> RemixBootLogo_CreateWindow();
#endif
