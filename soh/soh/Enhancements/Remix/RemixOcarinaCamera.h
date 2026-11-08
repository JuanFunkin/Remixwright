#pragma once

// Remix of Harkinian: playing the ocarina without black bars and with a free camera.
//
// Like the remake, the camera is only free when playing the ocarina on your own. When the game is waiting for a
// song at a spot (Triforce mark on the floor, a frog, and so on) the original camera is kept.

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Player;

// Called when Link takes out the ocarina. Returns 1 when the free camera is used, in which case the caller must not
// start the original turn around camera (and its black bars). Returns 0 to keep the original behavior.
int RemixOcarinaCamera_Begin(struct PlayState* play, struct Player* player);

#ifdef __cplusplus
}
#endif
