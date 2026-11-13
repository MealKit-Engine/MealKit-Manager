#include "Job.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#endif

namespace mkmanager {
namespace {

std::vector<const char*> argv(const std::vector<std::string>& args) {
    std::vector<const char*> out;
    for (const std::string& a : args) out.push_back(a.c_str());
    out.push_back(nullptr);
    return out;
}

} // namespace

double Job::seconds() const { return static_cast<double>(SDL_GetTicks() - m_startTicks) / 1000.0; }

Job::~Job() {
    if (m_thread.joinable()) m_thread.join();
}

bool Job::start(const std::string& label, std::vector<std::string> args) {
    if (m_running.load()) return false;
    if (m_thread.joinable()) m_thread.join();
    m_label = label;
    m_startTicks = SDL_GetTicks();
    m_output.clear();
    m_exitCode = 0;
    m_finished = false;
    m_running = true;
    m_thread = std::thread([this, args = std::move(args)] {
        const std::vector<const char*> list = argv(args);
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(list.data()));
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true); // without a console window
        SDL_Process* process = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        if (!process) {
            m_output = std::string("Cannot start ") + args[0] + ": " + SDL_GetError();
            m_exitCode = -1;
        } else {
            size_t size = 0;
            int code = 0;
            // Keeps reading output until it ends
            if (char* text = static_cast<char*>(SDL_ReadProcess(process, &size, &code))) {
                m_output.assign(text, size);
                SDL_free(text);
            }
            m_exitCode = code;
            SDL_DestroyProcess(process);
        }
        m_finished = true;
        m_running = false;
    });
    return true;
}

bool Job::takeFinished() {
    if (!m_finished.load() || m_running.load()) return false;
    m_finished = false;
    if (m_thread.joinable()) m_thread.join();
    return true;
}

bool launchDetached(const std::vector<std::string>& args, std::string& error) {
    const std::vector<const char*> list = argv(args);
    SDL_Process* process = SDL_CreateProcess(list.data(), false);
    if (!process) {
        error = SDL_GetError();
        return false;
    }
    SDL_DestroyProcess(process); // the process keeps running
    return true;
}

bool processAlive(unsigned pid) {
    if (pid == 0) return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

std::vector<std::string> powershellArgs(const std::string& script, const std::vector<std::string>& args) {
    std::vector<std::string> out = {"powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script};
    out.insert(out.end(), args.begin(), args.end());
    return out;
}

} // namespace mkmanager
