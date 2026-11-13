#pragma once

// What MealKit Manager handles: projects, installed engines, tools. A data layer that knows nothing about windows/ImGui (tested by tests/CatalogTests.cpp).
// Connects to the engine only through files. The shared ones (the user folder and its lists, tool manifests) are read with the
// tool contract library mkcontract (MealKit-Contract, third_party/mkcontract, its docs/CONTRACT.md), the same code the engine editor uses. Read here:
//   mealkit.project in the project folder         name, engine (engine folder), engineVersion, created lines
//   engine folder                                 VERSION, build\Editor\bin\MealKitEditor.exe, tools\new_project.ps1
#include <mkcontract/Files.h>
#include <mkcontract/Live.h>
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
    std::string icon;          // mealkit.project "icon" (relative path; blank = icon.png)
    bool valid = false;        // the folder has mealkit.project with a name
    bool favorite = false;
    bool temp = false;         // inside %TEMP% (isTempFolder): kept out of the way in the list
};

struct Engine {
    std::string directory;
    std::string version;   // VERSION file
    std::string editorExe; // built editor (blank if none)
    bool valid = false;    // has VERSION and tools\new_project.ps1
};

using Tool = mkcontract::ToolManifest;

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
// A tool in the release index: its live protocol and the engine MINORs it works with (engine docs/DECISIONS.md "How the Manager installs")
struct ToolRelease {
    std::string name;
    std::string version;
    std::string url;
    std::string sha256;
    unsigned long long size = 0;
    uint32_t protocol = 0;
    std::vector<std::string> engines; // "0.9"
    std::string summary, icon;        // from the tool's manifest (optional)
};
// "tool<TAB>name<TAB>version<TAB>size<TAB>url<TAB>sha256<TAB>protocol<TAB>engines[<TAB>summary<TAB>icon]" lines (engines comma separated)
std::vector<ToolRelease> parseToolReleases(const std::string& output);
// Where installed tools go: <user folder>\tools\<tool> (where the editor's [Tools] menu and this Manager look)
std::string toolsRoot();
// The folder is inside this PC's temp folder (%TEMP%): throwaway and test projects the project list keeps out of the way
bool isTempFolder(const std::string& directory);
// The index's MealKit Manager entry, "manager<TAB>version<TAB>size<TAB>url<TAB>sha256" (version blank if none)
Release parseManagerRelease(const std::string& output);
// Folder from the "installed<TAB><folder>" line of install.ps1 engine (blank if none)
std::string installedFolder(const std::string& output);
// Release index location: MEALKIT_RELEASE_INDEX (url or path). Blank until the index is published (engine ROADMAP R2)
std::string releaseIndex();
// Where installed engines go: <user folder>\engines (%LOCALAPPDATA%\MealKit\engines)
std::string enginesRoot();

// What this PC has for building game code (scripts\install.ps1 check)
struct Toolchain {
    bool checked = false;
    bool compiler = false; // Visual Studio or Build Tools with the C++ x64 tools
    std::string visualStudio;
    bool dxc = false; // the engine downloads DXC on the first build (cmake/Dxc.cmake)
    std::string dxcPath;
};
Toolchain parseToolchain(const std::string& output);
// Installs Visual Studio Build Tools with the C++ workload (shown to copy when no compiler is found)
inline constexpr const char* kBuildToolsWinget =
    "winget install Microsoft.VisualStudio.2022.BuildTools --override \"--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended\"";

// "0.9.4" → "0.9" (blank if not a version)
std::string minorOf(const std::string& version);
// true if version a < b, comparing the numbers
bool versionLess(const std::string& a, const std::string& b);
// Sets the first "key value" line of mealkit.project (added at the end when missing; a blank value removes it). Other lines,
// comments and line endings are kept
bool setProjectKey(const std::string& projectDirectory, const std::string& key, const std::string& value);
// Points the project's engine line at this folder (the project's build.bat reads it)
inline bool setProjectEngine(const std::string& projectDirectory, const std::string& engineDirectory) {
    return setProjectKey(projectDirectory, "engine", engineDirectory);
}
// Copies a PNG into the project as icon.png and writes the icon line; a blank file goes back to the default (line removed, icon.png deleted)
bool setProjectIcon(const std::string& projectDirectory, const std::string& pngFile, std::string& error);

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
    // When the engine line's folder isn't an engine here (a project made on another PC), an engine of the same MINOR as engineVersion:
    // that exact version, else the newest patch (engine docs/DECISIONS.md "How the Manager installs"). byVersion is set then.
    const Engine* engineFor(const Project& project, bool* byVersion = nullptr) const;
    // Engine in the release list for a project whose engine wasn't found (same MINOR as engineVersion). nullptr if none
    static const Release* releaseFor(const Project& project, const std::vector<Release>& releases);
    // Whether it needs upgrading to this engine (project engineVersion != engine VERSION)
    bool needsUpgrade(const Project& project) const;
    // Whether an engine of this version is already in the list
    bool hasEngineVersion(const std::string& version) const;
    // A tool release is offered when it speaks this Manager's live protocol and works with an engine in the list
    bool toolMatches(const ToolRelease& tool) const;
    // The installed tool of that name (nullptr if none)
    const Tool* installedTool(const std::string& name) const;

    void addRecent(const std::string& directory);    // to the top of the list
    void removeRecent(const std::string& directory); // removed only from the list (the folder stays)
    void setFavorite(const std::string& directory, bool favorite);
    // A project now lives in another folder (moved, or found again): the list points there, at the top, and keeps its favorite mark
    void replaceProject(const std::string& from, const std::string& to);
    void addEngine(const std::string& directory);

private:
    std::vector<Project> m_projects;
    std::vector<Engine> m_engines;
    std::vector<Tool> m_tools;
    std::string m_language = "ko";
};

} // namespace mkmanager
