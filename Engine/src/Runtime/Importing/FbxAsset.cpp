#include "Runtime/Importing/FbxAsset.h"

#include <fstream>

#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Foundation/Logger.h"
#include "ThirdParty/json/json.hpp"

namespace Alice
{
    bool LoadFbxInstanceAsset(const std::filesystem::path& path,
                              FbxInstanceAsset& out)
    {
        out = {};

        std::ifstream ifs(path);
        if (!ifs.is_open())
            return false;

        nlohmann::json j;
        try
        {
            ifs >> j;
        }
        catch (...)
        {
            return false;
        }

        out.sourceFbx = j.value("source_fbx", std::string{});
        out.meshAssetPath = j.value("mesh", std::string{});
        out.materialAssetPaths.clear();

        auto it = j.find("materials");
        if (it != j.end() && it->is_array())
        {
            for (const auto& v : *it)
            {
                if (v.is_string())
                    out.materialAssetPaths.push_back(v.get<std::string>());
            }
        }

        if (out.meshAssetPath.empty())
            return false;

        return true;
    }

    bool SaveFbxInstanceAsset(const std::filesystem::path& path,
                              const FbxInstanceAsset& asset)
    {
        auto parent = path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent))
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }

        std::ofstream ofs(path);
        if (!ofs.is_open())
            return false;

        nlohmann::json j;
        j["source_fbx"] = asset.sourceFbx;
        j["mesh"] = asset.meshAssetPath;
        j["materials"] = asset.materialAssetPaths;

        ofs << j.dump(4);
        return true;
    }

    bool LoadFbxInstanceAssetAuto(const ResourceManager& resources,
                                  const std::filesystem::path& logicalPath,
                                  FbxInstanceAsset& out)
    {
        const std::filesystem::path resolved = resources.Resolve(logicalPath);

        // Metas/Chunks 로 매핑된 경우: chunk 파일(.alice)이므로 직접 파일 파싱하면 안 됨
        if (resolved.extension() == ".alice")
        {
            auto sp = resources.LoadSharedBinaryAuto(logicalPath);
            if (!sp)
            {
                ALICE_LOG_ERRORF("[FbxAsset] LoadAuto FAILED: chunk load failed. logical=\"%s\" resolved=\"%s\"",
                                 logicalPath.generic_string().c_str(),
                                 resolved.generic_string().c_str());
                return false;
            }

            nlohmann::json j;
            try { j = nlohmann::json::parse(sp->begin(), sp->end()); }
            catch (...)
            {
                ALICE_LOG_ERRORF("[FbxAsset] JSON parse FAILED. logical=\"%s\" bytes=%zu",
                                 logicalPath.generic_string().c_str(),
                                 sp->size());
                return false;
            }

            out = {};
            out.sourceFbx = j.value("source_fbx", std::string{});
            out.meshAssetPath = j.value("mesh", std::string{});
            out.materialAssetPaths.clear();

            auto it = j.find("materials");
            if (it != j.end() && it->is_array())
            {
                for (const auto& v : *it)
                    if (v.is_string()) out.materialAssetPaths.push_back(v.get<std::string>());
            }

            if (out.meshAssetPath.empty())
            {
                ALICE_LOG_ERRORF("[FbxAsset] LoadAuto FAILED: empty mesh. logical=\"%s\"", logicalPath.generic_string().c_str());
                return false;
            }

            ALICE_LOG_INFO("[FbxAsset] LoadAuto OK. logical=\"%s\" mesh=\"%s\" mats=%zu",
                           logicalPath.generic_string().c_str(),
                           out.meshAssetPath.c_str(),
                           out.materialAssetPaths.size());
            return true;
        }

        // 일반 파일: resolved 경로로 로드
        const bool ok = LoadFbxInstanceAsset(resolved, out);
        if (!ok)
        {
            ALICE_LOG_ERRORF("[FbxAsset] LoadAuto FAILED: file load failed. logical=\"%s\" resolved=\"%s\"",
                             logicalPath.generic_string().c_str(),
                             resolved.generic_string().c_str());
            return false;
        }

        return true;
    }
}


