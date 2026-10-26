#pragma once

// 다른 프로그램 실행. 엔진 스크립트(new_project.ps1, upgrade_project.ps1, build.bat)는 창 없이 배경 스레드에서 끝까지 돌리고
// 출력을 모은다(파이프가 차서 멈추지 않게 스레드가 계속 읽는다). 에디터·도구는 띄우고 놓아준다.
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace mkmanager {

class Job {
public:
    ~Job();
    // 이미 돌고 있으면 false
    bool start(const std::string& label, std::vector<std::string> args);
    bool running() const { return m_running.load(); }
    // 끝난 작업을 한 번만 돌려준다 (UI가 결과를 보이고 목록을 다시 읽는다)
    bool takeFinished();

    const std::string& label() const { return m_label; }
    int exitCode() const { return m_exitCode; }
    const std::string& output() const { return m_output; } // 끝난 뒤에만 읽는다

private:
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_finished{false};
    std::string m_label;
    std::string m_output;
    int m_exitCode = 0;
};

// 프로그램을 띄우고 기다리지 않는다 (에디터, 도구). 실패하면 이유
bool launchDetached(const std::vector<std::string>& args, std::string& error);

// PowerShell 스크립트 실행 인자 (Windows PowerShell 5.1, 실행 정책 우회)
std::vector<std::string> powershellArgs(const std::string& script, const std::vector<std::string>& args);

} // namespace mkmanager
