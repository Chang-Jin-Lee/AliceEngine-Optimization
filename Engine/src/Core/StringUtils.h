#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <string>

namespace Alice
{
    /// UTF-8 문자열을 Windows wide 문자열(UTF-16, std::wstring)로 변환합니다.
    inline std::wstring WStringFromUtf8(const std::string& s)
    {
        if (s.empty()) return std::wstring();

        int lenW = MultiByteToWideChar(CP_UTF8,
                                       MB_ERR_INVALID_CHARS,
                                       s.c_str(),
                                       static_cast<int>(s.size()),
                                       nullptr,
                                       0);
        if (lenW <= 0) return std::wstring(s.begin(), s.end());

        std::wstring w(static_cast<size_t>(lenW), L'\0');
        MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            s.c_str(),
                            static_cast<int>(s.size()),
                            &w[0],
                            lenW);
        return w;
    }

    /// Windows wide 문자열(UTF-16, std::wstring)을 UTF-8 문자열(std::string)로 변환합니다.
    inline std::string Utf8FromWString(const std::wstring& ws)
    {
        if (ws.empty()) return std::string();

        int lenU8 = WideCharToMultiByte(CP_UTF8,
                                        0,
                                        ws.c_str(),
                                        static_cast<int>(ws.size()),
                                        nullptr,
                                        0,
                                        nullptr,
                                        nullptr);
        if (lenU8 <= 0) return std::string();

        std::string out(static_cast<size_t>(lenU8), '\0');
        WideCharToMultiByte(CP_UTF8,
                            0,
                            ws.c_str(),
                            static_cast<int>(ws.size()),
                            &out[0],
                            lenU8,
                            nullptr,
                            nullptr);
        return out;
    }

    /// 편의를 위한 literal 전용 헬퍼: TEXT("...") / L"..." 를 UTF-8 std::string 으로 변환합니다.
    /// - 예: ImGui::TextUnformatted( Alice::Utf8(TEXT("한글텍스트")).c_str() );
    inline std::string Utf8(const wchar_t* ws)
    {
        if (!ws) return std::string();
        return Utf8FromWString(std::wstring(ws));
    }
}


