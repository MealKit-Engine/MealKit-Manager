#pragma once

// MealKit Manager가 다루는 것: 프로젝트, 설치한 엔진, 도구. 창·ImGui를 모르는 데이터 층 (tests/CatalogTests.cpp가 시험한다).
// 엔진과는 파일로만 이어진다 (엔진 docs/CODING_MODEL.md "도구 사이 약속"):
//   사용자 폴더 %LOCALAPPDATA%\MealKit\ (시험은 환경 변수 MEALKIT_USER_DIR로 바꾼다)
//     recent_projects.txt, favorite_projects.txt   에디터와 같이 쓰는 목록 (한 줄에 폴더 하나)
//     engines.txt                                  Manager가 더한 엔진 폴더 (한 줄에 하나)
//     editor_settings.txt                          에디터 설정: language, toolPath 줄을 읽는다
//     tools\<도구>\mealkit-tool.txt               설치한 도구의 매니페스트 (@format tool 1)
//   프로젝트 폴더의 mealkit.project               name, engine(엔진 폴더), engineVersion, created 줄
//   엔진 폴더                                     VERSION, build\Editor\bin\MealKitEditor.exe, tools\new_project.ps1
#include <string>
#include <vector>

namespace mkmanager {

struct Project {
    std::string directory;
    std::string name;
    std::string engine;        // mealkit.project의 engine 줄 (없으면 빈 칸: 엔진 저장소 자신)
    std::string engineVersion; // 프로젝트를 만들거나 올린 엔진 버전
    std::string created;
    bool valid = false;        // 폴더에 mealkit.project가 있고 name이 있다
    bool favorite = false;
};

struct Engine {
    std::string directory;
    std::string version;   // VERSION 파일
    std::string editorExe; // 빌드한 에디터 (없으면 빈 칸)
    bool valid = false;    // VERSION과 tools\new_project.ps1이 있다
};

struct Tool {
    std::string folder; // 매니페스트가 있는 폴더
    std::string name;
    std::string exe;    // 절대 경로
    std::string version;
    int protocol = 0;   // 라이브 연결 약속 버전 (엔진 live::kProtocol)
    std::string opens;  // 이 도구로 여는 파일 확장자 (공백으로 나눔)
};

// 소스 모듈 (엔진 docs/MODULES.md). 엔진의 tools\module.ps1 search -Tsv -Project <폴더> 출력에서 읽는다
// (엔진 에디터 editor/ModulesPanel.cpp의 parseModuleList와 같은 약속: 줄 모양을 바꾸면 세 곳을 같이 고친다)
struct InstalledModule {
    std::string name, version, source;
    int modified = 0; // 설치한 뒤 게임이 고친 파일 수
    int pending = 0;  // 갱신이 남긴 .new 파일 수
};

struct RegistryModule {
    std::string name;
    std::string version;      // 프로젝트 엔진 MINOR용 가장 새 버전 (없으면 빈 칸)
    std::string latest;       // 가장 새 버전
    std::string latestEngine; // 그 버전의 엔진 MAJOR.MINOR
    std::string title, summary, tags, author, repo;
};

struct ModuleList {
    std::vector<InstalledModule> installed;
    std::vector<RegistryModule> registry;
    std::string error; // 레지스트리 목록을 받지 못한 이유
};

ModuleList parseModuleList(const std::string& output);

constexpr int kLiveProtocol = 1; // 이 Manager가 아는 라이브 연결 약속 버전

std::string userDirectory();
// 한 줄에 하나인 목록 파일 (BOM·빈 줄·앞뒤 공백 무시)
std::vector<std::string> readList(const std::string& path);
void writeList(const std::string& path, const std::vector<std::string>& list);
// 폴더 표기 맞추기 (절대 경로, 끝 구분자 없음, Windows는 \) / 같은 폴더인가 (Windows는 대소문자 무시)
std::string normalizeDirectory(const std::string& directory);
bool sameDirectory(const std::string& a, const std::string& b);

bool readProject(const std::string& directory, Project& out);
bool readEngine(const std::string& directory, Engine& out);
bool readToolManifest(const std::string& folder, Tool& out);
// "key value" 줄 파일에서 키 하나 (첫 번째 값). 주석(#)과 @format 줄은 건너뛴다
std::string readKey(const std::string& path, const std::string& key);

class Catalog {
public:
    // extraEngine: 명령줄 --engine (그 에디터가 Manager를 열었다)
    void load(const std::string& extraEngine = {});

    const std::vector<Project>& projects() const { return m_projects; }
    const std::vector<Engine>& engines() const { return m_engines; }
    const std::vector<Tool>& tools() const { return m_tools; }
    const std::string& language() const { return m_language; }

    // 프로젝트를 여는 엔진: engine 줄의 폴더, 없으면 프로젝트 폴더 자신(엔진 저장소). 찾지 못하면 nullptr
    const Engine* engineFor(const Project& project) const;
    // 이 엔진으로 올려야 하나 (프로젝트 engineVersion != 엔진 VERSION)
    bool needsUpgrade(const Project& project) const;

    void addRecent(const std::string& directory);    // 목록 맨 위로
    void removeRecent(const std::string& directory); // 목록에서만 뺀다 (폴더는 그대로)
    void setFavorite(const std::string& directory, bool favorite);
    void addEngine(const std::string& directory);

private:
    std::vector<Project> m_projects;
    std::vector<Engine> m_engines;
    std::vector<Tool> m_tools;
    std::string m_language = "ko";
};

} // namespace mkmanager
