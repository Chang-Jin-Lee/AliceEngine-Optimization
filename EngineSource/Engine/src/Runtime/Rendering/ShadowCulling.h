#pragma once

// 방향광 그림자 패스의 캐스터 컬링 판정.
//
// 그림자 캐스터를 "카메라"에 안 보인다고 버리면 안 된다. 화면 밖 물체도
// 화면 안으로 그림자를 드리우기 때문이다. 카메라 프러스텀으로 걸러내면
// 실내에서 이동해 벽이 시야를 벗어나는 순간 그 벽이 섀도우맵에서 빠지고,
// 벽이 만들던 그림자가 사라져 화면이 갑자기 밝아진다.
//
// 그래서 기준은 카메라가 아니라 "그 빛이 실제로 섀도우맵에 담을 수 있는 범위",
// 즉 라이트 공간의 ortho 박스여야 한다.
//
// D3D에 의존하지 않는 순수 함수만 둔다. 렌더러와 테스트가 함께 쓴다.
namespace Alice::ShadowCulling
{
    // 방향광 섀도우맵이 덮는 라이트 공간 ortho 박스.
    // XY는 [-halfExtentXY, +halfExtentXY], Z는 [nearZ, farZ].
    struct OrthoVolume
    {
        float halfExtentXY = 0.0f;
        float nearZ = 0.0f;
        float farZ = 0.0f;
    };

    // 라이트 공간으로 옮긴 바운딩 스피어가 그림자 볼륨과 겹치는가.
    //
    // lightView는 회전과 평행이동뿐이라 구의 반지름은 변하지 않는다.
    // 그래서 중심만 변환해 넘기고, 여기서는 박스를 반지름만큼 부풀려 판정한다.
    // 경계에 걸친 물체를 버리지 않도록 보수적으로 잡는다.
    constexpr bool IntersectsShadowVolume(
        float centerX, float centerY, float centerZ,
        float radius, const OrthoVolume& volume) noexcept
    {
        const float extent = volume.halfExtentXY + radius;
        if (centerX < -extent || centerX > extent) return false;
        if (centerY < -extent || centerY > extent) return false;
        if (centerZ < volume.nearZ - radius) return false;
        if (centerZ > volume.farZ + radius) return false;
        return true;
    }
}
