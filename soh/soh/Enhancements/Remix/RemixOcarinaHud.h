#pragma once

// Remix of Harkinian: the HUD shown while the ocarina is out, laid out like the remake.
//  - Bottom: one thin line of hints (songbook, the five note buttons, cancel).
//  - ZL opens the songbook: a ring of the songs Link knows and, for the selected one, a parchment with the notes.
//  - When the microphone option is on, a small staff in a corner shows the note level that is heard.
// It is drawn behind everything else on the game's own view, so it is part of the screen and never a window.

#ifdef __cplusplus
extern "C" {
#endif

// 1 while the songbook is open. The ocarina ignores the note buttons then, since they move the selection.
int RemixOcarinaHud_SongbookOpen(void);

#ifdef __cplusplus
}

#include <memory>
namespace Ship {
class GuiWindow;
}
std::shared_ptr<Ship::GuiWindow> RemixOcarinaHud_CreateWindow();
#endif
