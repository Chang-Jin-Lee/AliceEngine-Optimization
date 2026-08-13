#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Alice
{
    struct CommandLineOptions
    {
        std::filesystem::path scene;
        std::filesystem::path cameraRecord;
        std::filesystem::path cameraReplay;
        std::filesystem::path csvPath;
        std::wstring framePattern;
        double durationSeconds = 0.0;
        double warmupSeconds = 5.0;
        std::uint32_t frameStride = 1;
        std::uint32_t width = 1920;
        std::uint32_t height = 1080;
        bool legacy = false;
        bool vsyncEnabled = true;
        bool benchRequested = false;
    };

    bool ParseCommandLineOptions(
        const std::vector<std::wstring>& arguments,
        CommandLineOptions& outOptions,
        std::string& outError);

    bool ParseProcessCommandLine(
        CommandLineOptions& outOptions,
        std::string& outError);
}
