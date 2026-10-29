// MealKit Manager executable: SDL3 window + SDL_Renderer + Dear ImGui (same framework as the MealKit Audio tool). The actual screen is App.
//   MealKitManager [--engine <engine folder>] [--lang ko|en] [--modules <project folder>] [--frames N --capture <png>]   (--modules: start on the [Modules] tab)
//   MealKitManager --list      prints project/engine/tool lists without a window (for tests/scripts)
//   MealKitManager [--engine <engine>] --new <name> --path <location>   new project without a window (same script as the screen's [New Project])
//   MealKitManager --open <project folder>                         opens that project in its engine's editor without a window
//   MealKitManager --version
#include "App.h"
#include "Strings.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// Merges a system font + a Korean font (ImGui's default vector font if none)
void loadFonts(float size) {
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
        std::printf("tool: %s %s protocol %d %s%s\n", t.name.c_str(), t.version.c_str(), t.protocol, t.exe.c_str(),
                    t.protocol != mkmanager::kLiveProtocol ? " (protocol differs)" : "");
    std::fflush(stdout);
    return 0;
}

// --new: same as [New Project] on the screen (the first engine's new_project.ps1; on success, to the top of the recent list)
int newProject(mkmanager::Catalog& catalog, const std::string& name, const std::string& path) {
    if (catalog.engines().empty()) {
        std::printf("No engine found (use --engine <folder>)\n");
        return 1;
    }
    const mkmanager::Engine& engine = catalog.engines()[0];
    mkmanager::Job job;
    job.start("new project", mkmanager::powershellArgs(engine.directory + "\\tools\\new_project.ps1", {"-Name", name, "-Path", path}));
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
    const mkmanager::Engine* engine = catalog.engineFor(project);
    if (!engine || engine->editorExe.empty()) {
        std::printf("engine of %s not found or its editor is not built\n", project.name.c_str());
        return 1;
    }
    std::string error;
    if (!mkmanager::launchDetached({engine->editorExe, "--project", project.directory}, error)) {
        std::printf("cannot start the editor: %s\n", error.c_str());
        return 1;
    }
    std::printf("opened %s with %s\n", project.name.c_str(), engine->editorExe.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string engine, capture, language, newName, newPath, open, modules;
    long frames = 0; // --frames N: quit after N frames (--capture saves the last screen as PNG, for docs/tests)
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--engine") == 0 && i + 1 < argc) engine = argv[++i];
        else if (std::strcmp(argv[i], "--new") == 0 && i + 1 < argc) newName = argv[++i];
        else if (std::strcmp(argv[i], "--path") == 0 && i + 1 < argc) newPath = argv[++i];
        else if (std::strcmp(argv[i], "--open") == 0 && i + 1 < argc) open = argv[++i];
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::strtol(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture = argv[++i];
        else if (std::strcmp(argv[i], "--lang") == 0 && i + 1 < argc) language = argv[++i];
        else if (std::strcmp(argv[i], "--modules") == 0 && i + 1 < argc) modules = argv[++i];
        else if (std::strcmp(argv[i], "--list") == 0) list = true;
        else if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("MealKit Manager %s\n", MKMANAGER_VERSION);
            return 0;
        }
    }
    if (list || !newName.empty() || !open.empty()) { // windowless commands
        if (!engine.empty()) {
            mkmanager::Engine probe;
            if (mkmanager::readEngine(engine, probe)) mkmanager::Catalog().addEngine(engine);
        }
        mkmanager::Catalog catalog;
        catalog.load(engine);
        if (!newName.empty()) return newProject(catalog, newName, newPath.empty() ? "." : newPath);
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
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

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
    loadFonts(17.0f);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    mkmanager::App app;
    app.window = window;
    app.init(engine);
    if (!language.empty()) mkmanager::setLanguage(language);
    if (!modules.empty()) app.showModulesTab(modules);

    long frame = 0;
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
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

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
