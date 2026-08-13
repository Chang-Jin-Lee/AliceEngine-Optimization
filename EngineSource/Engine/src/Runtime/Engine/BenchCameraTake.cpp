#include "Runtime/Engine/BenchCameraTake.h"

#include <Windows.h>

#include "ThirdParty/json/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace Alice
{
    namespace
    {
        CameraTakeFrame Interpolate(
            const CameraTakeFrame& a,
            const CameraTakeFrame& b,
            float t)
        {
            CameraTakeFrame result{};
            result.position = {
                a.position.x + (b.position.x - a.position.x) * t,
                a.position.y + (b.position.y - a.position.y) * t,
                a.position.z + (b.position.z - a.position.z) * t
            };
            using namespace DirectX;
            const XMVECTOR qa = XMQuaternionNormalize(XMLoadFloat4(&a.rotation));
            const XMVECTOR qb = XMQuaternionNormalize(XMLoadFloat4(&b.rotation));
            XMStoreFloat4(&result.rotation, XMQuaternionNormalize(XMQuaternionSlerp(qa, qb, t)));
            result.fovY = a.fovY + (b.fovY - a.fovY) * t;
            return result;
        }

        bool ReadFrame(const nlohmann::json& json, CameraTakeFrame& frame)
        {
            if (!json.is_object() || !json.contains("p") || !json.contains("q") ||
                !json.contains("fovY")) return false;
            const auto& p = json["p"];
            const auto& q = json["q"];
            if (!p.is_array() || p.size() != 3 || !q.is_array() || q.size() != 4)
                return false;
            try
            {
                frame.position = { p[0].get<float>(), p[1].get<float>(), p[2].get<float>() };
                frame.rotation = { q[0].get<float>(), q[1].get<float>(), q[2].get<float>(), q[3].get<float>() };
                frame.fovY = json["fovY"].get<float>();
            }
            catch (...)
            {
                return false;
            }
            const float quaternionLengthSquared =
                frame.rotation.x * frame.rotation.x + frame.rotation.y * frame.rotation.y +
                frame.rotation.z * frame.rotation.z + frame.rotation.w * frame.rotation.w;
            return std::isfinite(frame.position.x) && std::isfinite(frame.position.y) &&
                std::isfinite(frame.position.z) && std::isfinite(frame.fovY) &&
                frame.fovY > 0.0f && quaternionLengthSquared > 0.000001f;
        }
    }

    bool BenchCameraTake::FrameAt(std::size_t frameIndex, CameraTakeFrame& output) const noexcept
    {
        if (frameIndex >= frames.size()) return false;
        output = frames[frameIndex];
        return true;
    }

    void BenchCameraRecorder::AddSample(double elapsedSeconds, const CameraTakeFrame& frame)
    {
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) return;
        if (!m_samples.empty() && elapsedSeconds < m_samples.back().elapsedSeconds) return;
        m_samples.push_back({ elapsedSeconds, frame });
    }

    BenchCameraTake BenchCameraRecorder::BuildTake(
        std::string scene, double fixedDeltaSeconds) const
    {
        BenchCameraTake take{};
        take.scene = std::move(scene);
        take.fixedDeltaSeconds = fixedDeltaSeconds;
        if (m_samples.empty() || !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0)
            return take;

        const double duration = m_samples.back().elapsedSeconds;
        const std::size_t frameCount =
            static_cast<std::size_t>(std::floor(duration / fixedDeltaSeconds + 0.000001)) + 1;
        take.frames.reserve(frameCount);
        std::size_t right = 0;
        for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const double time = static_cast<double>(frameIndex) * fixedDeltaSeconds;
            while (right + 1 < m_samples.size() && m_samples[right + 1].elapsedSeconds < time)
                ++right;
            if (right + 1 >= m_samples.size())
            {
                take.frames.push_back(m_samples.back().frame);
                continue;
            }
            const Sample& a = m_samples[right];
            const Sample& b = m_samples[right + 1];
            const double span = b.elapsedSeconds - a.elapsedSeconds;
            const float t = span <= 0.0 ? 0.0f :
                static_cast<float>((time - a.elapsedSeconds) / span);
            take.frames.push_back(Interpolate(a.frame, b.frame, std::clamp(t, 0.0f, 1.0f)));
        }
        return take;
    }

    std::string SerializeBenchCameraTake(const BenchCameraTake& take)
    {
        nlohmann::json json;
        json["version"] = take.version;
        json["scene"] = take.scene;
        json["fixedDeltaSeconds"] = take.fixedDeltaSeconds;
        json["frameCount"] = take.frames.size();
        json["frames"] = nlohmann::json::array();
        for (const CameraTakeFrame& frame : take.frames)
        {
            json["frames"].push_back({
                { "p", { frame.position.x, frame.position.y, frame.position.z } },
                { "q", { frame.rotation.x, frame.rotation.y, frame.rotation.z, frame.rotation.w } },
                { "fovY", frame.fovY }
            });
        }
        return json.dump(2);
    }

    bool DeserializeBenchCameraTake(
        std::string_view source,
        BenchCameraTake& outTake,
        std::string& outError)
    {
        outError.clear();
        try
        {
            const nlohmann::json json = nlohmann::json::parse(source);
            BenchCameraTake take{};
            take.version = json.value("version", 0u);
            take.scene = json.value("scene", std::string{});
            take.fixedDeltaSeconds = json.value("fixedDeltaSeconds", 0.0);
            if (take.version != 1 || !std::isfinite(take.fixedDeltaSeconds) ||
                take.fixedDeltaSeconds <= 0.0 || !json.contains("frames") ||
                !json["frames"].is_array())
            {
                outError = "invalid camera take header";
                return false;
            }
            take.frames.reserve(json["frames"].size());
            for (const auto& frameJson : json["frames"])
            {
                CameraTakeFrame frame{};
                if (!ReadFrame(frameJson, frame))
                {
                    outError = "invalid camera take frame";
                    return false;
                }
                take.frames.push_back(frame);
            }
            if (json.value("frameCount", take.frames.size()) != take.frames.size())
            {
                outError = "frameCount does not match frames array";
                return false;
            }
            if (take.frames.empty())
            {
                outError = "camera take contains no frames";
                return false;
            }
            outTake = std::move(take);
            return true;
        }
        catch (const std::exception& exception)
        {
            outError = exception.what();
            return false;
        }
    }

    bool LoadBenchCameraTake(
        const std::filesystem::path& path,
        BenchCameraTake& outTake,
        std::string& outError)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            outError = "could not open camera take: " + path.string();
            return false;
        }
        std::ostringstream contents;
        contents << stream.rdbuf();
        return DeserializeBenchCameraTake(contents.str(), outTake, outError);
    }

    bool SaveBenchCameraTake(
        const std::filesystem::path& path,
        const BenchCameraTake& take,
        std::string& outError)
    {
        outError.clear();
        std::error_code error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            outError = "could not create camera take directory: " + error.message();
            return false;
        }
        std::filesystem::path temporaryPath = path;
        temporaryPath += L".tmp";
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            outError = "could not write camera take: " + temporaryPath.string();
            return false;
        }
        stream << SerializeBenchCameraTake(take) << '\n';
        stream.close();
        if (!stream)
        {
            outError = "failed while writing camera take: " + path.string();
            return false;
        }
        if (!MoveFileExW(
            temporaryPath.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            outError = "could not replace camera take: " + path.string();
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
        return true;
    }
}
