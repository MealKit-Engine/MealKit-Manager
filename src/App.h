#pragma once

// MealKit Manager 화면: [프로젝트] [모듈] [엔진] [도구] 탭. 창·글꼴·루프는 main.cpp.
#include "Catalog.h"
#include "Job.h"

#include <mutex>
#include <string>

struct SDL_Window;

namespace mkmanager {

class App {
public:
    SDL_Window* window = nullptr;

    void init(const std::string& extraEngine);
    void frame();
    const Catalog& catalog() const { return m_catalog; }
    // [모듈] 탭으로 시작한다 (--tab modules, --select <프로젝트 폴더>: 화면 확인)
    void showModulesTab(const std::string& project) {
        m_openModulesTab = true;
        if (!project.empty()) m_selected = normalizeDirectory(project);
    }

private:
    void reload();
    void finishJob();
    void drawWindow();
    void projectsTab();
    void enginesTab();
    void toolsTab();
    void modulesTab();
    void runModuleTool(const Engine& engine, const Project& project, const std::string& label, std::vector<std::string> args, bool listing);
    void newProjectPopup();
    void statusBar();
    void openProject(const Project& project);
    void runScript(const std::string& label, const std::string& script, const std::vector<std::string>& args,
                   const std::string& addOnSuccess = {});
    void pickFolder(int purpose); // 0 = 프로젝트 가져오기, 1 = 엔진 더하기, 2 = 새 프로젝트 위치
    void takePickedFolder();

    Catalog m_catalog;
    std::string m_extraEngine;
    Job m_job;
    std::string m_jobAdd; // 작업이 성공하면 최근 목록에 더할 프로젝트 폴더
    std::string m_status;
    bool m_statusError = false;
    std::string m_jobOutput; // 실패한 작업의 출력 (상태 줄에서 펼쳐 본다)

    std::string m_selected;

    // [모듈] 탭: 고른 프로젝트의 모듈과 레지스트리 (엔진 tools\module.ps1 search -Tsv)
    ModuleList m_modules;
    std::string m_modulesFor;      // 목록을 받은 프로젝트 폴더 (다르면 다시 받는다)
    bool m_jobListsModules = false; // 지금 작업이 모듈 목록 받기다 (끝나면 상태 줄 대신 목록)
    bool m_jobChangesModules = false;
    char m_moduleSearch[96] = {};
    std::string m_confirmRemove;
    bool m_openModulesTab = false; // 다음 프레임에 [모듈] 탭을 고른다
    char m_search[128] = {};

    // 새 프로젝트
    bool m_openNew = false;
    char m_newName[64] = "MyGame";
    char m_newLocation[512] = {};
    int m_newEngine = 0;

    // 폴더 고르기 대화 상자 (다른 스레드에서 돌아온다)
    std::mutex m_pickMutex;
    std::string m_picked;
    int m_pickPurpose = -1;
};

} // namespace mkmanager
