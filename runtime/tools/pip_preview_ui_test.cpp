// Exercise the real diagram's ImGui pointer and D-pad handling without a game
// or GPU.
#include "../src/overlay/overlay.cpp"
#include <cassert>
void log_msg(const char *, ...) {}
namespace input {
void set_touch(bool, float, float) {}
} // namespace input
namespace aspect {
void set_window_aspect(float) {}
} // namespace aspect
namespace hostui {
void post(std::function<void()> f) { f(); }
void save_pip_position() {}
} // namespace hostui
int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    gfx::g_pip_anchor = gfx::kPipBR;
    auto frame = [&](bool focus = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 500));
        ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::SetCursorScreenPos(ImVec2(50, 50));
        if (focus) {
            auto *window = ImGui::GetCurrentWindow();
            ImGui::SetFocusID(window->GetID("Placement diagram"), window);
            ImGui::GetCurrentContext()->NavCursorVisible = true;
        }
        overlay::pip_diagram();
        ImGui::End();
        overlay::pip_live_drag();
        ImGui::Render();
    };
    frame();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(50 + 240 * 0.4f, 50 + 135 * 0.4f);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    assert(gfx::g_pip_anchor == gfx::kPipCustom);
    assert(std::fabs(gfx::g_pip_x - 0.4f) < 0.002f);
    assert(std::fabs(gfx::g_pip_y - 0.4f) < 0.002f);
    io.AddMouseButtonEvent(0, false);
    frame();
    frame(true);
    for (int i = 0; i < 3; ++i) {
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, true);
        frame();
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
        frame();
    }
    assert(std::fabs(gfx::g_pip_x - 0.43f) < 0.002f);
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    gfx::g_pip_preview = true;
    gfx::g_pip_anchor = gfx::kPipBR;
    auto box = gfx::pip_box({0, 0, 1280, 720}, gfx::g_drc_aspect, gfx::g_pip_size, gfx::pip_position());
    io.AddMousePosEvent(box.x + box.w / 2, box.y + box.h / 2);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(1280 * 0.6f, 720 * 0.5f);
    frame();
    assert(gfx::g_pip_anchor == gfx::kPipCustom);
    assert(std::fabs(gfx::g_pip_x - 0.6f) < 0.002f && std::fabs(gfx::g_pip_y - 0.5f) < 0.002f);
    io.AddMouseButtonEvent(0, false);
    frame();
    gfx::g_pip_preview = false;
    auto percentFrame = [&](bool focus = false, bool opacity = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 500));
        ImGui::Begin("percent", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::SetCursorScreenPos(ImVec2(50, 50));
        if (focus) {
            auto* window = ImGui::GetCurrentWindow();
            ImGui::FocusWindow(window);
            ImGui::SetFocusID(window->GetID(opacity ? "Opacity (%)" : "Size (%)"), window);
            ImGui::GetCurrentContext()->NavCursorVisible = true;
        }
        ImGui::SetNextItemWidth(200);
        if (opacity)
            overlay::pip_percent("Opacity (%)", "Exact opacity (%)", gfx::g_pip_opacity, 20, 100,
                                 [](float value) { gfx::g_pip_opacity = value; });
        else
            overlay::pip_percent("Size (%)", "Exact size (%)", gfx::g_pip_size, 10, 50,
                                 [](float value) { gfx::g_pip_size = value; });
        ImGui::End();
        ImGui::Render();
    };
    percentFrame(true);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true); percentFrame();
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false); percentFrame();
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, true); percentFrame();
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false); percentFrame();
    assert(std::fabs(gfx::g_pip_size - 0.26f) < 0.002f);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true); percentFrame();
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false); percentFrame();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(150, 60); percentFrame();
    io.AddMouseButtonEvent(0, true); percentFrame();
    io.AddMouseButtonEvent(0, false); percentFrame();
    assert(std::fabs(gfx::g_pip_size - 0.30f) < 0.002f);
    // The touch-accessible exact field accepts a typed value and clamps it.
    auto typeSize = [&](const char* text) {
        io.AddMousePosEvent(65, 50 + ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight() / 2); percentFrame();
        io.AddMouseButtonEvent(0, true); percentFrame();
        io.AddMouseButtonEvent(0, false); percentFrame();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true); percentFrame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false); percentFrame();
        io.AddInputCharactersUTF8(text); percentFrame();
        io.AddKeyEvent(ImGuiKey_Enter, true); percentFrame();
        io.AddKeyEvent(ImGuiKey_Enter, false); percentFrame();
    };
    typeSize("37");
    assert(std::fabs(gfx::g_pip_size - 0.37f) < 0.002f);
    typeSize("99");
    assert(gfx::g_pip_size == 0.5f);
    percentFrame(false, true);
    io.AddMousePosEvent(150, 60); percentFrame(false, true);
    io.AddMouseButtonEvent(0, true); percentFrame(false, true);
    io.AddMouseButtonEvent(0, false); percentFrame(false, true);
    assert(std::fabs(gfx::g_pip_opacity - 0.60f) < 0.002f);
    percentFrame(true, true);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true); percentFrame(false, true);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false); percentFrame(false, true);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, true); percentFrame(false, true);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false); percentFrame(false, true);
    assert(std::fabs(gfx::g_pip_opacity - 0.61f) < 0.002f);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true); percentFrame(false, true);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false); percentFrame(false, true);
    // Use the production Android control height and every UI scale preset.
    auto& style = ImGui::GetStyle();
    style.FontSizeBase = 17;
    style.FramePadding = ImVec2(10, 16);
    gfx::g_pip_size = 0.25f;
    for (int preset = 0; preset < 4; ++preset) {
        const float scale = mobile_ui::scale(400, 2.5f, preset);
        io.DisplaySize = ImVec2(1920 / scale, 1080 / scale);
        io.DisplayFramebufferScale = ImVec2(scale, scale);
        frame();
        io.AddMousePosEvent(50 + 240 * 0.4f, 50 + 135 * 0.4f); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
        assert(std::fabs(gfx::g_pip_x - 0.4f) < 0.002f && std::fabs(gfx::g_pip_y - 0.4f) < 0.002f);
    }
    percentFrame();
    typeSize("42");
    assert(std::fabs(gfx::g_pip_size - 0.42f) < 0.002f);
    ImGui::DestroyContext();
}
