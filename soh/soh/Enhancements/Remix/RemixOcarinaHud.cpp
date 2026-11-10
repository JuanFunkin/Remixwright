#include "RemixOcarinaHud.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <fast/Fast3dGui.h>
#include <ship/Context.h>
#include <imgui.h>
#include <ship/window/gui/GuiWindow.h>
#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "soh/ResourceManagerHelpers.h"
#include "RemixOcarinaMic.h"
#include "RemixItemSelector.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

struct SongEntry {
    const char* name;
    const char* description;
    int songId;
    int questItem;
    ImU32 color;
};

const SongEntry kSongs[] = {
    { "Zelda's Lullaby", "This mystical song from the royal family has strange powers.", OCARINA_SONG_LULLABY,
      QUEST_SONG_LULLABY, IM_COL32(230, 110, 170, 255) },
    { "Epona's Song", "A song to call a horse, and to have it listen to you.", OCARINA_SONG_EPONAS, QUEST_SONG_EPONA,
      IM_COL32(190, 130, 90, 255) },
    { "Saria's Song", "A cheerful tune from a forest friend, heard even from far away.", OCARINA_SONG_SARIAS,
      QUEST_SONG_SARIA, IM_COL32(110, 200, 110, 255) },
    { "Sun's Song", "Makes the day come sooner, or the night.", OCARINA_SONG_SUNS, QUEST_SONG_SUN,
      IM_COL32(245, 215, 80, 255) },
    { "Song of Time", "A song that opens what only time can open.", OCARINA_SONG_TIME, QUEST_SONG_TIME,
      IM_COL32(110, 170, 240, 255) },
    { "Song of Storms", "Calls the rain, and chases it away.", OCARINA_SONG_STORMS, QUEST_SONG_STORMS,
      IM_COL32(170, 170, 185, 255) },
    { "Minuet of Forest", "A song that takes you to the Sacred Forest Meadow.", OCARINA_SONG_MINUET, QUEST_SONG_MINUET,
      IM_COL32(90, 190, 100, 255) },
    { "Bolero of Fire", "A song that takes you to the crater of Death Mountain.", OCARINA_SONG_BOLERO,
      QUEST_SONG_BOLERO, IM_COL32(230, 90, 60, 255) },
    { "Serenade of Water", "A song that takes you to Lake Hylia.", OCARINA_SONG_SERENADE, QUEST_SONG_SERENADE,
      IM_COL32(70, 130, 235, 255) },
    { "Requiem of Spirit", "A song that takes you to the Desert Colossus.", OCARINA_SONG_REQUIEM, QUEST_SONG_REQUIEM,
      IM_COL32(235, 150, 60, 255) },
    { "Nocturne of Shadow", "A song that takes you to the Graveyard.", OCARINA_SONG_NOCTURNE, QUEST_SONG_NOCTURNE,
      IM_COL32(150, 90, 210, 255) },
    { "Prelude of Light", "A song that takes you to the Temple of Time.", OCARINA_SONG_PRELUDE, QUEST_SONG_PRELUDE,
      IM_COL32(240, 225, 110, 255) },
};
constexpr int kSongCount = sizeof(kSongs) / sizeof(kSongs[0]);

// The five notes from low to high (D4 F4 A4 B4 D5) are played with A, C-Down, C-Right, C-Left and C-Up for now.
// The index is the same one the game uses for its ocarina buttons.

bool sSongbookOpen = false;
int sSelected = 0;
bool sPrevZ = false;
int sPrevStickDir = 0;
bool sPrevLeft = false;
bool sPrevRight = false;
bool sPrevA = false;
double sDemoStart = -1.0; // when "Listen to the melody" began, -1 when idle
constexpr double kDemoNoteTime = 0.55;

bool OcarinaOut() {
    if (gPlayState == nullptr) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING);
}

bool Known(const SongEntry& s) {
    return CHECK_QUEST_ITEM(s.questItem);
}

// A custom texture, if the project ships one with this name. Otherwise nullptr and a drawn version is used.
ImTextureID CustomTexture(const char* name) {
    auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
    if (gui == nullptr) {
        return nullptr;
    }
    const std::string path = std::string("__OTR__textures/remix/") + name;
    if (!gui->HasTextureByName(name)) {
        if (!ResourceMgr_FileExists(path.c_str())) {
            return nullptr;
        }
        gui->LoadGuiTexture(name, path);
    }
    return gui->GetTextureByName(name);
}

// The glyph of one of the game's ocarina buttons: 0 = A, 1 = C-Down, 2 = C-Right, 3 = C-Left, 4 = C-Up
void DrawNoteGlyph(ImDrawList* dl, ImVec2 c, float r, int button) {
    if (button == 0) {
        dl->AddCircleFilled(c, r, IM_COL32(70, 150, 255, 255), 24);
        dl->AddCircle(c, r, IM_COL32(255, 255, 255, 230), 24, 1.5f);
        ImFont* font = ImGui::GetFont();
        const float size = r * 1.3f;
        ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "A");
        dl->AddText(font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), "A");
        return;
    }
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
    dl->AddTriangleFilled(a, b, d, IM_COL32(255, 205, 60, 255));
    dl->AddTriangle(a, b, d, IM_COL32(255, 255, 255, 230), 1.5f);
}

// A hint chip: the ocarina buttons ("A", "CD", "CR", "CL", "CU") as glyphs, anything else as a small tag with text
float DrawChip(ImDrawList* dl, ImVec2 pos, float h, const char* label) {
    int note = -1;
    if (std::string(label) == "A") {
        note = 0;
    } else if (std::string(label) == "CD") {
        note = 1;
    } else if (std::string(label) == "CR") {
        note = 2;
    } else if (std::string(label) == "CL") {
        note = 3;
    } else if (std::string(label) == "CU") {
        note = 4;
    }
    if (note >= 0) {
        DrawNoteGlyph(dl, ImVec2(pos.x + h * 0.5f, pos.y + h * 0.5f), h * 0.5f, note);
        return h;
    }

    ImFont* font = ImGui::GetFont();
    const float size = h * 0.62f;
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
    const float w = std::max(h, ts.x + h * 0.5f);
    ImVec2 a(pos.x, pos.y);
    ImVec2 b(pos.x + w, pos.y + h);
    dl->AddRectFilled(a, b, IM_COL32(235, 235, 235, 245), h * 0.5f);
    dl->AddText(font, size, ImVec2(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), IM_COL32(30, 30, 30, 255),
                label);
    return w;
}

float TextWidth(float size, const char* text) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

void DrawText(ImDrawList* dl, float size, ImVec2 pos, ImU32 color, const char* text) {
    dl->AddText(ImGui::GetFont(), size, ImVec2(pos.x + 1.5f, pos.y + 1.5f), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(ImGui::GetFont(), size, pos, color, text);
}

// Bottom line of hints
void DrawHints(ImDrawList* dl, ImVec2 vpPos, ImVec2 vpSize, float unit, bool songbook) {
    const float h = 24.0f * unit;
    const float size = 20.0f * unit;
    const float gap = 36.0f * unit;
    const float y = vpPos.y + vpSize.y - 44.0f * unit;

    struct Hint {
        const char* text;
        const char* chips[5];
        int chipCount;
    };
    Hint hints[4];
    int n = 0;
    if (!songbook) {
        hints[n++] = { "Check songbook", { "ZL" }, 1 };
        hints[n++] = { "Play a note", { "CL", "CU", "CR", "CD", "A" }, 5 };
        hints[n++] = { "Cancel", { "B" }, 1 };
    } else {
        hints[n++] = { "Choose a song", { "<", ">" }, 2 };
        if (Known(kSongs[sSelected])) {
            hints[n++] = { "Listen to the melody", { "A" }, 1 };
        }
        hints[n++] = { "Close the songbook", { "ZL" }, 1 };
        hints[n++] = { "Cancel", { "B" }, 1 };
    }

    // Total width, to center the line
    float total = 0.0f;
    for (int i = 0; i < n; i++) {
        total += TextWidth(size, hints[i].text) + 8.0f * unit;
        for (int c = 0; c < hints[i].chipCount; c++) {
            {
                const std::string chip = hints[i].chips[c];
                const bool glyph = chip == "A" || chip == "CD" || chip == "CR" || chip == "CL" || chip == "CU";
                total += (glyph ? h : std::max(h, TextWidth(h * 0.62f, hints[i].chips[c]) + h * 0.5f)) + 4.0f * unit;
            }
        }
        if (i + 1 < n) {
            total += gap;
        }
    }
    // A soft band behind the line so it reads over any scene
    const float bandY = y - 14.0f * unit;
    dl->AddRectFilledMultiColor(ImVec2(vpPos.x, bandY), ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 150),
                                IM_COL32(0, 0, 0, 150));

    float x = vpPos.x + (vpSize.x - total) * 0.5f;
    for (int i = 0; i < n; i++) {
        DrawText(dl, size, ImVec2(x, y + (h - size) * 0.5f - 2.0f * unit), IM_COL32(255, 255, 255, 255),
                 hints[i].text);
        x += TextWidth(size, hints[i].text) + 8.0f * unit;
        for (int c = 0; c < hints[i].chipCount; c++) {
            x += DrawChip(dl, ImVec2(x, y), h, hints[i].chips[c]) + 4.0f * unit;
        }
        x += gap;
    }
}

// One song of the ring: its own icon if the project has one, otherwise a colored disc with a note
void DrawSongIcon(ImDrawList* dl, ImVec2 c, float r, int index, bool known, bool selected) {
    const SongEntry& s = kSongs[index];
    ImTextureID tex = nullptr;
    if (known) {
        tex = CustomTexture((std::string("RemixSongIcon") + std::to_string(index)).c_str());
    } else {
        tex = CustomTexture("RemixSongLocked");
    }
    if (tex != nullptr) {
        dl->AddImage(tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
    } else {
        dl->AddCircleFilled(c, r, IM_COL32(25, 25, 28, 235), 32);
        if (known) {
            dl->AddCircleFilled(c, r * 0.62f, s.color, 32);
            // A small note
            dl->AddCircleFilled(ImVec2(c.x - r * 0.12f, c.y + r * 0.18f), r * 0.17f, IM_COL32(25, 25, 28, 255), 16);
            dl->AddLine(ImVec2(c.x + r * 0.04f, c.y + r * 0.14f), ImVec2(c.x + r * 0.04f, c.y - r * 0.32f),
                        IM_COL32(25, 25, 28, 255), std::max(1.5f, r * 0.07f));
        } else {
            DrawText(dl, r * 1.1f, ImVec2(c.x - r * 0.22f, c.y - r * 0.55f), IM_COL32(150, 150, 150, 255), "?");
        }
    }
    if (selected) {
        dl->AddCircle(c, r + 3.0f, IM_COL32(255, 255, 255, 255), 40, 3.0f);
    }
}

void DrawSongbook(ImDrawList* dl, ImVec2 vpPos, ImVec2 vpSize, float unit) {
    // Soft dim so the ring and the parchment read over the scene
    dl->AddRectFilled(vpPos, ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y), IM_COL32(0, 0, 0, 110));

    const float cx = vpPos.x + vpSize.x * 0.5f;
    const float ringY = vpPos.y + vpSize.y * 0.30f;
    const float rx = vpSize.x * 0.30f;
    const float ry = vpSize.y * 0.12f;
    const float iconR = 25.0f * unit;

    for (int i = 0; i < kSongCount; i++) {
        float t = static_cast<float>(i) / (kSongCount - 1);
        float ang = 3.14159265f * (1.0f - t) * 0.82f + 3.14159265f * 0.09f; // an arc open to the bottom
        ImVec2 c(cx + rx * std::cos(ang), ringY - ry * std::sin(ang) + ry * 0.6f);
        DrawSongIcon(dl, c, iconR * (i == sSelected ? 1.18f : 1.0f), i, Known(kSongs[i]), i == sSelected);
    }

    // Parchment
    const SongEntry& s = kSongs[sSelected];
    const bool known = Known(s);
    const float pw = 560.0f * unit;
    const float ph = 215.0f * unit;
    ImVec2 p0(cx - pw * 0.5f, vpPos.y + vpSize.y * 0.50f);
    ImVec2 p1(p0.x + pw, p0.y + ph);
    ImTextureID parchment = CustomTexture("RemixParchment");
    if (parchment != nullptr) {
        dl->AddImage(parchment, p0, p1);
    } else {
        dl->AddRectFilled(p0, p1, IM_COL32(236, 224, 190, 245), 10.0f * unit);
        dl->AddRect(p0, p1, IM_COL32(150, 120, 70, 255), 10.0f * unit, 0, 3.0f);
        dl->AddRect(ImVec2(p0.x + 8.0f * unit, p0.y + 8.0f * unit), ImVec2(p1.x - 8.0f * unit, p1.y - 8.0f * unit),
                    IM_COL32(190, 160, 105, 255), 6.0f * unit, 0, 1.5f);
    }

    const ImU32 ink = IM_COL32(70, 50, 30, 255);
    const char* title = known ? s.name : "???";
    {
        const float size = 28.0f * unit;
        float w = TextWidth(size, title);
        dl->AddText(ImGui::GetFont(), size, ImVec2(cx - w * 0.5f, p0.y + 14.0f * unit), ink, title);
    }

    // The staff, with the notes in order. Five lines, low note at the bottom.
    const float staffL = p0.x + 90.0f * unit;
    const float staffR = p1.x - 40.0f * unit;
    const float staffTop = p0.y + 62.0f * unit;
    const float lineGap = 14.0f * unit;
    for (int i = 0; i < 5; i++) {
        float y = staffTop + lineGap * i;
        dl->AddLine(ImVec2(staffL - 40.0f * unit, y), ImVec2(staffR, y), IM_COL32(70, 50, 30, 200), 1.4f);
    }
    ImTextureID clef = CustomTexture("RemixClef");
    if (clef != nullptr) {
        dl->AddImage(clef, ImVec2(staffL - 52.0f * unit, staffTop - 16.0f * unit),
                     ImVec2(staffL - 8.0f * unit, staffTop + lineGap * 4 + 16.0f * unit));
    }

    if (known) {
        const OcarinaSongButtons& song = gOcarinaSongButtons[s.songId];
        const int count = std::max<int>(1, song.numButtons);
        const float step = (staffR - staffL - 20.0f * unit) / std::max(count, 6);
        for (int i = 0; i < song.numButtons; i++) {
            int btn = std::clamp<int>(song.buttonsIndex[i], 0, 4);
            // Low notes sit on the bottom line, high ones on the top line
            float y = staffTop + lineGap * (4 - btn);
            float x = staffL + step * (i + 0.5f);
            // While the melody is being shown, the current note lights up and pops
            float pop = 1.0f;
            bool lit = false;
            if (sDemoStart >= 0.0) {
                const double t = (ImGui::GetTime() - sDemoStart) / kDemoNoteTime;
                if (static_cast<int>(t) == i) {
                    lit = true;
                    pop = 1.0f + 0.35f * static_cast<float>(1.0 - (t - i));
                }
            }
            if (lit) {
                dl->AddCircleFilled(ImVec2(x, y), 20.0f * unit * pop, IM_COL32(255, 235, 140, 150), 24);
            }
            dl->AddCircleFilled(ImVec2(x, y), 13.0f * unit * pop, lit ? IM_COL32(70, 55, 30, 255) : IM_COL32(35, 30, 28, 255), 24);
            DrawNoteGlyph(dl, ImVec2(x, y), 9.0f * unit * pop, btn);
        }
    } else {
        const char* t = "You have not learned this song yet.";
        float w = TextWidth(18.0f * unit, t);
        dl->AddText(ImGui::GetFont(), 18.0f * unit, ImVec2(cx - w * 0.5f, staffTop + lineGap * 2 - 9.0f * unit), ink,
                    t);
    }

    if (known) {
        const float size = 18.0f * unit;
        float w = TextWidth(size, s.description);
        dl->AddText(ImGui::GetFont(), size, ImVec2(cx - w * 0.5f, p1.y - 38.0f * unit), ink, s.description);
    }
}

// A small line of text in the corner: what the microphone is doing
void DrawMicStatus(ImDrawList* dl, ImVec2 vpPos, ImVec2 vpSize, float unit) {
    const int status = RemixOcarinaMic_GetStatus();
    const char* text = "Waiting for audio...";
    ImU32 color = IM_COL32(255, 255, 255, 235);
    if (status == 3) {
        text = "Receiving audio...";
        color = IM_COL32(150, 240, 160, 255);
    } else if (status == 1) {
        text = "Microphone not available";
        color = IM_COL32(255, 150, 130, 255);
    }
    const float size = 16.0f * unit;
    const float w = TextWidth(size, text);
    ImVec2 p0(vpPos.x + vpSize.x - w - 34.0f * unit, vpPos.y + 14.0f * unit);
    ImVec2 p1(p0.x + w + 20.0f * unit, p0.y + size + 12.0f * unit);
    dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 140), 8.0f * unit);
    dl->AddText(ImGui::GetFont(), size, ImVec2(p0.x + 10.0f * unit, p0.y + 6.0f * unit), color, text);
}

// Songbook input: Z opens and closes it, left and right (stick or D-pad) move through the songs
void UpdateSongbookInput() {
    const Input& in = gPlayState->state.input[0];
    const bool z = (in.cur.button & BTN_Z) != 0;
    if (z && !sPrevZ) {
        sSongbookOpen = !sSongbookOpen;
        if (sSongbookOpen) {
            // Start on a song Link knows
            for (int i = 0; i < kSongCount; i++) {
                if (Known(kSongs[(sSelected + i) % kSongCount])) {
                    sSelected = (sSelected + i) % kSongCount;
                    break;
                }
            }
        }
    }
    sPrevZ = z;

    if (!sSongbookOpen) {
        sDemoStart = -1.0;
        sPrevA = false;
        return;
    }
    // A: listen to the melody of the selected song
    const bool a = (in.cur.button & BTN_A) != 0;
    if (a && !sPrevA && Known(kSongs[sSelected])) {
        sDemoStart = ImGui::GetTime();
    }
    sPrevA = a;
    if (sDemoStart >= 0.0) {
        const int notes = gOcarinaSongButtons[kSongs[sSelected].songId].numButtons;
        if ((ImGui::GetTime() - sDemoStart) / kDemoNoteTime >= notes) {
            sDemoStart = -1.0;
        }
    }
    const bool left = (in.cur.button & BTN_DLEFT) != 0;
    const bool right = (in.cur.button & BTN_DRIGHT) != 0;
    const int stick = in.cur.stick_x > 40 ? 1 : (in.cur.stick_x < -40 ? -1 : 0);
    int move = 0;
    if (stick != 0 && stick != sPrevStickDir) {
        move = stick;
    }
    if (left && !sPrevLeft) {
        move = -1;
    }
    if (right && !sPrevRight) {
        move = 1;
    }
    sPrevStickDir = stick;
    sPrevLeft = left;
    sPrevRight = right;
    if (move != 0) {
        sSelected = (sSelected + move + kSongCount) % kSongCount;
        sDemoStart = -1.0;
    }
}

void RemixOcarinaHud_Draw() {
    const bool out = OcarinaOut();
    // Microphone: opens only while the ocarina is out and the option is on
    RemixOcarinaMic_Update(out ? 1 : 0);

    if (!out) {
        sSongbookOpen = false;
        sPrevZ = false;
        return;
    }
    if (!CVarGetInteger(CVAR_ENHANCEMENT("RemixOcarinaHud"), 1)) {
        sSongbookOpen = false;
        return;
    }
    UpdateSongbookInput();

    // Drawn on the background list of the game's view: part of the screen, nothing to drag or resize
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    const float unit = vp->Size.y / 720.0f;

    if (sSongbookOpen) {
        DrawSongbook(dl, vp->Pos, vp->Size, unit);
    }
    if (CVarGetInteger(CVAR_SETTING("RemixOcarinaMic.Enabled"), 0)) {
        DrawMicStatus(dl, vp->Pos, vp->Size, unit);
    }
    DrawHints(dl, vp->Pos, vp->Size, unit, sSongbookOpen);
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
        RemixItemSelector_Draw();
    }
};

} // namespace

extern "C" int RemixOcarinaHud_SongbookOpen(void) {
    return sSongbookOpen ? 1 : 0;
}

std::shared_ptr<Ship::GuiWindow> RemixOcarinaHud_CreateWindow() {
    return std::make_shared<RemixOcarinaHudWindow>("gWindows.RemixOcarinaHud", "Remix Ocarina HUD");
}
