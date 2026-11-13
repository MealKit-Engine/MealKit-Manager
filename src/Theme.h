#pragma once

// Adobe Spectrum 2 tokens (dark theme), the one table for colors, type and shape. main.cpp applyStyle maps them onto ImGui.
// The Manager is an "occasional screen": icon + label buttons, one accent (primary) button per page.
#include <imgui.h>

namespace mkmanager::theme {

inline ImVec4 rgb(unsigned v, float a = 1.0f) {
    return ImVec4(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, a);
}

// Spectrum 2 dark gray scale, by job (1 = darkest). Values follow S2 gray-25..gray-900
//  1 background base (gutter, title bar)   2 layer 1 (panels)        3 layer 2 (controls at rest, popups)
//  4 hover                                 5 selected / pressed      6 borders, separators
//  7 strong border                         8 disabled text           9 icons at rest
// 10 placeholder                          11 secondary text         12 primary text
inline constexpr unsigned kGray[13] = {0,        0x111111, 0x1B1B1B, 0x222222, 0x2C2C2C, 0x323232, 0x393939,
                                       0x444444, 0x6D6D6D, 0x8A8A8A, 0x909090, 0xAFAFAF, 0xEBEBEB};
inline ImVec4 gray(int step) { return rgb(kGray[step < 1 ? 1 : step > 12 ? 12 : step]); }

// Accent (S2 blue): the page's one primary button, the selection bar, focus
inline ImVec4 accent() { return rgb(0x3B63FB); }
inline ImVec4 accentHover() { return rgb(0x4B75FF); }
inline ImVec4 accentDown() { return rgb(0x274DEA); }
inline ImVec4 focus() { return rgb(0x5D89FF); }
// Semantic (S2 notice / negative / positive)
inline ImVec4 warning() { return rgb(0xF29423); } // needs an action (upgrade, not installed)
inline ImVec4 danger() { return rgb(0xFF6250); }  // error, missing
inline ImVec4 good() { return rgb(0x2DB574); }    // done

// Shape: S2 rounds fields and cards, and draws buttons as pills
inline constexpr float kFieldRounding = 8.0f;
inline constexpr float kCardRounding = 10.0f;
inline constexpr float kPillRounding = 100.0f; // ImGui clamps to half the height

// Type ramp (S2 desktop: body 14 scaled up one step for Korean, headings bold)
inline constexpr float kCaption = 13.0f;
inline constexpr float kBody = 16.0f;
inline constexpr float kBodyLarge = 18.0f;
inline constexpr float kSubtitle = 22.0f;
inline constexpr float kTitle = 28.0f;

// Fonts loaded by main.cpp loadFonts (semibold falls back to regular)
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
};
inline Fonts& fonts() {
    static Fonts f;
    return f;
}

// Scoped font + size: theme::FontScope title(theme::kTitle, true);
struct FontScope {
    FontScope(float size, bool strong = false) { ImGui::PushFont(strong && fonts().semibold ? fonts().semibold : fonts().regular, size); }
    ~FontScope() { ImGui::PopFont(); }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
};

} // namespace mkmanager::theme
