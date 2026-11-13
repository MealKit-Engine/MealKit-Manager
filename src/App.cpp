#include "App.h"

#include "Logo.h"
#include "Strings.h"
#include "Theme.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace mkmanager {
namespace {

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
            m_releasesError = m_job.exitCode() != 0 ? m_job.output() : std::string();
        } else {
            finishJob();
        }
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
    if (m_jobUpdatesManager) { // new files are in place: start the new Manager and quit this one
        m_jobUpdatesManager = false;
        std::string error;
        if (ok && launchDetached({mkcontract::joinPath(mkcontract::joinPath(userDirectory(), "manager"), "MealKitManager.exe")}, error))
            quitRequested = true;
        else if (ok)
            setStatus(trf("Updated, but the new Manager didn't start: %s", error.c_str()), true);
    }
    if (m_jobInstallsEngine) {
        m_jobInstallsEngine = false;
        const std::string folder = installedFolder(m_job.output());
        if (ok && !folder.empty()) m_catalog.addEngine(folder);
    }
    reload();
}

App::~App() {
    for (auto& [folder, texture] : m_icons)
        if (texture) SDL_DestroyTexture(texture);
    if (m_logo) SDL_DestroyTexture(m_logo);
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
    ImGui::SetItemTooltip("MealKit Manager %s\n%s", MKMANAGER_VERSION, userDirectory().c_str());

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
    }
    ImGui::PopStyleVar(3);

    // Settings, with a dot when a newer Manager is in the release index
    const float button = ImGui::GetFrameHeight();
    ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - button, top + (height - button) * 0.5f));
    const ImVec2 gear = ImGui::GetCursorScreenPos();
    if (iconButton(kIconSettings, tr("Settings"), false, managerUpdateAvailable() ? tr("A new MealKit Manager is available.") : "Ctrl+,")) m_openSettings = true;
    if (managerUpdateAvailable())
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(gear.x + button - 5.0f * dpi, gear.y + 5.0f * dpi), 3.5f * dpi, ImGui::GetColorU32(theme::accent()));
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
    const float rounding = 8.0f * ImGui::GetStyle().FontScaleDpi;
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

void App::projectMenu(const Project& p, bool busy) {
    ImGui::BeginDisabled(busy || !p.valid);
    if (ImGui::MenuItem(iconLabel(kIconLaunch, tr("Open in Editor")).c_str(), "Enter")) requestRow(p.directory, RowAction::Open);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!p.valid);
    if (ImGui::MenuItem(iconLabel(kIconFolderOpen, tr("Show Folder")).c_str())) SDL_OpenURL(("file:///" + p.directory).c_str());
    ImGui::EndDisabled();
    if (ImGui::MenuItem(iconLabel(kIconCopy, tr("Copy Path")).c_str())) SDL_SetClipboardText(p.directory.c_str());
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

    int missing = 0;
    for (const Project& p : projects) missing += p.valid ? 0 : 1;

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
    if (missing > 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::danger(), "%s", trf("%d project(s) whose folder can't be found", missing).c_str());
        ImGui::SameLine();
        if (iconButton(kIconRemove, tr("Remove from List"), true, tr("Takes projects whose folder or mealkit.project is gone off the list (nothing is deleted)."),
                       tr("Remove from List")))
            removeMissingProjects();
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
            } else if (action == RowAction::InstallEngine) {
                if (const Release* release = Catalog::releaseFor(p, m_releases)) installEngine(*release);
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
    for (const Project& p : m_catalog.projects())
        if (containsNoCase(p.name, m_search) || containsNoCase(p.directory, m_search)) rows.push_back(&p);
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
        ImGui::TextDisabled("%s", tr("No projects match the search."));
    }

    const float dpi = ImGui::GetStyle().FontScaleDpi;
    const float rowHeight = 52.0f * dpi;
    const float tile = 36.0f * dpi;
    const float pad = 10.0f * dpi;
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
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_selected = p.directory;
        if (ImGui::BeginPopupContextItem("##menu")) {
            projectMenu(p, busy);
            ImGui::EndPopup();
        }
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (isSelected) {
            const float bar = 3.0f * dpi;
            draw->AddRectFilled(ImVec2(rowPos.x, rowPos.y + rowHeight * 0.2f), ImVec2(rowPos.x + bar, rowPos.y + rowHeight * 0.8f),
                                ImGui::GetColorU32(theme::accent()), bar);
        }

        // Favorite star first, always shown (off = gray outline, on = orange fill)
        const float button = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos(ImVec2(rowPos.x + pad, rowPos.y + (rowHeight - button) * 0.5f));
        const char* star = iconFont ? (p.favorite ? kIconStarFilled : kIconStar) : (p.favorite ? "\xE2\x98\x85" : "\xE2\x98\x86");
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::gray(4));
        ImGui::PushStyleColor(ImGuiCol_Text, p.favorite ? theme::warning() : theme::gray(9));
        const bool starPressed = ImGui::Button((std::string(star) + "##fav").c_str(), ImVec2(button, button));
        ImGui::PopStyleColor(3);
        if (starPressed) {
            m_pendingFavorite = p.directory;
            m_pendingFavoriteOn = !p.favorite;
        }
        ImGui::SetItemTooltip("%s", tr("Favorite"));

        const float tileX = rowPos.x + pad + button + 6.0f * dpi;
        ImGui::SetCursorScreenPos(ImVec2(tileX, rowPos.y + (rowHeight - tile) * 0.5f));
        projectTile(p, tile);
        if (p.valid && isSelected && !busy) {
            const ImVec2 tileMin = ImGui::GetItemRectMin(), tileMax = ImGui::GetItemRectMax();
            ImGui::SetCursorScreenPos(tileMin);
            if (ImGui::InvisibleButton("##icon", ImVec2(tile, tile))) pickIcon(p.directory);
            if (ImGui::IsItemHovered()) {
                draw->AddRectFilled(tileMin, tileMax, IM_COL32(0, 0, 0, 120), 8.0f * dpi);
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
        if (p.valid && !engine) {
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
            theme::FontScope font(theme::kCaption);
            const ImVec2 text = ImGui::CalcTextSize(version.c_str());
            const float tagPad = 6.0f * dpi;
            right -= text.x + tagPad * 2 + 10.0f * dpi;
            const float y = rowPos.y + (rowHeight - text.y - 4.0f * dpi) * 0.5f;
            draw->AddRectFilled(ImVec2(right, y), ImVec2(right + text.x + tagPad * 2, y + text.y + 4.0f * dpi), ImGui::GetColorU32(theme::gray(6)),
                                4.0f * dpi);
            draw->AddText(ImVec2(right + tagPad, y + 2.0f * dpi), ImGui::GetColorU32(theme::gray(11)), version.c_str());
        }

        // Name and path, clipped before the right side so nothing there is pushed out
        const float textX = tileX + tile + pad;
        const float textRight = std::max(textX, right - 12.0f * dpi);
        std::string name = p.name;
        if (!p.valid) {
            const size_t slash = p.directory.find_last_of("\\/");
            name = slash == std::string::npos ? p.directory : p.directory.substr(slash + 1);
        }
        draw->PushClipRect(ImVec2(textX, rowPos.y), ImVec2(textRight, rowPos.y + rowHeight), true);
        {
            theme::FontScope font(theme::kBody, true);
            draw->AddText(ImVec2(textX, rowPos.y + rowHeight * 0.5f - ImGui::GetTextLineHeight() - 1.0f * dpi),
                          ImGui::GetColorU32(p.valid ? theme::gray(12) : theme::gray(11)), name.c_str());
        }
        {
            theme::FontScope font(theme::kCaption);
            draw->AddText(ImVec2(textX, rowPos.y + rowHeight * 0.5f + 2.0f * dpi), ImGui::GetColorU32(theme::gray(11)), p.directory.c_str());
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
                if (iconButton(kIconDownload, tr("Install"), true)) installEngine(r);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void App::toolsTab() {
    const Project* project = selectedProject();
    if (project && !project->valid) project = nullptr;
    if (m_catalog.tools().empty()) {
        // No empty table: say where tools come from (the folder only here, not as a permanent line)
        ImGui::Spacing();
        ImGui::TextUnformatted(tr("No tools installed."));
        ImGui::TextDisabled("%s", !releaseIndex().empty() && !m_toolReleases.empty()
                                      ? tr("Install one from the list below.")
                                      : tr("Unzip a tool into %LOCALAPPDATA%\\MealKit\\tools\\<tool>\\, then press F5."));
    } else if (ImGui::BeginTable("##tools", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Tool"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr("Version"), ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn(tr("Folder"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("##open", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Open    ").x * 1.5f);
        ImGui::TableHeadersRow();
        for (const Tool& t : m_catalog.tools()) {
            ImGui::PushID(t.exe.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.name.c_str());
            ImGui::TableNextColumn();
            if (t.protocol != kLiveProtocol)
                ImGui::TextColored(theme::warning(), "%s %s", t.version.c_str(), tr("(protocol differs)"));
            else ImGui::TextUnformatted(t.version.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", t.folder.c_str());
            ImGui::TableNextColumn();
            const std::string openTip = project ? trf("Opens with %s", project->name.c_str()) : std::string();
            if (iconButton(kIconLaunch, tr("Open"), true, project ? openTip.c_str() : nullptr)) {
                std::vector<std::string> args = {t.exe};
                if (project) args.insert(args.end(), {"--project", project->directory});
                std::string error;
                if (!launchDetached(args, error)) {
                    setStatus(trf("Cannot start %s: %s", t.name.c_str(), error.c_str()), true);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Tools in the release index. Offered only when they match an installed engine; an update waits until the tool and the editor are closed
    if (releaseIndex().empty() || m_toolReleases.empty()) return;
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Install"));
    if (ImGui::BeginTable("##toolreleases", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Tool"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr("Version"), ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn(tr("Works with"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("##install", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Update to 0.00.00").x * 1.3f);
        ImGui::TableHeadersRow();
        for (const ToolRelease& r : m_toolReleases) {
            ImGui::PushID(r.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s  (%.0f MB)", r.version.c_str(), static_cast<double>(r.size) / (1024.0 * 1024.0));
            ImGui::TableNextColumn();
            std::string engines;
            for (const std::string& e : r.engines) engines += (engines.empty() ? "" : ", ") + e;
            ImGui::TextDisabled(tr("engine %s"), engines.c_str());
            ImGui::TableNextColumn();
            const Tool* installed = m_catalog.installedTool(r.name);
            const bool newer = !installed || versionLess(installed->version, r.version);
            if (!newer) ImGui::TextUnformatted(tr("installed"));
            else if (!m_catalog.toolMatches(r)) ImGui::TextDisabled("%s", r.protocol != kLiveProtocol ? tr("needs another Manager") : tr("no matching engine"));
            else {
                ImGui::BeginDisabled(m_job.running());
                const std::string label = installed ? trf("Update to %s", r.version.c_str()) : std::string(tr("Install"));
                if (iconButton(installed ? kIconSync : kIconDownload, label, true, nullptr, installed ? r.version : std::string()) &&
                    m_job.start(trf("Install %s %s", r.name.c_str(), r.version.c_str()),
                                powershellArgs(installerScript(), {"tool", "-Index", releaseIndex(), "-Name", r.name, "-Root", toolsRoot()}))) {
                    m_status.clear();
                    m_jobOutput.clear();
                }
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
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
    ImGui::BeginChild("##settingsnav", ImVec2(170.0f * dpi, 0));
    for (const Item& item : items) {
        const bool current = m_settingsPage == item.page;
        ImGui::PushStyleColor(ImGuiCol_Header, theme::gray(5));
        theme::FontScope font(theme::kBody, current);
        if (ImGui::Selectable(iconLabel(item.glyph, item.label).c_str(), current, 0, ImVec2(0, ImGui::GetFrameHeight()))) m_settingsPage = item.page;
        ImGui::PopStyleColor();
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

void App::pickFolder(int purpose) {
    {
        std::lock_guard<std::mutex> lock(m_pickMutex);
        m_pickPurpose = purpose;
        m_picked = "\x01"; // waiting
    }
    g_pickTarget = {&m_pickMutex, &m_picked};
    SDL_ShowOpenFolderDialog(onFolderPicked, &g_pickTarget, window, purpose >= 2 ? m_newLocation : nullptr, false);
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
    } else {
        std::snprintf(m_newLocation, sizeof(m_newLocation), "%s", picked.c_str());
        if (purpose == 3) saveProjectLocation(m_newLocation);
    }
    reload();
}

} // namespace mkmanager
