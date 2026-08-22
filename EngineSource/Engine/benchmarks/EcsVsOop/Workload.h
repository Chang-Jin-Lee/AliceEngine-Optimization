#pragma once

#include <DirectXMath.h>
#include <cstdint>

// 두 백엔드가 공유하는 컴포넌트와 산술.
// 산술을 여기 한 곳에만 두어 양쪽의 계산량이 같음을 구조로 보장한다.
namespace Alice::Bench
{
    /// 엔진 TransformComponent와 같은 필드 구성.
    /// 부모 참조와 enabled/visible 플래그는 이 워크로드에서 쓰지 않아 제외했다.
    struct BenchTransform
    {
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct BenchDecal
    {
        float lifetime = 1.0f;
        float fade = 1.0f;
    };

    struct BenchAnimation
    {
        std::uint32_t clipIndex = 0;
        float timeSec = 0.0f;
        float speed = 1.0f;
    };

    // 분기 조건이 데이터에 의존하지 않게 짠다. 두 백엔드의 분기 예측 조건을 같게 두기 위함이다.

    inline void StepTransform(BenchTransform& t, float dt) noexcept
    {
        t.position.x += dt;
        t.position.y += dt * 0.5f;
        t.rotation.y += dt * 0.25f;
    }

    inline void StepDecal(BenchDecal& d, float dt) noexcept
    {
        d.lifetime -= dt;
        if (d.lifetime < 0.0f)
            d.lifetime = 0.0f;
        d.fade = d.lifetime;
    }

    inline void StepAnimation(BenchAnimation& a, float dt) noexcept
    {
        a.timeSec += dt * a.speed;
        if (a.timeSec > 10.0f)
            a.timeSec -= 10.0f;
    }
}
