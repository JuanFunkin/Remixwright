#pragma once

// Remix of Harkinian: the HUD shown while the ocarina is out.
//  - Top: what the microphone hears, as a note level moving over the five ocarina lines (when the mic option is on).
//  - Left: the songs Link knows and the buttons that play each one.
//  - Bottom: the button for each of the five notes, lit while it is played.

#ifdef __cplusplus
#include <memory>
namespace Ship {
class GuiWindow;
}
std::shared_ptr<Ship::GuiWindow> RemixOcarinaHud_CreateWindow();
#endif
