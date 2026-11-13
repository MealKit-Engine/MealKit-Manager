#pragma once

// Running other programs. Engine scripts (new_project.ps1, upgrade_project.ps1, build.bat) run to completion on a background thread without a window
// and their output is collected (the thread keeps reading so the pipe doesn't fill up and stall). The editor/tools are launched and let go.
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace mkmanager {

class Job {
public:
    ~Job();
    // false if already running
    bool start(const std::string& label, std::vector<std::string> args);
    bool running() const { return m_running.load(); }
    // Returns a finished job only once (the UI shows the result and rereads the list)
    bool takeFinished();

    const std::string& label() const { return m_label; }
    double seconds() const; // since the last start (the status bar shows it while running)
    int exitCode() const { return m_exitCode; }
    const std::string& output() const { return m_output; } // read only after it's finished

private:
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_finished{false};
    std::string m_label;
    std::string m_output;
    int m_exitCode = 0;
    unsigned long long m_startTicks = 0;
};

// Launches a program without waiting (editor, tools). A reason on failure
bool launchDetached(const std::vector<std::string>& args, std::string& error);

// true while a process with this id runs (live info files can outlive their editor)
bool processAlive(unsigned pid);

// Arguments for running PowerShell scripts (Windows PowerShell 5.1, bypassing the execution policy)
std::vector<std::string> powershellArgs(const std::string& script, const std::vector<std::string>& args);

} // namespace mkmanager
