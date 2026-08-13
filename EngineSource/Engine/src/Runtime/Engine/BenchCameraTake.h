#pragma once

#include <DirectXMath.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Alice
{
    struct CameraTakeFrame
    {
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        float fovY = DirectX::XM_PIDIV4;
    };

    struct BenchCameraTake
    {
        std::uint32_t version = 1;
        std::string scene;
        double fixedDeltaSeconds = 1.0 / 60.0;
        std::vector<CameraTakeFrame> frames;

        std::size_t FrameCount() const noexcept { return frames.size(); }
        bool FrameAt(std::size_t frameIndex, CameraTakeFrame& output) const noexcept;
    };

    class BenchCameraRecorder
    {
    public:
        void AddSample(double elapsedSeconds, const CameraTakeFrame& frame);
        BenchCameraTake BuildTake(std::string scene, double fixedDeltaSeconds) const;
        bool Empty() const noexcept { return m_samples.empty(); }

    private:
        struct Sample
        {
            double elapsedSeconds = 0.0;
            CameraTakeFrame frame{};
        };
        std::vector<Sample> m_samples;
    };

    std::string SerializeBenchCameraTake(const BenchCameraTake& take);
    bool DeserializeBenchCameraTake(
        std::string_view json,
        BenchCameraTake& outTake,
        std::string& outError);
    bool LoadBenchCameraTake(
        const std::filesystem::path& path,
        BenchCameraTake& outTake,
        std::string& outError);
    bool SaveBenchCameraTake(
        const std::filesystem::path& path,
        const BenchCameraTake& take,
        std::string& outError);
}
