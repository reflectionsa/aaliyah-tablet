// The mod's log: bin/win10/plugin_logs/<stem>/<stem>.log beside echovr.exe, where the other
// plugins log and where the launcher's Logs button and log upload look. Where nEVR offers its own
// log (API v6), each line goes there too.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

#include "mod.h"

namespace mod {

namespace {

std::mutex g_lock;
FILE* g_file = nullptr;
const char* g_pluginId = nullptr;  // process-lifetime, from the host
NevrLogFn g_hostLog = nullptr;

void Write(uint32_t level, const char* format, va_list args) {
    char line[2048];
    vsnprintf(line, sizeof(line), format, args);

    std::lock_guard<std::mutex> lock(g_lock);
    if (g_file) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        static const char* const kLevels[] = {"debug", "info", "warning", "error"};
        fprintf(g_file, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s %s\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                t.wMinute, t.wSecond, t.wMilliseconds, kLevels[level & 3], line);
        fflush(g_file);
    }
    if (g_hostLog && g_pluginId) g_hostLog(g_pluginId, level, line, nullptr, 0);
}

}  // namespace

void OpenLog(const NvrGameContext* ctx) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (NEVR_CTX_HAS(ctx, log) && ctx->log && ctx->plugin_id) {
        g_pluginId = ctx->plugin_id;
        g_hostLog = ctx->log;
    }
    if (g_file) return;

    wchar_t dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) return;
    *slash = 0;
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%ls\\plugin_logs", dir);
    CreateDirectoryW(path, nullptr);
    _snwprintf(path, MAX_PATH, L"%ls\\plugin_logs\\%hs", dir, MOD_STEM);
    CreateDirectoryW(path, nullptr);
    _snwprintf(path, MAX_PATH, L"%ls\\plugin_logs\\%hs\\%hs.log", dir, MOD_STEM, MOD_STEM);
    g_file = _wfopen(path, L"w");
}

void CloseLog() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_file) fclose(g_file);
    g_file = nullptr;
    // The host's log stays usable: plugin_id lives as long as the process.
}

void Log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    Write(NEVR_LOG_INFO, format, args);
    va_end(args);
}

void Warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    Write(NEVR_LOG_WARNING, format, args);
    va_end(args);
}

void Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    Write(NEVR_LOG_ERROR, format, args);
    va_end(args);
}

}  // namespace mod
