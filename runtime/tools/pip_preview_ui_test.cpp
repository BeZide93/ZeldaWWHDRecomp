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
    for (int i = 0; i < 3; ++i) {
        frame(true);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, true);
        frame(true);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
        frame(true);
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
    ImGui::DestroyContext();
}
