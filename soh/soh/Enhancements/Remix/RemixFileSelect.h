#pragma once

// Remix of Harkinian: title screen menu (New Game / Continue / Options) in the style of the remake.
//
// The menu is drawn with ImGui over the original file select scene. The original file select code (C) feeds it
// the game's own input every frame and asks it for the action the player chose, so it works with any controller
// or keyboard mapping and the original name entry, options and fade-out code keep doing their job.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REMIX_FS_NONE,
    REMIX_FS_NEW_GAME, // slot is an empty playthrough slot (0..2)
    REMIX_FS_LOAD,     // slot is the file to load, any valid slot
    REMIX_FS_OPTIONS,
} RemixFsAction;

typedef enum {
    REMIX_FS_SFX_NONE,
    REMIX_FS_SFX_CURSOR,
    REMIX_FS_SFX_DECIDE,
    REMIX_FS_SFX_CLOSE,
    REMIX_FS_SFX_ERROR,
} RemixFsSfx;

// Shown or hidden for this frame
void RemixFileSelect_SetVisible(int visible);
// Game input for this frame (edge triggered, with stick repeat already applied). Returns the sound to play.
int RemixFileSelect_Input(int up, int down, int confirm, int back, int erase);
// The action chosen since the last call, REMIX_FS_NONE if nothing
int RemixFileSelect_PollAction(int* slot);
// Back to the first screen, called when the file select starts
void RemixFileSelect_Reset(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <memory>
namespace Ship {
class GuiWindow;
}
// The ImGui window that draws the menu
std::shared_ptr<Ship::GuiWindow> RemixFileSelect_CreateWindow();
#endif
