#include "RemixBootLogo.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <fast/Fast3dGui.h>
#include <ship/Context.h>
#include <imgui.h>
#include <ship/window/gui/GuiWindow.h>

namespace {

int sAlpha = 0;
int sFramesSet = 0; // frames since the last SetAlpha, so the logo disappears if the boot states stop calling it

constexpr float kAspect = 1800.0f / 625.0f;

class RemixBootLogoWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }
    void Draw() override {
        if (sAlpha <= 0) {
            return;
        }
        if (++sFramesSet > 3) {
            sAlpha = 0;
            return;
        }
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        if (gui == nullptr) {
            return;
        }
        const std::string name = "RemixBootLogo";
        if (!gui->HasTextureByName(name)) {
            gui->LoadGuiTexture(name, "__OTR__textures/remix/RemixBootLogo");
        }
        ImTextureID tex = gui->GetTextureByName(name);
        if (tex == nullptr) {
            return;
        }
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
        const float t = std::clamp(sAlpha / 255.0f, 0.0f, 1.0f);
        // Eases in with a small zoom and a little rise, and goes out the same way
        const float ease = t * t * (3.0f - 2.0f * t);
        const float scale = 0.90f + 0.10f * ease;
        float w = vp->Size.x * 0.62f * scale;
        float h = w / kAspect;
        if (h > vp->Size.y * 0.7f) {
            h = vp->Size.y * 0.7f * scale;
            w = h * kAspect;
        }
        const float cx = vp->Pos.x + vp->Size.x * 0.5f;
        const float cy = vp->Pos.y + vp->Size.y * 0.5f + (1.0f - ease) * vp->Size.y * 0.03f;
        dl->AddImage(tex, ImVec2(cx - w * 0.5f, cy - h * 0.5f), ImVec2(cx + w * 0.5f, cy + h * 0.5f), ImVec2(0, 0),
                     ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(255 * ease)));
    }
};

} // namespace

extern "C" void RemixBootLogo_SetAlpha(int alpha) {
    sAlpha = std::clamp(alpha, 0, 255);
    sFramesSet = 0;
}

std::shared_ptr<Ship::GuiWindow> RemixBootLogo_CreateWindow() {
    return std::make_shared<RemixBootLogoWindow>("gWindows.RemixBootLogo", "Remix Boot Logo");
}
