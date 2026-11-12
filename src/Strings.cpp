#include "Strings.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace mkmanager {
namespace {

struct Pair {
    const char* en;
    const char* ko;
};

// When new text is shown on screen, add its Korean here (tests/CatalogTests.cpp checks that all tr text in App.cpp exists)
constexpr Pair kKorean[] = {
    {"Projects", "프로젝트"},
    {"Engines", "엔진"},
    {"Tools", "도구"},
    {"Modules", "모듈"},
    {"Pick a project on the Projects tab.", "[프로젝트] 탭에서 프로젝트를 고르세요."},
    {"This project's engine has no module tool (tools\\module.ps1, engine 0.5 or later).", "이 프로젝트의 엔진에 모듈 도구가 없습니다 (tools\\module.ps1, 엔진 0.5 이상)."},
    {"Module list", "모듈 목록"},
    {"Project: %s", "프로젝트: %s"},
    {"In this project", "이 프로젝트"},
    {"Reading...", "읽는 중..."},
    {"No modules.", "모듈이 없습니다."},
    {"Module", "모듈"},
    {"Version", "버전"},
    {"Changes", "바뀐 것"},
    {"%d .new file(s) to merge", "합칠 .new 파일 %d개"},
    {"%d file(s) changed here", "여기서 고친 파일 %d개"},
    {"Update", "갱신"},
    {"Update %s", "%s 갱신"},
    {"Files you changed are kept; the new version is written next to them as .new", "고친 파일은 그대로 두고, 새 버전을 옆에 .new로 씁니다"},
    {"Remove", "제거"},
    {"Remove %s", "%s 제거"},
    {"Registry (MealKit-Modules)", "레지스트리 (MealKit-Modules)"},
    {"Description", "설명"},
    {"%s (engine %s)", "%s (엔진 %s)"},
    {"by %s", "만든 이 %s"},
    {"Added", "추가됨"},
    {"Add", "추가"},
    {"Add %s", "%s 추가"},
    {"Module code runs inside the editor and the game. The registry checks that it builds, not what it does - read what you add.", "모듈 코드는 에디터와 게임 안에서 실행됩니다. 레지스트리는 빌드되는지만 확인하고 하는 일은 검토하지 않습니다. 추가하기 전에 읽어 보세요."},
    {"Remove modules/%s from the project?", "프로젝트에서 modules/%s를 뺄까요?"},
    {"It is refused while game code includes it. Undo with version control.", "게임 코드가 include하는 동안에는 거절됩니다. 되돌리기는 버전 관리로 하세요."},
    {"+ New Project", "+ 새 프로젝트"},
    {"Import...", "가져오기..."},
    {"Refresh", "새로 고침"},
    {"Search", "검색"},
    {"Project", "프로젝트"},
    {"Engine", "엔진"},
    {"Created", "만든 날"},
    {"Favorite", "즐겨찾기"},
    {"(missing)", "(없음)"},
    {"engine not found", "엔진 없음"},
    {" (engine sample)", " (엔진 예제)"},
    {"Open in Editor", "에디터에서 열기"},
    {"Upgrade Project", "프로젝트 업그레이드"},
    {"Upgrade", "업그레이드"},
    {"Show Folder", "폴더 보기"},
    {"Remove from List", "목록에서 빼기"},
    {"Removed %s from the list (the folder is kept)", "%s을(를) 목록에서 뺐다 (폴더는 그대로)"},
    {"Add Engine Folder...", "엔진 폴더 더하기..."},
    {"Engines used by your projects are found automatically.", "프로젝트가 쓰는 엔진은 저절로 찾는다."},
    {"Version", "버전"},
    {"Folder", "폴더"},
    {"Editor", "에디터"},
    {"built", "빌드됨"},
    {"Build Editor", "에디터 빌드"},
    {"Editor build", "에디터 빌드"},
    {"Install", "설치"},
    {"Toolchain check", "빌드 도구 확인"},
    {"Works with", "맞는 엔진"},
    {"engine %s", "엔진 %s"},
    {"needs another Manager", "다른 Manager 버전 필요"},
    {"no matching engine", "맞는 엔진 없음"},
    {"Update to %s", "%s(으)로 업데이트"},
    {"Install %s %s", "%s %s 설치"},
    {"MealKit Manager %s is available (this is %s).", "MealKit Manager %s이(가) 나왔습니다 (지금 %s)."},
    {"Update and Restart", "업데이트 후 다시 시작"},
    {"Manager update", "Manager 업데이트"},
    {"Updated, but the new Manager didn't start: %s", "업데이트했지만 새 Manager가 시작되지 않았습니다: %s"},
    {"%s not installed", "%s 설치 안 됨"},
    {" (installed here)", " (이 PC에 설치된 것)"},
    {"Install Engine %s", "엔진 %s 설치"},
    {"Cannot update the engine line of %s", "%s의 engine 줄을 바꿀 수 없습니다"},
    {" (engine line now %s)", " (engine 줄을 %s(으)로 바꿈)"},
    {"No C++ compiler on this PC: game code can't be built, so projects can't open in the editor.", "이 PC에 C++ 컴파일러가 없습니다. 게임 코드를 빌드할 수 없어서 프로젝트를 에디터에서 열 수 없습니다."},
    {"Install Visual Studio Build Tools with C++ (about 6 GB), from a terminal:", "터미널에서 C++가 포함된 Visual Studio Build Tools를 설치하세요 (약 6GB):"},
    {"Copy", "복사"},
    {"Check Again", "다시 확인"},
    {"Without a compiler, use designer mode: open a project from the shared folder a programmer builds.", "컴파일러 없이 쓰려면 디자이너 모드로, 프로그래머가 빌드해 둔 공유 폴더의 프로젝트를 여세요."},
    {"C++ compiler: %s", "C++ 컴파일러: %s"},
    {"C++ compiler: not found", "C++ 컴파일러: 없음"},
    {"DXC: ready", "DXC: 준비됨"},
    {"DXC: downloaded with the first build", "DXC: 첫 빌드 때 내려받음"},
    {"No release index yet (MEALKIT_RELEASE_INDEX).", "아직 릴리스 목록이 없습니다 (MEALKIT_RELEASE_INDEX)."},
    {"Engine list", "엔진 목록"},
    {"Could not read the release index.", "릴리스 목록을 읽지 못했습니다."},
    {"Retry", "다시 시도"},
    {"Size", "크기"},
    {"installed", "설치됨"},
    {"Install engine %s", "엔진 %s 설치"},
    {"Installing engine %s...", "엔진 %s 설치 중..."},
    {"Tools are found in %LOCALAPPDATA%\\MealKit\\tools\\<tool>\\ and the editor's toolPath lines.",
     "도구는 %LOCALAPPDATA%\\MealKit\\tools\\<도구>\\와 에디터 설정의 toolPath 줄에서 찾는다."},
    {"Opens with project: %s", "열 프로젝트: %s"},
    {"Tool", "도구"},
    {"(protocol differs)", "(약속 버전 다름)"},
    {"Open", "열기"},
    {"Cannot start %s: %s", "%s을(를) 실행할 수 없다: %s"},
    {"No tools installed.", "설치한 도구가 없다."},
    {"New Project", "새 프로젝트"},
    {"Name", "이름"},
    {"Start with a letter; letters, digits and _ only (it becomes the exe name).",
     "영문자로 시작하고 영문자·숫자·_만 쓴다 (exe 이름이 된다)."},
    {"Location", "위치"},
    {"Template", "템플릿"},
    {"Arena versus (sample game)", "아레나 대전 (예제 게임)"},
    {"Empty", "빈 프로젝트"},
    {"A copy of the sample game: physics party versus with AI, boxes, jump pads and cutscenes.", "예제 게임을 복사합니다: AI, 상자, 점프대, 컷신이 있는 물리 파티 대전."},
    {"One character per player on a floor, with the title, lobby and results screens.", "바닥 위에 플레이어마다 캐릭터 하나와 타이틀·로비·결과 화면만 있습니다."},
    {"No engine found. Add one on the Engines tab.", "엔진이 없다. [엔진] 탭에서 더한다."},
    {"Create", "만들기"},
    {"New project", "새 프로젝트"},
    {"Cancel", "취소"},
    {"%s: done", "%s: 끝"},
    {"%s: failed (exit code %d)", "%s: 실패 (종료 코드 %d)"},
    {"Output", "출력"},
    {"The engine of %s was not found: %s", "%s의 엔진을 찾지 못했다: %s"},
    {"The editor of engine %s is not built yet - build it on the Engines tab.", "엔진 %s의 에디터가 아직 빌드되지 않았다 - [엔진] 탭에서 빌드한다."},
    {"Cannot start the editor: %s", "에디터를 실행할 수 없다: %s"},
    {"Opened %s", "%s을(를) 열었다"},
    {"%s has no mealkit.project", "%s에 mealkit.project가 없다"},
    {"Imported %s", "%s을(를) 가져왔다"},
    {"%s is not a MealKit engine folder (VERSION, tools\\new_project.ps1)", "%s은(는) MealKit 엔진 폴더가 아니다 (VERSION, tools\\new_project.ps1)"},
    {"Added engine %s", "엔진 %s을(를) 더했다"},
    {"Removed %d missing project(s) from the list", "없는 프로젝트 %d개를 목록에서 뺐다"},
    {"Copy Path", "경로 복사"},
    {"No projects yet", "아직 프로젝트가 없다"},
    {"Make a new game, or import a folder that has mealkit.project.", "새 게임을 만들거나, mealkit.project가 있는 폴더를 가져온다."},
    {"Remove Missing (%d)", "없는 프로젝트 빼기 (%d)"},
    {"Takes projects whose folder or mealkit.project is gone off the list (nothing is deleted).", "폴더나 mealkit.project가 없어진 프로젝트를 목록에서 뺀다 (아무것도 지우지 않는다)."},
    {"Search (Ctrl+F)", "검색 (Ctrl+F)"},
    {"Right-click a project for more", "프로젝트를 우클릭하면 더 있다"},
    {"No projects match the search.", "맞는 프로젝트가 없다."},
    {"no mealkit.project", "mealkit.project 없음"},
    {"folder missing", "폴더 없음"},
    {"Made with an older engine: [Upgrade Project] brings it to this one.", "예전 엔진으로 만든 프로젝트: [프로젝트 업그레이드]로 이 엔진에 맞춘다."},
    {"Used by", "쓰는 곳"},
    {"%d project(s)", "프로젝트 %d개"},
    {"Install one from the list below, or unzip a tool into the folder above.", "아래 목록에서 설치하거나, 위 폴더에 도구 압축을 푼다."},
    {"Unzip a tool into the folder above, then press Refresh on the Projects tab.", "위 폴더에 도구 압축을 풀고 [프로젝트] 탭에서 [새로 고침]을 누른다."},
};

bool g_korean = true;

} // namespace

void setLanguage(const std::string& language) { g_korean = language != "en"; }

const char* tr(const char* english) {
    if (!g_korean) return english;
    for (const Pair& p : kKorean)
        if (std::strcmp(p.en, english) == 0) return p.ko;
    return english;
}

std::string trf(const char* english, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, english);
    std::vsnprintf(buffer, sizeof(buffer), tr(english), args);
    va_end(args);
    return buffer;
}

} // namespace mkmanager
