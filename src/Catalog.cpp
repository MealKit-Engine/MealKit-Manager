#include "Catalog.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>

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

namespace {
std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return out;
}
std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}
} // namespace

std::vector<Release> parseReleaseList(const std::string& output) {
    std::vector<Release> out;
    for (const std::string& line : lines(output)) {
        const std::vector<std::string> f = splitTabs(line);
        if (f.size() < 5 || f[0] != "engine" || f[1].empty() || f[3].empty()) continue;
        Release r;
        r.version = f[1];
        r.size = std::strtoull(f[2].c_str(), nullptr, 10);
        r.url = f[3];
        r.sha256 = f[4];
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<ToolRelease> parseToolReleases(const std::string& output) {
    std::vector<ToolRelease> out;
    for (const std::string& line : lines(output)) {
        const std::vector<std::string> f = splitTabs(line);
        if (f.size() < 8 || f[0] != "tool" || f[1].empty() || f[2].empty() || f[4].empty()) continue;
        ToolRelease t;
        t.name = f[1];
        t.version = f[2];
        t.size = std::strtoull(f[3].c_str(), nullptr, 10);
        t.url = f[4];
        t.sha256 = f[5];
        t.protocol = static_cast<uint32_t>(std::strtoul(f[6].c_str(), nullptr, 10));
        size_t start = 0;
        while (start <= f[7].size()) {
            const size_t comma = f[7].find(',', start);
            const std::string minor = f[7].substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!minor.empty()) t.engines.push_back(minor);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        out.push_back(std::move(t));
    }
    return out;
}

std::string toolsRoot() { return joinPath(userDirectory(), "tools"); }

Release parseManagerRelease(const std::string& output) {
    for (const std::string& line : lines(output)) {
        const std::vector<std::string> f = splitTabs(line);
        if (f.size() < 5 || f[0] != "manager" || f[1].empty() || f[3].empty()) continue;
        return Release{f[1], f[3], f[4], std::strtoull(f[2].c_str(), nullptr, 10)};
    }
    return {};
}

std::string installedFolder(const std::string& output) {
    std::string folder;
    for (const std::string& line : lines(output))
        if (line.rfind("installed\t", 0) == 0) folder = line.substr(10);
    return folder;
}

namespace {
std::vector<int> versionParts(const std::string& version) {
    std::vector<int> parts;
    size_t start = 0;
    while (start <= version.size()) {
        const size_t dot = version.find('.', start);
        const std::string part = version.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (part.empty() || part.find_first_not_of("0123456789") != std::string::npos) return {};
        parts.push_back(std::atoi(part.c_str()));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return parts;
}
} // namespace

std::string minorOf(const std::string& version) {
    const std::vector<int> p = versionParts(version);
    if (p.size() < 2) return {};
    return std::to_string(p[0]) + "." + std::to_string(p[1]);
}

bool versionLess(const std::string& a, const std::string& b) { return versionParts(a) < versionParts(b); }

bool setProjectKey(const std::string& projectDirectory, const std::string& key, const std::string& value) {
    const std::string file = joinPath(projectDirectory, "mealkit.project");
    std::string text;
    if (!mkcontract::readFile(file, text)) return false;
    const bool crlf = text.find("\r\n") != std::string::npos;
    const char* eol = crlf ? "\r\n" : "\n";
    std::string out;
    bool replaced = false;
    for (const std::string& line : lines(text)) {
        const size_t start = line.find_first_not_of(" \t\xEF\xBB\xBF");
        const bool isKey = start != std::string::npos && line.compare(start, key.size(), key) == 0 && line.size() > start + key.size() &&
                           (line[start + key.size()] == ' ' || line[start + key.size()] == '\t');
        if (isKey && !replaced) {
            if (!value.empty()) out += line.substr(0, start) + key + " " + value + eol;
            replaced = true;
        } else {
            out += line + eol;
        }
    }
    if (!replaced && !value.empty()) out += key + " " + value + eol;
    return mkcontract::writeFile(file, out);
}

bool setProjectIcon(const std::string& projectDirectory, const std::string& pngFile, std::string& error) {
    namespace fs = std::filesystem;
    auto native = [](const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); };
    const fs::path target = native(joinPath(projectDirectory, "icon.png"));
    std::error_code ec;
    if (pngFile.empty()) {
        fs::remove(target, ec);
        if (!setProjectKey(projectDirectory, "icon", "")) {
            error = "mealkit.project can't be written";
            return false;
        }
        return true;
    }
    std::string head;
    if (!mkcontract::readFile(pngFile, head) || head.compare(0, 8, "\x89PNG\r\n\x1A\n") != 0) {
        error = "not a PNG file";
        return false;
    }
    if (!fs::equivalent(native(pngFile), target, ec)) {
        fs::copy_file(native(pngFile), target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
    }
    if (!setProjectKey(projectDirectory, "icon", "icon.png")) {
        error = "mealkit.project can't be written";
        return false;
    }
    return true;
}

Toolchain parseToolchain(const std::string& output) {
    Toolchain t;
    for (const std::string& line : lines(output)) {
        const std::vector<std::string> f = splitTabs(line);
        if (f.size() < 2) continue;
        const bool ok = f[1] == "ok";
        const std::string path = f.size() > 2 ? f[2] : std::string();
        if (f[0] == "compiler") {
            t.checked = true;
            t.compiler = ok;
            t.visualStudio = path;
        } else if (f[0] == "dxc") {
            t.dxc = ok;
            t.dxcPath = path;
        }
    }
    return t;
}

std::string releaseIndex() {
    const char* env = std::getenv("MEALKIT_RELEASE_INDEX");
    return env ? env : "";
}

std::string enginesRoot() { return joinPath(userDirectory(), "engines"); }

bool readProject(const std::string& directory, Project& out) {
    out = Project{};
    out.directory = normalizeDirectory(directory);
    const std::string file = joinPath(out.directory, "mealkit.project");
    if (!pathExists(file)) return false;
    out.name = readKey(file, "name");
    out.engine = readKey(file, "engine");
    out.engineVersion = readKey(file, "engineVersion");
    out.created = readKey(file, "created");
    out.icon = readKey(file, "icon"); // blank: icon.png
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

const Engine* Catalog::engineFor(const Project& project, bool* byVersion) const {
    if (byVersion) *byVersion = false;
    for (const std::string& dir : engineCandidates(project))
        for (const Engine& e : m_engines)
            if (sameDirectory(e.directory, dir)) return &e;
    // Only for a project with an engine line (not the engine repo's samples) whose folder isn't an engine on this PC
    if (project.engine.empty() || minorOf(project.engineVersion).empty()) return nullptr;
    Engine there;
    if (readEngine(project.engine, there)) return nullptr; // an engine is there, just not in the list yet
    const Engine* best = nullptr;
    for (const Engine& e : m_engines) {
        if (minorOf(e.version) != minorOf(project.engineVersion)) continue;
        if (e.version == project.engineVersion) { best = &e; break; }
        if (!best || versionLess(best->version, e.version)) best = &e;
    }
    if (best && byVersion) *byVersion = true;
    return best;
}

const Release* Catalog::releaseFor(const Project& project, const std::vector<Release>& releases) {
    const std::string minor = minorOf(project.engineVersion);
    if (minor.empty()) return nullptr;
    for (const Release& r : releases) // the index keeps one patch per MINOR
        if (minorOf(r.version) == minor) return &r;
    return nullptr;
}

bool Catalog::needsUpgrade(const Project& project) const {
    const Engine* engine = engineFor(project);
    // The engine repo itself (no engine line) and old projects without engineVersion aren't asked about (same as the editor)
    return engine && !project.engine.empty() && !project.engineVersion.empty() && project.engineVersion != engine->version;
}

bool Catalog::hasEngineVersion(const std::string& version) const {
    return std::any_of(m_engines.begin(), m_engines.end(), [&](const Engine& e) { return e.version == version; });
}

bool Catalog::toolMatches(const ToolRelease& tool) const {
    if (tool.protocol != kLiveProtocol) return false;
    for (const Engine& e : m_engines)
        if (std::find(tool.engines.begin(), tool.engines.end(), minorOf(e.version)) != tool.engines.end()) return true;
    return false;
}

const Tool* Catalog::installedTool(const std::string& name) const {
    for (const Tool& t : m_tools)
        if (t.name == name) return &t;
    return nullptr;
}

void Catalog::addRecent(const std::string& directory) { mkcontract::addRecentProject(directory); }
void Catalog::removeRecent(const std::string& directory) { mkcontract::removeRecentProject(directory); }
void Catalog::setFavorite(const std::string& directory, bool favorite) { mkcontract::setFavoriteProject(directory, favorite); }
void Catalog::addEngine(const std::string& directory) { mkcontract::addEngine(directory); }

} // namespace mkmanager
