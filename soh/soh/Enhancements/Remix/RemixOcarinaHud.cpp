#include "RemixOcarinaHud.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <ship/window/gui/GuiWindow.h>
#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "RemixOcarinaMic.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

struct SongEntry {
    const char* name;
    int songId;
    int questItem;
};

const SongEntry kSongs[] = {
    { "Minuet of Forest", OCARINA_SONG_MINUET, QUEST_SONG_MINUET },
    { "Bolero of Fire", OCARINA_SONG_BOLERO, QUEST_SONG_BOLERO },
    { "Serenade of Water", OCARINA_SONG_SERENADE, QUEST_SONG_SERENADE },
    { "Requiem of Spirit", OCARINA_SONG_REQUIEM, QUEST_SONG_REQUIEM },
    { "Nocturne of Shadow", OCARINA_SONG_NOCTURNE, QUEST_SONG_NOCTURNE },
    { "Prelude of Light", OCARINA_SONG_PRELUDE, QUEST_SONG_PRELUDE },
    { "Zelda's Lullaby", OCARINA_SONG_LULLABY, QUEST_SONG_LULLABY },
    { "Epona's Song", OCARINA_SONG_EPONAS, QUEST_SONG_EPONA },
    { "Saria's Song", OCARINA_SONG_SARIAS, QUEST_SONG_SARIA },
    { "Sun's Song", OCARINA_SONG_SUNS, QUEST_SONG_SUN },
    { "Song of Time", OCARINA_SONG_TIME, QUEST_SONG_TIME },
    { "Song of Storms", OCARINA_SONG_STORMS, QUEST_SONG_STORMS },
};

// Ocarina buttons in the order of the notes D4, F4, A4, B4, D5
const char* kNoteNames[5] = { "D4", "F4", "A4", "B4", "D5" };

const ImU32 kBlue = IM_COL32(70, 150, 255, 255);
const ImU32 kYellow = IM_COL32(255, 205, 60, 255);

// Recent pitch levels for the bubbles on the staff, newest last
constexpr int kTrail = 72;
float sTrail[kTrail];
bool sTrailInit = false;
float sAnimTime = 0.0f;

// Draws the glyph of an ocarina button (0 = A, 1 = C-Down, 2 = C-Right, 3 = C-Left, 4 = C-Up)
void DrawButtonGlyph(ImDrawList* dl, ImVec2 c, float r, int button, bool lit) {
    const int alpha = lit ? 255 : 150;
    if (button == 0) {
        ImU32 col = IM_COL32(70, 150, 255, alpha);
        dl->AddCircleFilled(c, r, col, 24);
        dl->AddCircle(c, r, IM_COL32(255, 255, 255, lit ? 255 : 110), 24, 1.5f);
        const char* t = "A";
        ImVec2 ts = ImGui::CalcTextSize(t);
        float k = (r * 1.2f) / ts.y;
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * k, ImVec2(c.x - ts.x * k * 0.5f, c.y - ts.y * k * 0.5f),
                    IM_COL32(255, 255, 255, alpha), t);
        return;
    }
    ImU32 col = IM_COL32(255, 205, 60, alpha);
    ImVec2 a;
    ImVec2 b;
    ImVec2 d;
    switch (button) {
        case 1: // down
            a = ImVec2(c.x - r, c.y - r * 0.7f);
            b = ImVec2(c.x + r, c.y - r * 0.7f);
            d = ImVec2(c.x, c.y + r);
            break;
        case 2: // right
            a = ImVec2(c.x - r * 0.7f, c.y - r);
            b = ImVec2(c.x - r * 0.7f, c.y + r);
            d = ImVec2(c.x + r, c.y);
            break;
        case 3: // left
            a = ImVec2(c.x + r * 0.7f, c.y - r);
            b = ImVec2(c.x + r * 0.7f, c.y + r);
            d = ImVec2(c.x - r, c.y);
            break;
        default: // up
            a = ImVec2(c.x - r, c.y + r * 0.7f);
            b = ImVec2(c.x + r, c.y + r * 0.7f);
            d = ImVec2(c.x, c.y - r);
            break;
    }
    dl->AddTriangleFilled(a, b, d, col);
    dl->AddTriangle(a, b, d, IM_COL32(255, 255, 255, lit ? 255 : 110), 1.5f);
}

bool OcarinaOut() {
    if (gPlayState == nullptr) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING);
}

// Which of the five notes the player is holding with the controller right now (-1 = none)
int HeldNote() {
    if (gPlayState == nullptr) {
        return -1;
    }
    const uint32_t b = gPlayState->state.input[0].cur.button;
    if (b & BTN_A) {
        return 0;
    }
    if (b & BTN_CDOWN) {
        return 1;
    }
    if (b & BTN_CRIGHT) {
        return 2;
    }
    if (b & BTN_CLEFT) {
        return 3;
    }
    if (b & BTN_CUP) {
        return 4;
    }
    return -1;
}

void DrawMicPanel(ImDrawList* dl, ImVec2 origin, float unit) {
    const float w = 420.0f * unit;
    const float h = 170.0f * unit;
    ImVec2 p0(origin.x - w * 0.5f, origin.y);
    ImVec2 p1(origin.x + w * 0.5f, origin.y + h);
    dl->AddRectFilled(p0, p1, IM_COL32(15, 12, 8, 170), 10.0f * unit);
    dl->AddRect(p0, p1, IM_COL32(230, 200, 120, 200), 10.0f * unit, 0, 2.0f);

    const bool listening = RemixOcarinaMic_IsListening();
    const float vol = RemixOcarinaMic_GetVolume();
    const float level = RemixOcarinaMic_GetPitchLevel();

    // Title and volume meter
    const char* title = listening ? "Listening..." : "Microphone starting...";
    dl->AddText(ImGui::GetFont(), 16.0f * unit, ImVec2(p0.x + 14.0f * unit, p0.y + 8.0f * unit),
                IM_COL32(255, 240, 200, 255), title);
    ImVec2 m0(p1.x - 130.0f * unit, p0.y + 12.0f * unit);
    ImVec2 m1(p1.x - 14.0f * unit, p0.y + 22.0f * unit);
    dl->AddRectFilled(m0, m1, IM_COL32(255, 255, 255, 40), 4.0f * unit);
    dl->AddRectFilled(m0, ImVec2(m0.x + (m1.x - m0.x) * vol, m1.y), IM_COL32(120, 230, 140, 230), 4.0f * unit);

    // Staff: five lines, one per ocarina note, D5 on top
    const float left = p0.x + 54.0f * unit;
    const float right = p1.x - 16.0f * unit;
    const float top = p0.y + 42.0f * unit;
    const float bottom = p1.y - 16.0f * unit;
    auto yOf = [&](float lv) { return bottom - (bottom - top) * (lv / 4.0f); };
    for (int i = 0; i < 5; i++) {
        float y = yOf(static_cast<float>(i));
        dl->AddLine(ImVec2(left, y), ImVec2(right, y), IM_COL32(255, 255, 255, 90), 1.2f);
        dl->AddText(ImGui::GetFont(), 13.0f * unit, ImVec2(p0.x + 14.0f * unit, y - 7.0f * unit),
                    IM_COL32(255, 240, 200, 220), kNoteNames[i]);
    }

    // Trail of bubbles: older ones further left and smaller
    for (int i = 0; i < kTrail; i++) {
        float lv = sTrail[i];
        if (lv < 0.0f) {
            continue;
        }
        float t = static_cast<float>(i) / (kTrail - 1);
        float x = left + (right - left) * t;
        float rad = (2.0f + 6.0f * t) * unit;
        dl->AddCircleFilled(ImVec2(x, yOf(lv)), rad, IM_COL32(255, 220, 120, static_cast<int>(40 + 200 * t)), 14);
    }
    if (level >= 0.0f) {
        ImVec2 c(right, yOf(level));
        dl->AddCircle(c, 10.0f * unit, IM_COL32(255, 255, 255, 230), 18, 2.0f);
    }
}

void DrawSongList(ImDrawList* dl, float unit, ImVec2 vpSize) {
    // Only the songs Link has learned
    int count = 0;
    for (const SongEntry& s : kSongs) {
        if (CHECK_QUEST_ITEM(s.questItem)) {
            count++;
        }
    }
    if (count == 0) {
        return;
    }

    const float rowH = 30.0f * unit;
    const float w = 330.0f * unit;
    const float x0 = 20.0f * unit;
    const float h = rowH * count + 16.0f * unit;
    const float y0 = (vpSize.y - h) * 0.5f;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(15, 12, 8, 170), 10.0f * unit);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(230, 200, 120, 200), 10.0f * unit, 0, 2.0f);

    int row = 0;
    for (const SongEntry& s : kSongs) {
        if (!CHECK_QUEST_ITEM(s.questItem)) {
            continue;
        }
        float cy = y0 + 8.0f * unit + rowH * row + rowH * 0.5f;
        dl->AddText(ImGui::GetFont(), 13.0f * unit, ImVec2(x0 + 10.0f * unit, cy - 7.0f * unit),
                    IM_COL32(255, 240, 200, 255), s.name);
        const OcarinaSongButtons& song = gOcarinaSongButtons[s.songId];
        const float step = 15.0f * unit;
        float x = x0 + w - 10.0f * unit - step * song.numButtons + step * 0.5f;
        for (int i = 0; i < song.numButtons; i++) {
            int btn = song.buttonsIndex[i];
            if (btn > 4) {
                btn = 2; // B flat, shown as C-Right
            }
            DrawButtonGlyph(dl, ImVec2(x + step * i, cy), 6.0f * unit, btn, true);
        }
        row++;
    }
}

void DrawButtonBar(ImDrawList* dl, float unit, ImVec2 vpSize, int litNote) {
    const float slot = 74.0f * unit;
    const float w = slot * 5.0f + 20.0f * unit;
    const float h = 84.0f * unit;
    const float x0 = (vpSize.x - w) * 0.5f;
    const float y0 = vpSize.y - h - 22.0f * unit;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(15, 12, 8, 170), 12.0f * unit);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(230, 200, 120, 200), 12.0f * unit, 0, 2.0f);

    // Left to right the way the notes read on the game's own staff: top note first is D5
    static const int order[5] = { 3, 1, 0, 2, 4 }; // B4 (C-Left), F4 (C-Down), D4 (A), A4 (C-Right), D5 (C-Up)
    for (int i = 0; i < 5; i++) {
        int note = order[i];
        ImVec2 c(x0 + 10.0f * unit + slot * (i + 0.5f), y0 + 32.0f * unit);
        bool lit = (note == litNote);
        if (lit) {
            dl->AddCircleFilled(c, 27.0f * unit, IM_COL32(255, 255, 255, 60), 24);
        }
        DrawButtonGlyph(dl, c, 17.0f * unit, note, lit);
        const char* name = kNoteNames[note];
        ImVec2 ts = ImGui::CalcTextSize(name);
        float k = (12.0f * unit) / ImGui::GetFontSize();
        dl->AddText(ImGui::GetFont(), 12.0f * unit, ImVec2(c.x - ts.x * k * 0.5f, y0 + h - 22.0f * unit),
                    IM_COL32(255, 240, 200, 230), name);
    }
}

void RemixOcarinaHud_Draw() {
    const bool out = OcarinaOut();
    // Microphone: opens only while the ocarina is out and the option is on
    RemixOcarinaMic_Update(out ? 1 : 0);

    const bool hudOn = CVarGetInteger(CVAR_ENHANCEMENT("RemixOcarinaHud"), 1) != 0;
    if (!out || !hudOn) {
        if (sTrailInit) {
            std::fill(sTrail, sTrail + kTrail, -1.0f);
        }
        return;
    }
    if (!sTrailInit) {
        std::fill(sTrail, sTrail + kTrail, -1.0f);
        sTrailInit = true;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("Remix Ocarina HUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoInputs);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float unit = vp->Size.y / 720.0f;

    // Move the trail along at a steady pace
    sAnimTime += ImGui::GetIO().DeltaTime;
    if (sAnimTime >= 1.0f / 30.0f) {
        sAnimTime = 0.0f;
        for (int i = 0; i < kTrail - 1; i++) {
            sTrail[i] = sTrail[i + 1];
        }
        sTrail[kTrail - 1] = RemixOcarinaMic_GetPitchLevel();
    }

    if (CVarGetInteger(CVAR_SETTING("RemixOcarinaMic.Enabled"), 0)) {
        DrawMicPanel(dl, ImVec2(vp->Size.x * 0.5f, 18.0f * unit), unit);
    }
    DrawSongList(dl, unit, vp->Size);

    int lit = RemixOcarinaMic_GetNote();
    if (lit < 0) {
        lit = HeldNote();
    }
    DrawButtonBar(dl, unit, vp->Size, lit);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

class RemixOcarinaHudWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }
    void Draw() override {
        RemixOcarinaHud_Draw();
    }
};

} // namespace

std::shared_ptr<Ship::GuiWindow> RemixOcarinaHud_CreateWindow() {
    return std::make_shared<RemixOcarinaHudWindow>("gWindows.RemixOcarinaHud", "Remix Ocarina HUD");
}
