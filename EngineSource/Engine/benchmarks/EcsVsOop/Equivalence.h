#pragma once

#include "EcsVsOop/EcsBackend.h"
#include "EcsVsOop/OopBackend.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace Alice::Bench
{
    /// 두 백엔드에 같은 시나리오를 적용하고 최종 상태를 비교한다.
    /// 시나리오: 전부 추가 -> steps회 Step -> 3의 배수 ID 삭제 -> steps회 Step -> 남은 엔티티 전량 삭제
    ///
    /// dt 기본값이 0.05f인 이유: BenchDecal.lifetime은 1.0에서 시작해 0에서 클램프되고,
    /// BenchAnimation.timeSec은 10.0에서 랩어라운드한다. 이 함수는 dt를 총 steps*2회 적용하는데,
    /// dt=0.25f·steps=4(총 8회)면 lifetime이 정확히 1.0 - 2.0 = 0.0으로 포화돼, 한쪽 백엔드가 dt를
    /// 다르게 써도 양쪽 모두 0.0이 되어 오류를 검출하지 못한다. dt=0.05f면 8회 적용해도
    /// lifetime=0.6, timeSec=0.4로 세 컴포넌트 모두 선형(비포화) 구간에 머물러 어느 컴포넌트의
    /// dt 오류든 값 차이로 드러난다. 포화 구간 자체를 검증하려면 호출부에서 dt를 명시적으로 키운다.
    inline bool CheckEquivalence(std::size_t entityCount, int steps, float dt = 0.05f)
    {
        EcsBackend ecs;
        OopBackend oop;

        for (std::size_t i = 0; i < entityCount; ++i)
        {
            const EntityId id = static_cast<EntityId>(i + 1);
            ecs.Add(id);
            oop.Add(id);
        }

        for (int s = 0; s < steps; ++s)
        {
            ecs.Step(dt);
            oop.Step(dt);
        }

        std::vector<EntityId> alive;
        for (std::size_t i = 0; i < entityCount; ++i)
        {
            const EntityId id = static_cast<EntityId>(i + 1);
            if (id % 3 == 0)
            {
                ecs.Remove(id);
                oop.Remove(id);
            }
            else
            {
                alive.push_back(id);
            }
        }

        for (int s = 0; s < steps; ++s)
        {
            ecs.Step(dt);
            oop.Step(dt);
        }

        if (ecs.Size() != oop.Size())
        {
            std::printf("  [FAIL] 크기 불일치 ecs=%zu oop=%zu\n", ecs.Size(), oop.Size());
            return false;
        }

        for (const EntityId id : alive)
        {
            const BenchTransform* ta = ecs.GetTransform(id);
            const BenchTransform* tb = oop.GetTransform(id);
            if (ta == nullptr || tb == nullptr)
            {
                std::printf("  [FAIL] id=%u Transform 한쪽에만 존재\n", id);
                return false;
            }
            if (std::abs(ta->position.x - tb->position.x) > 1e-5f ||
                std::abs(ta->position.y - tb->position.y) > 1e-5f ||
                std::abs(ta->rotation.y - tb->rotation.y) > 1e-5f)
            {
                std::printf("  [FAIL] id=%u Transform 값 불일치 ecs=(%.6f,%.6f,%.6f) oop=(%.6f,%.6f,%.6f)\n",
                    id, ta->position.x, ta->position.y, ta->rotation.y,
                    tb->position.x, tb->position.y, tb->rotation.y);
                return false;
            }

            const BenchDecal* da = ecs.GetDecal(id);
            const BenchDecal* db = oop.GetDecal(id);
            if (da == nullptr || db == nullptr)
            {
                std::printf("  [FAIL] id=%u Decal 한쪽에만 존재\n", id);
                return false;
            }
            if (std::abs(da->lifetime - db->lifetime) > 1e-5f ||
                std::abs(da->fade - db->fade) > 1e-5f)
            {
                std::printf("  [FAIL] id=%u Decal 값 불일치 ecs=(%.6f,%.6f) oop=(%.6f,%.6f)\n",
                    id, da->lifetime, da->fade, db->lifetime, db->fade);
                return false;
            }

            const BenchAnimation* aa = ecs.GetAnimation(id);
            const BenchAnimation* ab = oop.GetAnimation(id);
            if (aa == nullptr || ab == nullptr)
            {
                std::printf("  [FAIL] id=%u Animation 한쪽에만 존재\n", id);
                return false;
            }
            if (std::abs(aa->timeSec - ab->timeSec) > 1e-5f)
            {
                std::printf("  [FAIL] id=%u Animation 값 불일치 ecs=%.6f oop=%.6f\n",
                    id, aa->timeSec, ab->timeSec);
                return false;
            }
        }

        // 삭제한 엔티티는 양쪽 모두 없어야 한다 (세 컴포넌트 각각 확인)
        for (std::size_t i = 0; i < entityCount; ++i)
        {
            const EntityId id = static_cast<EntityId>(i + 1);
            if (id % 3 != 0)
                continue;
            if (ecs.GetTransform(id) != nullptr || oop.GetTransform(id) != nullptr)
            {
                std::printf("  [FAIL] id=%u Transform 삭제됐어야 하는데 남아있다\n", id);
                return false;
            }
            if (ecs.GetDecal(id) != nullptr || oop.GetDecal(id) != nullptr)
            {
                std::printf("  [FAIL] id=%u Decal 삭제됐어야 하는데 남아있다\n", id);
                return false;
            }
            if (ecs.GetAnimation(id) != nullptr || oop.GetAnimation(id) != nullptr)
            {
                std::printf("  [FAIL] id=%u Animation 삭제됐어야 하는데 남아있다\n", id);
                return false;
            }
        }

        // 전량 소진: 살아남은 엔티티를 하나씩 지워 swap-and-pop의 no-swap(마지막 원소 삭제) 분기를
        // 양쪽 백엔드 모두에서 최소 1회 실행하도록 구조로 보장한다.
        for (const EntityId id : alive)
        {
            ecs.Remove(id);
            oop.Remove(id);
        }

        if (ecs.Size() != 0 || oop.Size() != 0)
        {
            std::printf("  [FAIL] 전량 소진 후 크기 불일치 ecs=%zu oop=%zu\n", ecs.Size(), oop.Size());
            return false;
        }

        for (std::size_t i = 0; i < entityCount; ++i)
        {
            const EntityId id = static_cast<EntityId>(i + 1);
            if (ecs.GetTransform(id) != nullptr || oop.GetTransform(id) != nullptr ||
                ecs.GetDecal(id) != nullptr || oop.GetDecal(id) != nullptr ||
                ecs.GetAnimation(id) != nullptr || oop.GetAnimation(id) != nullptr)
            {
                std::printf("  [FAIL] id=%u 전량 소진 후에도 남아있다\n", id);
                return false;
            }
        }

        return true;
    }
}
