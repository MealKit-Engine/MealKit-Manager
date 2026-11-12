#pragma once

// What MealKit Manager handles: projects, installed engines, tools. A data layer that knows nothing about windows/ImGui (tested by tests/CatalogTests.cpp).
// Connects to the engine only through files. The shared ones (the user folder and its lists, tool manifests, the module list lines) are read with the
// tool contract library mkcontract (MealKit-Contract, third_party/mkcontract, its docs/CONTRACT.md), the same code the engine editor uses. Read here:
//   mealkit.project in the project folder         name, engine (engine folder), engineVersion, created lines
//   engine folder                                 VERSION, build\Editor\bin\MealKitEditor.exe, tools\new_project.ps1
#include <mkcontract/Files.h>
#include <mkcontract/Live.h>
#include <mkcontract/Modules.h>
#include <mkcontract/Tool.h>
#include <mkcontract/User.h>

#include <string>
#include <vector>

namespace mkmanager {

struct Project {
    std::string directory;
    std::string name;
    std::string engine;        // engine line of mealkit.project (blank if absent: the engine repo itself or a sample inside it)
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

using Tool = mkcontract::ToolManifest;

// Source modules (engine docs/MODULES.md), from the engine's tools\module.ps1 search -Tsv -Project <folder>
using mkcontract::InstalledModule;
using mkcontract::ModuleList;
using mkcontract::parseModuleList;
using mkcontract::RegistryModule;

inline constexpr uint32_t kLiveProtocol = mkcontract::live::kProtocol; // live connection contract version this Manager knows

using mkcontract::normalizeDirectory;
using mkcontract::readKey;
using mkcontract::readList;
using mkcontract::sameDirectory;
using mkcontract::userDirectory;
using mkcontract::writeList;

// An engine in the suite release index (engine tools\release_index.ps1), as scripts\install.ps1 list prints it
struct Release {
    std::string version;
    std::string url;
    std::string sha256;
    unsigned long long size = 0;
};
// "engine<TAB>version<TAB>size<TAB>url<TAB>sha256" lines; other lines are skipped
std::vector<Release> parseReleaseList(const std::string& output);
// Folder from the "installed<TAB><folder>" line of install.ps1 engine (blank if none)
std::string installedFolder(const std::string& output);
// Release index location: MEALKIT_RELEASE_INDEX (url or path). Blank until the index is published (engine ROADMAP R2)
std::string releaseIndex();
// Where installed engines go: <user folder>\engines (%LOCALAPPDATA%\MealKit\engines)
std::string enginesRoot();

bool readProject(const std::string& directory, Project& out);
bool readEngine(const std::string& directory, Engine& out);

class Catalog {
public:
    // extraEngine: command line --engine (that editor opened Manager)
    void load(const std::string& extraEngine = {});

    const std::vector<Project>& projects() const { return m_projects; }
    const std::vector<Engine>& engines() const { return m_engines; }
    const std::vector<Tool>& tools() const { return m_tools; }
    const std::string& language() const { return m_language; }

    // Engine that opens the project: the folder in the engine line, or if absent the project folder or its nearest parent that is an engine (engine repo and its samples). nullptr if not found
    const Engine* engineFor(const Project& project) const;
    // Whether it needs upgrading to this engine (project engineVersion != engine VERSION)
    bool needsUpgrade(const Project& project) const;
    // Whether an engine of this version is already in the list
    bool hasEngineVersion(const std::string& version) const;

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
