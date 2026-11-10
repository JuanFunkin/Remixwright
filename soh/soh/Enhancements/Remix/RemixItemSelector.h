#pragma once

// Remix of Harkinian: the item selector, in the style of the remake.
//  - L opens a wheel with the items Link has and freezes the game while it is open.
//  - Choose an item with the stick and press D-Right or D-Down to put it on that D-pad direction.
//  - D-Left is always the ocarina and D-Up the mask Link carries, so the D-pad is the quick use of items.

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

// Called every game frame from Play_Update. Returns 1 while the selector is open: the game must not advance then.
int RemixItemSelector_Update(struct PlayState* play);

// 1 while the selector is open
int RemixItemSelector_IsOpen(void);

#ifdef __cplusplus
}

// Drawn from the same GUI window as the ocarina HUD
void RemixItemSelector_Draw();
#endif
