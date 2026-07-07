#pragma once

#include <filesystem>
#include <vector>

struct ID3D11Device;

namespace Alice
{
    class SkinnedMeshRegistry;
}

namespace Alice::AssetDropImport
{
    /// 탐색기에서 드롭된 파일들을 확장자 규칙에 따라 프로젝트로 복사한다.
    /// 폴더는 재귀 처리. 복사 후 ResourceManager negative cache를 비운다.
    /// - device/skinnedRegistry가 주어지면, 복사된 .fbx에 대해 기존 FbxImporter 파이프라인을
    ///   그대로 실행해 .fbxasset까지 생성한다 (에디터 GUI 상태에는 의존하지 않음).
    void Handle(const std::vector<std::filesystem::path>& files,
                const std::filesystem::path& projectRoot,
                ID3D11Device* device = nullptr,
                SkinnedMeshRegistry* skinnedRegistry = nullptr);
}
