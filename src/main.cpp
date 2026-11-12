// MealKit Manager executable: SDL3 window + SDL_Renderer + Dear ImGui (same framework as the MkAudio tool). The actual screen is App.
//   MealKitManager [--engine <engine folder>] [--lang ko|en] [--modules <project folder>] [--frames N --capture <png>]   (--modules: start on the [Modules] tab, --engines: on the [Engines] tab)
//   MealKitManager --list      prints project/engine/tool lists without a window (for tests/scripts)
//   MealKitManager [--engine <engine>] --new <name> --path <location> [--template arena|empty]   new project without a window (same script as the screen's [New Project])
//   MealKitManager --open <project folder>                         opens that project in its engine's editor without a window
//   MealKitManager --releases | --install-engine <version>             engines in the release index (MEALKIT_RELEASE_INDEX) / install one
//   (run from a downloaded package, with SETUP beside the exe)          installs itself with shortcuts and starts the installed copy (--no-launch: doesn't start it)
//   MealKitManager --uninstall [--yes]                                 remove the installed Manager, its shortcuts and its Installed apps entry
//   MealKitManager --version
#include "App.h"
#include "Strings.h"

#include <mkcontract/Files.h>
#include <mkcontract/User.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#if defined(_WIN32)
#include <process.h> // _getpid (--uninstall)
#else
#include <unistd.h>
#define _getpid getpid
#endif

namespace {

// Merges a system font + a Korean font (ImGui's default vector font if none)
// Returns whether an icon font (module icons) was merged in
bool loadFonts(float size) {
    ImGuiIO& io = ImGui::GetIO();
    const char* latin[] = {
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\segoeui.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
    };
    const char* korean[] = {
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\malgun.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
#else
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
#endif
    };
    bool loaded = false;
    for (const char* path : latin)
        if (!loaded && SDL_GetPathInfo(path, nullptr) && io.Fonts->AddFontFromFileTTF(path, size)) loaded = true;
    if (!loaded) {
        ImFontConfig config;
        config.SizePixels = size;
        io.Fonts->AddFontDefaultVector(&config);
    }
    ImFontConfig merge;
    merge.MergeMode = true;
    for (const char* path : korean)
        if (SDL_GetPathInfo(path, nullptr) && io.Fonts->AddFontFromFileTTF(path, size, &merge)) break;
#if defined(_WIN32)
    // Module icons (App::moduleGlyph): Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on 10, the same code points as the editor
    for (const char* path : {"C:\\Windows\\Fonts\\SegoeIcons.ttf", "C:\\Windows\\Fonts\\segmdl2.ttf"}) {
        ImFontConfig icons;
        icons.MergeMode = true;
        icons.GlyphOffset = ImVec2(0.0f, 3.0f);
        if (SDL_GetPathInfo(path, nullptr) && io.Fonts->AddFontFromFileTTF(path, size, &icons)) return true;
    }
#endif
    return false;
}

int printList(const mkmanager::Catalog& catalog) {
    for (const mkmanager::Engine& e : catalog.engines())
        std::printf("engine: %s %s%s\n", e.version.c_str(), e.directory.c_str(), e.editorExe.empty() ? " (editor not built)" : "");
    for (const mkmanager::Project& p : catalog.projects()) {
        const mkmanager::Engine* engine = catalog.engineFor(p);
        std::printf("project: %s %s engine %s%s%s\n", p.valid ? p.name.c_str() : "(missing)", p.directory.c_str(),
                    engine ? engine->version.c_str() : "(not found)", p.favorite ? " favorite" : "",
                    catalog.needsUpgrade(p) ? " upgrade" : "");
    }
    for (const mkmanager::Tool& t : catalog.tools())
        std::printf("tool: %s %s protocol %u %s%s\n", t.name.c_str(), t.version.c_str(), t.protocol, t.exe.c_str(),
                    t.protocol != mkmanager::kLiveProtocol ? " (protocol differs)" : "");
    std::fflush(stdout);
    return 0;
}

// --new: same as [New Project] on the screen (the first engine's new_project.ps1; on success, to the top of the recent list)
int newProject(mkmanager::Catalog& catalog, const std::string& name, const std::string& path, const std::string& projectTemplate) {
    if (catalog.engines().empty()) {
        std::printf("No engine found (use --engine <folder>)\n");
        return 1;
    }
    const mkmanager::Engine& engine = catalog.engines()[0];
    mkmanager::Job job;
    std::vector<std::string> args = {"-Name", name, "-Path", path};
    if (!projectTemplate.empty()) args.insert(args.end(), {"-Template", projectTemplate}); // engines from 0.7.21
    job.start("new project", mkmanager::powershellArgs(engine.directory + "\\tools\\new_project.ps1", args));
    while (!job.takeFinished()) SDL_Delay(50);
    std::printf("%s", job.output().c_str());
    if (job.exitCode() != 0) {
        std::printf("new project failed (exit code %d)\n", job.exitCode());
        return 1;
    }
    catalog.addRecent(path + "\\" + name);
    std::printf("created %s\\%s with engine %s\n", path.c_str(), name.c_str(), engine.version.c_str());
    return 0;
}

// --open: same as [Open in Editor] on the screen
int openProject(const mkmanager::Catalog& catalog, const std::string& directory) {
    mkmanager::Project project;
    if (!mkmanager::readProject(directory, project)) {
        std::printf("%s has no mealkit.project\n", directory.c_str());
        return 1;
    }
    bool byVersion = false;
    const mkmanager::Engine* engine = catalog.engineFor(project, &byVersion);
    if (!engine || engine->editorExe.empty()) {
        std::printf("engine of %s not found or its editor is not built\n", project.name.c_str());
        return 1;
    }
    if (byVersion) { // same as the screen: the engine line follows the engine found by version
        if (!mkmanager::setProjectEngine(project.directory, engine->directory)) return 1;
        std::printf("engine line of %s now %s\n", project.name.c_str(), engine->directory.c_str());
    }
    std::string error;
    if (!mkmanager::launchDetached({engine->editorExe, "--project", project.directory}, error)) {
        std::printf("cannot start the editor: %s\n", error.c_str());
        return 1;
    }
    std::printf("opened %s with %s\n", project.name.c_str(), engine->editorExe.c_str());
    return 0;
}

// --releases / --install-engine <version>: same as the [Engines] tab's install list and [Install] (scripts\install.ps1 beside the exe)
int installCommand(const std::string& version) {
    const std::string index = mkmanager::releaseIndex();
    if (index.empty()) {
        std::printf("No release index yet (set MEALKIT_RELEASE_INDEX)\n");
        return 1;
    }
    const char* base = SDL_GetBasePath();
    const std::string script = std::string(base ? base : "") + "install.ps1";
    std::vector<std::string> args = {"list", "-Index", index};
    if (!version.empty()) args = {"engine", "-Index", index, "-Version", version, "-Root", mkmanager::enginesRoot()};
    mkmanager::Job job;
    job.start("install", mkmanager::powershellArgs(script, args));
    while (!job.takeFinished()) SDL_Delay(50);
    if (version.empty()) {
        if (job.exitCode() != 0) std::printf("%s", job.output().c_str());
        for (const mkmanager::Release& r : mkmanager::parseReleaseList(job.output()))
            std::printf("release: %s %llu %s\n", r.version.c_str(), r.size, r.url.c_str());
        return job.exitCode();
    }
    std::printf("%s", job.output().c_str());
    const std::string folder = mkmanager::installedFolder(job.output());
    if (job.exitCode() != 0 || folder.empty()) return 1;
    mkmanager::Catalog().addEngine(folder);
    std::printf("engine %s added to the engine list\n", version.c_str());
    return 0;
}

// A downloaded Manager package (scripts\package.ps1) has a SETUP file beside the exe. Run from there, the Manager installs itself into
// <user folder>\manager with its Start menu and desktop shortcuts (install.ps1 self), starts the installed copy and quits.
// Returns -1 when this isn't a package run (go on normally), otherwise the exit code.
int setupFromPackage(bool launch) {
    const char* base = SDL_GetBasePath();
    const std::string from = mkcontract::normalizeDirectory(base ? base : "");
    const std::string to = mkcontract::joinPath(mkmanager::userDirectory(), "manager");
    if (from.empty() || !SDL_GetPathInfo(mkcontract::joinPath(from, "SETUP").c_str(), nullptr) || mkcontract::sameDirectory(from, to)) return -1;
    mkmanager::Job job;
    job.start("setup", mkmanager::powershellArgs(mkcontract::joinPath(from, "install.ps1"), {"self", "-From", from, "-To", to}));
    while (!job.takeFinished()) SDL_Delay(50);
    std::printf("%s", job.output().c_str());
    if (job.exitCode() != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MealKit Manager", ("Installing MealKit Manager failed.\n\n" + job.output()).c_str(), nullptr);
        return 1;
    }
    if (!launch) return 0;
    std::string error;
    if (!mkmanager::launchDetached({mkcontract::joinPath(to, "MealKitManager.exe")}, error)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MealKit Manager", ("Cannot start the installed MealKit Manager: " + error).c_str(), nullptr);
        return 1;
    }
    return 0;
}

// An update renames the running exe to MealKitManager.old.exe (install.ps1 update); the next start deletes it
void removeOldExe() {
    const char* base = SDL_GetBasePath();
    if (!base) return;
    std::error_code ec;
    std::filesystem::remove(mkcontract::pathOf(mkcontract::joinPath(base, "MealKitManager.old.exe")), ec);
}

// --uninstall (Windows "Installed apps" runs it): asks, then a detached install.ps1 uninstall waits for this process to quit and removes
// the shortcuts, the uninstall entry and the Manager folder. Engines, tools and the user lists stay (--yes: no question, for tests)
int uninstallManager(bool assumeYes) {
    const std::string to = mkcontract::joinPath(mkmanager::userDirectory(), "manager");
    if (!assumeYes) {
        const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Uninstall"},
                                                    {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"}};
        const std::string text = "Remove MealKit Manager, its shortcuts and " + to +
                                 "?\n\nInstalled engines, tools and your projects are kept (" + mkmanager::userDirectory() + ").";
        const SDL_MessageBoxData box = {SDL_MESSAGEBOX_WARNING, nullptr, "MealKit Manager", text.c_str(), 2, buttons, nullptr};
        int pressed = 0;
        if (!SDL_ShowMessageBox(&box, &pressed) || pressed != 1) return 1;
    }
    const char* base = SDL_GetBasePath();
    // A copy of the script in the temp folder runs it, since the Manager folder (and the script in it) is being deleted
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "mealkit-manager-uninstall.ps1";
    std::error_code ec;
    std::filesystem::copy_file(mkcontract::pathOf(mkcontract::joinPath(base ? base : "", "install.ps1")), script,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::printf("cannot copy install.ps1: %s\n", ec.message().c_str());
        return 1;
    }
    std::string error;
    if (!mkmanager::launchDetached(mkmanager::powershellArgs(mkcontract::utf8Of(script),
                                                             {"uninstall", "-To", to, "-WaitPid", std::to_string(static_cast<unsigned long>(_getpid()))}),
                                   error)) {
        std::printf("cannot start the uninstaller: %s\n", error.c_str());
        return 1;
    }
    std::printf("uninstalling %s\n", to.c_str());
    return 0;
}

// Window placement kept between runs: manager_window.txt in the user folder ("x y w h maximized" keys, Manager only)
struct Placement {
    int x = 0, y = 0, w = 0, h = 0;
    bool maximized = false;
    bool valid() const { return w >= 400 && h >= 300; }
};

std::string placementPath() { return mkcontract::userPath("manager_window.txt"); }

Placement loadPlacement() {
    Placement p;
    const std::string path = placementPath();
    if (path.empty()) return p;
    auto key = [&](const char* name) { return std::atoi(mkcontract::readKey(path, name).c_str()); };
    p.x = key("x");
    p.y = key("y");
    p.w = key("w");
    p.h = key("h");
    p.maximized = key("maximized") != 0;
    return p;
}

// The normal (not maximized/minimized) placement, called on every move/resize so maximizing keeps the size it returns to
void trackPlacement(SDL_Window* window, Placement& normal) {
    if (SDL_GetWindowFlags(window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN)) return;
    SDL_GetWindowSize(window, &normal.w, &normal.h);
    SDL_GetWindowPosition(window, &normal.x, &normal.y);
}

void savePlacement(SDL_Window* window, Placement p) {
    const std::string path = placementPath();
    if (path.empty() || !p.valid()) return;
    p.maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
    char text[160];
    std::snprintf(text, sizeof text, "x %d\ny %d\nw %d\nh %d\nmaximized %d\n", p.x, p.y, p.w, p.h, p.maximized ? 1 : 0);
    mkcontract::writeFile(path, text);
}

// The saved position only if part of the window's title bar is on some display (a monitor may have been unplugged)
bool onSomeDisplay(const Placement& p) {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    bool found = false;
    for (int i = 0; i < count && !found; ++i) {
        SDL_Rect r;
        if (!SDL_GetDisplayUsableBounds(displays[i], &r)) continue;
        found = p.x + 100 > r.x && p.x < r.x + r.w - 100 && p.y >= r.y - 10 && p.y < r.y + r.h - 50;
    }
    SDL_free(displays);
    return found;
}

} // namespace

int main(int argc, char** argv) {
    std::string engine, capture, language, newName, newPath, newTemplate, open, modules;
    long frames = 0; // --frames N: quit after N frames (--capture saves the last screen as PNG, for docs/tests)
    bool list = false, releases = false, enginesTab = false, noLaunch = false, uninstall = false, assumeYes = false;
    std::string installVersion;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--engine") == 0 && i + 1 < argc) engine = argv[++i];
        else if (std::strcmp(argv[i], "--new") == 0 && i + 1 < argc) newName = argv[++i];
        else if (std::strcmp(argv[i], "--template") == 0 && i + 1 < argc) newTemplate = argv[++i];
        else if (std::strcmp(argv[i], "--path") == 0 && i + 1 < argc) newPath = argv[++i];
        else if (std::strcmp(argv[i], "--open") == 0 && i + 1 < argc) open = argv[++i];
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::strtol(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture = argv[++i];
        else if (std::strcmp(argv[i], "--lang") == 0 && i + 1 < argc) language = argv[++i];
        else if (std::strcmp(argv[i], "--modules") == 0 && i + 1 < argc) modules = argv[++i];
        else if (std::strcmp(argv[i], "--list") == 0) list = true;
        else if (std::strcmp(argv[i], "--releases") == 0) releases = true;
        else if (std::strcmp(argv[i], "--engines") == 0) enginesTab = true;
        else if (std::strcmp(argv[i], "--no-launch") == 0) noLaunch = true;
        else if (std::strcmp(argv[i], "--uninstall") == 0) uninstall = true;
        else if (std::strcmp(argv[i], "--yes") == 0) assumeYes = true;
        else if (std::strcmp(argv[i], "--install-engine") == 0 && i + 1 < argc) installVersion = argv[++i];
        else if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("MealKit Manager %s\n", MKMANAGER_VERSION);
            return 0;
        }
    }
    if (uninstall) return uninstallManager(assumeYes);
    removeOldExe();
    if (releases || !installVersion.empty()) return installCommand(installVersion);
    if (frames == 0 && !list && newName.empty() && open.empty()) {
        const int setup = setupFromPackage(!noLaunch);
        if (setup >= 0) return setup;
    }
    if (list || !newName.empty() || !open.empty()) { // windowless commands
        if (!engine.empty()) {
            mkmanager::Engine probe;
            if (mkmanager::readEngine(engine, probe)) mkmanager::Catalog().addEngine(engine);
        }
        mkmanager::Catalog catalog;
        catalog.load(engine);
        if (!newName.empty()) return newProject(catalog, newName, newPath.empty() ? "." : newPath, newTemplate);
        if (!open.empty()) return openProject(catalog, open);
        return printList(catalog);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_Window* window = SDL_CreateWindow("MealKit Manager", static_cast<int>(1100 * scale), static_cast<int>(680 * scale),
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        SDL_Log("Cannot create the window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    const bool automated = frames > 0; // docs/tests captures keep the fixed size
    const Placement placement = automated ? Placement{} : loadPlacement();
    if (placement.valid()) {
        SDL_SetWindowSize(window, placement.w, placement.h);
        if (onSomeDisplay(placement)) SDL_SetWindowPosition(window, placement.x, placement.y);
        else SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    } else {
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_ShowWindow(window);
    Placement normal;
    trackPlacement(window, normal);
    if (placement.maximized) SDL_MaximizeWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // a single window, so there's no layout to save
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    style.FrameRounding = 3.0f;
    style.CellPadding.y *= 2.0f;
    const bool iconFont = loadFonts(17.0f);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    mkmanager::App app;
    app.window = window;
    app.iconFont = iconFont;
    app.init(engine);
    if (!language.empty()) mkmanager::setLanguage(language);
    if (!modules.empty()) app.showModulesTab(modules);
    if (enginesTab) app.showEnginesTab();

    long frame = 0;
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) trackPlacement(window, normal);
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) running = false;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        app.frame();
        if (app.quitRequested) running = false;
        ImGui::Render();

        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(renderer, 0.08f, 0.08f, 0.09f, 1.0f);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (frames > 0 && ++frame >= frames) {
            if (!capture.empty()) {
                SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
                if (!shot || !SDL_SavePNG(shot, capture.c_str())) SDL_Log("Cannot save %s: %s", capture.c_str(), SDL_GetError());
                SDL_DestroySurface(shot);
            }
            running = false;
        }
        SDL_RenderPresent(renderer);
    }

    if (!automated) savePlacement(window, normal);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
