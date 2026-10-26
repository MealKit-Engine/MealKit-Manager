#include "App.h"

#include "Strings.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace mkmanager {
namespace {

bool containsNoCase(const std::string& text, const char* needle) {
    if (!needle[0]) return true;
    auto it = std::search(text.begin(), text.end(), needle, needle + std::strlen(needle), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != text.end();
}

// 이름은 exe 이름과 CMake 타깃 이름이 된다 (new_project.ps1과 같은 규칙)
bool validGameName(const char* name) {
    if (!std::isalpha(static_cast<unsigned char>(name[0]))) return false;
    for (const char* c = name; *c; ++c)
        if (!std::isalnum(static_cast<unsigned char>(*c)) && *c != '_') return false;
    return true;
}

std::string pathJoin(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '\\' || last == '/') ? a + b : a + "\\" + b;
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
        const bool ok = m_job.exitCode() == 0;
        m_status = ok ? trf("%s: done", m_job.label().c_str()) : trf("%s: failed (exit code %d)", m_job.label().c_str(), m_job.exitCode());
        m_statusError = !ok;
        m_jobOutput = ok ? std::string() : m_job.output();
        if (ok && !m_jobAdd.empty()) {
            m_catalog.addRecent(m_jobAdd);
            m_selected = normalizeDirectory(m_jobAdd);
        }
        m_jobAdd.clear();
        reload();
    }

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
        if (ImGui::BeginTabItem(tr("Engines"))) {
            enginesTab();
            ImGui::EndTabItem();
        }
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
                break; // 목록이 바뀌었다
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
            const Engine* engine = m_catalog.engineFor(p);
            if (!p.valid) ImGui::TextDisabled("-");
            else if (!engine) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tr("engine not found"));
            else if (m_catalog.needsUpgrade(p))
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s -> %s", p.engineVersion.c_str(), engine->version.c_str());
            else ImGui::Text("%s%s", engine->version.c_str(), p.engine.empty() ? tr(" (engine sample)") : "");

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", p.created.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // 고른 프로젝트 동작
    ImGui::BeginDisabled(!selected || busy);
    if (ImGui::Button(tr("Open in Editor")) && selected) openProject(*selected);
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
                    // 콘솔이 없어 시스템 코드 페이지로 돈다 (ninja의 /showIncludes 접두어와 맞는다)
                    m_job.start(tr("Editor build"), {"cmd.exe", "/d", "/c", pathJoin(e.directory, "build.bat"), "Editor"});
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
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
    ImGui::TextDisabled("%s", pathJoin(m_newLocation, m_newName).c_str());
    ImGui::Separator();
    ImGui::BeginDisabled(!nameOk || engines.empty() || !m_newLocation[0]);
    if (ImGui::Button(tr("Create"))) {
        saveProjectLocation(m_newLocation);
        const Engine& engine = engines[m_newEngine];
        runScript(tr("New project"), pathJoin(engine.directory, "tools\\new_project.ps1"), {"-Name", m_newName, "-Path", m_newLocation},
                  pathJoin(m_newLocation, m_newName));
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

void App::openProject(const Project& project) {
    const Engine* engine = m_catalog.engineFor(project);
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
    std::string error;
    if (!launchDetached({engine->editorExe, "--project", project.directory}, error)) {
        m_status = trf("Cannot start the editor: %s", error.c_str());
        m_statusError = true;
        return;
    }
    m_catalog.addRecent(project.directory); // 에디터도 열면 더하지만, 목록 순서를 바로 맞춘다
    m_status = trf("Opened %s", project.name.c_str());
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
        m_picked = "\x01"; // 기다리는 중
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
    if (picked.empty()) return; // 취소
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
