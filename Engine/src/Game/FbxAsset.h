#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Alice
{
    class ResourceManager;

    struct FbxInstanceAsset
    {
        std::string              sourceFbx;
        std::string              meshAssetPath;
        std::vector<std::string> materialAssetPaths;
    };

    /// JSON(.fbxasset) 읽기/쓰기
    /// - { "source_fbx": "...", "mesh": "...", "materials": ["..."] }
    bool LoadFbxInstanceAsset(const std::filesystem::path& path,
                              FbxInstanceAsset& out);

    bool SaveFbxInstanceAsset(const std::filesystem::path& path,
                              const FbxInstanceAsset& asset);

    /// 에디터/최종빌드 모두에서 동작하는 자동 로더입니다.
    /// - editorMode: 실제 파일(Assets/...)을 읽습니다.
    /// - gameMode  : ResourceManager를 통해 Metas/Chunks에서 바이트를 로드해서 JSON으로 파싱합니다.
    bool LoadFbxInstanceAssetAuto(const ResourceManager& resources,
                                  const std::filesystem::path& logicalPath,
                                  FbxInstanceAsset& out);
}


