#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <wrl/client.h>
#include <d3d11.h>

#include "3Dmodel/FbxTypes.h"

// FbxModel은 전역 네임스페이스(3Dmodel/FbxModel.h) 에 정의되어 있습니다.
class FbxModel;

namespace Alice
{
    /// GPU 상의 스키닝 메시 1개를 표현하는 구조입니다.
    /// - 정점/인덱스 버퍼 + 정점 포맷 정보
    /// - FBX 로부터 생성된 서브셋/머티리얼/스켈레톤 메타데이터를 함께 보관합니다.
    struct SkinnedMeshGPU
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;

        UINT stride      { 0 };
        UINT indexCount  { 0 };
        UINT startIndex  { 0 };
        INT  baseVertex  { 0 };

        // === FBX 서브셋/머티리얼 ===
        std::vector<FbxSubset> subsets; // 인덱스 범위 + 머티리얼 인덱스
        std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> materialSRVs; // FBX 기본 디퓨즈 텍스처
        std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> normalSRVs;   // FBX 노말맵 텍스처(선택)
        std::vector<std::string> materialOverridePaths; // 에디터에서 교체한 텍스처 경로 (선택 사항)

        // === FBX 스켈레톤 ===
        std::vector<FbxSkeletonNode> skeleton; // 전체 노드 캐시
        int                          skeletonRoot { -1 }; // 루트 인덱스
        std::string                  skeletonText;         // 간단한 트리 텍스트 (Inspector에서 표시)

        // === FBX 애니메이션/원본 컨텍스트 ===
        // - per-entity 애니메이션 재생을 위해 "공유 데이터(Assimp scene + bone metadata)"를 유지합니다.
        // - 재생 상태(시간/클립/속도)는 엔티티 컴포넌트에서 관리합니다.
        std::shared_ptr<FbxModel> sourceModel;
    };

    class ResourceManager;
    class FbxImporter;

    /// FBX 로부터 만들어진 스키닝 메시 자산을
    /// 문자열 키(논리 경로)로 보관하는 레지스트리입니다.
    /// - 엔진(Rendering 계층)의 일부로, 게임/에디터 양쪽에서 공유합니다.
    class SkinnedMeshRegistry
    {
    public:
        void Register(const std::string& assetPath,
                      std::shared_ptr<SkinnedMeshGPU> mesh)
        {
            m_meshes[assetPath] = std::move(mesh);
        }

        std::shared_ptr<SkinnedMeshGPU> Find(const std::string& assetPath) const
        {
            auto it = m_meshes.find(assetPath);
            if (it == m_meshes.end())
                return nullptr;
            return it->second;
        }

        /// meshKey가 레지스트리에 있는지 확인합니다.
        bool Has(const std::string& assetPath) const
        {
            return m_meshes.find(assetPath) != m_meshes.end();
        }

        /// fbxasset 파일로부터 메시를 온디맨드 로딩하고 레지스트리에 등록합니다.
        /// - 씬/빌드에서 meshKey가 없을 때 호출됩니다.
        /// - ResourceManager를 통해 fbxasset을 읽고, FbxImporter로 메시를 로드합니다.
        /// @return 로딩 성공 여부
        bool LoadFromFbxAsset(const std::string& meshKey,
                              const std::string& instanceAssetPath,
                              ResourceManager& resources,
                              FbxImporter& importer,
                              ID3D11Device* device);

    private:
        std::unordered_map<std::string, std::shared_ptr<SkinnedMeshGPU>> m_meshes;
    };
}


