// MealKit Manager tests (without a window): creates a fake user folder/engine/projects/tools and checks what Catalog reads.
//   mkmanager_tests   PASS or FAILED at the end
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
    // Engine: VERSION + new_project.ps1 + a built editor. Its sample inside the repo is a project too (no engine line)
    write(engine / "VERSION", "0.5.2\n");
    write(engine / "tools" / "new_project.ps1", "#\n");
    write(engine / "build" / "Editor" / "bin" / "MealKitEditor.exe", "");
    write(engine / "samples" / "sandbox" / "mealkit.project", "# comment\n@format project 1\nname sandbox\n");
    write(game / "mealkit.project", "\xEF\xBB\xBF# MealKit project\n@format project 1\nname Game\nengine " + str(engine) +
                                        "\nengineVersion 0.5.2\ncreated 2026-10-01\n");
    write(old / "mealkit.project", "@format project 1\nname Old\nengine " + str(engine) + "\nengineVersion 0.4.1\n");
    write(user / "recent_projects.txt", str(engine / "samples" / "sandbox") + "\n" + str(game) + "\n" + str(old) + "\n" + str(root / "gone") + "\n");
    write(user / "favorite_projects.txt", str(old) + "\n");
    write(user / "editor_settings.txt", "language en\ntoolPath " + str(root / "devtool") + "\n");
    write(user / "tools" / "audio" / "mealkit-tool.txt", "# MealKit tool manifest\n@format tool 1\nname MealKit Audio\nexe MealKitAudio.exe\nversion 0.2.0\nprotocol 1\nopens .events .wav\n");
    write(root / "devtool" / "mealkit-tool.txt", "@format tool 1\nname Future Tool\nexe future.exe\nprotocol 9\n");
    write(user / "tools" / "broken" / "mealkit-tool.txt", "@format tool 1\nversion 1\n"); // no name/exe → dropped
    setUserDirectory(user);

    Catalog catalog;
    catalog.load();
    CHECK(catalog.language() == "en");
    CHECK(catalog.projects().size() == 4);
    if (catalog.projects().size() == 4) {
        const Project& first = catalog.projects()[0]; // favorites first
        CHECK(first.name == "Old" && first.favorite);
        CHECK(catalog.needsUpgrade(first));
        const Project& sample = catalog.projects()[1];
        CHECK(sample.name == "sandbox" && sample.engine.empty());
        CHECK(catalog.engineFor(sample) && !catalog.needsUpgrade(sample));
        const Project& g = catalog.projects()[2];
        CHECK(g.name == "Game" && g.engineVersion == "0.5.2" && g.created == "2026-10-01"); // read even with a BOM
        CHECK(catalog.engineFor(g) && !catalog.needsUpgrade(g));
        CHECK(!catalog.projects()[3].valid);
    }
    CHECK(catalog.engines().size() == 1); // one engine even when several projects point to it
    if (!catalog.engines().empty()) {
        CHECK(catalog.engines()[0].version == "0.5.2");
        CHECK(!catalog.engines()[0].editorExe.empty());
    }
    CHECK(catalog.tools().size() == 2);
    if (catalog.tools().size() == 2) {
        CHECK(catalog.tools()[0].name == "Future Tool" && catalog.tools()[0].protocol == 9);
        CHECK(catalog.tools()[1].name == "MealKit Audio" && catalog.tools()[1].opens == (std::vector<std::string>{".events", ".wav"}));
        CHECK(fs::path(catalog.tools()[1].exe).filename() == "MealKitAudio.exe");
    }

    // Editing the list: uses the same files as the editor
    catalog.addRecent(str(game));
    catalog.removeRecent(str(old));
    catalog.setFavorite(str(game), true);
    catalog.load();
    CHECK(catalog.projects().size() == 3);
    if (!catalog.projects().empty()) CHECK(catalog.projects()[0].name == "Game" && catalog.projects()[0].favorite);
    CHECK(readList(str(user / "favorite_projects.txt")).size() == 1); // Old was removed from favorites too

    // Adding another engine (engines.txt); folders that aren't engines don't appear in the list
    const fs::path engine2 = root / "engine2";
    write(engine2 / "VERSION", "0.6.0\n");
    write(engine2 / "tools" / "new_project.ps1", "#\n");
    catalog.addEngine(str(engine2));
    catalog.addEngine(str(root / "games"));
    catalog.addEngine(str(engine2)); // adding twice gives one line
    catalog.load();
    CHECK(catalog.engines().size() == 2);
    CHECK(readList(str(user / "engines.txt")).size() == 2);
    if (catalog.engines().size() == 2) { // engines.txt comes before engines that projects point to
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

// Whether all tr("...")/trf("...") text in App.cpp is in the Korean table
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

// Lines of the engine's tools\module.ps1 search -Tsv (unknown lines are skipped)
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

// A project from another PC (its engine line points nowhere here) gets an installed engine of the same MINOR (engine ROADMAP R2)
void testEngineMatching(const fs::path& root) {
    const fs::path user = root / "user2", e92 = root / "engines" / "0.9.2", e95 = root / "engines" / "0.9.5";
    for (const auto& [dir, v] : {std::pair{e92, "0.9.2"}, std::pair{e95, "0.9.5"}}) {
        write(dir / "VERSION", std::string(v) + "\n");
        write(dir / "tools" / "new_project.ps1", "#\n");
    }
    auto project = [&](const char* name, const char* version) {
        const fs::path dir = root / "shared" / name;
        write(dir / "mealkit.project", std::string("@format project 1\r\nname ") + name + "\r\nengine D:\\Other\\PC\\MealKit\r\nengineVersion " +
                                           version + "\r\n");
        return dir;
    };
    const fs::path exact = project("Exact", "0.9.2"), newer = project("Newer", "0.9.1"), none = project("None", "0.8.3");
    write(user / "recent_projects.txt", str(exact) + "\n" + str(newer) + "\n" + str(none) + "\n");
    write(user / "engines.txt", str(e92) + "\n" + str(e95) + "\n");
    setUserDirectory(user);
    Catalog catalog;
    catalog.load();
    CHECK(catalog.engines().size() == 2 && catalog.projects().size() == 3);
    if (catalog.projects().size() != 3) return;
    bool byVersion = false;
    const Engine* a = catalog.engineFor(catalog.projects()[0], &byVersion);
    CHECK(a && a->version == "0.9.2" && byVersion); // exact version wins over the newer patch
    const Engine* b = catalog.engineFor(catalog.projects()[1], &byVersion);
    CHECK(b && b->version == "0.9.5" && byVersion); // else the newest patch
    CHECK(!catalog.engineFor(catalog.projects()[2], &byVersion) && !byVersion);
    const std::vector<Release> releases = {{"0.9.6", "u", "s", 1}, {"0.8.4", "u", "s", 1}};
    const Release* r = Catalog::releaseFor(catalog.projects()[2], releases);
    CHECK(r && r->version == "0.8.4");

    CHECK(minorOf("0.9.12") == "0.9" && minorOf("x").empty() && versionLess("0.9.9", "0.9.10") && !versionLess("0.9.10", "0.9.9"));
    CHECK(setProjectEngine(str(none), str(e95)));
    Project p;
    readProject(str(none), p);
    CHECK(p.engine == str(e95) && p.engineVersion == "0.8.3" && p.name == "None");
    std::string text;
    mkcontract::readFile(str(none / "mealkit.project"), text);
    CHECK(text.find("\r\nengineVersion") != std::string::npos); // line endings kept
}

// install.ps1 list / engine output (engine ROADMAP R2)
void testReleases() {
    const std::vector<Release> list = parseReleaseList(
        "[MealKit] noise\r\nengine\t0.9.4\t39366595\thttps://x/mealkit-engine-0.9.4-windows-x64.zip\tabc\r\n"
        "engine\t0.8.2\t100\tfile:///C:/e.zip\tdef\nengine\t\t1\turl\tsha\ntool\tMkAudio\t1\tu\ts\n");
    CHECK(list.size() == 2);
    CHECK(list[0].version == "0.9.4" && list[0].size == 39366595ull && list[0].sha256 == "abc");
    CHECK(list[0].url == "https://x/mealkit-engine-0.9.4-windows-x64.zip");
    CHECK(list[1].version == "0.8.2" && list[1].url == "file:///C:/e.zip");
    CHECK(installedFolder("[MealKit] Downloading\r\ninstalled\tC:\\Users\\a\\engines\\0.9.4\r\n") == "C:\\Users\\a\\engines\\0.9.4");
    CHECK(installedFolder("[MealKit] failed\n").empty());

    const Toolchain have = parseToolchain("compiler\tok\tC:\\VS\r\ndxc\tok\tC:\\dxc.exe\r\n");
    CHECK(have.checked && have.compiler && have.visualStudio == "C:\\VS" && have.dxc && have.dxcPath == "C:\\dxc.exe");
    const Toolchain none = parseToolchain("compiler\tmissing\t\ndxc\tmissing\t\n");
    CHECK(none.checked && !none.compiler && none.visualStudio.empty() && !none.dxc);
    CHECK(!parseToolchain("[MealKit] error\n").checked);
}

int main() {
    const fs::path root = fs::temp_directory_path() / "mkmanager_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    testCatalog(root);
    testKeys(root);
    testTranslations();
    testModuleList();
    testReleases();
    testEngineMatching(root);
    fs::remove_all(root, ec);
    std::printf(g_failures ? "FAILED (%d)\n" : "PASS\n", g_failures);
    return g_failures ? 1 : 0;
}
