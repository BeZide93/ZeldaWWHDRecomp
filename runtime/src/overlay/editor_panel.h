// The world editor's Editor tab of the settings overlay and its switch under Mods > More
// (docs/mod-editor.md). Everything it changes goes through the command layer (editor/commands.h).
#pragma once

namespace editor_ui {
void register_tab();  // main thread, at start-up (extensions.h): the tab shows while the editor is on
void setting();       // Mods > More > World editor: the switch
void draw();          // the tab's page (render thread)
}  // namespace editor_ui
