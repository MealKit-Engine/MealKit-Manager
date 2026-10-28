#include "Catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace mkmanager {
namespace {
namespace fs = std::filesystem;

constexpr size_t kMaxRecent = 100; // 에디터(editor/Project.cpp)와 같다

fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string toUtf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::exists(fromUtf8(path), ec);
}

std::string join(const std::string& directory, const std::string& name) { return toUtf8(fromUtf8(directory) / fromUtf8(name)); }

std::string environment(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || !value) return {};
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

void eraseDirectory(std::vector<std::string>& list, const std::string& directory) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const std::string& d) { return sameDirectory(d, directory); }),
               list.end());
}

bool containsDirectory(const std::vector<std::string>& list, const std::string& directory) {
    return std::any_of(list.begin(), list.end(), [&](const std::string& d) { return sameDirectory(d, directory); });
}

} // namespace

std::string userDirectory() {
    const std::string overridden = environment("MEALKIT_USER_DIR");
    if (!overridden.empty()) return overridden;
#if defined(_WIN32)
    const std::string base = environment("LOCALAPPDATA");
#else
    std::string base = environment("XDG_DATA_HOME");
    if (base.empty()) base = environment("HOME") + "/.local/share";
#endif
    return join(base, "MealKit");
}

std::vector<std::string> readList(const std::string& path) {
    std::vector<std::string> list;
    std::ifstream in(fromUtf8(path), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (list.empty() && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3); // 메모장·PowerShell이 붙인 BOM
        line = trim(line);
        if (!line.empty()) list.push_back(line);
    }
    return list;
}

void writeList(const std::string& path, const std::vector<std::string>& list) {
    std::error_code ec;
    fs::create_directories(fromUtf8(path).parent_path(), ec);
    std::ofstream out(fromUtf8(path), std::ios::binary | std::ios::trunc);
    for (const std::string& line : list) out << line << '\n';
}

std::string normalizeDirectory(const std::string& directory) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fromUtf8(directory), ec);
    if (ec) p = fromUtf8(directory);
    std::string s = toUtf8(p.make_preferred());
    while (s.size() > 3 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    return s;
}

bool sameDirectory(const std::string& a, const std::string& b) {
    const std::string x = normalizeDirectory(a), y = normalizeDirectory(b);
#if defined(_WIN32)
    return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), [](char p, char q) {
               return std::tolower(static_cast<unsigned char>(p)) == std::tolower(static_cast<unsigned char>(q));
           });
#else
    return x == y;
#endif
}

std::string readKey(const std::string& path, const std::string& key) {
    for (const std::string& line : readList(path)) {
        if (line[0] == '#' || line[0] == '@') continue;
        const size_t space = line.find_first_of(" \t=");
        if (space == std::string::npos || line.compare(0, space, key) != 0) continue;
        size_t value = line.find_first_not_of(" \t=", space);
        return value == std::string::npos ? std::string() : trim(line.substr(value));
    }
    return {};
}

bool readProject(const std::string& directory, Project& out) {
    out = Project{};
    out.directory = normalizeDirectory(directory);
    const std::string file = join(out.directory, "mealkit.project");
    if (!exists(file)) return false;
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
    const std::vector<std::string> version = readList(join(out.directory, "VERSION"));
    if (!version.empty()) out.version = version[0];
    const std::string editor = join(out.directory, "build/Editor/bin/MealKitEditor.exe");
    if (exists(editor)) out.editorExe = normalizeDirectory(editor);
    out.valid = !out.version.empty() && exists(join(out.directory, "tools/new_project.ps1"));
    return out.valid;
}

bool readToolManifest(const std::string& folder, Tool& out) {
    out = Tool{};
    out.folder = normalizeDirectory(folder);
    const std::string file = join(out.folder, "mealkit-tool.txt");
    if (!exists(file)) return false;
    out.name = readKey(file, "name");
    const std::string exe = readKey(file, "exe");
    out.version = readKey(file, "version");
    out.protocol = std::atoi(readKey(file, "protocol").c_str());
    out.opens = readKey(file, "opens");
    if (out.name.empty() || exe.empty()) return false;
    out.exe = normalizeDirectory(join(out.folder, exe));
    return true;
}

void Catalog::load(const std::string& extraEngine) {
    const std::string user = userDirectory();
    const std::vector<std::string> favorites = readList(join(user, "favorite_projects.txt"));

    m_projects.clear();
    for (const std::string& dir : readList(join(user, "recent_projects.txt"))) {
        Project p;
        readProject(dir, p);
        if (p.directory.empty()) p.directory = dir;
        p.favorite = containsDirectory(favorites, p.directory);
        m_projects.push_back(std::move(p));
    }
    // 즐겨찾기 먼저, 그 안에서는 최근 순(목록 순서) 그대로
    std::stable_sort(m_projects.begin(), m_projects.end(),
                     [](const Project& a, const Project& b) { return a.favorite && !b.favorite; });

    // 엔진: --engine, engines.txt, 프로젝트가 가리키는 엔진, 엔진 저장소 자신(engine 줄이 없는 프로젝트)
    std::vector<std::string> candidates;
    if (!extraEngine.empty()) candidates.push_back(extraEngine);
    for (const std::string& dir : readList(join(user, "engines.txt"))) candidates.push_back(dir);
    for (const Project& p : m_projects) {
        if (!p.valid) continue;
        candidates.push_back(p.engine.empty() ? p.directory : p.engine);
    }
    m_engines.clear();
    for (const std::string& dir : candidates) {
        const bool known = std::any_of(m_engines.begin(), m_engines.end(),
                                       [&](const Engine& e) { return sameDirectory(e.directory, dir); });
        Engine e;
        if (!known && readEngine(dir, e)) m_engines.push_back(std::move(e));
    }

    // 도구: tools\<도구>\, 에디터 설정의 toolPath (그 폴더에 매니페스트가 있거나 그 아래 폴더마다 있다). 에디터 editor/Tools.cpp와 같은 규칙
    const std::string settings = join(user, "editor_settings.txt");
    std::vector<std::string> roots = {join(user, "tools")};
    for (const std::string& line : readList(settings))
        if (line.rfind("toolPath", 0) == 0) roots.push_back(trim(line.substr(8)));
    const std::string language = readKey(settings, "language");
    if (!language.empty()) m_language = language;

    m_tools.clear();
    auto addTool = [&](const std::string& folder) {
        Tool t;
        if (!readToolManifest(folder, t)) return;
        for (const Tool& known : m_tools)
            if (sameDirectory(known.exe, t.exe)) return;
        m_tools.push_back(std::move(t));
    };
    for (const std::string& root : roots) {
        if (exists(join(root, "mealkit-tool.txt"))) {
            addTool(root);
            continue;
        }
        std::error_code ec;
        for (fs::directory_iterator it(fromUtf8(root), ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec)) addTool(toUtf8(it->path()));
    }
    std::sort(m_tools.begin(), m_tools.end(), [](const Tool& a, const Tool& b) { return a.name < b.name; });
}

const Engine* Catalog::engineFor(const Project& project) const {
    const std::string dir = project.engine.empty() ? project.directory : project.engine;
    for (const Engine& e : m_engines)
        if (sameDirectory(e.directory, dir)) return &e;
    return nullptr;
}

bool Catalog::needsUpgrade(const Project& project) const {
    const Engine* engine = engineFor(project);
    // 엔진 저장소 자신(engine 줄 없음)과 engineVersion 없는 옛 프로젝트는 묻지 않는다 (에디터와 같다)
    return engine && !project.engine.empty() && !project.engineVersion.empty() && project.engineVersion != engine->version;
}

void Catalog::addRecent(const std::string& directory) {
    const std::string path = join(userDirectory(), "recent_projects.txt");
    const std::string dir = normalizeDirectory(directory);
    std::vector<std::string> list = readList(path);
    eraseDirectory(list, dir);
    list.insert(list.begin(), dir);
    if (list.size() > kMaxRecent) list.resize(kMaxRecent);
    writeList(path, list);
}

void Catalog::removeRecent(const std::string& directory) {
    const std::string path = join(userDirectory(), "recent_projects.txt");
    std::vector<std::string> list = readList(path);
    eraseDirectory(list, directory);
    writeList(path, list);
    setFavorite(directory, false);
}

void Catalog::setFavorite(const std::string& directory, bool favorite) {
    const std::string path = join(userDirectory(), "favorite_projects.txt");
    std::vector<std::string> list = readList(path);
    eraseDirectory(list, directory);
    if (favorite) list.push_back(normalizeDirectory(directory));
    writeList(path, list);
}

void Catalog::addEngine(const std::string& directory) {
    const std::string path = join(userDirectory(), "engines.txt");
    std::vector<std::string> list = readList(path);
    if (!containsDirectory(list, directory)) list.push_back(normalizeDirectory(directory));
    writeList(path, list);
}

ModuleList parseModuleList(const std::string& output) {
    ModuleList list;
    size_t start = 0;
    while (start < output.size()) {
        size_t nl = output.find('\n', start);
        if (nl == std::string::npos) nl = output.size();
        std::string line = output.substr(start, nl - start);
        start = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        size_t from = 0;
        for (size_t tab = line.find('\t'); tab != std::string::npos; tab = line.find('\t', from)) {
            f.push_back(line.substr(from, tab - from));
            from = tab + 1;
        }
        f.push_back(line.substr(from));
        if (f[0] == "installed" && f.size() >= 6) {
            list.installed.push_back({f[1], f[2], f[5], std::atoi(f[3].c_str()), std::atoi(f[4].c_str())});
        } else if (f[0] == "module" && f.size() >= 10) {
            list.registry.push_back({f[1], f[2] == "-" ? std::string() : f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9]});
        } else if (f[0] == "error" && f.size() >= 2) {
            list.error = f[1];
        }
    }
    return list;
}

} // namespace mkmanager
