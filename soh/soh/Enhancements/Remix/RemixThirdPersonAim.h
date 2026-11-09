#pragma once

// Remix of Harkinian: aiming the bow, slingshot, hookshot and longshot from behind Link (like the remake), instead of
// the first person view. Link stays visible, can walk while aiming and the camera follows where he aims.

#ifdef __cplusplus
extern "C" {
#endif

struct Player;

// Called where the game picks the camera mode for aiming. When the third person aim applies, changes *camMode to the
// normal (free look) camera and returns 1. Returns 0 and leaves *camMode alone otherwise.
int RemixThirdPersonAim_OverrideCamMode(struct Player* player, int* camMode);

// 1 while the third person aim camera is in use. Used by the camera to offset the view over Link's shoulder.
int RemixThirdPersonAim_IsActive(void);

#ifdef __cplusplus
}
#endif
