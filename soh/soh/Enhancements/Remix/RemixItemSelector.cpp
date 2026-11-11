#include "RemixItemSelector.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <fast/Fast3dGui.h>
#include <ship/Context.h>
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "soh/util.h"
#include "soh/SohGui/ImGuiUtils.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "seqcmd.h"
#include "sequence.h"
extern SaveContext gSaveContext;
extern PlayState* gPlayState;
void Player_UseItem(PlayState* play, Player* this, s32 item);
}

namespace {

struct WheelItem {
    int slot;
    int item;
};

bool sOpen = false;
int sSelected = 0;
std::vector<WheelItem> sItems;

// Equipment table: 1 = the item button (C-Left), 4 = D-pad up, 5 = down, 6 = left, 7 = right
constexpr int kItemButton = 1;
constexpr int kDpadUp = 4;
constexpr int kDpadDown = 5;
constexpr int kDpadLeft = 6;
constexpr int kDpadRight = 7;

int sStickHold = 0; // frames the stick has been held to one side, for the repeat

bool Enabled() {
    return CVarGetInteger(CVAR_ENHANCEMENT("RemixItemSelector"), 1) != 0;
}

bool IsMask(int item) {
    return item >= ITEM_MASK_KEATON && item <= ITEM_MASK_TRUTH;
}

// What the selector can offer: every item in the inventory that the current age can use
void BuildItems() {
    sItems.clear();
    for (int slot = 0; slot < 24; slot++) {
        const int item = gSaveContext.inventory.items[slot];
        if (item == ITEM_NONE || item == ITEM_SOLD_OUT) {
            continue;
        }
        if (slot == SLOT_TRADE_CHILD && !IsMask(item)) {
            continue;
        }
        if (gSlotAgeReqs[slot] != 9 && gSlotAgeReqs[slot] != gSaveContext.linkAge) {
            continue;
        }
        sItems.push_back({ slot, item });
    }
}

void SetButton(PlayState* play, int button, int item, int slot) {
    gSaveContext.equips.buttonItems[button] = static_cast<u8>(item);
    gSaveContext.equips.cButtonSlots[button - 1] = static_cast<u8>(slot);
    // buttonStatus: 0 = B, 1..3 = C buttons, 4 = A, 5..8 = D-pad
    gSaveContext.buttonStatus[button <= 3 ? button : button + 1] = (item == ITEM_NONE) ? BTN_DISABLED : BTN_ENABLED;
    if (item != ITEM_NONE) {
        Interface_LoadItemIcon1(play, static_cast<u16>(button));
    }
}

// There is a single item button now (and the D-pad): the other C buttons stay empty and out of the HUD
void ClearUnusedButtons(PlayState* play) {
    for (int button = 2; button <= 3; button++) {
        if (gSaveContext.equips.buttonItems[button] != ITEM_NONE) {
            gSaveContext.equips.buttonItems[button] = ITEM_NONE;
            gSaveContext.equips.cButtonSlots[button - 1] = SLOT_NONE;
            gSaveContext.buttonStatus[button] = BTN_DISABLED;
        }
    }

    // The Ocarina is on D-pad left from the moment Link has it, unless the player put something else there
    if (gSaveContext.inventory.items[SLOT_OCARINA] != ITEM_NONE && gSaveContext.equips.buttonItems[kDpadLeft] == ITEM_NONE) {
        SetButton(play, kDpadLeft, gSaveContext.inventory.items[SLOT_OCARINA], SLOT_OCARINA);
    }
}

void Assign(PlayState* play, int button) {
    if (sItems.empty()) {
        return;
    }
    const WheelItem& chosen = sItems[std::clamp(sSelected, 0, static_cast<int>(sItems.size()) - 1)];

    // An item sits on one button only
    for (int other = kItemButton; other <= kDpadRight; other++) {
        if (other != button && other != 2 && other != 3 && gSaveContext.equips.buttonItems[other] == chosen.item) {
            gSaveContext.equips.buttonItems[other] = ITEM_NONE;
            gSaveContext.equips.cButtonSlots[other - 1] = SLOT_NONE;
            gSaveContext.buttonStatus[other <= 3 ? other : other + 1] = BTN_DISABLED;
        }
    }
    SetButton(play, button, chosen.item, chosen.slot);
    Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

void SetMusicLow(bool low) {
    SEQCMD_SET_SEQPLAYER_VOLUME(SEQ_PLAYER_BGM_MAIN, 8, low ? 0x30 : 0x7F);
}

bool CanOpen(PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == nullptr || play->pauseCtx.state != 0 || play->csCtx.state != CS_STATE_IDLE ||
        play->transitionMode != TRANS_MODE_OFF || play->transitionTrigger != TRANS_TRIGGER_OFF ||
        play->msgCtx.msgMode != 0 || play->gameOverCtx.state != GAMEOVER_INACTIVE || gSaveContext.gameMode != 0 ||
        Player_InCsMode(play)) {
        return false;
    }
    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_TALKING)) {
        return false;
    }
    if (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) {
        return false;
    }
    return true;
}

} // namespace

extern "C" int RemixItemSelector_IsOpen(void) {
    return sOpen ? 1 : 0;
}

extern "C" int RemixItemSelector_Update(PlayState* play) {
    if (!Enabled()) {
        if (sOpen) {
            SetMusicLow(false);
        }
        sOpen = false;
        return 0;
    }
    Input* in = &play->state.input[0];

    if (play->csCtx.state == CS_STATE_IDLE) {
        ClearUnusedButtons(play);
    }

    if (!sOpen) {
        if (CHECK_BTN_ALL(in->press.button, BTN_L) && CanOpen(play)) {
            BuildItems();
            if (!sItems.empty()) {
                sOpen = true;
                sStickHold = 0;
                sSelected = 0;
                // Start on the item that is on the item button now
                for (size_t i = 0; i < sItems.size(); i++) {
                    if (sItems[i].item == gSaveContext.equips.buttonItems[kItemButton]) {
                        sSelected = static_cast<int>(i);
                    }
                }
                SetMusicLow(true);
                Audio_PlaySfxGeneral(NA_SE_SY_CAMERA_ZOOM_UP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        }
        return sOpen ? 1 : 0;
    }

    // Left and right with either stick, one step per push and then it repeats while held
    const int sx = std::abs(in->cur.stick_x) > std::abs(in->cur.right_stick_x) ? in->cur.stick_x : in->cur.right_stick_x;
    const int n = static_cast<int>(sItems.size());
    int step = 0;
    if (std::abs(sx) > 40) {
        if (sStickHold == 0 || (sStickHold > 14 && sStickHold % 5 == 0)) {
            step = sx > 0 ? 1 : -1;
        }
        sStickHold++;
    } else {
        sStickHold = 0;
    }
    if (step != 0) {
        sSelected = (sSelected + step + n) % n;
        Audio_PlaySfxGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    bool close = false;
    if (CHECK_BTN_ALL(in->press.button, BTN_A)) {
        // Take the item in hand right now. It sits on the hidden item button so the game keeps it out.
        Assign(play, kItemButton);
        Player* player = GET_PLAYER(play);
        player->heldItemButton = kItemButton;
        Player_UseItem(play, player, sItems[std::clamp(sSelected, 0, static_cast<int>(sItems.size()) - 1)].item);
        close = true;
    } else if (CHECK_BTN_ALL(in->press.button, BTN_DUP)) {
        Assign(play, kDpadUp);
    } else if (CHECK_BTN_ALL(in->press.button, BTN_DDOWN)) {
        Assign(play, kDpadDown);
    } else if (CHECK_BTN_ALL(in->press.button, BTN_DLEFT)) {
        Assign(play, kDpadLeft);
    } else if (CHECK_BTN_ALL(in->press.button, BTN_DRIGHT)) {
        Assign(play, kDpadRight);
    } else if (CHECK_BTN_ALL(in->press.button, BTN_L) || CHECK_BTN_ALL(in->press.button, BTN_B) ||
               CHECK_BTN_ALL(in->press.button, BTN_START)) {
        close = true;
    }
    if (close) {
        sOpen = false;
        SetMusicLow(false);
        Audio_PlaySfxGeneral(NA_SE_SY_CAMERA_ZOOM_DOWN, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
    return sOpen ? 1 : 0;
}

namespace {

ImTextureID IconFor(int item) {
    auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
    if (gui == nullptr) {
        return nullptr;
    }
    auto it = itemMapping.find(static_cast<uint32_t>(item));
    if (it == itemMapping.end()) {
        return nullptr;
    }
    return gui->GetTextureByName(it->second.name);
}

void DrawIcon(ImDrawList* dl, ImVec2 c, float r, int item) {
    ImTextureID tex = IconFor(item);
    if (tex != nullptr) {
        dl->AddImage(tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
    } else {
        dl->AddCircleFilled(c, r * 0.8f, IM_COL32(200, 200, 200, 200), 20);
    }
}

void Shadowed(ImDrawList* dl, float size, ImVec2 pos, ImU32 color, const char* text) {
    dl->AddText(ImGui::GetFont(), size, ImVec2(pos.x + 1.5f, pos.y + 1.5f), IM_COL32(0, 0, 0, 220), text);
    dl->AddText(ImGui::GetFont(), size, pos, color, text);
}

float TextW(float size, const char* t) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x;
}

} // namespace

// The item button: a box with the item inside, used with ZR. It lives on the hidden C-Left slot of the game, but it
// is drawn here and it is not a C button for the player.
static void DrawItemBox() {
    if (gPlayState == nullptr || !Enabled()) {
        return;
    }
    PlayState* play = gPlayState;
    if (play->pauseCtx.state != 0 || play->csCtx.state != CS_STATE_IDLE || gSaveContext.gameMode != 0 ||
        play->gameOverCtx.state != GAMEOVER_INACTIVE) {
        return;
    }
    const int item = gSaveContext.equips.buttonItems[kItemButton];
    if (item == ITEM_NONE) {
        return;
    }
    const float hud = std::min(1.0f, play->interfaceCtx.healthAlpha / 255.0f);
    if (hud <= 0.01f) {
        return;
    }
    const bool usable = gSaveContext.buttonStatus[kItemButton] != BTN_DISABLED;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    const float unit = vp->Size.y / 720.0f;
    const float size = 74.0f * unit;
    const ImVec2 min(vp->Pos.x + vp->Size.x - size - 40.0f * unit, vp->Pos.y + vp->Size.y - size - 70.0f * unit);
    const ImVec2 max(min.x + size, min.y + size);
    const int a = static_cast<int>(255 * hud);

    dl->AddRectFilled(min, max, IM_COL32(10, 14, 20, static_cast<int>(185 * hud)), 10.0f * unit);
    dl->AddRect(min, max, IM_COL32(235, 225, 190, static_cast<int>(240 * hud)), 10.0f * unit, 0, 3.0f * unit);
    ImTextureID tex = IconFor(item);
    const float pad = 9.0f * unit;
    if (tex != nullptr) {
        dl->AddImage(tex, ImVec2(min.x + pad, min.y + pad), ImVec2(max.x - pad, max.y - pad), ImVec2(0, 0),
                     ImVec2(1, 1), usable ? IM_COL32(255, 255, 255, a) : IM_COL32(120, 120, 120, a));
    }
    const float ts = 18.0f * unit;
    const char* label = "ZR";
    dl->AddText(ImGui::GetFont(), ts, ImVec2(min.x + (size - TextW(ts, label)) * 0.5f, max.y + 4.0f * unit),
                IM_COL32(255, 255, 255, a), label);
}

void RemixItemSelector_Draw() {
    DrawItemBox();
    if (!sOpen || sItems.empty()) {
        return;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    const float unit = vp->Size.y / 720.0f;
    const float cx = vp->Pos.x + vp->Size.x * 0.5f;
    const float cy = vp->Pos.y + vp->Size.y * 0.72f;

    // A soft band behind the row, the game stays in view
    dl->AddRectFilledMultiColor(ImVec2(vp->Pos.x, cy - 120.0f * unit), ImVec2(vp->Pos.x + vp->Size.x, cy + 120.0f * unit),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 150),
                                IM_COL32(0, 0, 0, 150));
    dl->AddRectFilled(ImVec2(vp->Pos.x, cy + 120.0f * unit), ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                      IM_COL32(0, 0, 0, 150));

    const int n = static_cast<int>(sItems.size());
    const float gap = 74.0f * unit;
    const int visible = std::min(n, 9);
    for (int k = -(visible / 2); k <= visible / 2; k++) {
        const int i = ((sSelected + k) % n + n) % n;
        if (n < 9 && (sSelected + k < 0 || sSelected + k >= n)) {
            continue; // short lists do not wrap around
        }
        const bool sel = (k == 0);
        const float fade = 1.0f - std::min(1.0f, std::abs(static_cast<float>(k)) / (visible / 2 + 1));
        ImVec2 p(cx + k * gap, cy);
        const float r = (sel ? 36.0f : 25.0f) * unit;
        dl->AddCircleFilled(p, r + 7.0f * unit, sel ? IM_COL32(255, 255, 255, 80) : IM_COL32(0, 0, 0, 120), 32);
        DrawIcon(dl, p, r, sItems[i].item);
        if (sel) {
            dl->AddCircle(p, r + 9.0f * unit, IM_COL32(255, 255, 255, 255), 32, 3.0f);
        } else {
            dl->AddCircleFilled(p, r + 7.0f * unit, IM_COL32(0, 0, 0, static_cast<int>(120 * (1.0f - fade))), 32);
        }
    }

    // Name of the chosen item
    const WheelItem& cur = sItems[std::clamp(sSelected, 0, n - 1)];
    const std::string& name = SohUtils::GetItemName(cur.item);
    {
        const float size = 28.0f * unit;
        Shadowed(dl, size, ImVec2(cx - TextW(size, name.c_str()) * 0.5f, cy - 100.0f * unit),
                 IM_COL32(255, 255, 255, 255), name.c_str());
    }

    const float size = 19.0f * unit;
    const char* hint = "Stick: choose     A: use this item     D-pad: put it on that direction     L / B: close";
    Shadowed(dl, size, ImVec2(cx - TextW(size, hint) * 0.5f, vp->Pos.y + vp->Size.y - 46.0f * unit),
             IM_COL32(255, 255, 255, 255), hint);
}
