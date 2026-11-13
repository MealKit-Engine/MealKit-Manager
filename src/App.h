#pragma once

// MealKit Manager screen: a launcher (docs/UX_REVIEW.md). A header with the Projects | Tools switch and the settings button (General, Engines),
// the page below it, and a status strip only while a job runs or after one failed. Window/fonts/loop are in main.cpp.
#include "Catalog.h"
#include "Job.h"

#include <map>
#include <mutex>
#include <string>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace mkmanager {

// Segoe Fluent Icons / MDL2 Assets glyphs, one per action across the suite (engine docs/TOOL_DESIGN.md "Glyphs")
inline constexpr const char* kIconRemove = "\xEE\x9C\xB8";     // U+E738 remove from a list
inline constexpr const char* kIconUp = "\xEE\x9D\x8A";         // U+E74A upgrade
inline constexpr const char* kIconCopy = "\xEE\xA3\x88";       // U+E8C8
inline constexpr const char* kIconLaunch = "\xEE\xA2\xA7";     // U+E8A7 open in its program
inline constexpr const char* kIconFolderOpen = "\xEE\xA0\xB8"; // U+E838 show in Explorer
inline constexpr const char* kIconDownload = "\xEE\xA2\x96";   // U+E896 install
inline constexpr const char* kIconSync = "\xEE\xA2\x95";       // U+E895 update
inline constexpr const char* kIconBuild = "\xEE\xA4\x8F";      // U+E90F
inline constexpr const char* kIconOutput = "\xEE\xA2\xA5";     // U+E8A5
inline constexpr const char* kIconAdd = "\xEE\x9C\x90";        // U+E710
inline constexpr const char* kIconFolder = "\xEE\xA2\xB7";     // U+E8B7
inline constexpr const char* kIconRefresh = "\xEE\x9C\xAC";    // U+E72C
inline constexpr const char* kIconStar = "\xEE\x9C\xB4";       // U+E734
inline constexpr const char* kIconStarFilled = "\xEE\x9C\xB5"; // U+E735
inline constexpr const char* kIconMore = "\xEE\x9C\x92";     // U+E712
inline constexpr const char* kIconSearch = "\xEE\x9C\xA1";   // U+E721
inline constexpr const char* kIconProjects = "\xEE\xA3\xB1"; // U+E8F1 Library
inline constexpr const char* kIconEngines = "\xEE\x9F\xBC";  // U+E7FC Game
inline constexpr const char* kIconTools = "\xEE\xB1\xBA";    // U+EC7A DeveloperTools
inline constexpr const char* kIconSettings = "\xEE\x9C\x93"; // U+E713 Setting
inline constexpr const char* kIconCancel = "\xEE\x9C\x91";   // U+E711 close, dismiss

class App {
public:
    bool iconFont = false; // module icons can be drawn (loadFonts merged an icon font)
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr; // project icons are textures on it
    ~App();
    bool quitRequested = false; // after an update started the new Manager

    void init(const std::string& extraEngine);
    void frame();
    const Catalog& catalog() const { return m_catalog; }
    // Start with the settings' Engines page open (--engines) or on the Tools page (--tools): screen checks
    void showEngines() {
        m_openSettings = true;
        m_settingsPage = SettingsPage::Engines;
    }
    void showToolsPage() { m_page = Page::Tools; }

private:
    enum class Page { Projects, Tools };
    Page m_page = Page::Projects;
    void header();
    // Settings (a modal: General, Engines). Occasional things live here instead of pages
    enum class SettingsPage { General, Engines };
    SettingsPage m_settingsPage = SettingsPage::General;
    bool m_openSettings = false;
    void settingsPopup();
    void generalSettings();
    bool managerUpdateAvailable() const;
    // icon + label button of an occasional screen (TOOL_DESIGN section 1); primary = the page's one accent-filled button
    bool actionButton(const char* glyph, const std::string& label, bool primary = false, const char* tooltip = nullptr);
    void pageTitle(const char* title);
    // Project icons (mealkit.project "icon", default icon.png), loaded once per folder; null = draw the letter tile
    std::map<std::string, SDL_Texture*> m_icons;
    SDL_Texture* projectIcon(const Project& p);
    void projectTile(const Project& p, float size);
    void reload();
    void finishJob();
    void drawWindow();
    void projectsTab();
    void enginesTab();
    void toolsTab();
    void newProjectPopup();
    void statusBar();
    void openProject(const Project& project);
    void installEngine(const Release& release);
    void runScript(const std::string& label, const std::string& script, const std::vector<std::string>& args,
                   const std::string& addOnSuccess = {});
    void pickFolder(int purpose); // 0 = import project, 1 = add engine, 2 = new project location, 3 = default location (settings)
    void takePickedFolder();

    Catalog m_catalog;
    std::string m_extraEngine;
    Job m_job;
    std::string m_jobAdd; // project folder to add to the recent list if the job succeeds
    std::string m_status;
    bool m_statusError = false;
    std::string m_jobOutput; // output of a failed job (expanded from the status line)
    double m_statusTime = 0.0; // when m_status was set: a success fades after a few seconds, a failure stays until closed
    void setStatus(const std::string& text, bool error);
    bool statusVisible() const;

    std::string m_selected;

    // [Engines] tab: engines in the release index (scripts\install.ps1 list), installed with install.ps1 engine
    std::vector<Release> m_releases;
    std::string m_releasesError;
    bool m_releasesFetched = false;
    bool m_jobListsReleases = false;
    bool m_jobInstallsEngine = false;
    Release m_managerRelease; // the index's Manager entry (update check)
    std::vector<ToolRelease> m_toolReleases; // the index's tools ([Tools] tab)
    bool m_jobUpdatesManager = false;

    // C++ compiler and DXC on this PC (install.ps1 check, once at start and on [Check Again])
    Toolchain m_toolchain;
    bool m_toolchainRequested = false;
    bool m_jobChecksToolchain = false;
    void toolchainBanner();

    char m_search[128] = {};

    // [Projects] tab list: column sort (-1 = recent order; favorites stay on top) and keyboard
    int m_sortColumn = -1;
    bool m_sortAscending = true;
    bool m_focusSearch = false;
    // Changes picked while drawing the list, applied after it (reload rebuilds the list, so pointers into it die)
    std::string m_pendingRemove;
    std::string m_pendingFavorite;
    bool m_pendingFavoriteOn = false;
    // Row actions (the row's buttons, Enter, double-click and the menu), run after the list is drawn
    enum class RowAction { Open, Upgrade, InstallEngine };
    std::string m_pendingAction;
    RowAction m_pendingKind = RowAction::Open;
    void requestRow(const std::string& folder, RowAction kind) {
        m_pendingAction = folder;
        m_pendingKind = kind;
    }
    void projectList(bool busy);
    const Project* selectedProject() const;
    void projectMenu(const Project& p, bool busy);
    void removeMissingProjects();
    std::string iconLabel(const char* glyph, const char* label) const;
    // Icon-only button with its name (and an optional second line) in the tooltip; the name when there's no icon font.
    // suffix is shown after the glyph (a count or a version)
    bool iconButton(const char* glyph, const std::string& name, bool small = false, const char* detail = nullptr, const std::string& suffix = {});

    // New project
    bool m_openNew = false;
    char m_newName[64] = "MyGame";
    int m_newTemplate = 0; // 0 arena, 1 empty (new_project.ps1 -Template, engines from 0.7.21)
    char m_newLocation[512] = {};
    int m_newEngine = 0;

    // Folder picker dialog (returns from another thread)
    std::mutex m_pickMutex;
    std::string m_picked;
    int m_pickPurpose = -1;
};

} // namespace mkmanager
