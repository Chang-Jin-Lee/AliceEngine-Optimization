#pragma once

#include <string>

namespace Alice
{
    /// 간단한 공용 로거입니다.
    /// - 각 로그는 시간, 레벨, 파일/함수/라인 정보를 포함합니다.
    /// - 디버거(OutputDebugString)와 함께 실행 파일 옆의 Logs 폴더에 .log 파일로 기록합니다.
    enum class LogLevel
    {
        Info,
        Warning,
        Error,
    };

    class Logger
    {
    public:
        /// 한 프로세스에서 한 번만 호출하면 됩니다.
        /// - 실행 파일 경로를 기준으로 Logs/Alice_YYYYMMDD_HHMMSS.log 파일을 생성합니다.
        static void Initialize();

        /// 종료 시 호출해서 파일 핸들을 닫습니다.
        static void Shutdown();

        /// 서식 없는 단순 문자열 로그 (내부에서만 주로 사용).
        static void Log(LogLevel level,
                        const char* file,
                        int line,
                        const char* function,
                        const char* message);

        /// printf 스타일의 가변 인자 로그 함수입니다.
        static void LogFormat(LogLevel level,
                              const char* file,
                              int line,
                              const char* function,
                              const char* fmt,
                              ...);
    };

    // 편의 매크로들
    // 사용 예)
    //   ALICE_LOG_INFO("value = %d", value);
    //   ALICE_LOG_ERROR("Failed to open: %s", path.c_str());
#define ALICE_LOG_INFO(...)  ::Alice::Logger::LogFormat(::Alice::LogLevel::Info,    __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define ALICE_LOG_WARN(...)  ::Alice::Logger::LogFormat(::Alice::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define ALICE_LOG_ERRORF(...) ::Alice::Logger::LogFormat(::Alice::LogLevel::Error,  __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
}



