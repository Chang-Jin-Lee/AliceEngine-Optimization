#pragma once

#include <cstdint>

#include "Runtime/ECS/World.h"
#include "Runtime/Rendering/Camera.h"

namespace Alice
{
    class SkinnedMeshRegistry;

	/// 뷰포트 상의 마우스 좌표(u,v)를 이용해
	/// - 카메라 기준 레이를 만들고
	/// - 월드에 있는 단순 오브젝트(현재는 큐브)를 선택하는 간단한 피커입니다.
	/// 복잡한 가속 구조(BVH)는 사용하지 않고, 바운딩 스피어만 사용합니다.
	class ViewportPicker
	{
	public:
		ViewportPicker() = default;
		~ViewportPicker() = default;

		/// \param world  피킹 대상 엔티티/Transform 정보를 가진 월드
		/// \param camera 현재 카메라 (View/Projection 행렬을 사용)
		/// \param skinnedRegistry SkinnedMeshComponent(meshAssetPath) aabb로 반판된 메시를 가진 레지스트리
		/// \param u,v    [0,1] 범위의 뷰포트 상대 좌표 (좌상단 (0,0), 우하단 (1,1))
		/// \return      히트한 엔티티 ID (없으면 InvalidEntityId)
        EntityId Pick(const World& world,
                      const Camera& camera,
                      const SkinnedMeshRegistry* skinnedRegistry,
                      float u,
                      float v) const;
    };
}