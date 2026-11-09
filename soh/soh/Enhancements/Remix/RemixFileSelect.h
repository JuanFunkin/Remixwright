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
    REMIX_FS_NEW_GAME, // slot is an empty playthrough slot (0..2), kind is the QUEST_ of the game to start
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
int RemixFileSelect_Input(int up, int down, int left, int right, int confirm, int back, int erase);
// The action chosen since the last call, REMIX_FS_NONE if nothing. kind is only set for REMIX_FS_NEW_GAME.
int RemixFileSelect_PollAction(int* slot, int* kind);
// 1 when an action was chosen and has not been collected yet
int RemixFileSelect_HasPending(void);
// The pending action without collecting it
int RemixFileSelect_PendingAction(void);
// 1 when the menu is on the title screen (Options opens the settings menu there), 0 on the file select
void RemixFileSelect_SetHost(int title);
// Black over everything, 0..255. The title screen fades out with it before leaving.
void RemixFileSelect_SetFade(int alpha);
// Opacity of the logo, 0..255. The title screen fades it in and out.
void RemixFileSelect_SetLogoAlpha(int alpha);
// Back to the first screen, called when the file select starts. A chosen action is kept.
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
