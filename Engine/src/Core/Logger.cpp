#include "Core/Logger.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace Alice
{
    namespace
    {
        std::mutex              g_LogMutex;
        std::ofstream           g_LogFile;
        std::filesystem::path   g_LogFilePath;
        bool                    g_Initialized = false;

        std::filesystem::path GetExecutableDirectory()
        {
            wchar_t pathW[MAX_PATH] = {};
            const DWORD len = ::GetModuleFileNameW(nullptr, pathW, MAX_PATH);
            // 없다면
            if (len == 0 || len == MAX_PATH) return std::filesystem::current_path();

            std::filesystem::path exePath(pathW);
            return exePath.parent_path();
        }

        std::string LevelToString(LogLevel level)
        {
            switch (level)
            {
                case LogLevel::Info:    return "Info";
                case LogLevel::Warning: return "Warning";
                case LogLevel::Error:   return "Error";
                default:                return "Unknown";
            }
        }

        std::string GetTimestamp()
        {
            using namespace std::chrono;
            const auto now       = system_clock::now();
            const auto now_time  = system_clock::to_time_t(now);
            const auto millis    = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

            std::tm localTime{};
            ::localtime_s(&localTime, &now_time);

            std::ostringstream oss;
            oss << std::put_time(&localTime, "%H:%M:%S")
                << '.'
                << std::setw(3) << std::setfill('0') << millis;
            return oss.str();
        }
    }

    void Logger::Initialize()
    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        if (g_Initialized) return;

        const auto exeDir  = GetExecutableDirectory();
        const auto logDir  = exeDir / "Logs";

        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);

        // 파일 이름: Alice_YYYYMMDD_HHMMSS.log
        using namespace std::chrono;
        const auto now      = system_clock::now();
        const auto now_time = system_clock::to_time_t(now);
        std::tm localTime{};
        ::localtime_s(&localTime, &now_time);

        std::ostringstream name;
        name << "Alice_"
             << std::put_time(&localTime, "%Y%m%d_%H%M%S")
             << ".log";

        g_LogFilePath = logDir / name.str();
        g_LogFile.open(g_LogFilePath, std::ios::out | std::ios::trunc);

        if (g_LogFile.is_open())
        {
            g_LogFile << "==== AliceRenderer Log Started ====\n";
            g_LogFile.flush();
        }

        g_Initialized = true;
    }

    void Logger::Shutdown()
    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        if (!g_Initialized)
            return;

        if (g_LogFile.is_open())
        {
            g_LogFile << "==== AliceRenderer Log End ====\n";
            g_LogFile.flush();
            g_LogFile.close();
        }

        g_Initialized = false;
    }

    void Logger::Log(LogLevel level,
                     const char* file,
                     int line,
                     const char* function,
                     const char* message)
    {
        std::lock_guard<std::mutex> lock(g_LogMutex);

        const std::string ts    = GetTimestamp();
        const std::string levelStr = LevelToString(level);

        std::ostringstream oss;
        oss << "[" << ts << "]"
            << " [" << levelStr << "] "
            << file << "(" << line << ") "
            << function << " : "
            << message
            << '\n';

        const std::string finalLine = oss.str();

        // 파일 출력
        if (g_LogFile.is_open())
        {
            g_LogFile << finalLine;
            g_LogFile.flush();
        }

        // 디버거 출력 (간단하게 ANSI 로)
        ::OutputDebugStringA(finalLine.c_str());
    }

    void Logger::LogFormat(LogLevel level,
                           const char* file,
                           int line,
                           const char* function,
                           const char* fmt,
                           ...)
    {
        char buffer[1024] = {};

        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        Log(level, file, line, function, buffer);
    }
}



