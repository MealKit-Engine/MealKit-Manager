#pragma once

// Tokens of engine docs/TOOL_DESIGN.md 3.1 (colors from applyEditorStyle in main.cpp, the type ramp, spacing).
// The Manager is an "occasional screen": icon + label buttons, one primary button per page.
#include <imgui.h>

namespace mkmanager::theme {

inline ImVec4 rgb(unsigned v, float a = 1.0f) {
    return ImVec4(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, a);
}

// Radix slateDark steps 1..12
inline ImVec4 gray(int step) {
    static constexpr unsigned kSlate[13] = {0,        0x111113, 0x18191B, 0x212225, 0x272A2D, 0x2E3135, 0x363A3F,
                                            0x43484E, 0x5A6169, 0x696E77, 0x777B84, 0xB0B4BA, 0xEDEEF0};
    return rgb(kSlate[step < 1 ? 1 : step > 12 ? 12 : step]);
}

inline ImVec4 blue8() { return rgb(0x2870BD); }   // focus, the selection bar
inline ImVec4 blue9() { return rgb(0x0090FF); }   // primary button
inline ImVec4 blue10() { return rgb(0x3B9EFF); }  // primary button hovered
inline ImVec4 warning() { return rgb(0xFFCA16); } // needs an action (upgrade, not installed)
inline ImVec4 danger() { return rgb(0xFF6369); }  // error, missing
inline ImVec4 good() { return rgb(0x3DD68C); }    // done

// Type ramp (sizes before DPI scale; ImGui scales them with FontScaleDpi)
inline constexpr float kCaption = 13.0f;
inline constexpr float kBody = 16.0f;
inline constexpr float kBodyLarge = 18.0f;
inline constexpr float kSubtitle = 22.0f;
inline constexpr float kTitle = 30.0f;

// Fonts loaded by main.cpp loadFonts (semibold falls back to regular)
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
};
inline Fonts& fonts() {
    static Fonts f;
    return f;
}

// Scoped font + size: theme::Text title(theme::kTitle, true);
struct FontScope {
    FontScope(float size, bool strong = false) { ImGui::PushFont(strong && fonts().semibold ? fonts().semibold : fonts().regular, size); }
    ~FontScope() { ImGui::PopFont(); }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
};

} // namespace mkmanager::theme
