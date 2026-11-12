#include "App.h"

#include "Strings.h"

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
    if (m_job.takeFinished()) {
        if (m_jobChecksToolchain) {
            m_jobChecksToolchain = false;
            m_toolchain = parseToolchain(m_job.output());
        } else if (m_jobListsReleases) { // release list: shows in the [Engines] tab
            m_jobListsReleases = false;
            m_releases = parseReleaseList(m_job.output());
            m_releasesError = m_job.exitCode() != 0 ? m_job.output() : std::string();
        } else if (m_jobListsModules) { // module list: the status line stays and it shows in the [Modules] tab
            m_jobListsModules = false;
            m_modules = parseModuleList(m_job.output());
            if (m_job.exitCode() != 0) m_modules.error = m_job.output();
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
    const ImVec4 warn(1.0f, 0.75f, 0.35f, 1.0f);
    ImGui::TextColored(warn, "%s", tr("No C++ compiler on this PC: game code can't be built, so projects can't open in the editor."));
    ImGui::TextUnformatted(tr("Install Visual Studio Build Tools with C++ (about 6 GB), from a terminal:"));
    static char line[256];
    std::snprintf(line, sizeof(line), "%s", kBuildToolsWinget);
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Check Again ").x * 2.6f);
    ImGui::InputText("##winget", line, sizeof(line), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button(tr("Copy"))) SDL_SetClipboardText(kBuildToolsWinget);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_job.running());
    if (ImGui::Button(tr("Check Again"))) m_toolchainRequested = false;
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", tr("Without a compiler, use designer mode: open a project from the shared folder a programmer builds."));
    ImGui::Separator();
}

// Engines tab: compiler and DXC state in one line
void App::toolchainLine() {
    if (!m_toolchain.checked) return;
    ImGui::TextDisabled("%s  |  %s", m_toolchain.compiler ? trf("C++ compiler: %s", m_toolchain.visualStudio.c_str()).c_str() : tr("C++ compiler: not found"),
                        m_toolchain.dxc ? tr("DXC: ready") : tr("DXC: downloaded with the first build"));
}

void App::finishJob() {
    if (m_jobChangesModules) {
        m_jobChangesModules = false;
        m_modulesFor.clear(); // refetch the current state whether it succeeded or not
    }
    const bool ok = m_job.exitCode() == 0;
    m_status = ok ? trf("%s: done", m_job.label().c_str()) : trf("%s: failed (exit code %d)", m_job.label().c_str(), m_job.exitCode());
    m_statusError = !ok;
    m_jobOutput = ok ? std::string() : m_job.output();
    if (ok && !m_jobAdd.empty()) {
        m_catalog.addRecent(m_jobAdd);
        m_selected = normalizeDirectory(m_jobAdd);
    }
    m_jobAdd.clear();
    if (m_jobInstallsEngine) {
        m_jobInstallsEngine = false;
        const std::string folder = installedFolder(m_job.output());
        if (ok && !folder.empty()) m_catalog.addEngine(folder);
    }
    reload();
}

void App::drawWindow() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##manager", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float statusHeight = ImGui::GetFrameHeightWithSpacing() * 1.4f;
    ImGui::BeginChild("##body", ImVec2(0, -statusHeight));
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem(tr("Projects"))) {
            projectsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Modules"), nullptr, m_openModulesTab ? ImGuiTabItemFlags_SetSelected : 0)) {
            modulesTab();
            ImGui::EndTabItem();
        }
        m_openModulesTab = false;
        if (ImGui::BeginTabItem(tr("Engines"), nullptr, m_openEnginesTab ? ImGuiTabItemFlags_SetSelected : 0)) {
            enginesTab();
            ImGui::EndTabItem();
        }
        m_openEnginesTab = false;
        if (ImGui::BeginTabItem(tr("Tools"))) {
            toolsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    statusBar();
    newProjectPopup();
    ImGui::End();
}

void App::projectsTab() {
    toolchainBanner();
    const bool busy = m_job.running();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("+ New Project"))) m_openNew = true;
    ImGui::SameLine();
    if (ImGui::Button(tr("Import..."))) pickFolder(0);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Refresh"))) reload();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", tr("Search"), m_search, sizeof(m_search));

    const Project* selected = nullptr;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;
    const float actionsHeight = ImGui::GetFrameHeightWithSpacing() * 1.6f;
    if (ImGui::BeginTable("##projects", 4, flags, ImVec2(0, -actionsHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn(tr("Project"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(tr("Engine"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr("Created"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0000-00-00 ").x);
        ImGui::TableHeadersRow();
        for (const Project& p : m_catalog.projects()) {
            if (!containsNoCase(p.name, m_search) && !containsNoCase(p.directory, m_search)) continue;
            ImGui::PushID(p.directory.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(p.favorite ? "\xE2\x98\x85##fav" : "\xE2\x98\x86##fav")) { // ★ ☆
                m_catalog.setFavorite(p.directory, !p.favorite);
                ImGui::PopID();
                reload();
                break; // the list changed
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Favorite"));

            ImGui::TableNextColumn();
            const bool isSelected = sameDirectory(m_selected, p.directory);
            if (isSelected) selected = &p;
            const std::string label = (p.valid ? p.name : std::string(tr("(missing)"))) + "##row";
            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                m_selected = p.directory;
                selected = &p;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && p.valid) openProject(p);
            }
            ImGui::TextDisabled("%s", p.directory.c_str());

            ImGui::TableNextColumn();
            bool byVersion = false;
            const Engine* engine = m_catalog.engineFor(p, &byVersion);
            const Release* release = engine ? nullptr : Catalog::releaseFor(p, m_releases);
            if (!p.valid) ImGui::TextDisabled("-");
            else if (!engine && release) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", trf("%s not installed", release->version.c_str()).c_str());
            else if (!engine) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tr("engine not found"));
            else if (byVersion) ImGui::Text("%s%s", engine->version.c_str(), tr(" (installed here)"));
            else if (m_catalog.needsUpgrade(p))
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s -> %s", p.engineVersion.c_str(), engine->version.c_str());
            else ImGui::Text("%s%s", engine->version.c_str(), p.engine.empty() ? tr(" (engine sample)") : "");

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", p.created.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Actions for the selected project
    ImGui::BeginDisabled(!selected || busy);
    const Release* missing = selected && !m_catalog.engineFor(*selected) ? Catalog::releaseFor(*selected, m_releases) : nullptr;
    if (missing) {
        // The project's engine isn't on this PC but the release index has its MINOR: install it, then [Open in Editor] finds it by version
        if (ImGui::Button(trf("Install Engine %s", missing->version.c_str()).c_str())) installEngine(*missing);
    } else if (ImGui::Button(tr("Open in Editor")) && selected) {
        openProject(*selected);
    }
    ImGui::SameLine();
    const bool upgrade = selected && m_catalog.needsUpgrade(*selected);
    ImGui::BeginDisabled(!upgrade);
    if (ImGui::Button(tr("Upgrade Project")) && upgrade) {
        const Engine* engine = m_catalog.engineFor(*selected);
        runScript(tr("Upgrade"), pathJoin(engine->directory, "tools\\upgrade_project.ps1"), {"-Project", selected->directory});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Show Folder")) && selected) SDL_OpenURL(("file:///" + selected->directory).c_str());
    ImGui::SameLine();
    if (ImGui::Button(tr("Remove from List")) && selected) {
        m_catalog.removeRecent(selected->directory);
        m_status = trf("Removed %s from the list (the folder is kept)", selected->directory.c_str());
        m_statusError = false;
        reload();
    }
    ImGui::EndDisabled();
}

void App::enginesTab() {
    toolchainBanner();
    toolchainLine();
    ImGui::BeginDisabled(m_job.running());
    if (ImGui::Button(tr("Add Engine Folder..."))) pickFolder(1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("Engines used by your projects are found automatically."));
    if (ImGui::BeginTable("##engines", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Version"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0.00.00  ").x);
        ImGui::TableSetupColumn(tr("Folder"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr("Editor"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Build Editor  ").x * 1.4f);
        ImGui::TableHeadersRow();
        for (const Engine& e : m_catalog.engines()) {
            ImGui::PushID(e.directory.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.version.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.directory.c_str());
            ImGui::TableNextColumn();
            if (!e.editorExe.empty()) ImGui::TextUnformatted(tr("built"));
            else {
                ImGui::BeginDisabled(m_job.running());
                if (ImGui::SmallButton(tr("Build Editor")))
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
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tr("Could not read the release index."));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Retry"))) m_releasesFetched = false;
        ImGui::TextDisabled("%s", m_releasesError.c_str());
        return;
    }
    if (ImGui::BeginTable("##releases", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
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
                if (ImGui::SmallButton(tr("Install"))) installEngine(r);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void App::runModuleTool(const Engine& engine, const Project& project, const std::string& label, std::vector<std::string> args,
                        bool listing) {
    args.insert(args.end(), {"-Project", project.directory});
    if (!m_job.start(label, powershellArgs(pathJoin(engine.directory, "tools\\module.ps1"), args))) return;
    m_jobListsModules = listing;
    m_jobChangesModules = !listing;
    if (!listing) {
        m_status.clear();
        m_jobOutput.clear();
    }
}

namespace {
// Module icon keywords (mkcontract::moduleIconKeyword) as Segoe Fluent Icons / MDL2 Assets glyphs, the same as the editor's [Modules] window
const char* keywordGlyph(const std::string& keyword) {
    static const std::pair<const char*, const char*> glyphs[] = {
        {"gameplay", "\xEE\x9F\xBC"}, {"game", "\xEE\x9F\xBC"},   {"timer", "\xEE\xA4\x96"},  {"combat", "\xEE\xA8\x98"},
        {"health", "\xEE\xAD\x91"},   {"ai", "\xEE\xA6\x9A"},     {"map", "\xEE\x9C\x87"},    {"save", "\xEE\x9D\x8E"},
        {"ui", "\xEE\x9F\xB4"},       {"audio", "\xEE\x9D\xA7"},  {"render", "\xEE\x9E\x90"}, {"rhi", "\xEE\x9E\x90"},
        {"camera", "\xEE\x9C\xA2"},   {"physics", "\xEF\x85\x98"}, {"net", "\xEE\xA5\xA8"},   {"online", "\xEE\x9D\xB4"},
        {"input", "\xEE\x9D\xA5"},    {"data", "\xEE\xA3\xAF"},   {"asset", "\xEE\xA2\xB7"},  {"scene", "\xEE\xA3\xBD"},
        {"debug", "\xEE\xAF\xA8"},    {"tool", "\xEE\xA4\x8F"},   {"editor", "\xEE\xA4\x8F"}, {"settings", "\xEE\x9C\x93"},
        {"loc", "\xEE\xA3\x81"},      {"feedback", "\xEE\xA5\x85"}, {"live", "\xEE\xA2\x95"}, {"session", "\xEE\x9C\x96"},
        {"sim", "\xEE\xA7\xB5"}};
    for (const auto& [k, g] : glyphs)
        if (keyword == k) return g;
    return "\xEE\xAA\x86"; // puzzle piece
}
} // namespace

// "<icon>  <label>" when the icon font is there
std::string App::withIcon(const std::string& icon, const std::string& tags, const std::string& name, const std::string& label) const {
    if (!iconFont) return label;
    return std::string(keywordGlyph(mkcontract::moduleIconKeyword(icon, tags, name))) + "  " + label;
}

// Source modules of the selected project. The rules live in one place, that project's engine tools\module.ps1 (list via search -Tsv, changes via add/update/remove)
void App::modulesTab() {
    const Project* project = nullptr;
    for (const Project& p : m_catalog.projects())
        if (p.valid && sameDirectory(p.directory, m_selected)) project = &p;
    if (!project) {
        ImGui::TextDisabled("%s", tr("Pick a project on the Projects tab."));
        return;
    }
    const Engine* engine = m_catalog.engineFor(*project);
    if (!engine || !fileExists(pathJoin(engine->directory, "tools\\module.ps1"))) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tr("This project's engine has no module tool (tools\\module.ps1, engine 0.5 or later)."));
        return;
    }
    const bool busy = m_job.running();
    if (!busy && !sameDirectory(m_modulesFor, project->directory)) {
        m_modulesFor = project->directory;
        m_modules = {};
        runModuleTool(*engine, *project, tr("Module list"), {"search", "-Tsv"}, true);
    }

    ImGui::Text(tr("Project: %s"), project->name.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("Refresh"))) m_modulesFor.clear();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##modsearch", tr("Search"), m_moduleSearch, sizeof(m_moduleSearch));

    const ImVec4 warn(1.0f, 0.8f, 0.3f, 1.0f);
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    ImGui::SeparatorText(tr("In this project"));
    if (m_modules.installed.empty()) ImGui::TextDisabled("%s", busy ? tr("Reading...") : tr("No modules."));
    else if (ImGui::BeginTable("##installed", 4, flags)) {
        ImGui::TableSetupColumn(tr("Module"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr("Version"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr("Changes"), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Update  Remove  ").x * 1.6f);
        ImGui::TableHeadersRow();
        for (const InstalledModule& m : m_modules.installed) {
            if (!containsNoCase(m.name + " " + m.title + " " + m.summary + " " + m.tags, m_moduleSearch)) continue;
            ImGui::PushID(m.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(withIcon(m.icon, m.tags, m.name, m.title.empty() || m.title == m.name ? m.name : m.title + "  (" + m.name + ")").c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.summary.empty() ? m.source.c_str() : (m.summary + "\n" + m.source).c_str());
            if (!m.tags.empty()) ImGui::TextDisabled("%s", m.tags.c_str()); // module.txt fields on installed lines (blank from older engines)
            ImGui::TableNextColumn();
            const RegistryModule* reg = nullptr;
            for (const RegistryModule& r : m_modules.registry)
                if (r.name == m.name) reg = &r;
            if (reg && m.source.rfind("registry ", 0) == 0 && !reg->version.empty() && reg->version != m.version)
                ImGui::TextColored(warn, "%s -> %s", m.version.c_str(), reg->version.c_str());
            else ImGui::TextUnformatted(m.version.c_str());
            ImGui::TableNextColumn();
            if (m.pending > 0) ImGui::TextColored(warn, "%s", trf("%d .new file(s) to merge", m.pending).c_str());
            else if (m.modified > 0) ImGui::TextUnformatted(trf("%d file(s) changed here", m.modified).c_str());
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(busy);
            if (ImGui::SmallButton(tr("Update")))
                runModuleTool(*engine, *project, trf("Update %s", m.name.c_str()), {"update", m.name}, false);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tr("Files you changed are kept; the new version is written next to them as .new"));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Remove"))) m_confirmRemove = m.name;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText(tr("Registry (MealKit-Modules)"));
    if (!m_modules.error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_modules.error.c_str());
    if (ImGui::BeginTable("##registry", 3, flags | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("Module"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr("Description"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("##add", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Added  ").x * 1.5f);
        ImGui::TableHeadersRow();
        for (const RegistryModule& r : m_modules.registry) {
            if (!containsNoCase(r.name + " " + r.title + " " + r.summary + " " + r.tags + " " + r.author, m_moduleSearch)) continue;
            ImGui::PushID(r.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(withIcon(r.icon, r.tags, r.name, r.name).c_str());
            if (r.version.empty()) ImGui::TextColored(warn, "%s", trf("%s (engine %s)", r.latest.c_str(), r.latestEngine.c_str()).c_str());
            else ImGui::TextDisabled("%s", r.version.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.title.c_str());
            ImGui::TextWrapped("%s", r.summary.c_str());
            ImGui::TextDisabled("%s  %s", trf("by %s", r.author.c_str()).c_str(), r.tags.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.repo.c_str());
            ImGui::TableNextColumn();
            bool installed = false;
            for (const InstalledModule& m : m_modules.installed) installed |= m.name == r.name;
            if (installed) ImGui::TextDisabled("%s", tr("Added"));
            else {
                ImGui::BeginDisabled(busy);
                if (ImGui::SmallButton(tr("Add")))
                    runModuleTool(*engine, *project, trf("Add %s", r.name.c_str()), {"add", r.name}, false);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", tr("Module code runs inside the editor and the game. The registry checks that it builds, not what it does - read what you add."));
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!m_confirmRemove.empty()) ImGui::OpenPopup("##remove");
    if (ImGui::BeginPopupModal("##remove", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextUnformatted(trf("Remove modules/%s from the project?", m_confirmRemove.c_str()).c_str());
        ImGui::TextDisabled("%s", tr("It is refused while game code includes it. Undo with version control."));
        if (ImGui::Button(tr("Remove"))) {
            runModuleTool(*engine, *project, trf("Remove %s", m_confirmRemove.c_str()), {"remove", m_confirmRemove}, false);
            m_confirmRemove.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) {
            m_confirmRemove.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::toolsTab() {
    ImGui::TextDisabled("%s", tr("Tools are found in %LOCALAPPDATA%\\MealKit\\tools\\<tool>\\ and the editor's toolPath lines."));
    const Project* project = nullptr;
    for (const Project& p : m_catalog.projects())
        if (sameDirectory(p.directory, m_selected) && p.valid) project = &p;
    if (project) ImGui::Text(tr("Opens with project: %s"), project->name.c_str());
    if (ImGui::BeginTable("##tools", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
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
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s %s", t.version.c_str(), tr("(protocol differs)"));
            else ImGui::TextUnformatted(t.version.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", t.folder.c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(tr("Open"))) {
                std::vector<std::string> args = {t.exe};
                if (project) args.insert(args.end(), {"--project", project->directory});
                std::string error;
                if (!launchDetached(args, error)) {
                    m_status = trf("Cannot start %s: %s", t.name.c_str(), error.c_str());
                    m_statusError = true;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (m_catalog.tools().empty()) ImGui::TextDisabled("%s", tr("No tools installed."));
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
    if (!nameOk) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tr("Start with a letter; letters, digits and _ only (it becomes the exe name)."));
    ImGui::InputText(tr("Location"), m_newLocation, sizeof(m_newLocation));
    ImGui::SameLine();
    if (ImGui::Button("...")) pickFolder(2);
    if (engines.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tr("No engine found. Add one on the Engines tab."));
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
    ImGui::Separator();
    if (m_job.running()) {
        static const char spin[] = "|/-\\";
        ImGui::Text("%c %s...", spin[static_cast<int>(ImGui::GetTime() * 8.0) % 4], m_job.label().c_str());
        return;
    }
    if (m_status.empty()) {
        ImGui::TextDisabled("MealKit Manager %s  |  %s", MKMANAGER_VERSION, userDirectory().c_str());
        return;
    }
    if (m_statusError) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_status.c_str());
    else ImGui::TextUnformatted(m_status.c_str());
    if (!m_jobOutput.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Output"))) ImGui::OpenPopup("##output");
        if (ImGui::BeginPopup("##output")) {
            ImGui::InputTextMultiline("##text", m_jobOutput.data(), m_jobOutput.size() + 1,
                                      ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.8f, ImGui::GetTextLineHeight() * 20),
                                      ImGuiInputTextFlags_ReadOnly);
            ImGui::EndPopup();
        }
    }
}

void App::installEngine(const Release& release) {
    if (!m_job.start(trf("Install engine %s", release.version.c_str()),
                     powershellArgs(installerScript(), {"engine", "-Index", releaseIndex(), "-Version", release.version, "-Root", enginesRoot()})))
        return;
    m_jobInstallsEngine = true;
    m_status = trf("Installing engine %s...", release.version.c_str());
    m_statusError = false;
    m_jobOutput.clear();
}

void App::openProject(const Project& project) {
    bool byVersion = false;
    const Engine* engine = m_catalog.engineFor(project, &byVersion);
    if (!engine) {
        m_status = trf("The engine of %s was not found: %s", project.name.c_str(), project.engine.c_str());
        m_statusError = true;
        return;
    }
    if (engine->editorExe.empty()) {
        m_status = trf("The editor of engine %s is not built yet - build it on the Engines tab.", engine->version.c_str());
        m_statusError = true;
        return;
    }
    // Found by version: point the engine line at this PC's engine, since the project's build.bat builds with that folder
    std::string repointed;
    if (byVersion) {
        if (!setProjectEngine(project.directory, engine->directory)) {
            m_status = trf("Cannot update the engine line of %s", project.name.c_str());
            m_statusError = true;
            return;
        }
        repointed = trf(" (engine line now %s)", engine->directory.c_str());
    }
    std::string error;
    if (!launchDetached({engine->editorExe, "--project", project.directory}, error)) {
        m_status = trf("Cannot start the editor: %s", error.c_str());
        m_statusError = true;
        return;
    }
    m_catalog.addRecent(project.directory); // the editor adds it too when opening, but this fixes the list order right away
    m_status = trf("Opened %s", project.name.c_str()) + repointed;
    m_statusError = false;
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
    SDL_ShowOpenFolderDialog(onFolderPicked, &g_pickTarget, window, purpose == 2 ? m_newLocation : nullptr, false);
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
            m_status = trf("%s has no mealkit.project", picked.c_str());
            m_statusError = true;
            return;
        }
        m_catalog.addRecent(p.directory);
        m_selected = p.directory;
        m_status = trf("Imported %s", p.name.c_str());
        m_statusError = false;
    } else if (purpose == 1) {
        Engine e;
        if (!readEngine(picked, e)) {
            m_status = trf("%s is not a MealKit engine folder (VERSION, tools\\new_project.ps1)", picked.c_str());
            m_statusError = true;
            return;
        }
        m_catalog.addEngine(e.directory);
        m_status = trf("Added engine %s", e.version.c_str());
        m_statusError = false;
    } else {
        std::snprintf(m_newLocation, sizeof(m_newLocation), "%s", picked.c_str());
    }
    reload();
}

} // namespace mkmanager
