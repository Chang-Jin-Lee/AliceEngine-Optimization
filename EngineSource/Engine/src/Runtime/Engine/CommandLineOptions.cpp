#include "Runtime/Engine/CommandLineOptions.h"

#include <Windows.h>
#include <shellapi.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace Alice
{
    namespace
    {
        bool StartsWith(std::wstring_view value, std::wstring_view prefix)
        {
            return value.size() >= prefix.size() &&
                value.substr(0, prefix.size()) == prefix;
        }

        std::string NarrowAscii(std::wstring_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (wchar_t c : value)
                result.push_back(c <= 0x7f ? static_cast<char>(c) : '?');
            return result;
        }

        bool ParseDouble(std::wstring_view text, double minimum, double& output)
        {
            if (text.empty()) return false;
            const std::wstring copy(text);
            wchar_t* end = nullptr;
            errno = 0;
            const double value = std::wcstod(copy.c_str(), &end);
            if (errno == ERANGE || end != copy.c_str() + copy.size() ||
                !std::isfinite(value) || value < minimum)
                return false;
            output = value;
            return true;
        }

        bool ParseUint(std::wstring_view text, std::uint32_t minimum, std::uint32_t& output)
        {
            if (text.empty()) return false;
            const std::wstring copy(text);
            wchar_t* end = nullptr;
            errno = 0;
            const unsigned long long value = std::wcstoull(copy.c_str(), &end, 10);
            if (errno == ERANGE || end != copy.c_str() + copy.size() ||
                value < minimum || value > (std::numeric_limits<std::uint32_t>::max)())
                return false;
            output = static_cast<std::uint32_t>(value);
            return true;
        }

        std::filesystem::path ResolveScenePath(std::wstring_view value)
        {
            std::filesystem::path path(value);
            if (path.is_absolute()) return path;
            const auto generic = path.generic_wstring();
            if (StartsWith(generic, L"Assets/Scenes/")) return path;
            return std::filesystem::path(L"Assets/Scenes") / path;
        }

        bool IsSafeFramePattern(std::wstring_view pattern)
        {
            std::size_t placeholders = 0;
            for (std::size_t index = 0; index < pattern.size(); ++index)
            {
                if (pattern[index] != L'%') continue;
                if (index + 1 < pattern.size() && pattern[index + 1] == L'%')
                    return false;
                ++placeholders;
                ++index;
                if (index < pattern.size() && pattern[index] == L'0') ++index;
                while (index < pattern.size() && pattern[index] >= L'0' && pattern[index] <= L'9')
                    ++index;
                if (index >= pattern.size() || pattern[index] != L'd') return false;
            }
            return placeholders == 1;
        }
    }

    bool ParseCommandLineOptions(
        const std::vector<std::wstring>& arguments,
        CommandLineOptions& outOptions,
        std::string& outError)
    {
        CommandLineOptions options{};
        outError.clear();

        auto fail = [&](std::wstring_view argument, std::string_view reason)
        {
            outError = "invalid command-line option '" + NarrowAscii(argument) +
                "': " + std::string(reason);
            return false;
        };

        for (const std::wstring& argument : arguments)
        {
            const std::wstring_view arg(argument);
            auto valueAfter = [&](std::wstring_view prefix)
            {
                return arg.substr(prefix.size());
            };

            if (arg == L"--legacy")
            {
                options.legacy = true;
            }
            else if (StartsWith(arg, L"--scene="))
            {
                const auto value = valueAfter(L"--scene=");
                if (value.empty()) return fail(arg, "scene path is empty");
                options.scene = ResolveScenePath(value);
            }
            else if (StartsWith(arg, L"--camera-record="))
            {
                const auto value = valueAfter(L"--camera-record=");
                if (value.empty()) return fail(arg, "record path is empty");
                options.cameraRecord = std::filesystem::path(value);
            }
            else if (StartsWith(arg, L"--camera-replay="))
            {
                const auto value = valueAfter(L"--camera-replay=");
                if (value.empty()) return fail(arg, "replay path is empty");
                options.cameraReplay = std::filesystem::path(value);
            }
            else if (StartsWith(arg, L"--vsync="))
            {
                const auto value = valueAfter(L"--vsync=");
                if (value == L"on") options.vsyncEnabled = true;
                else if (value == L"off") options.vsyncEnabled = false;
                else return fail(arg, "expected on or off");
            }
            else if (StartsWith(arg, L"--duration="))
            {
                if (!ParseDouble(valueAfter(L"--duration="), 0.0, options.durationSeconds))
                    return fail(arg, "expected a non-negative finite number");
            }
            else if (StartsWith(arg, L"--warmup="))
            {
                if (!ParseDouble(valueAfter(L"--warmup="), 0.0, options.warmupSeconds))
                    return fail(arg, "expected a non-negative finite number");
            }
            else if (StartsWith(arg, L"--csv="))
            {
                const auto value = valueAfter(L"--csv=");
                if (value.empty()) return fail(arg, "CSV path is empty");
                options.csvPath = std::filesystem::path(value);
            }
            else if (StartsWith(arg, L"--frames="))
            {
                const auto value = valueAfter(L"--frames=");
                if (value.empty()) return fail(arg, "frame pattern is empty");
                if (!IsSafeFramePattern(value))
                    return fail(arg, "expected exactly one integer placeholder such as %06d");
                options.framePattern = value;
            }
            else if (StartsWith(arg, L"--frame-stride="))
            {
                if (!ParseUint(valueAfter(L"--frame-stride="), 1, options.frameStride))
                    return fail(arg, "expected an integer of at least one");
            }
            else if (StartsWith(arg, L"--width="))
            {
                if (!ParseUint(valueAfter(L"--width="), 1, options.width))
                    return fail(arg, "expected a positive integer");
            }
            else if (StartsWith(arg, L"--height="))
            {
                if (!ParseUint(valueAfter(L"--height="), 1, options.height))
                    return fail(arg, "expected a positive integer");
            }
            else
            {
                return fail(arg, "unknown option");
            }
        }

        if (!options.cameraRecord.empty() && !options.cameraReplay.empty())
            return fail(L"--camera-record/--camera-replay", "record and replay are mutually exclusive");
        if (!options.cameraRecord.empty() && options.scene.empty())
            return fail(L"--camera-record", "recording requires an explicit --scene");
        if (!options.csvPath.empty() && !options.framePattern.empty())
            return fail(L"--csv/--frames", "measurement and PNG capture must be separate runs");

        options.benchRequested = !arguments.empty();
        outOptions = std::move(options);
        return true;
    }

    bool ParseProcessCommandLine(CommandLineOptions& outOptions, std::string& outError)
    {
        int argumentCount = 0;
        wchar_t** argumentValues = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (!argumentValues)
        {
            outError = "CommandLineToArgvW failed";
            return false;
        }

        std::vector<std::wstring> arguments;
        arguments.reserve(argumentCount > 1 ? static_cast<std::size_t>(argumentCount - 1) : 0);
        for (int index = 1; index < argumentCount; ++index)
            arguments.emplace_back(argumentValues[index]);
        LocalFree(argumentValues);
        return ParseCommandLineOptions(arguments, outOptions, outError);
    }
}
