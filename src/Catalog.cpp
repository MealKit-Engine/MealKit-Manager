#include "Catalog.h"

#include <algorithm>
#include <filesystem>

namespace mkmanager {
namespace {

using mkcontract::joinPath;
using mkcontract::pathExists;

// Folders that may hold the project's engine: the engine line, or (absent) the project folder and its parents,
// since a sample inside the engine repo (samples/sandbox) has no engine line either
std::vector<std::string> engineCandidates(const Project& project) {
    if (!project.engine.empty()) return {project.engine};
    std::vector<std::string> dirs;
    for (std::filesystem::path dir = mkcontract::pathOf(project.directory); !dir.empty(); dir = dir.parent_path()) {
        dirs.push_back(mkcontract::utf8Of(dir));
        if (dir == dir.parent_path()) break;
    }
    return dirs;
}

} // namespace

bool readProject(const std::string& directory, Project& out) {
    out = Project{};
    out.directory = normalizeDirectory(directory);
    const std::string file = joinPath(out.directory, "mealkit.project");
    if (!pathExists(file)) return false;
    out.name = readKey(file, "name");
    out.engine = readKey(file, "engine");
    out.engineVersion = readKey(file, "engineVersion");
    out.created = readKey(file, "created");
    out.valid = !out.name.empty();
    return out.valid;
}

bool readEngine(const std::string& directory, Engine& out) {
    out = Engine{};
    out.directory = normalizeDirectory(directory);
    const std::vector<std::string> version = readList(joinPath(out.directory, "VERSION"));
    if (!version.empty()) out.version = version[0];
    const std::string editor = joinPath(out.directory, "build/Editor/bin/MealKitEditor.exe");
    if (pathExists(editor)) out.editorExe = normalizeDirectory(editor);
    out.valid = !out.version.empty() && pathExists(joinPath(out.directory, "tools/new_project.ps1"));
    return out.valid;
}

void Catalog::load(const std::string& extraEngine) {
    const std::vector<std::string> favorites = mkcontract::favoriteProjects();

    m_projects.clear();
    for (const std::string& dir : mkcontract::recentProjects()) {
        Project p;
        readProject(dir, p);
        if (p.directory.empty()) p.directory = dir;
        p.favorite = mkcontract::containsDirectory(favorites, p.directory);
        m_projects.push_back(std::move(p));
    }
    // Favorites first, and within them by recency (list order) as is
    std::stable_sort(m_projects.begin(), m_projects.end(),
                     [](const Project& a, const Project& b) { return a.favorite && !b.favorite; });

    // Engines: --engine, engines.txt, engines projects point to, the engine repo itself (projects without an engine line)
    std::vector<std::string> candidates;
    if (!extraEngine.empty()) candidates.push_back(extraEngine);
    for (const std::string& dir : mkcontract::addedEngines()) candidates.push_back(dir);
    for (const Project& p : m_projects) {
        if (!p.valid) continue;
        for (const std::string& dir : engineCandidates(p)) {
            Engine e;
            if (readEngine(dir, e)) { candidates.push_back(dir); break; }
        }
    }
    m_engines.clear();
    for (const std::string& dir : candidates) {
        const bool known = std::any_of(m_engines.begin(), m_engines.end(),
                                       [&](const Engine& e) { return sameDirectory(e.directory, dir); });
        Engine e;
        if (!known && readEngine(dir, e)) m_engines.push_back(std::move(e));
    }

    // Tools: the same search as the editor's [Tools] menu (unreadable manifests are left out)
    m_tools = mkcontract::findTools(mkcontract::toolSearchRoots());
    const std::string language = mkcontract::editorSetting("language");
    if (!language.empty()) m_language = language;
}

const Engine* Catalog::engineFor(const Project& project) const {
    for (const std::string& dir : engineCandidates(project))
        for (const Engine& e : m_engines)
            if (sameDirectory(e.directory, dir)) return &e;
    return nullptr;
}

bool Catalog::needsUpgrade(const Project& project) const {
    const Engine* engine = engineFor(project);
    // The engine repo itself (no engine line) and old projects without engineVersion aren't asked about (same as the editor)
    return engine && !project.engine.empty() && !project.engineVersion.empty() && project.engineVersion != engine->version;
}

void Catalog::addRecent(const std::string& directory) { mkcontract::addRecentProject(directory); }
void Catalog::removeRecent(const std::string& directory) { mkcontract::removeRecentProject(directory); }
void Catalog::setFavorite(const std::string& directory, bool favorite) { mkcontract::setFavoriteProject(directory, favorite); }
void Catalog::addEngine(const std::string& directory) { mkcontract::addEngine(directory); }

} // namespace mkmanager
