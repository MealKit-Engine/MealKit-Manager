// MealKit Manager 시험 (창 없이): 가짜 사용자 폴더·엔진·프로젝트·도구를 만들고 Catalog가 읽는 것을 본다.
//   mkmanager_tests   끝에 PASS 또는 FAILED
#include "Catalog.h"
#include "Strings.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using namespace mkmanager;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("  FAIL %s (%s:%d)\n", #cond, __FILE__, __LINE__);                                             \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string str(const fs::path& p) { return p.string(); }

void setUserDirectory(const fs::path& dir) {
#if defined(_WIN32)
    _putenv_s("MEALKIT_USER_DIR", str(dir).c_str());
#else
    setenv("MEALKIT_USER_DIR", str(dir).c_str(), 1);
#endif
}

void testCatalog(const fs::path& root) {
    const fs::path user = root / "user", engine = root / "engine", game = root / "games" / "Game", old = root / "games" / "Old";
    // 엔진: VERSION + new_project.ps1 + 빌드한 에디터. 엔진 저장소 자신도 프로젝트다 (engine 줄 없음)
    write(engine / "VERSION", "0.5.2\n");
    write(engine / "tools" / "new_project.ps1", "#\n");
    write(engine / "build" / "Editor" / "bin" / "MealKitEditor.exe", "");
    write(engine / "mealkit.project", "# comment\n@format project 1\nname sandbox\n");
    write(game / "mealkit.project", "\xEF\xBB\xBF# MealKit project\n@format project 1\nname Game\nengine " + str(engine) +
                                        "\nengineVersion 0.5.2\ncreated 2026-10-01\n");
    write(old / "mealkit.project", "@format project 1\nname Old\nengine " + str(engine) + "\nengineVersion 0.4.1\n");
    write(user / "recent_projects.txt", str(engine) + "\n" + str(game) + "\n" + str(old) + "\n" + str(root / "gone") + "\n");
    write(user / "favorite_projects.txt", str(old) + "\n");
    write(user / "editor_settings.txt", "language en\ntoolPath " + str(root / "devtool") + "\n");
    write(user / "tools" / "audio" / "mealkit-tool.txt", "# MealKit tool manifest\n@format tool 1\nname MealKit Audio\nexe MealKitAudio.exe\nversion 0.2.0\nprotocol 1\nopens .events .wav\n");
    write(root / "devtool" / "mealkit-tool.txt", "@format tool 1\nname Future Tool\nexe future.exe\nprotocol 9\n");
    write(user / "tools" / "broken" / "mealkit-tool.txt", "@format tool 1\nversion 1\n"); // 이름·exe 없음 → 빠진다
    setUserDirectory(user);

    Catalog catalog;
    catalog.load();
    CHECK(catalog.language() == "en");
    CHECK(catalog.projects().size() == 4);
    if (catalog.projects().size() == 4) {
        const Project& first = catalog.projects()[0]; // 즐겨찾기가 먼저
        CHECK(first.name == "Old" && first.favorite);
        CHECK(catalog.needsUpgrade(first));
        const Project& sample = catalog.projects()[1];
        CHECK(sample.name == "sandbox" && sample.engine.empty());
        CHECK(catalog.engineFor(sample) && !catalog.needsUpgrade(sample));
        const Project& g = catalog.projects()[2];
        CHECK(g.name == "Game" && g.engineVersion == "0.5.2" && g.created == "2026-10-01"); // BOM이 있어도 읽는다
        CHECK(catalog.engineFor(g) && !catalog.needsUpgrade(g));
        CHECK(!catalog.projects()[3].valid);
    }
    CHECK(catalog.engines().size() == 1); // 같은 엔진을 여러 프로젝트가 가리켜도 하나
    if (!catalog.engines().empty()) {
        CHECK(catalog.engines()[0].version == "0.5.2");
        CHECK(!catalog.engines()[0].editorExe.empty());
    }
    CHECK(catalog.tools().size() == 2);
    if (catalog.tools().size() == 2) {
        CHECK(catalog.tools()[0].name == "Future Tool" && catalog.tools()[0].protocol == 9);
        CHECK(catalog.tools()[1].name == "MealKit Audio" && catalog.tools()[1].opens == ".events .wav");
        CHECK(fs::path(catalog.tools()[1].exe).filename() == "MealKitAudio.exe");
    }

    // 목록 고치기: 에디터와 같은 파일을 쓴다
    catalog.addRecent(str(game));
    catalog.removeRecent(str(old));
    catalog.setFavorite(str(game), true);
    catalog.load();
    CHECK(catalog.projects().size() == 3);
    if (!catalog.projects().empty()) CHECK(catalog.projects()[0].name == "Game" && catalog.projects()[0].favorite);
    CHECK(readList(str(user / "favorite_projects.txt")).size() == 1); // Old는 즐겨찾기에서도 빠졌다

    // 다른 엔진 더하기 (engines.txt), 엔진이 아닌 폴더는 목록에 나오지 않는다
    const fs::path engine2 = root / "engine2";
    write(engine2 / "VERSION", "0.6.0\n");
    write(engine2 / "tools" / "new_project.ps1", "#\n");
    catalog.addEngine(str(engine2));
    catalog.addEngine(str(root / "games"));
    catalog.addEngine(str(engine2)); // 두 번 더해도 한 줄
    catalog.load();
    CHECK(catalog.engines().size() == 2);
    CHECK(readList(str(user / "engines.txt")).size() == 2);
    if (catalog.engines().size() == 2) { // engines.txt가 프로젝트가 가리키는 엔진보다 먼저
        CHECK(catalog.engines()[0].version == "0.6.0" && catalog.engines()[0].editorExe.empty());
        CHECK(catalog.engines()[1].version == "0.5.2");
    }
}

void testKeys(const fs::path& root) {
    write(root / "keys.txt", "# head\n@format x 1\nname = Spaced Name\nnameless\nscene scenes/a.scene\n");
    CHECK(readKey(str(root / "keys.txt"), "name") == "Spaced Name");
    CHECK(readKey(str(root / "keys.txt"), "scene") == "scenes/a.scene");
    CHECK(readKey(str(root / "keys.txt"), "format").empty());
    CHECK(sameDirectory(str(root / "a" / ".." / "keys.txt"), str(root / "keys.txt")));
}

// App.cpp의 tr("...")/trf("...") 글자가 모두 한국어 표에 있는가
void testTranslations() {
    std::ifstream in(fs::path(MKMANAGER_SOURCE_DIR) / "src" / "App.cpp", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(!text.empty());
    setLanguage("ko");
    int count = 0;
    for (size_t pos = 0; (pos = text.find("tr", pos)) != std::string::npos; pos += 2) {
        size_t open = pos + 2;
        if (open < text.size() && text[open] == 'f') ++open;
        if (text.compare(open, 2, "(\"") != 0 || (pos > 0 && (std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_')))
            continue;
        std::string s;
        for (size_t i = open + 2; i < text.size() && text[i] != '"'; ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;
            s += text[i];
        }
        ++count;
        if (std::string(tr(s.c_str())) == s) {
            std::printf("  FAIL no Korean for \"%s\"\n", s.c_str());
            ++g_failures;
        }
    }
    CHECK(count > 20);
}

// 엔진 tools\module.ps1 search -Tsv의 줄 (모르는 줄은 건너뛴다)
void testModuleList() {
    const ModuleList list = parseModuleList(
        "installed\tcooldown\t0.1.0\t2\t1\tregistry cooldown 0.1.0 abc https://x/module-cooldown v0.1.0\r\n"
        "some other line\n"
        "module\tcooldown\t0.2.0\t0.3.0\t0.6\tCooldowns\tTick timers\tgameplay ui\tKingProtea99\thttps://x/module-cooldown\n"
        "module\thealth\t-\t1.0.0\t0.9\tHealth\tHit points\tgameplay\tSomeone\thttps://x/module-health\n"
        "error\toffline\n");
    CHECK(list.installed.size() == 1);
    CHECK(list.installed[0].name == "cooldown" && list.installed[0].modified == 2 && list.installed[0].pending == 1);
    CHECK(list.installed[0].source.rfind("registry ", 0) == 0);
    CHECK(list.registry.size() == 2);
    CHECK(list.registry[0].version == "0.2.0" && list.registry[0].latest == "0.3.0" && list.registry[0].tags == "gameplay ui");
    CHECK(list.registry[1].version.empty() && list.registry[1].latestEngine == "0.9" && list.registry[1].repo == "https://x/module-health");
    CHECK(list.error == "offline");
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "mkmanager_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    testCatalog(root);
    testKeys(root);
    testTranslations();
    testModuleList();
    fs::remove_all(root, ec);
    std::printf(g_failures ? "FAILED (%d)\n" : "PASS\n", g_failures);
    return g_failures ? 1 : 0;
}
