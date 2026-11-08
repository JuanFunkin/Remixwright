#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/SaveManager.h"

extern "C" {
extern PlayState* gPlayState;
#include "functions.h"
#include "macros.h"
#include "variables.h"
}

// Remix of Harkinian: autosave is part of the game, not an option.
//
// Autosaves never overwrite the file being played. They rotate through their own slots (see
// SaveManager::AutosaveFiles) so the newest few are always kept, and the title screen lists them next to the
// playthrough files, like the autosaves of Breath of the Wild.

static uint64_t lastSaveTimestamp;
static bool sceneChangePending = false;

static constexpr uint64_t THREE_MINUTES_IN_UNIX = 3 * 60000;
// Loading zones can come one after another, so scene change autosaves are spaced out.
static constexpr uint64_t SCENE_CHANGE_MIN_SPACING = 30000;

static bool Autosave_CanSave() {
    // Don't save when in title screen or debug file
    // Don't save a file that doesn't exist (e.g. it was deleted on death by user option)
    // Don't save the first 60 frames to not save the magic meter when it's still in the animation of filling it.
    // Don't save in Chamber of Sages and the Cutscene map because of remember save location and cutscene item gives.
    // Don't save between obtaining Ocarina of Time and Song of Time because the latter would become unobtainable.
    if (!GameInteractor::IsSaveLoaded(false) || !SaveManager::Instance->SaveFile_Exist(gSaveContext.fileNum) ||
        gPlayState->gameplayFrames < 60 || gPlayState->sceneNum == SCENE_CHAMBER_OF_THE_SAGES ||
        gPlayState->sceneNum == SCENE_CUTSCENE_MAP ||
        (!CHECK_QUEST_ITEM(QUEST_SONG_TIME) && (INV_CONTENT(ITEM_OCARINA_TIME) == ITEM_OCARINA_TIME))) {
        return false;
    }

    return true;
}

static void Autosave_PerformSave() {
    int autosaveSlot = SaveManager::Instance->NextAutosaveSlot();
    if (autosaveSlot < 0) {
        return;
    }

    // SaveManager takes its copy of gSaveContext while Play_PerformSave runs, so pointing fileNum at the autosave
    // slot only for that call is enough, and the file being played is left alone.
    int playingSlot = gSaveContext.fileNum;
    SaveManager::Instance->autosaveInProgress = true;
    gSaveContext.fileNum = autosaveSlot;
    Play_PerformSave(gPlayState);
    gSaveContext.fileNum = playingSlot;
    SaveManager::Instance->autosaveInProgress = false;
    // No message and no sound: autosaves are meant to go unnoticed.
}

static void Autosave_FrameUpdate() {
    uint64_t currentTimestamp = GetUnixTimestamp();

    // Arriving in a new scene is a natural point to save. The scene needs a moment to settle first, which
    // Autosave_CanSave covers, so the request waits until saving is allowed.
    if (sceneChangePending) {
        if (!GameInteractor::IsSaveLoaded(false)) {
            sceneChangePending = false;
        } else if ((currentTimestamp - lastSaveTimestamp) < SCENE_CHANGE_MIN_SPACING) {
            sceneChangePending = false;
        } else if (Autosave_CanSave() && !GameInteractor::IsGameplayPaused()) {
            sceneChangePending = false;
            lastSaveTimestamp = currentTimestamp;
            Autosave_PerformSave();
            return;
        }
    }

    // Check if the interval has passed in minutes.
    if ((currentTimestamp - lastSaveTimestamp) < THREE_MINUTES_IN_UNIX) {
        return;
    }

    // If save available to create, do it and reset the interval.
    // Interval gets extra check for being paused to avoid rare issues like bypassing shop
    // rupees draining after buying an item. Since the interval can just retry until it
    // passes, it can use more conditions without hampering the player experience.
    if (Autosave_CanSave() && !GameInteractor::IsGameplayPaused()) {
        lastSaveTimestamp = currentTimestamp;
        Autosave_PerformSave();
    }
}

static void Autosave_SoftResetSave() {
    if (Autosave_CanSave()) {
        Autosave_PerformSave();
    }
}

static void RegisterAutosave() {
    lastSaveTimestamp = GetUnixTimestamp();
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](uint32_t fileNum) {
        lastSaveTimestamp = GetUnixTimestamp();
        sceneChangePending = false;
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(
        [](int16_t sceneNum) { sceneChangePending = true; });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(Autosave_FrameUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>(
        [](int32_t fileNum) { Autosave_SoftResetSave(); });
}

static RegisterShipInitFunc initFunc(RegisterAutosave);
