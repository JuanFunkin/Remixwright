#include "RemixOcarinaMic.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef __APPLE__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"

static SDL_AudioDeviceID sDevice = 0;
static int sSampleRate = 44100;
static std::vector<float> sSamples; // most recent samples, newest last

static int sNote = -1;          // note the game sees (debounced)
static int sCandidate = -1;     // note heard right now
static int sCandidateFrames = 0;
static int sSilentFrames = 0;
static float sLevel = -1.0f;
static float sVolume = 0.0f;

// MIDI numbers of the ocarina notes: D4, F4, A4, B4, D5
static const float kNoteMidi[5] = { 62.0f, 65.0f, 69.0f, 71.0f, 74.0f };

static const int kWindow = 1536;

static bool Enabled() {
    return CVarGetInteger(CVAR_SETTING("RemixOcarinaMic.Enabled"), 0) != 0;
}

static void CloseDevice() {
    if (sDevice != 0) {
        SDL_CloseAudioDevice(sDevice);
        sDevice = 0;
    }
    sSamples.clear();
    sNote = sCandidate = -1;
    sCandidateFrames = sSilentFrames = 0;
    sLevel = -1.0f;
    sVolume = 0.0f;
}

static void OpenDevice() {
    if (sDevice != 0) {
        return;
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        return;
    }
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    SDL_zero(want);
    SDL_zero(have);
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = nullptr; // queue mode, read with SDL_DequeueAudio
    sDevice = SDL_OpenAudioDevice(nullptr, 1, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (sDevice == 0) {
        return;
    }
    if (have.format != AUDIO_F32SYS || have.channels != 1) {
        // Not the format asked for, and nothing here knows how to read another one
        SDL_CloseAudioDevice(sDevice);
        sDevice = 0;
        return;
    }
    sSampleRate = have.freq;
    SDL_PauseAudioDevice(sDevice, 0);
}

// Normalized autocorrelation pitch detector. Returns the frequency in Hz or 0 when there is no clear pitch.
static float DetectPitch(const float* x, int n, int rate) {
    const int minLag = std::max(2, rate / 1100); // about 1100 Hz
    const int maxLag = std::min(n / 2, rate / 120); // about 120 Hz
    if (maxLag <= minLag) {
        return 0.0f;
    }

    const int len = n - maxLag;
    std::vector<float> nsdf(maxLag + 1, 0.0f);
    for (int lag = minLag - 1; lag <= maxLag; lag++) {
        double acf = 0.0;
        double energy = 0.0;
        for (int i = 0; i < len; i++) {
            acf += static_cast<double>(x[i]) * x[i + lag];
            energy += static_cast<double>(x[i]) * x[i] + static_cast<double>(x[i + lag]) * x[i + lag];
        }
        nsdf[lag] = energy > 1e-9 ? static_cast<float>(2.0 * acf / energy) : 0.0f;
    }

    // Local maxima after the curve first goes below zero; take the first one close to the best one
    float best = 0.0f;
    std::vector<int> peaks;
    bool wasNegative = false;
    for (int lag = minLag; lag < maxLag; lag++) {
        if (nsdf[lag] < 0.0f) {
            wasNegative = true;
        }
        if (wasNegative && nsdf[lag] > nsdf[lag - 1] && nsdf[lag] >= nsdf[lag + 1] && nsdf[lag] > 0.3f) {
            peaks.push_back(lag);
            best = std::max(best, nsdf[lag]);
        }
    }
    if (peaks.empty() || best < 0.6f) {
        return 0.0f;
    }
    int chosen = peaks[0];
    for (int lag : peaks) {
        if (nsdf[lag] >= 0.85f * best) {
            chosen = lag;
            break;
        }
    }

    // Parabolic interpolation for a finer lag
    float lagF = static_cast<float>(chosen);
    float a = nsdf[chosen - 1];
    float b = nsdf[chosen];
    float c = nsdf[chosen + 1];
    float denom = a - 2.0f * b + c;
    if (std::fabs(denom) > 1e-6f) {
        lagF += 0.5f * (a - c) / denom;
    }
    return static_cast<float>(rate) / lagF;
}

// Folds any octave into the ocarina's range (D4 to D5) and returns its position on the five lines
static bool PitchToLevel(float freq, float* outLevel, int* outNote) {
    float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);
    while (midi < 61.0f) {
        midi += 12.0f;
    }
    while (midi >= 76.0f) {
        midi -= 12.0f;
    }

    // Position over the lines, with fractions between neighbouring notes
    float level = -1.0f;
    for (int i = 0; i < 4; i++) {
        if (midi >= kNoteMidi[i] && midi <= kNoteMidi[i + 1]) {
            level = i + (midi - kNoteMidi[i]) / (kNoteMidi[i + 1] - kNoteMidi[i]);
            break;
        }
    }
    if (level < 0.0f) {
        level = midi < kNoteMidi[0] ? 0.0f : 4.0f;
    }
    *outLevel = level;

    // The note it counts as, if it is close enough to one of the five
    int note = -1;
    float bestDist = 1.5f;
    for (int i = 0; i < 5; i++) {
        float dist = std::fabs(midi - kNoteMidi[i]);
        if (dist < bestDist) {
            bestDist = dist;
            note = i;
        }
    }
    *outNote = note;
    return true;
}

extern "C" void RemixOcarinaMic_Update(int ocarinaOut) {
    if (!ocarinaOut || !Enabled()) {
        if (sDevice != 0) {
            CloseDevice();
        }
        return;
    }

    OpenDevice();
    if (sDevice == 0) {
        return;
    }

    // Take what the microphone heard since the last frame
    Uint32 queued = SDL_GetQueuedAudioSize(sDevice);
    if (queued > 0) {
        size_t count = queued / sizeof(float);
        std::vector<float> chunk(count);
        SDL_DequeueAudio(sDevice, chunk.data(), static_cast<Uint32>(count * sizeof(float)));
        sSamples.insert(sSamples.end(), chunk.begin(), chunk.end());
        if (sSamples.size() > static_cast<size_t>(kWindow)) {
            sSamples.erase(sSamples.begin(), sSamples.end() - kWindow);
        }
    }
    if (sSamples.size() < static_cast<size_t>(kWindow)) {
        return;
    }

    double sum = 0.0;
    for (float s : sSamples) {
        sum += static_cast<double>(s) * s;
    }
    float rms = static_cast<float>(std::sqrt(sum / sSamples.size()));
    sVolume = std::min(1.0f, rms * 8.0f);

    int heardNote = -1;
    sLevel = -1.0f;
    if (rms > 0.01f) {
        float freq = DetectPitch(sSamples.data(), kWindow, sSampleRate);
        if (freq > 100.0f && freq < 1500.0f) {
            float level = 0.0f;
            PitchToLevel(freq, &level, &heardNote);
            sLevel = level;
        }
    }

    // Hold a note only after hearing it a few frames in a row, release it after a short silence
    if (heardNote >= 0) {
        sSilentFrames = 0;
        if (heardNote == sCandidate) {
            sCandidateFrames++;
        } else {
            sCandidate = heardNote;
            sCandidateFrames = 1;
        }
        if (sCandidateFrames >= 3) {
            sNote = sCandidate;
        }
    } else {
        sCandidate = -1;
        sCandidateFrames = 0;
        if (++sSilentFrames >= 4) {
            sNote = -1;
        }
    }
}

extern "C" int RemixOcarinaMic_IsListening(void) {
    return (sDevice != 0 && Enabled()) ? 1 : 0;
}

extern "C" int RemixOcarinaMic_GetNote(void) {
    return sNote;
}

extern "C" float RemixOcarinaMic_GetPitchLevel(void) {
    return sLevel;
}

extern "C" float RemixOcarinaMic_GetVolume(void) {
    return sVolume;
}
