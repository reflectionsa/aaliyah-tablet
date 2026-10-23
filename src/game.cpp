// Where the mod runs: the game build, its command line, and nEVR's runtime exports.
#include <windows.h>
#include <shellapi.h>

#include <cstring>

#include "mod.h"

namespace mod {

uint8_t* GameBase() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

bool IsLiveBuild(uint32_t* stamp) {
    *stamp = 0;
    const uint8_t* base = GameBase();
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    *stamp = nt->FileHeader.TimeDateStamp;
    return *stamp == kLiveBuildStamp;
}

bool HasFlag(const wchar_t* flag) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc && !found; i++) found = _wcsicmp(argv[i], flag) == 0;
    LocalFree(argv);
    return found;
}

bool BytesAt(uintptr_t rva, const uint8_t* expected, size_t size) {
    const uint8_t* at = GameBase() + rva;
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(at, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    if (at + size > static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize) return false;
    return memcmp(at, expected, size) == 0;
}

void* RuntimeExport(const char* name) {
    // nevr.dll first: nEVR is moving its runtime there, with BugSplat64.dll forwarding.
    static const wchar_t* const kModules[] = {L"nevr.dll", L"BugSplat64.dll", L"dbgcore.dll"};
    for (const wchar_t* module : kModules) {
        HMODULE h = GetModuleHandleW(module);
        if (!h) continue;
        if (FARPROC f = GetProcAddress(h, name)) return reinterpret_cast<void*>(f);
    }
    return nullptr;
}

}  // namespace mod
