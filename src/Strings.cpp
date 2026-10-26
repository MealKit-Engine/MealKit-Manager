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

// 새 글자를 화면에 쓰면 여기에 한국어를 더한다 (tests/CatalogTests.cpp가 App.cpp의 tr 글자가 모두 있는지 본다)
constexpr Pair kKorean[] = {
    {"Projects", "프로젝트"},
    {"Engines", "엔진"},
    {"Tools", "도구"},
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
