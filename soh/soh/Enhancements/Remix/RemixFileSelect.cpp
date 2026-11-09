#include "RemixFileSelect.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

#include <fast/Fast3dGui.h>
#include <ship/Context.h>
#include <imgui.h>
#include <ship/window/gui/GuiWindow.h>

#include "soh/SaveManager.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/util.h"

namespace {

enum class Screen { Main, NewKind, Section, List, Confirm, Delete };

struct Entry {
    int slot;
    const SaveFileMetaInfo* meta;
    bool compatible;
};

struct State {
    bool visible = false;
    Screen screen = Screen::Main;
    int mainIndex = 0;
    int listIndex = 0;
    int confirmIndex = 0; // 0 = Start, 1 = Cancel
    int deleteIndex = 0;  // 0 = Cancel, 1 = Delete
    int slotChosen = -1;
    bool scrollToCursor = false;
    float noticeTimer = 0.0f;
    std::string notice;
    int kindIndex = 0;    // New Game: which kind of game
    int sectionIndex = 0; // Continue: which section
    int category = 0;     // Continue: section being listed
    int pendingAction = REMIX_FS_NONE;
    int pendingSlot = -1;
    int pendingKind = -1;
    int logoAlpha = 0; // 0..255, the title screen fades the logo in
    int fadeAlpha = 0; // 0..255 black over everything, the title screen fades out with it
    bool titleHost = false; // the menu is on the title screen, not on the file select
    int sfx = REMIX_FS_SFX_NONE;
};

State sState;

const ImVec4 kGold = ImVec4(0.96f, 0.82f, 0.40f, 1.0f);
const ImVec4 kText = ImVec4(0.92f, 0.92f, 0.90f, 1.0f);
const ImVec4 kDim = ImVec4(0.62f, 0.62f, 0.60f, 1.0f);

std::shared_ptr<Fast::Fast3dGui> GetGui() {
    return std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
}

// Character codes of the file name. Digits are 0-9 in both versions; the letters differ between the PAL and the NTSC
// numbering, and each file remembers which one it was written with.
std::string DecodeName(const SaveFileMetaInfo& meta) {
    const bool pal = meta.filenameLanguage == NAME_LANGUAGE_PAL;
    std::string out;
    for (int i = 0; i < 8; i++) {
        const int c = meta.playerName[i];
        char ch = '?';
        if (c <= 0x09) {
            ch = '0' + c;
        } else if (pal) {
            if (c <= 0x23) {
                ch = 'A' + (c - 0x0A);
            } else if (c <= 0x3D) {
                ch = 'a' + (c - 0x24);
            } else if (c == 0x3E) {
                ch = ' ';
            } else if (c == 0x3F) {
                ch = '-';
            } else if (c == 0x40) {
                ch = '.';
            }
        } else {
            if (c >= 0xAB && c <= 0xC4) {
                ch = 'A' + (c - 0xAB);
            } else if (c >= 0xC5 && c <= 0xDE) {
                ch = 'a' + (c - 0xC5);
            } else if (c == 0xDF) {
                ch = ' ';
            } else if (c == 0xE4) {
                ch = '-';
            } else if (c == 0xEA) {
                ch = '.';
            }
        }
        out.push_back(ch);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '?')) {
        out.pop_back();
    }
    return out.empty() ? "Link" : out;
}

std::string PlaceName(const SaveFileMetaInfo& meta) {
    // 0x6E is the number of scenes in the game. Anything else is a file that never recorded its place.
    if (meta.savedSceneNum < 0 || meta.savedSceneNum >= 0x6E) {
        return "Hyrule";
    }
    return SohUtils::GetSceneName(meta.savedSceneNum);
}

std::string DateText(const SaveFileMetaInfo& meta) {
    if (meta.savedAtUnix <= 0) {
        return "";
    }
    time_t t = static_cast<time_t>(meta.savedAtUnix);
    struct tm local;
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d  %H:%M", &local);
    return buf;
}

const char* KindText(int slot) {
    if (SaveManager::IsAutosaveSlot(slot)) {
        return "Autosave";
    }
    if (slot >= SaveManager::PlaythroughFiles) {
        return "Saved";
    }
    return "File";
}

// Continue is split in sections. Boss Rush has none: it does not keep saves.
enum Category { CAT_OOT, CAT_RANDOMIZER, CAT_SPEEDRUN, CAT_COUNT };

const char* kCategoryNames[CAT_COUNT] = { "Ocarina of Time", "Randomizer", "Speedrun" };

int CategoryOf(const SaveFileMetaInfo& meta) {
    switch (meta.quest) {
        case QUEST_RANDOMIZER:
            return CAT_RANDOMIZER;
        case QUEST_SPEEDRUN:
        case QUEST_SPEEDRUN_MASTER:
            return CAT_SPEEDRUN;
        case QUEST_BOSSRUSH:
            return -1;
        default:
            return CAT_OOT;
    }
}

// Files of one section (or of every section with category -1), newest first
std::vector<Entry> BuildList(int category = -1) {
    std::vector<Entry> list;
    for (int slot = 0; slot < SaveManager::MaxFiles; slot++) {
        const SaveFileMetaInfo& meta = SaveManager::Instance->fileMetaInfo[slot];
        if (!meta.valid) {
            continue;
        }
        const int cat = CategoryOf(meta);
        if (cat < 0 || (category >= 0 && cat != category)) {
            continue;
        }
        bool ok = true;
        if (meta.requiresMasterQuest) {
            ok = ok && ResourceMgr_GameHasMasterQuest();
        }
        if (meta.requiresOriginal) {
            ok = ok && ResourceMgr_GameHasOriginal();
        }
        list.push_back({ slot, &meta, ok });
    }
    std::stable_sort(list.begin(), list.end(),
                     [](const Entry& a, const Entry& b) { return a.meta->savedAtUnix > b.meta->savedAtUnix; });
    return list;
}

// Sections that have at least one file
std::vector<int> AvailableCategories() {
    std::vector<int> cats;
    for (int c = 0; c < CAT_COUNT; c++) {
        if (!BuildList(c).empty()) {
            cats.push_back(c);
        }
    }
    return cats;
}

struct Kind {
    const char* label;
    int quest;
};

std::vector<Kind> NewGameKinds() {
    std::vector<Kind> kinds;
    const bool original = ResourceMgr_GameHasOriginal();
    if (original) {
        kinds.push_back({ "Ocarina of Time", QUEST_NORMAL });
    }
    if (ResourceMgr_GameHasMasterQuest()) {
        kinds.push_back({ "Master Quest", QUEST_MASTER });
    }
    kinds.push_back({ "Randomizer", QUEST_RANDOMIZER });
    kinds.push_back({ "Boss Rush", QUEST_BOSSRUSH });
    kinds.push_back({ "Speedrun", original ? QUEST_SPEEDRUN : QUEST_SPEEDRUN_MASTER });
    return kinds;
}

int EmptyNewGameSlot() {
    for (int slot = 0; slot < SaveManager::PlaythroughFiles; slot++) {
        if (!SaveManager::Instance->fileMetaInfo[slot].valid) {
            return slot;
        }
    }
    return -1;
}

void SetNotice(const char* text) {
    sState.notice = text;
    sState.noticeTimer = 3.0f;
}

void Act(int action, int slot, int kind = -1) {
    sState.pendingAction = action;
    sState.pendingSlot = slot;
    sState.pendingKind = kind;
    sState.sfx = REMIX_FS_SFX_DECIDE;
}

void OpenList(int category) {
    sState.category = category;
    sState.screen = Screen::List;
    sState.listIndex = 0;
    sState.scrollToCursor = true;
    sState.sfx = REMIX_FS_SFX_DECIDE;
}

void ActivateMain() {
    switch (sState.mainIndex) {
        case 0:
            if (EmptyNewGameSlot() < 0) {
                SetNotice("The three New Game files are full. Open Continue and press Z on one to delete it.");
                sState.sfx = REMIX_FS_SFX_ERROR;
            } else {
                sState.screen = Screen::NewKind;
                sState.kindIndex = 0;
                sState.sfx = REMIX_FS_SFX_DECIDE;
            }
            break;
        case 1: {
            auto cats = AvailableCategories();
            if (cats.empty()) {
                SetNotice("There is no saved game to continue yet.");
                sState.sfx = REMIX_FS_SFX_ERROR;
            } else if (cats.size() == 1) {
                OpenList(cats[0]);
            } else {
                sState.screen = Screen::Section;
                sState.sectionIndex = 0;
                sState.sfx = REMIX_FS_SFX_DECIDE;
            }
            break;
        }
        default:
            if (sState.titleHost) {
                // On the title screen the settings menu opens right over Link and Epona
                auto menu = Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetMenu();
                if (menu != nullptr) {
                    menu->Show();
                }
                sState.sfx = REMIX_FS_SFX_DECIDE;
            } else {
                Act(REMIX_FS_OPTIONS, 0);
            }
            break;
    }
}

void ActivateNewKind() {
    auto kinds = NewGameKinds();
    int slot = EmptyNewGameSlot();
    if (slot < 0) {
        SetNotice("The three New Game files are full. Open Continue and press Z on one to delete it.");
        sState.sfx = REMIX_FS_SFX_ERROR;
        return;
    }
    Act(REMIX_FS_NEW_GAME, slot, kinds[std::clamp(sState.kindIndex, 0, (int)kinds.size() - 1)].quest);
}

void ActivateSection() {
    auto cats = AvailableCategories();
    if (cats.empty()) {
        sState.screen = Screen::Main;
        return;
    }
    OpenList(cats[std::clamp(sState.sectionIndex, 0, (int)cats.size() - 1)]);
}

void ActivateList(const std::vector<Entry>& list) {
    if (list.empty()) {
        return;
    }
    const Entry& e = list[std::clamp(sState.listIndex, 0, (int)list.size() - 1)];
    if (!e.compatible) {
        SetNotice("This file needs a game version that is not loaded.");
        sState.sfx = REMIX_FS_SFX_ERROR;
        return;
    }
    sState.slotChosen = e.slot;
    sState.confirmIndex = 0;
    sState.screen = Screen::Confirm;
    sState.sfx = REMIX_FS_SFX_DECIDE;
}

void Move(int& index, int count, int delta) {
    if (count <= 0) {
        return;
    }
    index = (index + delta + count) % count;
    sState.sfx = REMIX_FS_SFX_CURSOR;
    sState.scrollToCursor = true;
}

void DrawCentered(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 color, const char* text) {
    ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(center.x - sz.x * 0.5f, center.y - sz.y * 0.5f), color, text);
}

void DrawFooter(ImDrawList* dl, ImFont* font, ImVec2 vpPos, ImVec2 vpSize, float unit, const char* text) {
    float size = unit * 20.0f;
    ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(vpPos.x + vpSize.x * 0.5f - sz.x * 0.5f, vpPos.y + vpSize.y - unit * 46.0f),
                ImGui::GetColorU32(kDim), text);
}

} // namespace

void RemixFileSelect_SetVisible(int visible) {
    if (!visible && sState.visible) {
        sState.screen = Screen::Main;
    }
    sState.visible = visible != 0;
}

void RemixFileSelect_SetHost(int title) {
    sState.titleHost = title != 0;
}

void RemixFileSelect_SetFade(int alpha) {
    sState.fadeAlpha = std::clamp(alpha, 0, 255);
}

int RemixFileSelect_PendingAction(void) {
    return sState.pendingAction;
}

void RemixFileSelect_SetLogoAlpha(int alpha) {
    sState.logoAlpha = std::clamp(alpha, 0, 255);
}

int RemixFileSelect_HasPending(void) {
    return sState.pendingAction != REMIX_FS_NONE;
}

// Back to the first screen. A chosen action is kept: it is made in the title screen and carried out by the file select.
void RemixFileSelect_Reset(void) {
    State fresh;
    fresh.pendingAction = sState.pendingAction;
    fresh.pendingSlot = sState.pendingSlot;
    fresh.pendingKind = sState.pendingKind;
    sState = fresh;
}

int RemixFileSelect_Input(int up, int down, int left, int right, int confirm, int back, int erase) {
    (void)left;
    (void)right;
    sState.sfx = REMIX_FS_SFX_NONE;
    if (!sState.visible || SaveManager::Instance == nullptr || sState.pendingAction != REMIX_FS_NONE) {
        return REMIX_FS_SFX_NONE;
    }
    // The settings menu is open over the title: it has the input, not this menu
    if (sState.titleHost && Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetMenuOrMenubarVisible()) {
        return REMIX_FS_SFX_NONE;
    }

    switch (sState.screen) {
        case Screen::Main:
            if (up) {
                Move(sState.mainIndex, 3, -1);
            } else if (down) {
                Move(sState.mainIndex, 3, 1);
            } else if (confirm) {
                ActivateMain();
            }
            break;
        case Screen::NewKind: {
            const int count = (int)NewGameKinds().size();
            if (up) {
                Move(sState.kindIndex, count, -1);
            } else if (down) {
                Move(sState.kindIndex, count, 1);
            } else if (confirm) {
                ActivateNewKind();
            } else if (back) {
                sState.screen = Screen::Main;
                sState.sfx = REMIX_FS_SFX_CLOSE;
            }
            break;
        }
        case Screen::Section: {
            const int count = (int)AvailableCategories().size();
            if (up) {
                Move(sState.sectionIndex, count, -1);
            } else if (down) {
                Move(sState.sectionIndex, count, 1);
            } else if (confirm) {
                ActivateSection();
            } else if (back) {
                sState.screen = Screen::Main;
                sState.sfx = REMIX_FS_SFX_CLOSE;
            }
            break;
        }
        case Screen::List: {
            auto list = BuildList(sState.category);
            if (list.empty()) {
                sState.screen = Screen::Main;
                break;
            }
            sState.listIndex = std::clamp(sState.listIndex, 0, (int)list.size() - 1);
            if (up) {
                Move(sState.listIndex, (int)list.size(), -1);
            } else if (down) {
                Move(sState.listIndex, (int)list.size(), 1);
            } else if (confirm) {
                ActivateList(list);
            } else if (erase) {
                sState.slotChosen = list[sState.listIndex].slot;
                sState.deleteIndex = 0;
                sState.screen = Screen::Delete;
                sState.sfx = REMIX_FS_SFX_DECIDE;
            } else if (back) {
                sState.screen = AvailableCategories().size() > 1 ? Screen::Section : Screen::Main;
                sState.sfx = REMIX_FS_SFX_CLOSE;
            }
            break;
        }
        case Screen::Confirm:
            if (up || down) {
                sState.confirmIndex ^= 1;
                sState.sfx = REMIX_FS_SFX_CURSOR;
            } else if (confirm) {
                if (sState.confirmIndex == 0) {
                    Act(REMIX_FS_LOAD, sState.slotChosen);
                } else {
                    sState.screen = Screen::List;
                    sState.sfx = REMIX_FS_SFX_CLOSE;
                }
            } else if (back) {
                sState.screen = Screen::List;
                sState.sfx = REMIX_FS_SFX_CLOSE;
            }
            break;
        case Screen::Delete:
            if (up || down) {
                sState.deleteIndex ^= 1;
                sState.sfx = REMIX_FS_SFX_CURSOR;
            } else if (confirm) {
                if (sState.deleteIndex == 1) {
                    SaveManager::Instance->DeleteZeldaFile(sState.slotChosen);
                    sState.sfx = REMIX_FS_SFX_DECIDE;
                }
                if (sState.deleteIndex == 0) {
                    sState.sfx = REMIX_FS_SFX_CLOSE;
                }
                // The section may have run out of files
                sState.screen = BuildList(sState.category).empty() ? Screen::Main : Screen::List;
            } else if (back) {
                sState.screen = Screen::List;
                sState.sfx = REMIX_FS_SFX_CLOSE;
            }
            break;
    }
    return sState.sfx;
}

int RemixFileSelect_PollAction(int* slot, int* kind) {
    int action = sState.pendingAction;
    if (slot != nullptr) {
        *slot = sState.pendingSlot;
    }
    if (kind != nullptr) {
        *kind = sState.pendingKind;
    }
    sState.pendingAction = REMIX_FS_NONE;
    sState.pendingSlot = -1;
    sState.pendingKind = -1;
    return action;
}

// Drawn every rendered frame by the GUI. The window is registered in SohGui.cpp.
void RemixFileSelect_Draw() {
    if ((!sState.visible && sState.logoAlpha <= 0 && sState.fadeAlpha <= 0) || SaveManager::Instance == nullptr) {
        return;
    }
    auto gui = GetGui();
    if (gui == nullptr) {
        return;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("Remix File Select", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float unit = vp->Size.y / 720.0f; // everything is laid out for 720p and scaled from there
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    if (sState.noticeTimer > 0.0f) {
        sState.noticeTimer -= ImGui::GetIO().DeltaTime;
    }

    if (sState.screen == Screen::Main || sState.screen == Screen::NewKind || sState.screen == Screen::Section) {
        // Logo
        const std::string logoName = "RemixTitleLogo";
        if (!gui->HasTextureByName(logoName)) {
            gui->LoadGuiTexture(logoName, "__OTR__textures/remix/RemixTitleLogo");
        }
        ImTextureID logo = gui->GetTextureByName(logoName);
        if (logo != nullptr && sState.logoAlpha > 0) {
            const float aspect = 1139.0f / 850.0f;
            float h = vp->Size.y * 0.46f;
            float w = h * aspect;
            ImVec2 p0(vp->Pos.x + vp->Size.x * 0.5f - w * 0.5f, vp->Pos.y + vp->Size.y * 0.06f);
            dl->AddImage(logo, p0, ImVec2(p0.x + w, p0.y + h), ImVec2(0, 0), ImVec2(1, 1),
                         IM_COL32(255, 255, 255, sState.logoAlpha));
        }

        std::vector<std::string> labels;
        std::vector<bool> enabled;
        int* index = &sState.mainIndex;
        const char* footer = "A / Start: Select";
        if (sState.screen == Screen::Main) {
            labels = { "New Game", "Continue", "Options" };
            enabled = { true, !AvailableCategories().empty(), true };
        } else if (sState.screen == Screen::NewKind) {
            for (const Kind& k : NewGameKinds()) {
                labels.push_back(k.label);
                enabled.push_back(true);
            }
            index = &sState.kindIndex;
            footer = "A / Start: Select     B: Back";
        } else {
            for (int c : AvailableCategories()) {
                labels.push_back(kCategoryNames[c]);
                enabled.push_back(true);
            }
            index = &sState.sectionIndex;
            footer = "A / Start: Select     B: Back";
        }

        if (sState.visible) {
            const int count = (int)labels.size();
            *index = std::clamp(*index, 0, std::max(0, count - 1));
            const float step = unit * (count > 4 ? 46.0f : 56.0f);
            float y = vp->Pos.y + vp->Size.y * 0.66f;
            for (int i = 0; i < count; i++) {
                const bool selected = *index == i;
                const bool disabled = !enabled[i];
                const float size = unit * (selected ? 38.0f : 32.0f);
                ImVec2 center(vp->Pos.x + vp->Size.x * 0.5f, y + step * i);
                ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, labels[i].c_str());
                ImVec2 a(center.x - unit * 220.0f, center.y - step * 0.5f);
                ImVec2 b(center.x + unit * 220.0f, center.y + step * 0.5f);
                if (mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y) {
                    if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) {
                        *index = i;
                    }
                    if (clicked) {
                        *index = i;
                        if (sState.screen == Screen::Main) {
                            ActivateMain();
                        } else if (sState.screen == Screen::NewKind) {
                            ActivateNewKind();
                        } else {
                            ActivateSection();
                        }
                    }
                }
                if (selected) {
                    dl->AddRectFilled(ImVec2(center.x - sz.x * 0.5f - unit * 36.0f, center.y - step * 0.42f),
                                      ImVec2(center.x + sz.x * 0.5f + unit * 36.0f, center.y + step * 0.42f),
                                      IM_COL32(0, 0, 0, 120), unit * 8.0f);
                }
                ImVec4 color = disabled ? kDim : (selected ? kGold : kText);
                DrawCentered(dl, font, size, center, ImGui::GetColorU32(color), labels[i].c_str());
            }
            DrawFooter(dl, font, vp->Pos, vp->Size, unit, footer);
        }
    } else {
        // Dim the scene behind the list
        dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(8, 12, 24, 190));

        const float panelW = std::min(vp->Size.x * 0.86f, unit * 1000.0f);
        const float panelX = vp->Pos.x + (vp->Size.x - panelW) * 0.5f;
        DrawCentered(dl, font, unit * 40.0f, ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + unit * 58.0f),
                     ImGui::GetColorU32(kGold), kCategoryNames[std::clamp(sState.category, 0, (int)CAT_COUNT - 1)]);

        auto list = BuildList(sState.category);
        const float rowH = unit * 78.0f;
        const float listTop = vp->Pos.y + unit * 110.0f;
        const float listBottom = vp->Pos.y + vp->Size.y - unit * 80.0f;
        const int visibleRows = std::max(1, (int)((listBottom - listTop) / rowH));
        sState.listIndex = std::clamp(sState.listIndex, 0, std::max(0, (int)list.size() - 1));
        static int firstRow = 0;
        if (sState.listIndex < firstRow) {
            firstRow = sState.listIndex;
        }
        if (sState.listIndex >= firstRow + visibleRows) {
            firstRow = sState.listIndex - visibleRows + 1;
        }
        firstRow = std::clamp(firstRow, 0, std::max(0, (int)list.size() - visibleRows));
        sState.scrollToCursor = false;

        dl->PushClipRect(ImVec2(panelX, listTop), ImVec2(panelX + panelW, listBottom), true);
        for (int row = 0; row < visibleRows && firstRow + row < (int)list.size(); row++) {
            const int i = firstRow + row;
            const Entry& e = list[i];
            const bool selected = sState.listIndex == i && sState.screen == Screen::List;
            ImVec2 a(panelX, listTop + rowH * row);
            ImVec2 b(panelX + panelW, a.y + rowH - unit * 8.0f);
            if (sState.screen == Screen::List && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y &&
                mouse.y <= b.y) {
                if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) {
                    sState.listIndex = i;
                }
                if (clicked) {
                    sState.listIndex = i;
                    ActivateList(list);
                }
            }
            const bool lit = sState.listIndex == i;
            dl->AddRectFilled(a, b, lit ? IM_COL32(240, 205, 100, 60) : IM_COL32(255, 255, 255, 22), unit * 8.0f);
            if (lit) {
                dl->AddRect(a, b, IM_COL32(240, 205, 100, 230), unit * 8.0f, 0, unit * 2.5f);
            }
            (void)selected;

            const ImVec4 main = e.compatible ? (lit ? kGold : kText) : kDim;
            const float pad = unit * 22.0f;
            std::string title = PlaceName(*e.meta);
            dl->AddText(font, unit * 28.0f, ImVec2(a.x + pad, a.y + unit * 8.0f), ImGui::GetColorU32(main),
                        title.c_str());
            std::string info = DecodeName(*e.meta) + "   " + std::to_string(e.meta->health / 16) + " hearts";
            dl->AddText(font, unit * 21.0f, ImVec2(a.x + pad, a.y + unit * 42.0f), ImGui::GetColorU32(kDim),
                        info.c_str());

            // Right side: autosave tag and date
            std::string date = DateText(*e.meta);
            ImVec2 dsz = font->CalcTextSizeA(unit * 22.0f, FLT_MAX, 0.0f, date.c_str());
            dl->AddText(font, unit * 22.0f, ImVec2(b.x - pad - dsz.x, a.y + unit * 10.0f), ImGui::GetColorU32(kText),
                        date.c_str());
            std::string kind = KindText(e.slot);
            if (e.slot < SaveManager::PlaythroughFiles) {
                kind += " " + std::to_string(e.slot + 1);
            }
            ImVec2 ksz = font->CalcTextSizeA(unit * 20.0f, FLT_MAX, 0.0f, kind.c_str());
            ImVec2 kp(b.x - pad - ksz.x, a.y + unit * 42.0f);
            if (SaveManager::IsAutosaveSlot(e.slot)) {
                dl->AddRectFilled(ImVec2(kp.x - unit * 10.0f, kp.y - unit * 2.0f),
                                  ImVec2(kp.x + ksz.x + unit * 10.0f, kp.y + ksz.y + unit * 2.0f),
                                  IM_COL32(70, 130, 220, 200), unit * 6.0f);
                dl->AddText(font, unit * 20.0f, kp, IM_COL32(255, 255, 255, 255), kind.c_str());
            } else {
                dl->AddText(font, unit * 20.0f, kp, ImGui::GetColorU32(kDim), kind.c_str());
            }
        }
        dl->PopClipRect();

        DrawFooter(dl, font, vp->Pos, vp->Size, unit, "A / Start: Select     B: Back     Z: Delete");

        if (sState.screen == Screen::Confirm || sState.screen == Screen::Delete) {
            const bool del = sState.screen == Screen::Delete;
            dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                              IM_COL32(0, 0, 0, 150));
            ImVec2 c(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
            ImVec2 a(c.x - unit * 300.0f, c.y - unit * 130.0f);
            ImVec2 b(c.x + unit * 300.0f, c.y + unit * 130.0f);
            dl->AddRectFilled(a, b, IM_COL32(22, 30, 52, 245), unit * 12.0f);
            dl->AddRect(a, b, IM_COL32(240, 205, 100, 220), unit * 12.0f, 0, unit * 2.5f);
            const char* question = del ? "Delete this file for good?" : "Start from this file?";
            DrawCentered(dl, font, unit * 32.0f, ImVec2(c.x, c.y - unit * 70.0f), ImGui::GetColorU32(kText),
                         question);
            const char* yes = del ? "Delete" : "Start";
            const char* no = "Cancel";
            const int sel = del ? sState.deleteIndex : sState.confirmIndex;
            // Start is the first button for loading, Cancel is the first for deleting so it is the safe default
            const char* labels[2] = { del ? no : yes, del ? yes : no };
            for (int i = 0; i < 2; i++) {
                const bool s = (del ? (sel == (i == 0 ? 0 : 1)) : (sel == i));
                ImVec2 bc(c.x + (i == 0 ? -unit * 120.0f : unit * 120.0f), c.y + unit * 50.0f);
                ImVec2 ba(bc.x - unit * 100.0f, bc.y - unit * 28.0f);
                ImVec2 bb(bc.x + unit * 100.0f, bc.y + unit * 28.0f);
                if (mouse.x >= ba.x && mouse.x <= bb.x && mouse.y >= ba.y && mouse.y <= bb.y && clicked) {
                    if (del) {
                        if (i == 1) {
                            SaveManager::Instance->DeleteZeldaFile(sState.slotChosen);
                        }
                        sState.screen = BuildList(sState.category).empty() ? Screen::Main : Screen::List;
                    } else if (i == 0) {
                        Act(REMIX_FS_LOAD, sState.slotChosen);
                    } else {
                        sState.screen = Screen::List;
                    }
                }
                dl->AddRectFilled(ba, bb, s ? IM_COL32(240, 205, 100, 70) : IM_COL32(255, 255, 255, 20),
                                  unit * 8.0f);
                if (s) {
                    dl->AddRect(ba, bb, IM_COL32(240, 205, 100, 230), unit * 8.0f, 0, unit * 2.5f);
                }
                DrawCentered(dl, font, unit * 28.0f, bc, ImGui::GetColorU32(s ? kGold : kText), labels[i]);
            }
        }
    }

    if (sState.noticeTimer > 0.0f && !sState.notice.empty()) {
        ImVec2 sz = font->CalcTextSizeA(unit * 24.0f, FLT_MAX, 0.0f, sState.notice.c_str());
        ImVec2 c(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y - unit * 100.0f);
        dl->AddRectFilled(ImVec2(c.x - sz.x * 0.5f - unit * 20.0f, c.y - sz.y * 0.5f - unit * 10.0f),
                          ImVec2(c.x + sz.x * 0.5f + unit * 20.0f, c.y + sz.y * 0.5f + unit * 10.0f),
                          IM_COL32(0, 0, 0, 200), unit * 8.0f);
        DrawCentered(dl, font, unit * 24.0f, c, ImGui::GetColorU32(kText), sState.notice.c_str());
    }

    if (sState.fadeAlpha > 0) {
        dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                          IM_COL32(0, 0, 0, sState.fadeAlpha));
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

namespace {
class RemixFileSelectWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }
    void Draw() override {
        RemixFileSelect_Draw();
    }
};
} // namespace

std::shared_ptr<Ship::GuiWindow> RemixFileSelect_CreateWindow() {
    return std::make_shared<RemixFileSelectWindow>("gWindows.RemixFileSelect", "Remix File Select");
}
