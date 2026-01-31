#pragma once

#include <DirectXMath.h>
#include <string>

namespace Alice {
    // 전방 선언
    class GameObject;

    /// Skinned FBX 메시에 대한 최소 정보만 담는 컴포넌트입니다.
    /// - 실제 FBX 파싱/애니메이션은 게임(샘플) 레벨에서 처리합니다.
    /// - 엔진은 bone 행렬 배열과 본 개수만 사용합니다.
    struct SkinnedMeshComponent 
    {
        std::string meshAssetPath; // FBX/메시 에셋 경로 (SkinnedMeshRegistry 키)
        std::string instanceAssetPath; // .fbxasset 인스턴스 에셋 경로 (씬/프로젝트 저장용)
        const DirectX::XMFLOAT4X4* boneMatrices{ nullptr };               // 외부에서 관리하는 본 행렬 배열
        std::uint32_t boneCount{ 0 }; // 사용 중인 본 개수
    };
}