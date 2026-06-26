#pragma once

#include <vector>
#include <DirectXMath.h>

namespace Alice {
    /// 스키닝 애니메이션 재생 상태(엔티티 단위)
    /// - 실제 평가/팔레트 계산은 AdvancedAnimSystem 이 수행합니다.
    struct SkinnedAnimationComponent 
    {
        int clipIndex{ 0 }; // 현재 재생 클립 인덱스
        bool playing{ true };
        float speed{ 1.0f };   // 배속(1.0 = 정상)
        double timeSec{ 0.0 }; // 현재 시간(초)

        // CPU 본 팔레트(ForwardRenderSystem이 여기서 읽어 VS CB로 업로드)
        std::vector<DirectX::XMFLOAT4X4> palette;
    };
}