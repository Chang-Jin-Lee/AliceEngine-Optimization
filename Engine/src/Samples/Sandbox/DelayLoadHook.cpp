#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <delayimp.h>

#include <filesystem>
#include <string>

static std::wstring GetDllDir()
{
    static std::wstring cached;
    if (!cached.empty())
    {
        return cached;
    }

    wchar_t exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        return L"";
    }

    const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    cached = (exeDir / L"dll").wstring();
    return cached;
}

static std::wstring NarrowToWide(const char* s)
{
    if (!s)
    {
        return L"";
    }

    std::wstring out;
    for (const char* p = s; *p != '\0'; ++p)
    {
        out.push_back(static_cast<wchar_t>(*p));
    }
    return out;
}

static FARPROC WINAPI DelayLoadFailureHook(unsigned dliNotify, PDelayLoadInfo info)
{
    if (dliNotify != dliFailLoadLib || info == nullptr || info->szDll == nullptr)
    {
        return nullptr;
    }

    const std::wstring dllDir = GetDllDir();
    if (dllDir.empty())
    {
        return nullptr;
    }

    const std::filesystem::path fullPath = std::filesystem::path(dllDir) / NarrowToWide(info->szDll);
    HMODULE module = LoadLibraryW(fullPath.c_str());
    return reinterpret_cast<FARPROC>(module);
}

ExternC const PfnDliHook __pfnDliFailureHook2 = DelayLoadFailureHook;
