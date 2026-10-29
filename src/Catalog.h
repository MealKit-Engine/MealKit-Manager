#pragma once

// What MealKit Manager handles: projects, installed engines, tools. A data layer that knows nothing about windows/ImGui (tested by tests/CatalogTests.cpp).
// Connects to the engine only through files (engine docs/CODING_MODEL.md "Contract between tools"):
//   user folder %LOCALAPPDATA%\MealKit\ (tests change it with the environment variable MEALKIT_USER_DIR)
//     recent_projects.txt, favorite_projects.txt   lists shared with the editor (one folder per line)
//     engines.txt                                  engine folders added by Manager (one per line)
//     editor_settings.txt                          editor settings: reads the language and toolPath lines
//     tools\<tool>\mealkit-tool.txt               manifests of installed tools (@format tool 1)
//   mealkit.project in the project folder         name, engine (engine folder), engineVersion, created lines
//   engine folder                                 VERSION, build\Editor\bin\MealKitEditor.exe, tools\new_project.ps1
#include <string>
#include <vector>

namespace mkmanager {

struct Project {
    std::string directory;
    std::string name;
    std::string engine;        // engine line of mealkit.project (blank if absent: the engine repo itself)
    std::string engineVersion; // engine version that created or upgraded the project
    std::string created;
    bool valid = false;        // the folder has mealkit.project with a name
    bool favorite = false;
};

struct Engine {
    std::string directory;
    std::string version;   // VERSION file
    std::string editorExe; // built editor (blank if none)
    bool valid = false;    // has VERSION and tools\new_project.ps1
};

struct Tool {
    std::string folder; // folder containing the manifest
    std::string name;
    std::string exe;    // absolute path
    std::string version;
    int protocol = 0;   // live connection contract version (engine live::kProtocol)
    std::string opens;  // file extensions this tool opens (space-separated)
};

// Source modules (engine docs/MODULES.md). Read from the output of the engine's tools\module.ps1 search -Tsv -Project <folder>
// (same contract as parseModuleList in the engine editor's editor/ModulesPanel.cpp: change all three places together when the line shape changes)
struct InstalledModule {
    std::string name, version, source;
    int modified = 0; // number of files the game modified after install
    int pending = 0;  // number of .new files left by an update
};

struct RegistryModule {
    std::string name;
    std::string version;      // newest version for the project's engine MINOR (blank if none)
    std::string latest;       // newest version
    std::string latestEngine; // engine MAJOR.MINOR of that version
    std::string title, summary, tags, author, repo;
};

struct ModuleList {
    std::vector<InstalledModule> installed;
    std::vector<RegistryModule> registry;
    std::string error; // why the registry list couldn't be fetched
};

ModuleList parseModuleList(const std::string& output);

constexpr int kLiveProtocol = 1; // live connection contract version this Manager knows

std::string userDirectory();
// List files with one item per line (ignores BOM, empty lines, leading/trailing spaces)
std::vector<std::string> readList(const std::string& path);
void writeList(const std::string& path, const std::vector<std::string>& list);
// Normalizing folder notation (absolute path, no trailing separator, \ on Windows) / whether it's the same folder (case-insensitive on Windows)
std::string normalizeDirectory(const std::string& directory);
bool sameDirectory(const std::string& a, const std::string& b);

bool readProject(const std::string& directory, Project& out);
bool readEngine(const std::string& directory, Engine& out);
bool readToolManifest(const std::string& folder, Tool& out);
// One key from a "key value" line file (the first value). Comments (#) and @format lines are skipped
std::string readKey(const std::string& path, const std::string& key);

class Catalog {
public:
    // extraEngine: command line --engine (that editor opened Manager)
    void load(const std::string& extraEngine = {});

    const std::vector<Project>& projects() const { return m_projects; }
    const std::vector<Engine>& engines() const { return m_engines; }
    const std::vector<Tool>& tools() const { return m_tools; }
    const std::string& language() const { return m_language; }

    // Engine that opens the project: the folder in the engine line, or the project folder itself if absent (engine repo). nullptr if not found
    const Engine* engineFor(const Project& project) const;
    // Whether it needs upgrading to this engine (project engineVersion != engine VERSION)
    bool needsUpgrade(const Project& project) const;

    void addRecent(const std::string& directory);    // to the top of the list
    void removeRecent(const std::string& directory); // removed only from the list (the folder stays)
    void setFavorite(const std::string& directory, bool favorite);
    void addEngine(const std::string& directory);

private:
    std::vector<Project> m_projects;
    std::vector<Engine> m_engines;
    std::vector<Tool> m_tools;
    std::string m_language = "ko";
};

} // namespace mkmanager
