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
extern SaveContext gSaveContext;
}

namespace {

struct WheelItem {
    int slot;
    int item;
};

bool sOpen = false;
int sSelected = 0;
std::vector<WheelItem> sItems;

// D-pad buttons in the equipment table: 4 = up, 5 = down, 6 = left, 7 = right
constexpr int kDpadUp = 4;
constexpr int kDpadDown = 5;
constexpr int kDpadLeft = 6;
constexpr int kDpadRight = 7;

bool Enabled() {
    return CVarGetInteger(CVAR_ENHANCEMENT("RemixItemSelector"), 1) != 0;
}

bool IsMask(int item) {
    return item >= ITEM_MASK_KEATON && item <= ITEM_MASK_TRUTH;
}

// What the selector can offer: every item in the inventory that the current age can use, except the masks (D-Up)
void BuildItems() {
    sItems.clear();
    for (int slot = 0; slot < 24; slot++) {
        const int item = gSaveContext.inventory.items[slot];
        if (item == ITEM_NONE || item == ITEM_SOLD_OUT || slot == SLOT_TRADE_CHILD) {
            continue;
        }
        if (gSlotAgeReqs[slot] != 9 && gSlotAgeReqs[slot] != gSaveContext.linkAge) {
            continue;
        }
        sItems.push_back({ slot, item });
    }
}

void SetDpad(PlayState* play, int button, int item, int slot) {
    if (gSaveContext.equips.buttonItems[button] == item && gSaveContext.equips.cButtonSlots[button - 1] == slot) {
        return;
    }
    gSaveContext.equips.buttonItems[button] = static_cast<u8>(item);
    gSaveContext.equips.cButtonSlots[button - 1] = static_cast<u8>(slot);
    gSaveContext.buttonStatus[button + 1] = (item == ITEM_NONE) ? BTN_DISABLED : BTN_ENABLED;
    if (item != ITEM_NONE) {
        Interface_LoadItemIcon1(play, static_cast<u16>(button));
    }
}

// D-Left is the ocarina, D-Up the mask Link carries
void KeepFixedButtons(PlayState* play) {
    const int ocarina = gSaveContext.inventory.items[SLOT_OCARINA];
    if (ocarina == ITEM_OCARINA_FAIRY || ocarina == ITEM_OCARINA_TIME) {
        SetDpad(play, kDpadLeft, ocarina, SLOT_OCARINA);
    }
    const int mask = gSaveContext.inventory.items[SLOT_TRADE_CHILD];
    if (IsMask(mask)) {
        SetDpad(play, kDpadUp, mask, SLOT_TRADE_CHILD);
    } else if (gSaveContext.equips.buttonItems[kDpadUp] != ITEM_NONE) {
        gSaveContext.equips.buttonItems[kDpadUp] = ITEM_NONE;
    }
}

void Assign(PlayState* play, int button) {
    if (sItems.empty()) {
        return;
    }
    const WheelItem& chosen = sItems[std::clamp(sSelected, 0, static_cast<int>(sItems.size()) - 1)];

    // The same item cannot sit on both free D-pad directions
    const int other = (button == kDpadRight) ? kDpadDown : kDpadRight;
    if (gSaveContext.equips.buttonItems[other] == chosen.item) {
        gSaveContext.equips.buttonItems[other] = ITEM_NONE;
        gSaveContext.buttonStatus[other + 1] = BTN_DISABLED;
    }
    SetDpad(play, button, chosen.item, chosen.slot);
    Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
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
        sOpen = false;
        return 0;
    }
    Input* in = &play->state.input[0];

    if (CanOpen(play) || sOpen) {
        KeepFixedButtons(play);
    }

    if (!sOpen) {
        if (CHECK_BTN_ALL(in->press.button, BTN_L) && CanOpen(play)) {
            BuildItems();
            if (!sItems.empty()) {
                sOpen = true;
                sSelected = 0;
                Audio_PlaySfxGeneral(NA_SE_SY_CAMERA_ZOOM_UP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        }
        return sOpen ? 1 : 0;
    }

    // Choose with either stick, the first item sits at the top and the rest go around clockwise
    const int sx = std::abs(in->cur.stick_x) > std::abs(in->cur.right_stick_x) ? in->cur.stick_x : in->cur.right_stick_x;
    const int sy = std::abs(in->cur.stick_y) > std::abs(in->cur.right_stick_y) ? in->cur.stick_y : in->cur.right_stick_y;
    if (std::abs(sx) > 30 || std::abs(sy) > 30) {
        float ang = std::atan2(static_cast<float>(sx), static_cast<float>(sy)); // 0 = up, clockwise
        if (ang < 0.0f) {
            ang += 6.2831853f;
        }
        const int n = static_cast<int>(sItems.size());
        int idx = static_cast<int>(std::floor(ang / (6.2831853f / n) + 0.5f)) % n;
        if (idx != sSelected) {
            sSelected = idx;
            Audio_PlaySfxGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }

    if (CHECK_BTN_ALL(in->press.button, BTN_DRIGHT)) {
        Assign(play, kDpadRight);
    } else if (CHECK_BTN_ALL(in->press.button, BTN_DDOWN)) {
        Assign(play, kDpadDown);
    }

    if (CHECK_BTN_ALL(in->press.button, BTN_L) || CHECK_BTN_ALL(in->press.button, BTN_B) ||
        CHECK_BTN_ALL(in->press.button, BTN_START)) {
        sOpen = false;
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

void RemixItemSelector_Draw() {
    if (!sOpen || sItems.empty()) {
        return;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    const float unit = vp->Size.y / 720.0f;
    const ImVec2 c(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);

    dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 130));

    const int n = static_cast<int>(sItems.size());
    const float ring = std::min(vp->Size.y * 0.36f, 150.0f * unit + n * 9.0f * unit);
    const float iconR = 26.0f * unit;
    for (int i = 0; i < n; i++) {
        const float ang = 6.2831853f * i / n;
        ImVec2 p(c.x + std::sin(ang) * ring, c.y - std::cos(ang) * ring);
        const bool sel = (i == sSelected);
        dl->AddCircleFilled(p, iconR + 6.0f * unit, sel ? IM_COL32(255, 255, 255, 70) : IM_COL32(0, 0, 0, 120), 28);
        DrawIcon(dl, p, iconR * (sel ? 1.2f : 1.0f), sItems[i].item);
        if (sel) {
            dl->AddCircle(p, iconR + 8.0f * unit, IM_COL32(255, 255, 255, 255), 28, 3.0f);
        }
    }

    // Name of the chosen item
    const WheelItem& cur = sItems[std::clamp(sSelected, 0, n - 1)];
    const std::string& name = SohUtils::GetItemName(cur.item);
    {
        const float size = 24.0f * unit;
        Shadowed(dl, size, ImVec2(c.x - TextW(size, name.c_str()) * 0.5f, c.y - ring - iconR - 52.0f * unit),
                 IM_COL32(255, 255, 255, 255), name.c_str());
    }

    // The D-pad in the middle: what each direction does now
    const float cell = 34.0f * unit;
    struct Dir {
        int button;
        ImVec2 off;
        const char* label;
    };
    const Dir dirs[4] = { { kDpadUp, ImVec2(0, -1.5f), "Mask" },
                          { kDpadDown, ImVec2(0, 1.5f), "Set" },
                          { kDpadLeft, ImVec2(-1.5f, 0), "Ocarina" },
                          { kDpadRight, ImVec2(1.5f, 0), "Set" } };
    for (const Dir& d : dirs) {
        ImVec2 p(c.x + d.off.x * cell * 1.5f, c.y + d.off.y * cell * 1.5f);
        dl->AddCircleFilled(p, cell * 0.75f, IM_COL32(0, 0, 0, 150), 24);
        const int item = gSaveContext.equips.buttonItems[d.button];
        if (item != ITEM_NONE) {
            DrawIcon(dl, p, cell * 0.65f, item);
        }
    }

    // Hints
    const float size = 18.0f * unit;
    const char* hint = "Choose: stick     D-Right / D-Down: set the item     L / B: close";
    Shadowed(dl, size, ImVec2(c.x - TextW(size, hint) * 0.5f, vp->Pos.y + vp->Size.y - 48.0f * unit),
             IM_COL32(255, 255, 255, 255), hint);
}
