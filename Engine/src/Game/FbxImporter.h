#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct ID3D11Device;

namespace Alice
{
    class ResourceManager;
    class SkinnedMeshRegistry;

    /// 간단한 FBX 임포트 옵션입니다.
    /// - 튜토리얼의 Common/Mesh(FbxModel/FbxMaterial/...) 를 감싸는 용도입니다.
    struct FbxImportOptions
    {
        bool generateTangents { true };
        bool importAnimations { true };
    };

    /// FBX 한 개를 임포트한 뒤, 엔진/게임 쪽에서 사용할 핵심 정보입니다.
    /// - 실제 메시/본/애니메이션 데이터는 별도의 애셋 구조로 관리합니다.
    struct FbxImportResult
    {
        /// 이 FBX 로부터 생성된 스키닝 메시의 경로 입니다.
        /// 예) "Characters/Hero"
        std::string              meshAssetPath;

        /// FBX 에 포함된 각 서브머티리얼에서 생성된 .mat 파일 경로 목록입니다.
        std::vector<std::string> materialAssetPaths;

        /// 이 FBX 로부터 생성된 인스턴스 에셋(.fbxasset)의 경로입니다.
        /// - 에디터에서 월드에 배치할 때 사용합니다.
        std::string              instanceAssetPath;
    };

    /// D3D11-AliceTutorial 의 Common/Mesh(FbxModel/FbxMaterial/...) 를
    /// 게임 레벨에서 감싸기 위한 아주 얇은 FBX 임포터입니다.
    /// - 파일을 여는 책임만 가지고, 실제 GPU 버퍼/엔티티 등록은 별도의 시스템(예: SkinnedMeshRegistry)에서 처리합니다.
    class FbxImporter
    {
    public:
        explicit FbxImporter(ResourceManager& resources,
                             SkinnedMeshRegistry* meshRegistry = nullptr);

        /// FBX 파일을 임포트합니다.
        /// - 현재는 텍스처/.fbm/.mat/.alice 파이프라인을 완성한 상태입니다.
        /// - 이후 단계에서 FbxModel 을 사용해 메시/본/애니메이션을 연결합니다.
        FbxImportResult Import(ID3D11Device* device,
                               const std::filesystem::path& fbxPath,
                               const FbxImportOptions& options);

    private:
        ResourceManager&     m_resources;
        SkinnedMeshRegistry* m_meshRegistry; // 선택적: 스키닝 메시 GPU 등록용
    };
}


