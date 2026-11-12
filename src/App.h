#pragma once

// MealKit Manager screen: [Projects] [Modules] [Engines] [Tools] tabs. Window/fonts/loop are in main.cpp.
#include "Catalog.h"
#include "Job.h"

#include <mutex>
#include <string>

struct SDL_Window;

namespace mkmanager {

// Segoe Fluent Icons / MDL2 Assets glyphs, the same ones the editor uses (module-editor ImGuiLayer.h icon::)
inline constexpr const char* kIconAdd = "\xEE\x9C\x90";        // U+E710
inline constexpr const char* kIconFolder = "\xEE\xA2\xB7";     // U+E8B7
inline constexpr const char* kIconRefresh = "\xEE\x9C\xAC";    // U+E72C
inline constexpr const char* kIconStar = "\xEE\x9C\xB4";       // U+E734
inline constexpr const char* kIconStarFilled = "\xEE\x9C\xB5"; // U+E735

class App {
public:
    bool iconFont = false; // module icons can be drawn (loadFonts merged an icon font)
    SDL_Window* window = nullptr;
    bool quitRequested = false; // after an update started the new Manager

    void init(const std::string& extraEngine);
    void frame();
    const Catalog& catalog() const { return m_catalog; }
    // Start on the [Modules] tab (--tab modules, --select <project folder>: screen check)
    void showModulesTab(const std::string& project) {
        m_openModulesTab = true;
        if (!project.empty()) m_selected = normalizeDirectory(project);
    }

    // Start on the [Engines] tab (--engines: screen check)
    void showEnginesTab() { m_openEnginesTab = true; }
    void showToolsTab() { m_openToolsTab = true; }

private:
    bool m_openEnginesTab = false;
    bool m_openToolsTab = false;
    void reload();
    void finishJob();
    void drawWindow();
    void projectsTab();
    void enginesTab();
    void toolsTab();
    void modulesTab();
    std::string withIcon(const std::string& icon, const std::string& tags, const std::string& name, const std::string& label) const;
    void runModuleTool(const Engine& engine, const Project& project, const std::string& label, std::vector<std::string> args, bool listing);
    void newProjectPopup();
    void statusBar();
    void openProject(const Project& project);
    void installEngine(const Release& release);
    void runScript(const std::string& label, const std::string& script, const std::vector<std::string>& args,
                   const std::string& addOnSuccess = {});
    void pickFolder(int purpose); // 0 = import project, 1 = add engine, 2 = new project location
    void takePickedFolder();

    Catalog m_catalog;
    std::string m_extraEngine;
    Job m_job;
    std::string m_jobAdd; // project folder to add to the recent list if the job succeeds
    std::string m_status;
    bool m_statusError = false;
    std::string m_jobOutput; // output of a failed job (expanded from the status line)

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
    void toolchainLine();

    // [Modules] tab: modules of the selected project and the registry (engine tools\module.ps1 search -Tsv)
    ModuleList m_modules;
    std::string m_modulesFor;      // project folder the list was fetched for (refetched if different)
    bool m_jobListsModules = false; // the current job is fetching the module list (shows the list instead of a status line when done)
    bool m_jobChangesModules = false;
    char m_moduleSearch[96] = {};
    std::string m_confirmRemove;
    bool m_openModulesTab = false; // select the [Modules] tab next frame
    char m_search[128] = {};

    // [Projects] tab list: column sort (-1 = recent order; favorites stay on top) and keyboard
    int m_sortColumn = -1;
    bool m_sortAscending = true;
    bool m_focusSearch = false;
    // Changes picked while drawing the list, applied after it (reload rebuilds the list, so pointers into it die)
    std::string m_pendingRemove;
    std::string m_pendingFavorite;
    bool m_pendingFavoriteOn = false;
    std::string m_pendingOpen;
    void projectList(bool busy);
    const Project* selectedProject() const;
    void projectMenu(const Project& p, bool busy);
    void removeMissingProjects();
    std::string iconLabel(const char* glyph, const char* label) const;

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
