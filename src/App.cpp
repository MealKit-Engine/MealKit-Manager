#include "App.h"

#include "Logo.h"
#include "Strings.h"
#include "Theme.h"
#include "ToolIcons.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <mkcontract/User.h>

namespace mkmanager {
namespace {

// Checkbox and radio colors (engine docs/TOOL_DESIGN.md 3.1, 3.2): a gray 4 box on a gray 2 panel is about 1.25:1, so an unchecked
// box gets a gray 9 outline (gray 11 hovered) and a checked one the accent fill with a gray 12 mark. Same as MkRender tool/App.cpp
const ImVec4 kControlOutline(0x8A / 255.0f, 0x8A / 255.0f, 0x8A / 255.0f, 1.0f);      // gray 9
const ImVec4 kControlOutlineHover(0xAF / 255.0f, 0xAF / 255.0f, 0xAF / 255.0f, 1.0f); // gray 11
const ImVec4 kControlMark(0xEB / 255.0f, 0xEB / 255.0f, 0xEB / 255.0f, 1.0f);         // gray 12
const ImVec4 kAccent(0x3B / 255.0f, 0x63 / 255.0f, 0xFB / 255.0f, 1.0f);
const ImVec4 kAccentHover(0x4B / 255.0f, 0x75 / 255.0f, 0xFF / 255.0f, 1.0f);
const ImVec4 kAccentDown(0x27 / 255.0f, 0x4D / 255.0f, 0xEA / 255.0f, 1.0f);

// Pushes the fill and mark of a checkbox or radio button in the state it is drawn in; popControlColors after
void pushControlColors(bool on) {
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    const ImGuiStyle& s = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, on ? kAccent : clear);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, on ? kAccentHover : s.Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, on ? kAccentDown : s.Colors[ImGuiCol_FrameBgHovered]);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, kControlMark);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s.FrameRounding * 0.5f); // a 4 px box, not the round 8 px of fields
}

void popControlColors() {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

// Every checkbox goes through here, never ImGui::Checkbox (engine docs/TOOL_DESIGN.md 3.2)
bool checkbox(const char* label, bool* v) {
    const bool on = *v;
    pushControlColors(on);
    const bool changed = ImGui::Checkbox(label, v);
    const float rounding = ImGui::GetStyle().FrameRounding;
    popControlColors();
    if (!on) {
        // The box is the square at the start of the item (ImGui::Checkbox check_bb)
        const ImVec2 p = ImGui::GetItemRectMin();
        const float size = ImGui::GetFrameHeight();
        const float width = std::max(1.0f, ImGui::GetStyle().FontScaleDpi);
        const float inset = width * 0.5f;
        ImGui::GetWindowDrawList()->AddRect({p.x + inset, p.y + inset}, {p.x + size - inset, p.y + size - inset},
                                            ImGui::GetColorU32(ImGui::IsItemHovered() ? kControlOutlineHover : kControlOutline), rounding, 0, width);
    }
    return changed;
}

bool radioButton(const char* label, bool active) {
    pushControlColors(active);
    const bool pressed = ImGui::RadioButton(label, active);
    popControlColors();
    if (!active) {
        // Same circle as ImGui::RadioButton: radius (square - 1) / 2 around the square's center
        const ImVec2 p = ImGui::GetItemRectMin();
        const float size = ImGui::GetFrameHeight();
        const float width = std::max(1.0f, ImGui::GetStyle().FontScaleDpi);
        const ImVec2 center(std::floor(p.x + size * 0.5f + 0.5f), std::floor(p.y + size * 0.5f + 0.5f));
        ImGui::GetWindowDrawList()->AddCircle(center, (size - 1.0f) * 0.5f - width * 0.5f,
                                              ImGui::GetColorU32(ImGui::IsItemHovered() ? kControlOutlineHover : kControlOutline), 0, width);
    }
    return pressed;
}

// Follows the suite language when an Mk tool changes it in editor_settings.txt (engine docs/TOOL_DESIGN.md section 4 "Language").
// The first call only notes the file time, so --lang stays until the file changes
void followSuiteLanguage() {
    static std::filesystem::file_time_type seen{};
    static bool first = true;
    static Uint64 next = 0;
    const Uint64 now = SDL_GetTicks();
    if (now < next) return;
    next = now + 1000;
    const std::string path = mkcontract::userPath(mkcontract::kEditorSettings);
    std::error_code ec;
    const auto t = path.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
    if (first || t != seen) {
        if (!first) {
            const std::string code = mkcontract::editorSetting("language");
            if (!code.empty()) mkmanager::setLanguage(code);
        }
        first = false;
        seen = t;
    }
}

bool containsNoCase(const std::string& text, const char* needle) {
    if (!needle[0]) return true;
    auto it = std::search(text.begin(), text.end(), needle, needle + std::strlen(needle), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != text.end();
}

// The name becomes the exe name and the CMake target name (same rules as new_project.ps1)
bool validGameName(const char* name) {
    if (!std::isalpha(static_cast<unsigned char>(name[0]))) return false;
    for (const char* c = name; *c; ++c)
        if (!std::isalnum(static_cast<unsigned char>(*c)) && *c != '_') return false;
    return true;
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
}

std::string pathJoin(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '\\' || last == '/') ? a + b : a + "\\" + b;
}

// scripts\install.ps1, copied beside the exe by the build and cmake --install
std::string installerScript() {
    const char* base = SDL_GetBasePath();
    return pathJoin(base ? base : "", "install.ps1");
}

std::string defaultProjectLocation() {
    const std::vector<std::string> saved = readList(pathJoin(userDirectory(), "manager_settings.txt"));
    for (const std::string& line : saved)
        if (line.rfind("newProjectPath ", 0) == 0) return line.substr(15);
    if (const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) return pathJoin(docs, "MealKit");
    return {};
}

void saveProjectLocation(const std::string& location) {
    writeList(pathJoin(userDirectory(), "manager_settings.txt"), {"newProjectPath " + location});
}

void SDLCALL onFolderPicked(void* userdata, const char* const* filelist, int) {
    auto* target = static_cast<std::pair<std::mutex*, std::string*>*>(userdata);
    std::lock_guard<std::mutex> lock(*target->first);
    *target->second = (filelist && filelist[0]) ? filelist[0] : "";
}

std::pair<std::mutex*, std::string*> g_pickTarget;

} // namespace

void App::init(const std::string& extraEngine) {
    m_extraEngine = extraEngine;
    Engine probe;
    if (!extraEngine.empty() && readEngine(extraEngine, probe)) m_catalog.addEngine(extraEngine);
    reload();
    setLanguage(m_catalog.language());
    const std::string location = defaultProjectLocation();
    std::snprintf(m_newLocation, sizeof(m_newLocation), "%s", location.c_str());
    if (!m_catalog.projects().empty()) m_selected = m_catalog.projects()[0].directory;
}

void App::reload() { m_catalog.load(m_extraEngine); }

void App::frame() {
    followSuiteLanguage();
    takePickedFolder();
    takePickedIcon();
    if (m_job.takeFinished()) {
        if (m_jobChecksToolchain) {
            m_jobChecksToolchain = false;
            m_toolchain = parseToolchain(m_job.output());
        } else if (m_jobListsReleases) { // release list: shows in the [Engines] tab
            m_jobListsReleases = false;
            m_releases = parseReleaseList(m_job.output());
            m_managerRelease = parseManagerRelease(m_job.output());
            m_toolReleases = parseToolReleases(m_job.output());
            writeAvailableToolsFile();
            m_releasesError = m_job.exitCode() != 0 ? m_job.output() : std::string();
        } else {
            finishJob();
        }
    }
    if (!m_job.running() && !m_toolQueue.empty()) { // [Update All]: the next tool once the last one finished
        const std::string next = m_toolQueue.front();
        m_toolQueue.erase(m_toolQueue.begin());
        installTool(next);
    }
    if (!m_toolchainRequested && !m_job.running()) {
        m_toolchainRequested = true;
        if (m_job.start(tr("Toolchain check"), powershellArgs(installerScript(), {"check"}))) m_jobChecksToolchain = true;
    }
    // Release list at start too: the [Projects] tab offers the engine a project is missing
    if (!m_releasesFetched && !m_job.running() && !releaseIndex().empty()) {
        m_releasesFetched = true;
        if (m_job.start(tr("Engine list"), powershellArgs(installerScript(), {"list", "-Index", releaseIndex()}))) m_jobListsReleases = true;
    }
    if (!m_askEngine.empty() && m_releasesFetched && !m_jobListsReleases) {
        for (const Release& r : m_releases)
            if (r.version == m_askEngine) requestEngineInstall(r);
        m_askEngine.clear();
    }
    drawWindow();
}

// No C++ compiler: game code (the game DLL) can't be built here. Shown on the [Projects] and [Engines] tabs
void App::toolchainBanner() {
    if (!m_toolchain.checked || m_toolchain.compiler) return;
    const ImVec4 warn = theme::warning();
    ImGui::TextColored(warn, "%s", tr("No C++ compiler on this PC: game code can't be built, so projects can't open in the editor."));
    ImGui::TextUnformatted(tr("Install Visual Studio Build Tools with C++ (about 6 GB), from a terminal:"));
    static char line[256];
    std::snprintf(line, sizeof(line), "%s", kBuildToolsWinget);
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Check Again ").x * 2.6f);
    ImGui::InputText("##winget", line, sizeof(line), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (iconButton(kIconCopy, tr("Copy"))) SDL_SetClipboardText(kBuildToolsWinget);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_job.running());
    if (iconButton(kIconRefresh, tr("Check Again"))) m_toolchainRequested = false;
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", tr("Without a compiler, use designer mode: open a project from the shared folder a programmer builds."));
    ImGui::Separator();
}

void App::finishJob() {
    const bool ok = m_job.exitCode() == 0;
    setStatus(ok ? trf("%s: done", m_job.label().c_str()) : trf("%s: failed (exit code %d)", m_job.label().c_str(), m_job.exitCode()), !ok);
    m_jobOutput = ok ? std::string() : m_job.output();
    if (ok && !m_jobAdd.empty()) {
        m_catalog.addRecent(m_jobAdd);
        m_selected = normalizeDirectory(m_jobAdd);
    }
    m_jobAdd.clear();
    if (!m_jobMoveFrom.empty()) {
        if (ok) {
            m_catalog.replaceProject(m_jobMoveFrom, m_jobMoveTo);
            m_selected = normalizeDirectory(m_jobMoveTo);
            setStatus(trf("Moved to %s", m_jobMoveTo.c_str()), false);
        }
        m_jobMoveFrom.clear();
        m_jobMoveTo.clear();
    }
    if (m_jobUpdatesManager) { // new files are in place: start the new Manager and quit this one
        m_jobUpdatesManager = false;
        std::string error;
        if (ok && launchDetached({mkcontract::joinPath(mkcontract::joinPath(userDirectory(), "manager"), "MealKitManager.exe")}, error))
            quitRequested = true;
        else if (ok)
            setStatus(trf("Updated, but the new Manager didn't start: %s", error.c_str()), true);
    }
    if (!m_jobInstallsTool.empty()) {
        const std::string tool = m_jobInstallsTool;
        m_jobInstallsTool.clear();
        if (tool == m_openAfterInstall) {
            const std::string project = m_openAfterInstallProject;
            m_openAfterInstall.clear();
            m_openAfterInstallProject.clear();
            if (ok) {
                reload();
                if (const Tool* t = m_catalog.installedTool(tool)) launchTool(*t, project);
            }
        }
    }
    if (m_jobInstallsEngine) {
        m_jobInstallsEngine = false;
        const std::string folder = installedFolder(m_job.output());
        if (ok && !folder.empty()) m_catalog.addEngine(folder);
        if (!ok) m_toolQueue.clear(); // tools picked with the engine wait for it: nothing to match without it
    }
    reload();
}

App::~App() {
    for (auto& [folder, texture] : m_icons)
        if (texture) SDL_DestroyTexture(texture);
    if (m_logo) SDL_DestroyTexture(m_logo);
    for (auto& [name, texture] : m_toolIcons)
        if (texture) SDL_DestroyTexture(texture);
}

SDL_Texture* App::logoTexture() {
    if (m_logoLoaded || !renderer) return m_logo;
    m_logoLoaded = true;
    if (SDL_Surface* surface = SDL_LoadPNG_IO(SDL_IOFromConstMem(kLogoPng, sizeof(kLogoPng)), true)) {
        m_logo = SDL_CreateTextureFromSurface(renderer, surface);
        if (m_logo) SDL_SetTextureScaleMode(m_logo, SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(surface);
    }
    return m_logo;
}

namespace {
std::mutex g_iconMutex;
void SDLCALL onIconPicked(void* userdata, const char* const* filelist, int) {
    std::lock_guard<std::mutex> lock(g_iconMutex);
    *static_cast<std::string*>(userdata) = (filelist && filelist[0]) ? filelist[0] : "";
}
} // namespace

// Click on the selected row's icon: pick a PNG, copied into the project as icon.png (docs/UX_REVIEW.md 3.2)
void App::pickIcon(const std::string& project) {
    {
        std::lock_guard<std::mutex> lock(g_iconMutex);
        m_iconFor = project;
        m_pickedIcon = "\x01";
    }
    static const SDL_DialogFileFilter filter = {"PNG", "png"};
    SDL_ShowOpenFileDialog(onIconPicked, &m_pickedIcon, window, &filter, 1, project.c_str(), false);
}

void App::takePickedIcon() {
    std::string picked, project;
    {
        std::lock_guard<std::mutex> lock(g_iconMutex);
        if (m_iconFor.empty() || m_pickedIcon == "\x01") return;
        picked = m_pickedIcon;
        project.swap(m_iconFor);
    }
    if (picked.empty()) return; // canceled
    std::string error;
    if (!setProjectIcon(project, picked, error)) {
        setStatus(trf("Cannot change the icon: %s", error.c_str()), true);
        return;
    }
    auto found = m_icons.find(project);
    if (found != m_icons.end()) {
        if (found->second) SDL_DestroyTexture(found->second);
        m_icons.erase(found);
    }
    reload();
}

void App::resetIcon(const std::string& project) {
    std::string error;
    if (!setProjectIcon(project, "", error)) {
        setStatus(trf("Cannot change the icon: %s", error.c_str()), true);
        return;
    }
    auto found = m_icons.find(project);
    if (found != m_icons.end()) {
        if (found->second) SDL_DestroyTexture(found->second);
        m_icons.erase(found);
    }
    reload();
}

void App::setStatus(const std::string& text, bool error) {
    m_status = text;
    m_statusError = error;
    m_statusTime = ImGui::GetTime();
}

// The status strip shows only while a job the user started runs (the start-up toolchain check and release list stay silent),
// for a few seconds after a success, and until closed after a failure
bool App::statusVisible() const {
    if (m_job.running()) return !m_jobChecksToolchain && !m_jobListsReleases;
    if (m_status.empty()) return false;
    return m_statusError || ImGui::GetTime() - m_statusTime < 4.0;
}

bool App::managerUpdateAvailable() const {
    return !m_managerRelease.version.empty() && versionLess(MKMANAGER_VERSION, m_managerRelease.version);
}

void App::drawWindow() {
    // Shortcuts: Ctrl+1/2 pages, Ctrl+N new project, Ctrl+, settings, F5 refresh (shown in the tooltips)
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_1)) m_page = Page::Projects;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_2)) m_page = Page::Tools;
        if (!m_job.running() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) m_openNew = true;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) m_openSettings = true;
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) reload();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##manager", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    // Header and status strip on the gutter (WindowBg), the page on a panel (ChildBg), like the editor's panels
    header();
    const bool strip = statusVisible();
    const float stripHeight = strip ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
    if (ImGui::BeginChild("##page", ImVec2(0, -stripHeight), ImGuiChildFlags_AlwaysUseWindowPadding)) {
        switch (m_page) {
        case Page::Projects: projectsTab(); break;
        case Page::Tools: toolsTab(); break;
        }
    }
    ImGui::EndChild();
    if (strip) statusBar();
    newProjectPopup();
    settingsPopup();
    engineInstallPopup();
    renamePopup();
    movePopup();
    ImGui::End();
}

// Header: the app name (version in its tooltip), the Projects | Tools switch, and settings on the right
void App::header() {
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    const float height = ImGui::GetFrameHeight() + 8.0f * dpi;
    const float top = ImGui::GetCursorPosY();
    {
        theme::FontScope font(theme::kSubtitle, true);
        ImGui::SetCursorPosY(top + (height - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::TextUnformatted("MealKit");
    }
    // A newer Manager: a dot after the name, and the name opens Settings > General where [Update and Restart] is (docs/UX_REVIEW.md 3.3)
    if (managerUpdateAvailable()) {
        const ImVec2 nameMax = ImGui::GetItemRectMax(), nameMin = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(nameMax.x + 7.0f * dpi, nameMin.y + 6.0f * dpi), 3.5f * dpi, ImGui::GetColorU32(theme::accent()));
        ImGui::SetCursorScreenPos(nameMin);
        if (ImGui::InvisibleButton("##update", ImVec2(nameMax.x - nameMin.x + 12.0f * dpi, nameMax.y - nameMin.y))) {
            m_settingsPage = SettingsPage::General;
            m_openSettings = true;
        }
        ImGui::SetItemTooltip("%s", trf("MealKit Manager %s is available.", m_managerRelease.version.c_str()).c_str());
    } else {
        ImGui::SetItemTooltip("MealKit Manager %s\n%s", MKMANAGER_VERSION, userDirectory().c_str());
    }

    // Segmented switch: a gray 3 pill holding two buttons, the current one gray 5
    struct Item {
        Page page;
        const char* glyph;
        const char* label;
        const char* key;
    };
    const Item items[] = {{Page::Projects, kIconProjects, tr("Projects"), "Ctrl+1"}, {Page::Tools, kIconTools, tr("Tools"), "Ctrl+2"}};
    const ImGuiStyle& style = ImGui::GetStyle();
    float total = 0.0f;
    {
        theme::FontScope font(theme::kBody, true);
        for (const Item& item : items) total += ImGui::CalcTextSize(iconLabel(item.glyph, item.label).c_str()).x + style.FramePadding.x * 4;
    }
    const float pad = 3.0f * dpi;
    ImGui::SameLine((ImGui::GetContentRegionMax().x - total) * 0.5f);
    ImGui::SetCursorPosY(top + (height - ImGui::GetFrameHeight()) * 0.5f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x - pad, pos.y - pad), ImVec2(pos.x + total + pad, pos.y + ImGui::GetFrameHeight() + pad),
                                              ImGui::GetColorU32(theme::gray(3)), theme::kPillRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kPillRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * 2, style.FramePadding.y));
    float x = pos.x;
    for (const Item& item : items) {
        const bool current = m_page == item.page;
        ImGui::SetCursorScreenPos(ImVec2(x, pos.y)); // explicit: SameLine would drop the second button to the text baseline
        ImGui::PushStyleColor(ImGuiCol_Button, current ? theme::gray(5) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, current ? theme::gray(5) : theme::gray(4));
        ImGui::PushStyleColor(ImGuiCol_Text, current ? theme::gray(12) : theme::gray(11));
        bool pressed;
        {
            theme::FontScope font(theme::kBody, true);
            pressed = ImGui::Button((iconLabel(item.glyph, item.label) + "##" + item.key).c_str());
        }
        ImGui::PopStyleColor(3);
        ImGui::SetItemTooltip("%s", item.key);
        if (pressed) m_page = item.page;
        x = ImGui::GetItemRectMax().x;
        if (item.page == Page::Tools) { // a dot while a tool update waits (docs/UX_REVIEW.md 3.6)
            bool update = false;
            for (const ToolRowView& row : toolRows()) update |= toolUpdatable(row);
            if (update)
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(x - 10.0f * dpi, ImGui::GetItemRectMin().y + 7.0f * dpi), 3.5f * dpi,
                                                            ImGui::GetColorU32(theme::accent()));
        }
    }
    ImGui::PopStyleVar(3);

    // Settings
    const float button = ImGui::GetFrameHeight();
    ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - button, top + (height - button) * 0.5f));
    if (iconButton(kIconSettings, tr("Settings"), false, "Ctrl+,")) m_openSettings = true;
    ImGui::SetCursorPosY(top + height + 4.0f * dpi);
}

void App::pageTitle(const char* title) {
    theme::FontScope font(theme::kSubtitle, true);
    ImGui::TextUnformatted(title);
}

bool App::actionButton(const char* glyph, const std::string& label, bool primary, const char* tooltip) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kPillRounding); // S2 buttons are pills
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::accent());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentHover());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::accentDown());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    bool pressed;
    {
        theme::FontScope font(theme::kBody, true);
        pressed = ImGui::Button((iconLabel(glyph, label.c_str()) + "##" + label).c_str());
    }
    if (primary) ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

SDL_Texture* App::projectIcon(const Project& p) {
    auto found = m_icons.find(p.directory);
    if (found != m_icons.end()) return found->second;
    SDL_Texture* texture = nullptr;
    if (renderer && p.valid) {
        std::string icon = p.icon.empty() ? std::string("icon.png") : p.icon;
        std::replace(icon.begin(), icon.end(), '/', '\\');
        if (SDL_Surface* surface = SDL_LoadPNG((p.directory + "\\" + icon).c_str())) {
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(surface);
        }
    }
    m_icons[p.directory] = texture;
    return texture;
}

// The project's icon, or a rounded tile with its first letter (the suite icon family: one mark on a colored square)
void App::projectTile(const Project& p, float size) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float rounding = size * 0.22f; // the same curve at any size
    SDL_Texture* texture = projectIcon(p);
    if (!texture && p.valid) texture = logoTexture();
    if (texture) {
        draw->AddImageRounded(ImTextureRef(reinterpret_cast<ImTextureID>(texture)), pos, ImVec2(pos.x + size, pos.y + size), ImVec2(0, 0), ImVec2(1, 1),
                              IM_COL32_WHITE, rounding);
    } else {
        draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), ImGui::GetColorU32(theme::gray(5)), rounding);
        const std::string letter = "?"; // a missing project, or the logo didn't load
        theme::FontScope font(theme::kSubtitle, true);
        const ImVec2 text = ImGui::CalcTextSize(letter.c_str());
        draw->AddText(ImVec2(pos.x + (size - text.x) * 0.5f, pos.y + (size - text.y) * 0.5f), IM_COL32_WHITE, letter.c_str());
    }
    ImGui::Dummy(ImVec2(size, size));
}

const Project* App::selectedProject() const {
    for (const Project& p : m_catalog.projects())
        if (sameDirectory(m_selected, p.directory)) return &p;
    return nullptr;
}

// "<glyph> label" with the editor's Segoe icon glyphs when the icon font is loaded
std::string App::iconLabel(const char* glyph, const char* label) const {
    return iconFont ? std::string(glyph) + "  " + label : std::string(label);
}

bool App::iconButton(const char* glyph, const std::string& name, bool small, const char* detail, const std::string& suffix) {
    std::string label = iconFont ? std::string(glyph) + (suffix.empty() ? "" : " " + suffix) : name;
    label += "##" + name;
    // Flat like the editor's icon buttons: clear until hovered
    if (iconFont) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    }
    const bool pressed = small ? ImGui::SmallButton(label.c_str()) : ImGui::Button(label.c_str());
    if (iconFont) ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (detail) ImGui::SetTooltip("%s\n%s", name.c_str(), detail);
        else if (iconFont) ImGui::SetTooltip("%s", name.c_str());
    }
    return pressed;
}

void App::removeMissingProjects() {
    int removed = 0;
    for (const Project& p : m_catalog.projects())
        if (!p.valid) {
            m_catalog.removeRecent(p.directory);
            ++removed;
        }
    setStatus(trf("Removed %d missing project(s) from the list", removed), false);
    reload();
}

// The engine's own sample projects (no engine line: they live in the engine repo) can't be renamed (rename_project.ps1 refuses too)
bool App::canRename(const Project& p) const {
    const Engine* engine = p.valid ? m_catalog.engineFor(p) : nullptr;
    return engine && !p.engine.empty() && fileExists(pathJoin(engine->directory, "tools\\rename_project.ps1"));
}

// An editor running on the project (its live info file and a live process)
bool App::editorOpen(const std::string& project) const {
    for (const mkcontract::live::Instance& i : mkcontract::live::findInstances(project))
        if (i.kind == "editor" && processAlive(i.pid)) return true;
    return false;
}

bool App::canMove(const Project& p) const {
    const Engine* engine = p.valid ? m_catalog.engineFor(p) : nullptr;
    return engine && !p.engine.empty() && fileExists(pathJoin(engine->directory, "tools\\move_project.ps1"));
}

void App::movePopup() {
    if (!m_confirmMoveFor.empty() && !ImGui::IsPopupOpen("##move")) ImGui::OpenPopup("##move");
    if (!ImGui::BeginPopupModal("##move", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    const Project* project = nullptr;
    for (const Project& p : m_catalog.projects())
        if (sameDirectory(p.directory, m_confirmMoveFor)) project = &p;
    {
        theme::FontScope font(theme::kBody, true);
        ImGui::TextUnformatted(tr("Move the project folder?"));
    }
    ImGui::TextUnformatted(m_confirmMoveFor.c_str());
    ImGui::TextUnformatted(("-> " + m_confirmMoveTo).c_str());
    ImGui::TextDisabled("%s", tr("The build folder is deleted (it holds absolute paths): the game is rebuilt when it opens."));
    ImGui::Spacing();
    bool close = false;
    if (actionButton(kIconFolder, tr("Move"), true) && project) {
        const Engine* engine = m_catalog.engineFor(*project);
        if (editorOpen(project->directory)) setStatus(trf("Close the editor of %s first, then move it.", project->name.c_str()), true);
        else if (engine) {
            m_jobMoveFrom = project->directory;
            m_jobMoveTo = m_confirmMoveTo;
            runScript(trf("Move %s", project->name.c_str()), pathJoin(engine->directory, "tools\\move_project.ps1"),
                      {"-Project", project->directory, "-To", m_confirmMoveTo});
            if (!m_job.running()) m_jobMoveFrom.clear();
        }
        close = true;
    }
    ImGui::SameLine();
    if (actionButton(kIconCancel, tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
    if (close) {
        m_confirmMoveFor.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::startRename(const Project& p) {
    if (!canRename(p)) return;
    m_renaming = p.directory;
    std::snprintf(m_renameText, sizeof(m_renameText), "%s", p.name.c_str());
    m_renameFocus = true;
    m_renameActive = false;
}

void App::renamePopup() {
    if (!m_confirmRenameFor.empty() && !ImGui::IsPopupOpen("##rename")) ImGui::OpenPopup("##rename");
    if (!ImGui::BeginPopupModal("##rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    const Project* project = nullptr;
    for (const Project& p : m_catalog.projects())
        if (sameDirectory(p.directory, m_confirmRenameFor)) project = &p;
    if (project) {
        theme::FontScope font(theme::kBody, true);
        ImGui::TextUnformatted(trf("Rename %s to %s?", project->name.c_str(), m_confirmRenameTo.c_str()).c_str());
    }
    ImGui::TextDisabled("%s", tr("The exe name changes too, and the build folder is deleted: the game is rebuilt when it opens."));
    ImGui::Spacing();
    bool close = false;
    if (actionButton(kIconEdit, tr("Rename"), true) && project) {
        const Engine* engine = m_catalog.engineFor(*project);
        if (editorOpen(project->directory)) setStatus(trf("Close the editor of %s first, then rename it.", project->name.c_str()), true);
        else if (engine)
            runScript(trf("Rename %s", project->name.c_str()), pathJoin(engine->directory, "tools\\rename_project.ps1"),
                      {"-Project", project->directory, "-Name", m_confirmRenameTo});
        close = true;
    }
    ImGui::SameLine();
    if (actionButton(kIconCancel, tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
    if (close) {
        m_confirmRenameFor.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::projectMenu(const Project& p, bool busy) {
    ImGui::BeginDisabled(busy || !p.valid);
    if (ImGui::MenuItem(iconLabel(kIconLaunch, tr("Open in Editor")).c_str(), "Enter")) requestRow(p.directory, RowAction::Open);
    ImGui::EndDisabled();
    openWithMenu(p, busy);
    ImGui::BeginDisabled(!p.valid);
    if (ImGui::MenuItem(iconLabel(kIconFolderOpen, tr("Show Folder")).c_str())) SDL_OpenURL(("file:///" + p.directory).c_str());
    ImGui::EndDisabled();
    if (ImGui::MenuItem(iconLabel(kIconCopy, tr("Copy Path")).c_str())) SDL_SetClipboardText(p.directory.c_str());
    ImGui::BeginDisabled(busy || !canRename(p));
    if (ImGui::MenuItem(iconLabel(kIconEdit, tr("Rename")).c_str(), "F2")) startRename(p);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(busy || !canMove(p));
    if (ImGui::MenuItem(iconLabel(kIconFolder, tr("Move Folder...")).c_str())) pickFolder(5, p.directory);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(p.valid);
    if (ImGui::MenuItem(iconLabel(kIconSearch, tr("Find Folder...")).c_str())) pickFolder(4, p.directory);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!p.valid);
    if (ImGui::MenuItem(iconLabel(kIconEdit, tr("Change Icon...")).c_str())) pickIcon(p.directory);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!p.valid || p.icon.empty());
    if (ImGui::MenuItem(tr("Use Default Icon"))) resetIcon(p.directory);
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Favorite"), nullptr, p.favorite)) {
        m_pendingFavorite = p.directory;
        m_pendingFavoriteOn = !p.favorite;
    }
    if (ImGui::MenuItem(iconLabel(kIconRemove, tr("Remove from List")).c_str(), "Del")) m_pendingRemove = p.directory;
}

void App::projectsTab() {
    toolchainBanner();
    const bool busy = m_job.running();
    const std::vector<Project>& projects = m_catalog.projects();
    const std::string newLabel = iconFont ? iconLabel(kIconAdd, tr("New Project")) : std::string(tr("+ New Project"));
    const std::string importLabel = iconLabel(kIconFolder, tr("Import..."));

    // Nothing in the list yet: say what to do instead of showing an empty table
    if (projects.empty()) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const char* title = tr("No projects yet");
        const char* hint = tr("Make a new game, or import a folder that has mealkit.project.");
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.35f);
        ImGui::SetCursorPosX((avail.x - ImGui::CalcTextSize(title).x) * 0.5f);
        ImGui::TextUnformatted(title);
        ImGui::SetCursorPosX((avail.x - ImGui::CalcTextSize(hint).x) * 0.5f);
        ImGui::TextDisabled("%s", hint);
        ImGui::Spacing();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttons = ImGui::CalcTextSize(newLabel.c_str()).x + ImGui::CalcTextSize(importLabel.c_str()).x +
                              style.FramePadding.x * 4 + style.ItemSpacing.x;
        ImGui::SetCursorPosX((avail.x - buttons) * 0.5f);
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(newLabel.c_str())) m_openNew = true;
        ImGui::SameLine();
        if (ImGui::Button(importLabel.c_str())) pickFolder(0);
        ImGui::EndDisabled();
        return;
    }

    // Projects whose folder is gone and throwaway ones in %TEMP% stay out of the list until asked for (favorites always show)
    int missing = 0, temp = 0;
    for (const Project& p : projects) {
        if (p.favorite) continue;
        if (!p.valid) ++missing;
        else if (p.temp) ++temp;
    }

    if (m_focusSearch || (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))) {
        ImGui::SetKeyboardFocusHere();
        m_focusSearch = false;
    }
    int shown = 0;
    for (const Project& p : projects) shown += (containsNoCase(p.name, m_search) || containsNoCase(p.directory, m_search)) ? 1 : 0;
    char countText[32] = {};
    if (m_search[0]) std::snprintf(countText, sizeof(countText), "%d / %d", shown, static_cast<int>(projects.size()));
    const std::string count = countText;
    const char* sorts[] = {tr("Recent"), tr("Name"), tr("Created")};
    const int sortColumns[] = {-1, 1, 3}; // m_sortColumn: -1 recent order, 1 name, 3 created
    const int sortIndex = m_sortColumn < 0 ? 0 : m_sortColumn == 1 ? 1 : 2;
    const ImGuiStyle& style = ImGui::GetStyle();
    float sortWidth = 0.0f;
    for (const char* label : sorts) sortWidth = std::max(sortWidth, ImGui::CalcTextSize(label).x);
    sortWidth += ImGui::GetFrameHeight() + style.FramePadding.x * 2;
    const float countWidth = count.empty() ? 0.0f : ImGui::CalcTextSize(count.c_str()).x + style.ItemSpacing.x;
    // The page's actions end the search row (one toolbar line, docs/UX_REVIEW.md 3.1)
    float actionsWidth;
    {
        theme::FontScope font(theme::kBody, true);
        actionsWidth = ImGui::CalcTextSize(iconLabel(kIconAdd, tr("New Project")).c_str()).x + ImGui::CalcTextSize(importLabel.c_str()).x +
                       style.FramePadding.x * 4 + style.ItemSpacing.x * 2;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - countWidth - sortWidth - actionsWidth - style.ItemSpacing.x);
    ImGui::InputTextWithHint("##search", iconLabel(kIconSearch, tr("Search (Ctrl+F)")).c_str(), m_search, sizeof(m_search), ImGuiInputTextFlags_EscapeClearsAll);
    if (!count.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", count.c_str());
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(sortWidth);
    if (ImGui::BeginCombo("##sort", sorts[sortIndex])) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(sorts[i], i == sortIndex)) {
                m_sortColumn = sortColumns[i];
                m_sortAscending = sortColumns[i] != 3; // newest first for the date
            }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Sort (favorites stay on top)"));
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (actionButton(kIconFolder, tr("Import..."), false, tr("Add a folder that has mealkit.project to the list."))) pickFolder(0);
    ImGui::SameLine();
    if (actionButton(kIconAdd, tr("New Project"), true, "Ctrl+N")) m_openNew = true;
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 2.0f));
    if (missing + temp > 0 && !m_search[0]) {
        // One quiet line, not a red warning: these are leftovers, not something wrong with the user's games
        std::string what;
        if (missing > 0) what = trf("folder gone %d", missing);
        if (temp > 0) what += (what.empty() ? "" : ", ") + trf("temp folder %d", temp);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::gray(11), "%s", (m_showHidden ? trf("Showing %d hidden project(s) (%s)", missing + temp, what.c_str())
                                                                : trf("%d project(s) hidden (%s)", missing + temp, what.c_str()))
                                                      .c_str());
        ImGui::SameLine();
        if (iconButton(m_showHidden ? kIconHide : kIconView, m_showHidden ? tr("Hide") : tr("Show"), true,
                       tr("Projects whose folder is gone, or that live in the temp folder"), m_showHidden ? tr("Hide") : tr("Show")))
            m_showHidden = !m_showHidden;
        if (missing > 0) {
            ImGui::SameLine();
            if (iconButton(kIconRemove, tr("Remove Missing from List"), true,
                           tr("Takes projects whose folder or mealkit.project is gone off the list (nothing is deleted)."), tr("Remove Missing from List")))
                removeMissingProjects();
        }
    }

    projectList(busy);

    // Changes picked in the list, now that it isn't being drawn
    if (!m_pendingFavorite.empty()) {
        m_catalog.setFavorite(m_pendingFavorite, m_pendingFavoriteOn);
        m_pendingFavorite.clear();
        reload();
    }
    if (!m_pendingRemove.empty()) {
        m_catalog.removeRecent(m_pendingRemove);
        setStatus(trf("Removed %s from the list (the folder is kept)", m_pendingRemove.c_str()), false);
        m_pendingRemove.clear();
        reload();
    }
    if (!m_pendingAction.empty()) {
        const std::string folder = m_pendingAction;
        const RowAction action = m_pendingKind;
        m_pendingAction.clear();
        for (const Project& p : m_catalog.projects()) {
            if (!sameDirectory(p.directory, folder) || !p.valid) continue;
            if (action == RowAction::Open) openProject(p);
            else if (action == RowAction::Upgrade) {
                if (const Engine* engine = m_catalog.engineFor(p))
                    runScript(tr("Upgrade"), pathJoin(engine->directory, "tools\upgrade_project.ps1"), {"-Project", p.directory});
            } else if (action == RowAction::Locate) {
                pickFolder(4, p.directory);
            } else if (action == RowAction::InstallEngine) {
                if (const Release* release = Catalog::releaseFor(p, m_releases)) requestEngineInstall(*release);
            }
            break;
        }
    }
}

namespace {
bool lessNoCase(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}
} // namespace

void App::projectList(bool busy) {
    // Rows that match the search, in recent order or the picked sort (favorites stay on top either way)
    std::vector<const Project*> rows;
    // A search looks through everything; otherwise gone and %TEMP% projects wait behind [Show] (projectsTab)
    for (const Project& p : m_catalog.projects()) {
        if (!m_search[0] && !m_showHidden && !p.favorite && (!p.valid || p.temp)) continue;
        if (containsNoCase(p.name, m_search) || containsNoCase(p.directory, m_search)) rows.push_back(&p);
    }
    if (m_sortColumn > 0) {
        auto less = [&](const Project* a, const Project* b) {
            if (m_sortColumn == 1) return lessNoCase(a->name, b->name);
            return a->created < b->created;
        };
        std::stable_sort(rows.begin(), rows.end(), [&](const Project* a, const Project* b) {
            if (a->favorite != b->favorite) return a->favorite;
            return m_sortAscending ? less(a, b) : less(b, a);
        });
    } else {
        std::stable_partition(rows.begin(), rows.end(), [](const Project* p) { return p->favorite; });
    }

    // Two-line rows (TOOL_DESIGN 3.2: only what identifies a project): icon, name, "engine · created"; a state only when it needs attention.
    // The folder is in the row's tooltip and the More menu
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    const bool open = ImGui::BeginChild("##projects", ImVec2(0, 0));
    ImGui::PopStyleColor();
    if (!open) {
        ImGui::EndChild();
        return;
    }
    if (rows.empty()) {
        ImGui::Dummy(ImVec2(0, 24.0f));
        theme::FontScope font(theme::kBodyLarge);
        ImGui::TextDisabled("%s", m_search[0] ? tr("No projects match the search.") : tr("Every project here is hidden: [Show] above lists them."));
    }

    const float dpi = ImGui::GetStyle().FontScaleDpi;
    // Launcher rows with an icon, short enough that a usual list fits without scrolling: the name, the folder under it
    const float rowHeight = 60.0f * dpi;
    const float tile = 44.0f * dpi;
    const float pad = 14.0f * dpi;
    const float nameSize = theme::kBodyLarge;
    const float pathSize = theme::kBody;
    const bool typing = ImGui::GetIO().WantTextInput;
    for (const Project* pp : rows) {
        const Project& p = *pp;
        ImGui::PushID(p.directory.c_str());
        const bool isSelected = sameDirectory(m_selected, p.directory);
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const Engine* engine = p.valid ? m_catalog.engineFor(p) : nullptr;

        // What the row says on its right: a state that needs attention, else nothing
        std::string state;
        ImVec4 stateColor = theme::warning();
        if (!p.valid) {
            std::error_code ec;
            const bool folder = std::filesystem::is_directory(std::filesystem::path(std::u8string(p.directory.begin(), p.directory.end())), ec);
            state = folder ? tr("no mealkit.project") : tr("folder missing");
            stateColor = theme::danger();
        } else if (!engine) {
            const Release* release = Catalog::releaseFor(p, m_releases);
            state = release ? trf("Engine %s not installed", release->version.c_str()) : std::string(tr("engine not found"));
            if (!release) stateColor = theme::danger();
        }

        // The whole row: click selects, double-click / Enter opens (no Open button: docs/UX_REVIEW.md 3.2)
        ImGui::PushStyleColor(ImGuiCol_Header, theme::gray(5));
        const bool clicked = ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
                                               ImVec2(rowWidth, rowHeight));
        ImGui::PopStyleColor();
        if (clicked) {
            m_selected = p.directory;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && p.valid && !busy) requestRow(p.directory, RowAction::Open);
        }
        if (p.valid) ImGui::SetItemTooltip("%s", tr("Double-click or Enter to open in the editor"));
        // Keyboard: the selection follows the arrow keys, Enter opens, Delete takes it off the list
        if (ImGui::IsItemFocused() && !typing) {
            if (ImGui::GetIO().NavVisible && !isSelected) m_selected = p.directory;
            if (p.valid && !busy && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
                requestRow(p.directory, RowAction::Open);
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) m_pendingRemove = p.directory;
            if (!busy && ImGui::IsKeyPressed(ImGuiKey_F2, false)) startRename(p);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_selected = p.directory;
        if (ImGui::BeginPopupContextItem("##menu")) {
            projectMenu(p, busy);
            ImGui::EndPopup();
        }
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Favorite star first, always shown (off = gray outline, on = orange fill)
        // The star sits in the middle of the space before the icon (same gap on both sides), its glyph centered by its ink, not its advance
        const float button = ImGui::GetFrameHeight();
        const float starBox = 32.0f * dpi;
        const float starGap = 10.0f * dpi;
        const ImVec2 starMin(rowPos.x + starGap, rowPos.y + (rowHeight - starBox) * 0.5f);
        ImGui::SetCursorScreenPos(starMin);
        const bool starPressed = ImGui::InvisibleButton("##fav", ImVec2(starBox, starBox));
        if (ImGui::IsItemHovered())
            draw->AddRectFilled(starMin, ImVec2(starMin.x + starBox, starMin.y + starBox), ImGui::GetColorU32(theme::gray(4)), starBox * 0.5f);
        {
            const char* star = iconFont ? (p.favorite ? kIconStarFilled : kIconStar) : (p.favorite ? "\xE2\x98\x85" : "\xE2\x98\x86");
            theme::FontScope font(theme::kBodyLarge);
            // The star glyphs are 3-byte UTF-8 (U+E734/E735, U+2605/2606)
            const unsigned char* u = reinterpret_cast<const unsigned char*>(star);
            const ImWchar codepoint = static_cast<ImWchar>(((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F));
            ImVec2 inkMin(0, 0), inkMax = ImGui::CalcTextSize(star);
            if (const ImFontGlyph* glyph = ImGui::GetFontBaked()->FindGlyph(codepoint)) {
                inkMin = ImVec2(glyph->X0, glyph->Y0);
                inkMax = ImVec2(glyph->X1, glyph->Y1);
            }
            const ImVec2 at(starMin.x + (starBox - (inkMax.x - inkMin.x)) * 0.5f - inkMin.x, starMin.y + (starBox - (inkMax.y - inkMin.y)) * 0.5f - inkMin.y);
            draw->AddText(at, ImGui::GetColorU32(p.favorite ? theme::warning() : theme::gray(9)), star);
        }
        if (starPressed) {
            m_pendingFavorite = p.directory;
            m_pendingFavoriteOn = !p.favorite;
        }
        ImGui::SetItemTooltip("%s", tr("Favorite"));

        const float tileX = starMin.x + starBox + starGap;
        ImGui::SetCursorScreenPos(ImVec2(tileX, rowPos.y + (rowHeight - tile) * 0.5f));
        projectTile(p, tile);
        if (p.valid && isSelected && !busy) {
            const ImVec2 tileMin = ImGui::GetItemRectMin(), tileMax = ImGui::GetItemRectMax();
            ImGui::SetCursorScreenPos(tileMin);
            if (ImGui::InvisibleButton("##icon", ImVec2(tile, tile))) pickIcon(p.directory);
            if (ImGui::IsItemHovered()) {
                draw->AddRectFilled(tileMin, tileMax, IM_COL32(0, 0, 0, 120), tile * 0.22f);
                if (iconFont) {
                    const ImVec2 g = ImGui::CalcTextSize(kIconEdit);
                    draw->AddText(ImVec2((tileMin.x + tileMax.x - g.x) * 0.5f, (tileMin.y + tileMax.y - g.y) * 0.5f), IM_COL32_WHITE, kIconEdit);
                }
                ImGui::SetTooltip("%s", tr("Change Icon..."));
            }
        }

        // Right side: More, then the action that fixes a state ([Install Engine 0.9], [Upgrade]) or the state text, then the engine tag
        float right = rowPos.x + rowWidth - pad - button;
        ImGui::SetCursorScreenPos(ImVec2(right, rowPos.y + (rowHeight - button) * 0.5f));
        if (iconButton(kIconMore, tr("More"))) {
            m_selected = p.directory;
            ImGui::OpenPopup("##more");
        }
        if (ImGui::BeginPopup("##more")) {
            projectMenu(p, busy);
            ImGui::EndPopup();
        }

        const char* actionGlyph = nullptr;
        std::string actionLabel;
        RowAction action = RowAction::Open;
        const char* actionTip = nullptr;
        if (!p.valid) {
            actionGlyph = kIconSearch;
            actionLabel = tr("Find...");
            action = RowAction::Locate;
            actionTip = tr("Moved it? Pick where the folder is now.");
        } else if (p.valid && !engine) {
            if (const Release* release = Catalog::releaseFor(p, m_releases)) {
                actionGlyph = kIconDownload;
                actionLabel = trf("Install Engine %s", release->version.c_str());
                action = RowAction::InstallEngine;
                state.clear();
            }
        } else if (engine && m_catalog.needsUpgrade(p)) {
            actionGlyph = kIconUp;
            actionLabel = trf("Upgrade %s -> %s", p.engineVersion.c_str(), engine->version.c_str());
            action = RowAction::Upgrade;
            actionTip = tr("Made with an older engine: brings it to this one.");
        }
        if (actionGlyph) {
            float w;
            {
                theme::FontScope font(theme::kBody, true);
                w = ImGui::CalcTextSize(iconLabel(actionGlyph, actionLabel.c_str()).c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
            }
            right -= w + 8.0f * dpi;
            ImGui::SetCursorScreenPos(ImVec2(right, rowPos.y + (rowHeight - button) * 0.5f));
            ImGui::BeginDisabled(busy);
            if (actionButton(actionGlyph, actionLabel, false, actionTip)) {
                m_selected = p.directory;
                requestRow(p.directory, action);
            }
            ImGui::EndDisabled();
        }
        if (!state.empty()) {
            right -= ImGui::CalcTextSize(state.c_str()).x + 12.0f * dpi;
            draw->AddText(ImVec2(right, rowPos.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(stateColor), state.c_str());
        }
        // Engine tag (like Godot's version tag), only when nothing on the row already names the engine
        const std::string version = engine ? engine->version : p.engineVersion;
        if (!actionGlyph && state.empty() && !version.empty()) {
            theme::FontScope font(theme::kBody);
            const ImVec2 text = ImGui::CalcTextSize(version.c_str());
            const float tagPad = 8.0f * dpi;
            const float tagV = 4.0f * dpi;
            right -= text.x + tagPad * 2 + 12.0f * dpi;
            const float y = rowPos.y + (rowHeight - text.y - tagV * 2) * 0.5f;
            draw->AddRectFilled(ImVec2(right, y), ImVec2(right + text.x + tagPad * 2, y + text.y + tagV * 2), ImGui::GetColorU32(theme::gray(6)),
                                6.0f * dpi);
            draw->AddText(ImVec2(right + tagPad, y + tagV), ImGui::GetColorU32(theme::gray(11)), version.c_str());
        }

        // Name and path, clipped before the right side so nothing there is pushed out
        const float textX = tileX + tile + pad;
        const float textRight = std::max(textX, right - 12.0f * dpi);
        std::string name = p.name;
        if (!p.valid) {
            const size_t slash = p.directory.find_last_of("\\/");
            name = slash == std::string::npos ? p.directory : p.directory.substr(slash + 1);
        }
        const bool renaming = sameDirectory(m_renaming, p.directory);
        if (renaming) {
            // In place: Enter asks to rename, Esc or clicking elsewhere cancels; the name rules show while typing
            theme::FontScope font(nameSize, true);
            const bool ok = validGameName(m_renameText);
            ImGui::SetCursorScreenPos(ImVec2(textX - ImGui::GetStyle().FramePadding.x,
                                             rowPos.y + rowHeight * 0.5f - ImGui::GetTextLineHeight() - 2.0f * dpi - ImGui::GetStyle().FramePadding.y));
            ImGui::SetNextItemWidth(std::min(textRight - textX, 320.0f * dpi));
            if (m_renameFocus) {
                ImGui::SetKeyboardFocusHere();
                m_renameFocus = false;
            }
            if (!ok) ImGui::PushStyleColor(ImGuiCol_Border, theme::danger());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ok ? 0.0f : 1.0f);
            const bool enter = ImGui::InputText("##name", m_renameText, sizeof(m_renameText), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool wasActive = m_renameActive; // focus lands a frame later, so "not active" only cancels after it was active
            m_renameActive = ImGui::IsItemActive();
            ImGui::PopStyleVar();
            if (!ok) {
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("%s", tr("Start with a letter; letters, digits and _ only (it becomes the exe name)."));
            }
            if (enter && ok) {
                if (p.name != m_renameText) {
                    m_confirmRenameFor = p.directory;
                    m_confirmRenameTo = m_renameText;
                }
                m_renaming.clear();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (wasActive && !m_renameActive)) {
                m_renaming.clear();
            }
        } else if (isSelected && canRename(p) && !busy) {
            // Clicking the selected row's name starts renaming (a first click only selects, like Explorer)
            theme::FontScope font(nameSize, true);
            const ImVec2 size(std::min(ImGui::CalcTextSize(name.c_str()).x, textRight - textX), ImGui::GetTextLineHeight());
            ImGui::SetCursorScreenPos(ImVec2(textX, rowPos.y + rowHeight * 0.5f - size.y - 2.0f * dpi));
            if (size.x > 0 && ImGui::InvisibleButton("##rename", size)) startRename(p);
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
                ImGui::SetTooltip("%s", tr("Rename  (F2)"));
            }
        }
        if (isSelected && !busy && (canMove(p) || !p.valid)) {
            // Clicking the selected row's folder moves the project (or, when the folder is gone, finds it)
            theme::FontScope font(pathSize);
            const ImVec2 size(std::min(ImGui::CalcTextSize(p.directory.c_str()).x, textRight - textX), ImGui::GetTextLineHeight());
            const ImVec2 at(textX, rowPos.y + rowHeight * 0.5f + 4.0f * dpi);
            ImGui::SetCursorScreenPos(at);
            if (size.x > 0 && ImGui::InvisibleButton("##path", size)) pickFolder(p.valid ? 5 : 4, p.directory);
            if (ImGui::IsItemHovered()) {
                draw->AddRectFilled(ImVec2(at.x - 3.0f * dpi, at.y - 1.0f), ImVec2(at.x + size.x + 3.0f * dpi, at.y + size.y + 1.0f),
                                    ImGui::GetColorU32(theme::gray(6)), 4.0f * dpi);
                ImGui::SetTooltip("%s", p.valid ? tr("Move Folder...") : tr("Find Folder..."));
            }
        }
        draw->PushClipRect(ImVec2(textX, rowPos.y), ImVec2(textRight, rowPos.y + rowHeight), true);
        if (!renaming) {
            theme::FontScope font(nameSize, true);
            draw->AddText(ImVec2(textX, rowPos.y + rowHeight * 0.5f - ImGui::GetTextLineHeight() - 2.0f * dpi),
                          ImGui::GetColorU32(p.valid ? theme::gray(12) : theme::gray(11)), name.c_str());
        }
        {
            theme::FontScope font(pathSize);
            draw->AddText(ImVec2(textX, rowPos.y + rowHeight * 0.5f + 4.0f * dpi), ImGui::GetColorU32(theme::gray(11)), p.directory.c_str());
        }
        draw->PopClipRect();

        ImGui::SetCursorScreenPos(after);
        ImGui::Dummy(ImVec2(0, 2.0f * dpi));
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void App::enginesTab() {
    pageTitle(tr("Engines"));
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        theme::FontScope font(theme::kBody, true);
        const float w = ImGui::CalcTextSize(iconLabel(kIconFolder, tr("Add Engine Folder...")).c_str()).x + style.FramePadding.x * 2;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - w);
    }
    ImGui::BeginDisabled(m_job.running());
    if (actionButton(kIconFolder, tr("Add Engine Folder..."), false, tr("Engines used by your projects are found automatically."))) pickFolder(1);
    ImGui::EndDisabled();
    toolchainBanner(); // only when something is missing; the compiler is in the row tooltip
    ImGui::Dummy(ImVec2(0, 4.0f));
    // Version + folder (two lines, like the project rows), how many projects use it, and [Build Editor] only while the editor isn't built
    if (ImGui::BeginTable("##engines", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Engine"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr("Used by"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(tr("Used by")).x * 2.2f);
        ImGui::TableSetupColumn("##build", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Build Editor").x * 1.6f);
        ImGui::TableHeadersRow();
        for (const Engine& e : m_catalog.engines()) {
            ImGui::PushID(e.directory.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec2 cell = ImGui::GetCursorPos();
            ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(0, ImGui::GetTextLineHeight() * 2 + 2.0f));
            if (m_toolchain.checked && m_toolchain.compiler)
                ImGui::SetItemTooltip("%s\n%s", e.directory.c_str(), trf("C++ compiler: %s", m_toolchain.visualStudio.c_str()).c_str());
            else ImGui::SetItemTooltip("%s", e.directory.c_str());
            if (ImGui::BeginPopupContextItem("##menu")) {
                if (ImGui::MenuItem(iconLabel(kIconFolderOpen, tr("Show Folder")).c_str())) SDL_OpenURL(("file:///" + e.directory).c_str());
                if (ImGui::MenuItem(iconLabel(kIconCopy, tr("Copy Path")).c_str())) SDL_SetClipboardText(e.directory.c_str());
                ImGui::EndPopup();
            }
            ImGui::SetCursorPos(cell);
            ImGui::BeginGroup();
            {
                theme::FontScope font(theme::kBody, true);
                ImGui::TextUnformatted(trf("Engine %s", e.version.c_str()).c_str());
            }
            ImGui::TextDisabled("%s", e.directory.c_str());
            ImGui::EndGroup();
            ImGui::TableNextColumn();
            std::vector<std::string> users;
            for (const Project& p : m_catalog.projects())
                if (p.valid && m_catalog.engineFor(p) == &e) users.push_back(p.name);
            if (users.empty()) ImGui::TextDisabled("-");
            else {
                ImGui::TextUnformatted(trf("%d project(s)", static_cast<int>(users.size())).c_str());
                std::string names;
                for (const std::string& n : users) names += (names.empty() ? "" : "\n") + n;
                ImGui::SetItemTooltip("%s", names.c_str());
            }
            ImGui::TableNextColumn();
            if (e.editorExe.empty()) {
                ImGui::BeginDisabled(m_job.running());
                if (actionButton(kIconBuild, tr("Build Editor"), false, tr("Projects open in the editor once it is built.")))
                    // No console, so it runs in the system code page (matches ninja's /showIncludes prefix)
                    m_job.start(tr("Editor build"), {"cmd.exe", "/d", "/c", pathJoin(e.directory, "build.bat"), "Editor"});
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Engines to install from the release index (only the newest patch of each MINOR is listed: engine docs/DECISIONS.md "How the Manager installs")
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Install"));
    const std::string index = releaseIndex();
    if (index.empty()) {
        ImGui::TextDisabled("%s", tr("No release index yet (MEALKIT_RELEASE_INDEX)."));
        return;
    }
    if (m_jobListsReleases || !m_releasesFetched) {
        ImGui::TextDisabled("%s", tr("Reading..."));
        return;
    }
    if (!m_releasesError.empty()) {
        ImGui::TextColored(theme::danger(), "%s", tr("Could not read the release index."));
        ImGui::SameLine();
        if (iconButton(kIconRefresh, tr("Retry"), true)) m_releasesFetched = false;
        ImGui::TextDisabled("%s", m_releasesError.c_str());
        return;
    }
    if (ImGui::BeginTable("##releases", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Version"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0.00.00  ").x);
        ImGui::TableSetupColumn(tr("Size"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Build Editor  ").x * 1.4f);
        ImGui::TableHeadersRow();
        for (const Release& r : m_releases) {
            ImGui::PushID(r.version.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.version.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.0f MB", static_cast<double>(r.size) / (1024.0 * 1024.0));
            ImGui::TableNextColumn();
            if (m_catalog.hasEngineVersion(r.version)) ImGui::TextUnformatted(tr("installed"));
            else {
                ImGui::BeginDisabled(m_job.running());
                if (iconButton(kIconDownload, tr("Install"), true)) requestEngineInstall(r);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// The MealKit tool family: always on the Tools page, so people see what the suite has before anything is installed or published.
// Names are the manifests' names (each tool repo's tool/mealkit-tool.txt); the icons are their app icons (ToolIcons.h)
struct SuiteTool {
    const char* name;
    const char* summary; // English source, Korean in Strings.cpp
    const char* opens;   // the manifest's opens line, shown before the tool is installed
    const unsigned char* png;
    size_t pngSize;
};
namespace {
const SuiteTool kSuiteTools[] = {
    {"MkAudio", "Sound events and mixing: how each game sound plays", ".events .wav .ogg", kIconMkAudio, sizeof(kIconMkAudio)},
    {"MkUI", "Game screens and menus: layout, styles and widgets", ".ui", kIconMkUI, sizeof(kIconMkUI)},
    {"MkRender", "Scenes, materials, lighting and terrain", ".scene .gltf .glb", kIconMkRender, sizeof(kIconMkRender)},
    {"MkQA", "Bug reports, crashes and replays in one list; runs scenario tests", ".rec", kIconMkQA, sizeof(kIconMkQA)},
};

// Glyph for a tool's tile, by the words in its name (Segoe Fluent Icons)
const char* toolGlyph(const std::string& name) {
    std::string lower = name;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("audio") != std::string::npos) return "\xEE\x9D\xA7";  // U+E767 Volume
    if (lower.find("ui") != std::string::npos) return "\xEE\x9F\xB4";     // U+E7F4 TVMonitor
    if (lower.find("render") != std::string::npos) return "\xEE\x9E\x90"; // U+E790 Color
    if (lower.find("qa") != std::string::npos) return "\xEE\xA6\xA9";     // U+E9D9 Diagnostic
    return "\xEE\xB1\xBA";                                               // U+EC7A DeveloperTools
}
// A manifest icon keyword ("audio", "ui", "render", ...) as a glyph; unknown words fall back to the name
const char* withKeyword(const std::string& keyword, const std::string& name) {
    static const std::pair<const char*, const char*> glyphs[] = {{"audio", "\xEE\x9D\xA7"}, {"ui", "\xEE\x9F\xB4"}, {"render", "\xEE\x9E\x90"},
                                                                  {"qa", "\xEE\xA6\xA9"},    {"test", "\xEE\xA6\xA9"}, {"data", "\xEE\xA3\xAF"},
                                                                  {"map", "\xEE\x9C\x87"},   {"scene", "\xEE\xA3\xBD"}};
    for (const auto& [k, g] : glyphs)
        if (keyword == k) return g;
    return toolGlyph(name);
}
} // namespace

SDL_Texture* App::suiteIcon(const SuiteTool& tool) {
    auto found = m_toolIcons.find(tool.name);
    if (found != m_toolIcons.end()) return found->second;
    SDL_Texture* texture = nullptr;
    if (renderer)
        if (SDL_Surface* surface = SDL_LoadPNG_IO(SDL_IOFromConstMem(tool.png, tool.pngSize), true)) {
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(surface);
        }
    m_toolIcons[tool.name] = texture;
    return texture;
}

std::vector<App::ToolRowView> App::toolRows() const {
    std::vector<ToolRowView> rows;
    for (const SuiteTool& s : kSuiteTools) {
        rows.push_back({});
        rows.back().name = s.name;
        rows.back().suite = &s;
    }
    auto find = [&](const std::string& name) -> ToolRowView& {
        for (ToolRowView& r : rows)
            if (r.name == name) return r;
        rows.push_back({});
        rows.back().name = name;
        return rows.back();
    };
    for (const Tool& t : m_catalog.tools()) find(t.name).installed = &t;
    if (!releaseIndex().empty())
        for (const ToolRelease& r : m_toolReleases) {
            ToolRowView& row = find(r.name);
            const bool matches = m_catalog.toolMatches(r);
            if (!row.release || (matches && !row.matches) || (matches == row.matches && versionLess(row.release->version, r.version))) {
                row.release = &r;
                row.matches = matches;
            }
        }
    // The suite first in its own order, then other tools by name
    std::stable_sort(rows.begin() + static_cast<std::ptrdiff_t>(std::size(kSuiteTools)), rows.end(),
                     [](const ToolRowView& a, const ToolRowView& b) { return a.name < b.name; });
    return rows;
}

// What this PC can install, for the editor's [Tools] menu (mkcontract available_tools.txt): the newest matching release per tool
void App::writeAvailableToolsFile() {
    std::vector<mkcontract::AvailableTool> list;
    for (const ToolRowView& row : toolRows())
        if (row.release && row.matches) list.push_back({row.name, row.release->version, row.release->summary, row.release->icon});
    mkcontract::writeAvailableTools(list);
}

bool App::toolUpdatable(const ToolRowView& row) const {
    return row.installed && row.release && row.matches && versionLess(row.installed->version, row.release->version);
}

// Installs one tool from the release index, or queues it behind the running job (one job at a time)
void App::installTool(const std::string& name) {
    if (m_job.running()) {
        if (std::find(m_toolQueue.begin(), m_toolQueue.end(), name) == m_toolQueue.end()) m_toolQueue.push_back(name);
        return;
    }
    std::string version;
    for (const ToolRowView& row : toolRows())
        if (row.name == name && row.release) version = row.release->version;
    if (m_job.start(trf("Install %s %s", name.c_str(), version.c_str()),
                    powershellArgs(installerScript(), {"tool", "-Index", releaseIndex(), "-Name", name, "-Root", toolsRoot()}))) {
        m_jobInstallsTool = name;
        m_status.clear();
        m_jobOutput.clear();
    }
}

void App::launchTool(const Tool& tool, const std::string& project) {
    std::vector<std::string> args = {tool.exe};
    if (!project.empty()) args.insert(args.end(), {"--project", project});
    std::string error;
    if (!launchDetached(args, error)) setStatus(trf("Cannot start %s: %s", tool.name.c_str(), error.c_str()), true);
}

// The project a tool opens with: the selected one, else the first project in the list that exists
std::string App::toolProject() const {
    if (const Project* p = selectedProject(); p && p->valid) return p->directory;
    for (const Project& p : m_catalog.projects())
        if (p.valid) return p.directory;
    return {};
}

// Tools page (docs/UX_REVIEW.md 3.6): a grid of cards, the suite's tools first (always listed), then any other installed or published tool.
// Each card: app icon, name, state, one-line summary, the file types it opens, and one state button
void App::toolsTab() {
    const std::vector<ToolRowView> rows = toolRows();
    const bool busy = m_job.running();
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    const std::string project = toolProject();
    const Project* projectInfo = nullptr;
    for (const Project& p : m_catalog.projects())
        if (sameDirectory(p.directory, project)) projectInfo = &p;

    // Heading: what these are, which project [Open] uses, and [Update All] when updates wait
    {
        theme::FontScope font(theme::kSubtitle, true);
        ImGui::TextUnformatted(tr("MealKit Tools"));
    }
    int updates = 0;
    for (const ToolRowView& row : rows) updates += toolUpdatable(row) ? 1 : 0;
    if (updates > 0) {
        const std::string count = trf("%d update(s)", updates);
        float w;
        {
            theme::FontScope font(theme::kBody, true);
            w = ImGui::CalcTextSize(iconLabel(kIconSync, tr("Update All")).c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        }
        const float countW = ImGui::CalcTextSize(count.c_str()).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - w - countW - 12.0f * dpi);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::accent(), "%s", count.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(busy);
        if (actionButton(kIconSync, tr("Update All"))) {
            for (const ToolRowView& row : rows)
                if (toolUpdatable(row)) installTool(row.name);
        }
        ImGui::EndDisabled();
    }
    ImGui::TextDisabled("%s", projectInfo ? trf("Tools work on the same project as the editor. [Open] opens %s.", projectInfo->name.c_str()).c_str()
                                          : tr("Tools work on the same project as the editor."));
    ImGui::Dummy(ImVec2(0, 8.0f * dpi));

    // No release index yet and something isn't installed: say where a tool goes instead of leaving cards with no button
    int notInstalled = 0;
    for (const ToolRowView& row : rows) notInstalled += row.installed ? 0 : 1;
    if (notInstalled > 0 && releaseIndex().empty()) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float pad = 14.0f * dpi;
        const float width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + pad, pos.y + pad));
        ImGui::BeginGroup();
        {
            theme::FontScope font(theme::kBody, true);
            ImGui::TextUnformatted(tr("Tool downloads aren't published yet"));
        }
        ImGui::PushTextWrapPos(pos.x + width - pad - ImGui::GetWindowPos().x);
        ImGui::TextColored(theme::gray(11), "%s",
                           tr("To use a tool now, build it from its source ([Source] on its card) and put its folder in the tools folder. It shows up here after [Refresh]."));
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 2.0f * dpi));
        if (actionButton(kIconFolderOpen, tr("Open Tools Folder"))) {
            std::error_code ec;
            std::filesystem::create_directories(mkcontract::pathOf(toolsRoot()), ec);
            SDL_OpenURL(("file:///" + toolsRoot()).c_str());
        }
        ImGui::SameLine();
        if (actionButton(kIconSync, tr("Refresh"))) reload();
        ImGui::EndGroup();
        const float bottom = ImGui::GetItemRectMax().y + pad;
        draw->ChannelsSetCurrent(0);
        draw->AddRectFilled(pos, ImVec2(pos.x + width, bottom), ImGui::GetColorU32(theme::gray(3)), theme::kCardRounding * dpi);
        draw->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(pos.x, bottom));
        ImGui::Dummy(ImVec2(0, 12.0f * dpi));
    }

    // Cards in a grid that fills the width (two columns at the default window size)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    const bool open = ImGui::BeginChild("##tools", ImVec2(0, 0));
    ImGui::PopStyleColor();
    if (open) {
        const float gap = 16.0f * dpi;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int columns = std::clamp(static_cast<int>((avail + gap) / (400.0f * dpi + gap)), 1, 3);
        const ImVec2 cardSize((avail - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns), 184.0f * dpi);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        int i = 0;
        for (const ToolRowView& row : rows) {
            const int col = i % columns, line = i / columns;
            const ImVec2 pos(origin.x + static_cast<float>(col) * (cardSize.x + gap), origin.y + static_cast<float>(line) * (cardSize.y + gap));
            ImGui::PushID(row.name.c_str());
            toolCard(row, pos, cardSize, project, projectInfo);
            ImGui::PopID();
            ++i;
        }
        const int lines = (i + columns - 1) / columns;
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(avail, static_cast<float>(lines) * (cardSize.y + gap)));
    }
    ImGui::EndChild();

    // Uninstall asks first: project files are not touched
    if (!m_confirmUninstall.empty() && !ImGui::IsPopupOpen("##uninstall")) ImGui::OpenPopup("##uninstall");
    if (ImGui::BeginPopupModal("##uninstall", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        {
            theme::FontScope font(theme::kBody, true);
            ImGui::TextUnformatted(trf("Uninstall %s?", m_confirmUninstall.c_str()).c_str());
        }
        ImGui::TextDisabled("%s", tr("Your projects and their files stay as they are."));
        ImGui::Spacing();
        bool close = false;
        if (actionButton(kIconRemove, tr("Uninstall"), true)) {
            if (m_job.start(trf("Uninstall %s", m_confirmUninstall.c_str()),
                            powershellArgs(installerScript(), {"removetool", "-Name", m_confirmUninstall, "-Root", toolsRoot()}))) {
                m_status.clear();
                m_jobOutput.clear();
            }
            close = true;
        }
        ImGui::SameLine();
        if (actionButton(kIconCancel, tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
        if (close) {
            m_confirmUninstall.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// One tool card: icon and name with its state, the summary, then the file types it opens and the one state button along the bottom
void App::toolCard(const ToolRowView& row, ImVec2 pos, ImVec2 size, const std::string& project, const Project* projectInfo) {
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    const bool busy = m_job.running();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(pos, end);
    draw->AddRectFilled(pos, end, ImGui::GetColorU32(hovered ? theme::gray(4) : theme::gray(3)), theme::kCardRounding * dpi);
    const float pad = 18.0f * dpi;
    const float icon = 64.0f * dpi;
    const ImVec2 i0(pos.x + pad, pos.y + pad);

    // App icon (the suite's own), else a glyph tile from the manifest's icon keyword
    if (SDL_Texture* texture = row.suite ? suiteIcon(*row.suite) : nullptr) {
        draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(texture)), i0, ImVec2(i0.x + icon, i0.y + icon));
    } else {
        draw->AddRectFilled(i0, ImVec2(i0.x + icon, i0.y + icon), ImGui::GetColorU32(theme::gray(6)), icon * 0.22f);
        if (iconFont) {
            theme::FontScope font(theme::kTitle);
            const std::string keyword = row.installed && !row.installed->icon.empty() ? row.installed->icon : row.release ? row.release->icon : std::string();
            const char* glyph = keyword.empty() ? toolGlyph(row.name) : withKeyword(keyword, row.name);
            const ImVec2 g = ImGui::CalcTextSize(glyph);
            draw->AddText(ImVec2(i0.x + (icon - g.x) * 0.5f, i0.y + (icon - g.y) * 0.5f), ImGui::GetColorU32(theme::gray(12)), glyph);
        }
    }
    if (row.installed && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(i0, ImVec2(i0.x + icon, i0.y + icon)))
        ImGui::SetTooltip("%s", row.installed->folder.c_str());

    // What the button does, and the line under the name
    enum class Kind { None, Open, Install, Update, InstallEngine, Settings, Source } kind = Kind::None;
    std::string label, state;
    ImVec4 stateColor = theme::gray(11);
    const Release* engineRelease = nullptr;
    if (row.installed && row.installed->protocol != kLiveProtocol) {
        state = tr("Made for another MealKit Manager");
        stateColor = theme::warning();
        if (row.release && row.matches) kind = Kind::Update, label = trf("Update to %s", row.release->version.c_str());
    } else if (toolUpdatable(row)) {
        kind = Kind::Update;
        label = trf("Update to %s", row.release->version.c_str());
        state = row.installed->version + "  ->  " + row.release->version;
        stateColor = theme::accent();
    } else if (row.installed) {
        kind = Kind::Open;
        label = tr("Open");
        state = row.installed->version.empty() ? std::string(tr("Installed")) : trf("%s installed", row.installed->version.c_str());
        stateColor = theme::good();
    } else if (row.release && row.matches) {
        kind = Kind::Install;
        label = trf("Install  %.0f MB", static_cast<double>(row.release->size) / (1024.0 * 1024.0));
        state = trf("%s, not installed", row.release->version.c_str());
    } else if (row.release && row.release->protocol != kLiveProtocol) {
        kind = Kind::Settings;
        label = tr("Update MealKit Manager");
        state = tr("Needs a newer MealKit Manager");
        stateColor = theme::warning();
    } else if (row.release) {
        // No installed engine fits: offer the newest engine of a MINOR the tool lists
        for (const Release& r : m_releases)
            if (std::find(row.release->engines.begin(), row.release->engines.end(), minorOf(r.version)) != row.release->engines.end() &&
                (!engineRelease || versionLess(engineRelease->version, r.version)))
                engineRelease = &r;
        std::string list;
        for (const std::string& e : row.release->engines) list += (list.empty() ? "" : ", ") + e;
        state = trf("Needs engine %s", list.c_str());
        stateColor = theme::warning();
        if (engineRelease) kind = Kind::InstallEngine, label = trf("Install Engine %s", engineRelease->version.c_str());
    } else if (releaseIndex().empty()) {
        state = tr("Not installed");
        if (row.suite) kind = Kind::Source, label = tr("Source");
    } else if (m_jobListsReleases || !m_releasesFetched) {
        state = tr("Reading...");
    } else {
        state = tr("Not installed - not in the release index");
    }

    // Name and state, right of the icon
    const float textX = i0.x + icon + 16.0f * dpi;
    draw->PushClipRect(ImVec2(textX, pos.y), ImVec2(end.x - pad, end.y), true);
    float y = i0.y + 4.0f * dpi;
    {
        theme::FontScope font(theme::kSubtitle, true);
        draw->AddText(ImVec2(textX, y), ImGui::GetColorU32(theme::gray(12)), row.name.c_str());
        y += ImGui::GetTextLineHeight() + 6.0f * dpi;
    }
    {
        theme::FontScope font(theme::kCaption);
        float x = textX;
        if (kind == Kind::Open) {
            const float r = 3.5f * dpi;
            draw->AddCircleFilled(ImVec2(x + r, y + ImGui::GetTextLineHeight() * 0.5f), r, ImGui::GetColorU32(theme::good()));
            x += r * 2 + 6.0f * dpi;
        }
        draw->AddText(ImVec2(x, y), ImGui::GetColorU32(stateColor), state.c_str());
    }
    draw->PopClipRect();

    // Summary under the icon, wrapped to the card
    const std::string summary = row.suite ? std::string(tr(row.suite->summary))
                                : row.installed && !row.installed->summary.empty() ? row.installed->summary
                                : row.release ? row.release->summary
                                              : std::string();
    const float button = ImGui::GetFrameHeight();
    const float bottomY = end.y - pad - button;
    if (!summary.empty()) {
        theme::FontScope font(theme::kBody);
        draw->PushClipRect(pos, ImVec2(end.x, bottomY - 4.0f * dpi), true);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(i0.x, i0.y + icon + 14.0f * dpi), ImGui::GetColorU32(theme::gray(11)),
                      summary.c_str(), nullptr, size.x - pad * 2);
        draw->PopClipRect();
    }

    // Bottom right: More (installed only), then the state button (Open is a split button: the arrow picks the project)
    float right = end.x - pad;
    if (row.installed) {
        right -= button;
        ImGui::SetCursorScreenPos(ImVec2(right, bottomY));
        if (iconButton(kIconMore, tr("More"))) ImGui::OpenPopup("##more");
        if (ImGui::BeginPopup("##more")) {
            if (ImGui::MenuItem(iconLabel(kIconFolderOpen, tr("Show Folder")).c_str())) SDL_OpenURL(("file:///" + row.installed->folder).c_str());
            const bool ours = sameDirectory(row.installed->folder, pathJoin(toolsRoot(), row.name));
            ImGui::BeginDisabled(busy || !ours);
            if (ImGui::MenuItem(iconLabel(kIconRemove, tr("Uninstall...")).c_str())) m_confirmUninstall = row.name;
            ImGui::EndDisabled();
            if (!ours) ImGui::SetItemTooltip("%s", tr("Found through a toolPath line: remove it from that folder."));
            ImGui::EndPopup();
        }
        right -= 6.0f * dpi;
    }
    if (kind != Kind::None) {
        const char* glyph = kind == Kind::Open       ? kIconLaunch
                            : kind == Kind::Update   ? kIconSync
                            : kind == Kind::Settings ? kIconSettings
                            : kind == Kind::Source   ? kIconLaunch
                                                     : kIconDownload;
        float w;
        {
            theme::FontScope font(theme::kBody, true);
            w = ImGui::CalcTextSize(iconLabel(glyph, label.c_str()).c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        }
        const float arrow = kind == Kind::Open ? button : 0.0f;
        right -= w + arrow;
        ImGui::SetCursorScreenPos(ImVec2(right, bottomY));
        ImGui::BeginDisabled(busy && kind != Kind::Open && kind != Kind::Settings && kind != Kind::Source);
        const std::string openTip = kind == Kind::Source ? std::string(tr("Opens the tool's source repository (how to build it is in its README)"))
                                    : kind == Kind::Open && projectInfo ? trf("Opens with %s", projectInfo->name.c_str())
                                                         : std::string();
        // The card's one primary action: Install and Update are accent-filled, Open stays quiet (it's the usual state)
        const bool primary = kind == Kind::Install || kind == Kind::Update;
        if (actionButton(glyph, label, primary, openTip.empty() ? nullptr : openTip.c_str())) {
            if (kind == Kind::Open) launchTool(*row.installed, project);
            else if (kind == Kind::Source) SDL_OpenURL(("https://github.com/MealKit-Engine/" + row.name).c_str());
            else if (kind == Kind::Install || kind == Kind::Update) installTool(row.name);
            else if (kind == Kind::InstallEngine) requestEngineInstall(*engineRelease);
            else if (kind == Kind::Settings) m_openSettings = true;
        }
        ImGui::EndDisabled();
        if (kind == Kind::Open) {
            ImGui::SetCursorScreenPos(ImVec2(right + w, bottomY));
            if (iconButton(kIconChevronDown, tr("Open with..."))) ImGui::OpenPopup("##openwith");
            if (ImGui::BeginPopup("##openwith")) {
                int shown = 0;
                for (const Project& p : m_catalog.projects()) {
                    if (!p.valid || shown >= 12) continue;
                    ++shown;
                    if (ImGui::MenuItem(p.name.c_str())) launchTool(*row.installed, p.directory);
                    ImGui::SetItemTooltip("%s", p.directory.c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem(tr("Without a project"))) launchTool(*row.installed, {});
                ImGui::EndPopup();
            }
        }
    }

    // Bottom left: the file types it opens, as small tags (clipped before the buttons)
    std::vector<std::string> opens;
    if (row.installed) opens = row.installed->opens;
    else if (row.suite) {
        std::string word;
        for (const char* c = row.suite->opens;; ++c) {
            if (*c == ' ' || *c == '\0') {
                if (!word.empty()) opens.push_back(word);
                word.clear();
                if (*c == '\0') break;
            } else {
                word += *c;
            }
        }
    }
    if (!opens.empty()) {
        theme::FontScope font(theme::kCaption);
        const float tagPad = 6.0f * dpi;
        const float tagH = ImGui::GetTextLineHeight() + 4.0f * dpi;
        const float tagY = bottomY + (button - tagH) * 0.5f;
        float x = i0.x;
        draw->PushClipRect(ImVec2(x, pos.y), ImVec2(std::max(x, right - 12.0f * dpi), end.y), true);
        for (const std::string& ext : opens) {
            const float w = ImGui::CalcTextSize(ext.c_str()).x + tagPad * 2;
            draw->AddRectFilled(ImVec2(x, tagY), ImVec2(x + w, tagY + tagH), ImGui::GetColorU32(theme::gray(6)), 4.0f * dpi);
            draw->AddText(ImVec2(x + tagPad, tagY + 2.0f * dpi), ImGui::GetColorU32(theme::gray(11)), ext.c_str());
            x += w + 6.0f * dpi;
        }
        draw->PopClipRect();
        if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(i0.x, tagY), ImVec2(std::min(x, right), tagY + tagH)))
            ImGui::SetTooltip("%s", tr("File types it opens"));
    }
}

// Project row menu: Open with a tool. Tools that aren't installed yet are installed first, then opened
void App::openWithMenu(const Project& p, bool busy) {
    const std::vector<ToolRowView> rows = toolRows();
    // Only when a tool can be opened or installed (the suite's tools are always in the rows, installed or not)
    if (std::none_of(rows.begin(), rows.end(), [](const ToolRowView& r) { return r.installed || (r.release && r.matches); })) return;
    ImGui::BeginDisabled(!p.valid);
    if (ImGui::BeginMenu(iconLabel(kIconTools, tr("Open with Tool")).c_str())) {
        for (const ToolRowView& row : rows) {
            if (row.installed && row.installed->protocol == kLiveProtocol) {
                if (ImGui::MenuItem(row.name.c_str())) launchTool(*row.installed, p.directory);
            } else if (!row.installed && row.release && row.matches) {
                ImGui::BeginDisabled(busy);
                if (ImGui::MenuItem(trf("Install %s, then open", row.name.c_str()).c_str())) {
                    m_openAfterInstall = row.name;
                    m_openAfterInstallProject = p.directory;
                    installTool(row.name);
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::EndMenu();
    }
    ImGui::EndDisabled();
}

void App::newProjectPopup() {
    if (m_openNew) {
        ImGui::OpenPopup("##new");
        m_openNew = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.6f, 0));
    if (!ImGui::BeginPopupModal("##new", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(tr("New Project"));
    ImGui::Separator();
    const std::vector<Engine>& engines = m_catalog.engines();
    if (m_newEngine >= static_cast<int>(engines.size())) m_newEngine = 0;

    ImGui::InputText(tr("Name"), m_newName, sizeof(m_newName));
    const bool nameOk = validGameName(m_newName);
    if (!nameOk) ImGui::TextColored(theme::danger(), "%s", tr("Start with a letter; letters, digits and _ only (it becomes the exe name)."));
    ImGui::InputText(tr("Location"), m_newLocation, sizeof(m_newLocation));
    ImGui::SameLine();
    if (iconButton(kIconFolder, tr("Location"))) pickFolder(2);
    if (engines.empty()) ImGui::TextColored(theme::danger(), "%s", tr("No engine found. Add one on the Engines tab."));
    else {
        const std::string preview = engines[m_newEngine].version + "  " + engines[m_newEngine].directory;
        if (ImGui::BeginCombo(tr("Engine"), preview.c_str())) {
            for (int i = 0; i < static_cast<int>(engines.size()); ++i) {
                const std::string item = engines[i].version + "  " + engines[i].directory;
                if (ImGui::Selectable(item.c_str(), i == m_newEngine)) m_newEngine = i;
            }
            ImGui::EndCombo();
        }
    }
    // Project templates: engines that have templates\empty take new_project.ps1 -Template (older ones only make arena projects)
    const bool templates = !engines.empty() && fileExists(pathJoin(engines[m_newEngine].directory, "templates\\empty\\source\\app\\GameSession.cpp"));
    if (!templates) m_newTemplate = 0;
    else {
        const char* const labels[] = {tr("Arena versus (sample game)"), tr("Empty")};
        if (ImGui::BeginCombo(tr("Template"), labels[m_newTemplate])) {
            for (int i = 0; i < 2; ++i)
                if (ImGui::Selectable(labels[i], i == m_newTemplate)) m_newTemplate = i;
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("%s", m_newTemplate == 0
                                      ? tr("A copy of the sample game: physics party versus with AI, boxes, jump pads and cutscenes.")
                                      : tr("One character per player on a floor, with the title, lobby and results screens."));
    }
    ImGui::TextDisabled("%s", pathJoin(m_newLocation, m_newName).c_str());
    ImGui::Separator();
    ImGui::BeginDisabled(!nameOk || engines.empty() || !m_newLocation[0]);
    if (ImGui::Button(tr("Create"))) {
        saveProjectLocation(m_newLocation);
        const Engine& engine = engines[m_newEngine];
        std::vector<std::string> args = {"-Name", m_newName, "-Path", m_newLocation};
        if (m_newTemplate == 1) args.insert(args.end(), {"-Template", "empty"});
        runScript(tr("New project"), pathJoin(engine.directory, "tools\\new_project.ps1"), args, pathJoin(m_newLocation, m_newName));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::statusBar() {
    ImGui::AlignTextToFramePadding(); // on the gutter like the editor status bar, no line above
    if (m_job.running()) {
        // A turning arc and the time so far, so a long build reads as working rather than stuck
        const float size = ImGui::GetTextLineHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
        const float start = static_cast<float>(ImGui::GetTime()) * 6.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PathArcTo(center, size * 0.38f, start, start + 4.2f, 16);
        draw->PathStroke(ImGui::GetColorU32(ImGuiCol_CheckMark), ImDrawFlags_None, size * 0.14f);
        ImGui::Dummy(ImVec2(size, size));
        ImGui::SameLine();
        ImGui::Text("%s...", m_job.label().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%ds)", static_cast<int>(m_job.seconds()));
        return;
    }
    if (m_statusError) ImGui::TextColored(theme::danger(), "%s", m_status.c_str());
    else ImGui::TextUnformatted(m_status.c_str());
    if (!m_jobOutput.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::danger()); // the failure's output, in the error color
        const bool show = iconButton(kIconOutput, tr("Output"), true);
        ImGui::PopStyleColor();
        if (show) ImGui::OpenPopup("##output");
        if (ImGui::BeginPopup("##output")) {
            if (iconButton(kIconCopy, tr("Copy"), true)) SDL_SetClipboardText(m_jobOutput.c_str());
            ImGui::InputTextMultiline("##text", m_jobOutput.data(), m_jobOutput.size() + 1,
                                      ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.8f, ImGui::GetTextLineHeight() * 20),
                                      ImGuiInputTextFlags_ReadOnly);
            ImGui::EndPopup();
        }
    }
    if (m_statusError) { // a failure stays until closed
        ImGui::SameLine();
        if (iconButton(kIconCancel, tr("Close"), true)) m_status.clear();
    }
}

// Settings: a modal with General and Engines on its left (the things people change now and then; docs/UX_REVIEW.md 3.4)
void App::settingsPopup() {
    if (m_openSettings) {
        ImGui::OpenPopup("##settings");
        m_openSettings = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.8f, viewport->WorkSize.y * 0.8f));
    if (!ImGui::BeginPopupModal("##settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return;
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    {
        theme::FontScope font(theme::kSubtitle, true);
        ImGui::TextUnformatted(tr("Settings"));
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    if (iconButton(kIconCancel, tr("Close"), false, "Esc") || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput))
        ImGui::CloseCurrentPopup();
    ImGui::Spacing();

    struct Item {
        SettingsPage page;
        const char* glyph;
        const char* label;
    };
    const Item items[] = {{SettingsPage::General, kIconSettings, tr("General")}, {SettingsPage::Engines, kIconEngines, tr("Engines")}};
    // Nav items: padded off the panel's edges, each label vertically centered with a gap from the left wall
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * dpi, 8.0f * dpi));
    ImGui::BeginChild("##settingsnav", ImVec2(190.0f * dpi, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    const float itemHeight = 40.0f * dpi;
    for (const Item& item : items) {
        const bool current = m_settingsPage == item.page;
        ImGui::PushID(item.label);
        ImGui::PushStyleColor(ImGuiCol_Header, theme::gray(5));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##nav", current, 0, ImVec2(0, itemHeight))) m_settingsPage = item.page;
        ImGui::PopStyleColor();
        theme::FontScope font(theme::kBody, current);
        const std::string label = iconLabel(item.glyph, item.label);
        const float textY = at.y + (itemHeight - ImGui::GetTextLineHeight()) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 12.0f * dpi, textY), ImGui::GetColorU32(current ? theme::gray(12) : theme::gray(11)),
                                            label.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##settingspage", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (m_settingsPage == SettingsPage::General) generalSettings();
    else enginesTab();
    ImGui::EndChild();
    ImGui::EndPopup();
}

void App::generalSettings() {
    pageTitle(tr("General"));
    ImGui::Spacing();

    // Language: the editor settings' language line, shared with the editor and the tools (docs/UX_REVIEW.md 3.4)
    {
        struct Option {
            const char* code;
            const char* label; // each language's own name, never translated
        };
        static const Option options[] = {{"ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4"}, {"en", "English"}}; // 한국어
        const char* current = currentLanguage();
        const char* preview = options[0].label;
        for (const Option& o : options)
            if (std::strcmp(o.code, current) == 0) preview = o.label;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Language"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f * ImGui::GetStyle().FontScaleDpi);
        if (ImGui::BeginCombo("##language", preview)) {
            for (const Option& o : options)
                if (ImGui::Selectable(o.label, std::strcmp(o.code, current) == 0) && std::strcmp(o.code, current) != 0) {
                    if (mkcontract::setEditorSetting("language", o.code)) setLanguage(o.code);
                    else setStatus(trf("Cannot save the language: %s", userDirectory().c_str()), true);
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("The editor and the tools use the same language."));
    }
    ImGui::Spacing();
    ImGui::SeparatorText(tr("New projects"));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Default location"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputText("##location", m_newLocation, sizeof(m_newLocation), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
        saveProjectLocation(m_newLocation);
    ImGui::SameLine();
    if (iconButton(kIconFolder, tr("Location"))) pickFolder(3);

    ImGui::Spacing();
    ImGui::SeparatorText("MealKit Manager");
    ImGui::TextUnformatted(trf("Version %s", MKMANAGER_VERSION).c_str());
    // A newer Manager in the release index: applied only to the installed copy (a dev build in buildin is left alone)
    if (managerUpdateAvailable()) {
        ImGui::TextColored(theme::accent(), "%s", trf("MealKit Manager %s is available.", m_managerRelease.version.c_str()).c_str());
        const char* base = SDL_GetBasePath();
        const std::string installed = mkcontract::joinPath(userDirectory(), "manager");
        if (base && sameDirectory(base, installed)) {
            ImGui::BeginDisabled(m_job.running());
            if (actionButton(kIconSync, tr("Update and Restart"), true) &&
                m_job.start(tr("Manager update"), powershellArgs(installerScript(), {"update", "-Index", releaseIndex(), "-To", installed})))
                m_jobUpdatesManager = true;
            ImGui::EndDisabled();
        }
    }
    ImGui::TextDisabled("%s", userDirectory().c_str());
    ImGui::SameLine();
    if (iconButton(kIconFolderOpen, tr("Show Folder"), true)) SDL_OpenURL(("file:///" + userDirectory()).c_str());
}

// Tools of the release index made for this engine's MINOR and this Manager's protocol, not installed at that version yet (newest per name)
std::vector<const ToolRelease*> App::toolsForEngine(const std::string& version) const {
    std::vector<const ToolRelease*> out;
    const std::string minor = minorOf(version);
    for (const ToolRelease& r : m_toolReleases) {
        if (r.protocol != kLiveProtocol || std::find(r.engines.begin(), r.engines.end(), minor) == r.engines.end()) continue;
        const Tool* installed = m_catalog.installedTool(r.name);
        if (installed && !versionLess(installed->version, r.version)) continue;
        auto same = std::find_if(out.begin(), out.end(), [&](const ToolRelease* t) { return t->name == r.name; });
        if (same == out.end()) out.push_back(&r);
        else if (versionLess((*same)->version, r.version)) *same = &r;
    }
    return out;
}

void App::requestEngineInstall(const Release& release) {
    const std::vector<const ToolRelease*> tools = toolsForEngine(release.version);
    if (tools.empty()) {
        installEngine(release);
        return;
    }
    m_engineToInstall = release;
    m_engineTools.clear();
    for (const ToolRelease* t : tools) m_engineTools.push_back({t->name, true});
}

void App::engineInstallPopup() {
    if (!m_engineTools.empty() && !ImGui::IsPopupOpen("##engineinstall")) ImGui::OpenPopup("##engineinstall");
    if (!ImGui::BeginPopupModal("##engineinstall", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    const std::vector<const ToolRelease*> tools = toolsForEngine(m_engineToInstall.version);
    {
        theme::FontScope font(theme::kBody, true);
        ImGui::TextUnformatted(trf("Install engine %s", m_engineToInstall.version.c_str()).c_str());
    }
    ImGui::TextDisabled("%s", tr("Tools for this engine can come along:"));
    ImGui::Spacing();
    double total = static_cast<double>(m_engineToInstall.size);
    ImGui::TextUnformatted(trf("Engine %s", m_engineToInstall.version.c_str()).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f MB", static_cast<double>(m_engineToInstall.size) / (1024.0 * 1024.0));
    for (auto& [name, on] : m_engineTools) {
        const ToolRelease* release = nullptr;
        for (const ToolRelease* t : tools)
            if (t->name == name) release = t;
        if (!release) continue;
        checkbox((name + " " + release->version + "##" + name).c_str(), &on);
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f MB", static_cast<double>(release->size) / (1024.0 * 1024.0));
        if (on) total += static_cast<double>(release->size);
    }
    ImGui::Separator();
    ImGui::TextUnformatted(trf("Total %.0f MB", total / (1024.0 * 1024.0)).c_str());
    ImGui::Spacing();
    bool close = false;
    ImGui::BeginDisabled(m_job.running());
    if (actionButton(kIconDownload, tr("Install"), true)) {
        installEngine(m_engineToInstall);
        for (const auto& [name, on] : m_engineTools)
            if (on) m_toolQueue.push_back(name); // after the engine: tools are matched to installed engines
        close = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (actionButton(kIconCancel, tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
    if (close) {
        m_engineTools.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::installEngine(const Release& release) {
    if (!m_job.start(trf("Install engine %s", release.version.c_str()),
                     powershellArgs(installerScript(), {"engine", "-Index", releaseIndex(), "-Version", release.version, "-Root", enginesRoot()})))
        return;
    m_jobInstallsEngine = true;
    setStatus(trf("Installing engine %s...", release.version.c_str()), false);
    m_jobOutput.clear();
}

void App::openProject(const Project& project) {
    bool byVersion = false;
    const Engine* engine = m_catalog.engineFor(project, &byVersion);
    if (!engine) {
        setStatus(trf("The engine of %s was not found: %s", project.name.c_str(), project.engine.c_str()), true);
        return;
    }
    if (engine->editorExe.empty()) {
        setStatus(trf("The editor of engine %s is not built yet - build it on the Engines tab.", engine->version.c_str()), true);
        return;
    }
    // Found by version: point the engine line at this PC's engine, since the project's build.bat builds with that folder
    std::string repointed;
    if (byVersion) {
        if (!setProjectEngine(project.directory, engine->directory)) {
            setStatus(trf("Cannot update the engine line of %s", project.name.c_str()), true);
            return;
        }
        repointed = trf(" (engine line now %s)", engine->directory.c_str());
    }
    std::string error;
    if (!launchDetached({engine->editorExe, "--project", project.directory}, error)) {
        setStatus(trf("Cannot start the editor: %s", error.c_str()), true);
        return;
    }
    m_catalog.addRecent(project.directory); // the editor adds it too when opening, but this fixes the list order right away
    setStatus(trf("Opened %s", project.name.c_str()) + repointed, false);
    reload();
}

void App::runScript(const std::string& label, const std::string& script, const std::vector<std::string>& args,
                    const std::string& addOnSuccess) {
    if (!m_job.start(label, powershellArgs(script, args))) return;
    m_jobAdd = addOnSuccess;
    m_status.clear();
    m_jobOutput.clear();
}

void App::pickFolder(int purpose, const std::string& project) {
    {
        std::lock_guard<std::mutex> lock(m_pickMutex);
        m_pickPurpose = purpose;
        m_pickProject = project;
        m_picked = "\x01"; // waiting
    }
    g_pickTarget = {&m_pickMutex, &m_picked};
    const char* start = purpose == 2 || purpose == 3 ? m_newLocation : nullptr;
    std::string parent;
    if (purpose == 5) {
        parent = project.substr(0, project.find_last_of("\\/"));
        start = parent.c_str();
    }
    SDL_ShowOpenFolderDialog(onFolderPicked, &g_pickTarget, window, start, false);
}

void App::takePickedFolder() {
    std::string picked;
    int purpose;
    {
        std::lock_guard<std::mutex> lock(m_pickMutex);
        if (m_pickPurpose < 0 || m_picked == "\x01") return;
        picked = m_picked;
        purpose = m_pickPurpose;
        m_pickPurpose = -1;
    }
    if (picked.empty()) return; // canceled
    if (purpose == 0) {
        Project p;
        if (!readProject(picked, p)) {
            setStatus(trf("%s has no mealkit.project", picked.c_str()), true);
            return;
        }
        m_catalog.addRecent(p.directory);
        m_selected = p.directory;
        setStatus(trf("Imported %s", p.name.c_str()), false);
    } else if (purpose == 1) {
        Engine e;
        if (!readEngine(picked, e)) {
            setStatus(trf("%s is not a MealKit engine folder (VERSION, tools\\new_project.ps1)", picked.c_str()), true);
            return;
        }
        m_catalog.addEngine(e.directory);
        setStatus(trf("Added engine %s", e.version.c_str()), false);
    } else if (purpose == 4) { // the missing project is here now
        Project p;
        if (!readProject(picked, p)) {
            setStatus(trf("%s has no mealkit.project", picked.c_str()), true);
            return;
        }
        m_catalog.replaceProject(m_pickProject, p.directory);
        m_selected = p.directory;
        setStatus(trf("Found %s: %s", p.name.c_str(), p.directory.c_str()), false);
    } else if (purpose == 5) { // move into the picked folder, keeping the folder's own name
        const std::string leaf = m_pickProject.substr(m_pickProject.find_last_of("\\/") + 1);
        const std::string to = pathJoin(picked, leaf);
        if (sameDirectory(to, m_pickProject)) return;
        m_confirmMoveFor = m_pickProject;
        m_confirmMoveTo = to;
    } else {
        std::snprintf(m_newLocation, sizeof(m_newLocation), "%s", picked.c_str());
        if (purpose == 3) saveProjectLocation(m_newLocation);
    }
    reload();
}

} // namespace mkmanager
