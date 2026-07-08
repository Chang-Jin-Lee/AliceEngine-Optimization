#include "Runtime/Rendering/SkinnedMeshRegistry.h"

#include <filesystem>

#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"

namespace Alice
{
    bool SkinnedMeshRegistry::LoadFromFbxAsset(const std::string& meshKey,
                                                const std::string& instanceAssetPath,
                                                ResourceManager& resources,
                                                FbxImporter& importer,
                                                ID3D11Device* device)
    {
        // 이미 등록되어 있으면 건너뜀
        if (Has(meshKey))
        {
            ALICE_LOG_INFO("[SkinnedMeshRegistry] LoadFromFbxAsset: meshKey=\"%s\" already registered, skipping", meshKey.c_str());
            return true;
        }

        if (!device)
        {
            ALICE_LOG_ERRORF("[SkinnedMeshRegistry] LoadFromFbxAsset: Device is null. meshKey=\"%s\"", meshKey.c_str());
            return false;
        }

        if (instanceAssetPath.empty())
        {
            ALICE_LOG_ERRORF("[SkinnedMeshRegistry] LoadFromFbxAsset: instanceAssetPath is empty. meshKey=\"%s\"", meshKey.c_str());
            return false;
        }

        // 1. fbxasset 파일을 ResourceManager로 읽기
        FbxInstanceAsset asset;
        if (!LoadFbxInstanceAssetAuto(resources, std::filesystem::path(instanceAssetPath), asset))
        {
            ALICE_LOG_ERRORF("[SkinnedMeshRegistry] LoadFromFbxAsset: Failed to load fbxasset. path=\"%s\" meshKey=\"%s\"", 
                            instanceAssetPath.c_str(), meshKey.c_str());
            return false;
        }

        // 2. sourceFbx 경로로 FbxImporter::Import() 호출
        //    Import() 내부에서 이미 레지스트리에 등록되므로 여기서는 결과 확인만
        FbxImportOptions opt{};
        FbxImportResult result = importer.Import(device, std::filesystem::path(asset.sourceFbx), opt);

        if (result.meshAssetPath.empty())
        {
            ALICE_LOG_ERRORF("[SkinnedMeshRegistry] LoadFromFbxAsset: Import failed. sourceFbx=\"%s\" meshKey=\"%s\"", 
                            asset.sourceFbx.c_str(), meshKey.c_str());
            return false;
        }

        // 3. 결과 확인 (meshAssetPath가 meshKey와 일치해야 함)
        if (result.meshAssetPath != meshKey)
        {
            ALICE_LOG_WARN("[SkinnedMeshRegistry] LoadFromFbxAsset: meshAssetPath mismatch. expected=\"%s\" got=\"%s\"",
                          meshKey.c_str(), result.meshAssetPath.c_str());

            // 근본 수정: 임포터가 실제로 등록한 키(result.meshAssetPath)의 GPU 데이터를
            // 요청 키(meshKey)로도 별칭 등록한다. 이렇게 하면 호출부의 Has(meshKey) 검사가
            // 매 프레임 재임포트를 유발하지 않고 즉시 통과한다.
            if (!Has(meshKey))
            {
                if (auto existing = Find(result.meshAssetPath))
                {
                    Register(meshKey, existing);
                    ALICE_LOG_INFO("[SkinnedMeshRegistry] LoadFromFbxAsset: Aliased requested key=\"%s\" -> registered key=\"%s\"",
                                  meshKey.c_str(), result.meshAssetPath.c_str());
                }
            }
        }

        // 4. 등록 확인
        if (!Has(meshKey) && !Has(result.meshAssetPath))
        {
            ALICE_LOG_ERRORF("[SkinnedMeshRegistry] LoadFromFbxAsset: Mesh not registered after import. meshKey=\"%s\" meshAssetPath=\"%s\"", 
                            meshKey.c_str(), result.meshAssetPath.c_str());
            return false;
        }

        ALICE_LOG_INFO("[SkinnedMeshRegistry] LoadFromFbxAsset: Success. meshKey=\"%s\" sourceFbx=\"%s\"", 
                      meshKey.c_str(), asset.sourceFbx.c_str());
        return true;
    }
}
