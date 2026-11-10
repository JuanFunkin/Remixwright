#pragma once

// Remix of Harkinian: play the ocarina by humming, whistling or playing the tune into the microphone.
//
// The microphone is only opened while the ocarina is out AND the player turned the option on (that toggle is the
// permission). The pitch is detected in real time, folded to the ocarina's range and turned into one of its five
// notes, which are then pressed for the game as if the matching button was held.

#ifdef __cplusplus
extern "C" {
#endif

// Opens or closes the microphone and updates the detection. Call once per frame with 1 while the ocarina is out.
void RemixOcarinaMic_Update(int ocarinaOut);

// 1 when the option is on and the microphone is open and listening
int RemixOcarinaMic_IsListening(void);

// 0 = off, 1 = the microphone could not be opened, 2 = open but nothing is heard, 3 = sound is coming in
int RemixOcarinaMic_GetStatus(void);

// The note being sung: 0 = D4 (A), 1 = F4 (C-Down), 2 = A4 (C-Right), 3 = B4 (C-Left), 4 = D5 (C-Up). -1 = none
int RemixOcarinaMic_GetNote(void);

// Pitch heard right now as a position over the five ocarina lines: 0.0 (D4) to 4.0 (D5), fractions in between.
// Returns a negative value when no pitch is heard.
float RemixOcarinaMic_GetPitchLevel(void);

// Loudness of the microphone, 0.0 to 1.0
float RemixOcarinaMic_GetVolume(void);

#ifdef __cplusplus
}
#endif
