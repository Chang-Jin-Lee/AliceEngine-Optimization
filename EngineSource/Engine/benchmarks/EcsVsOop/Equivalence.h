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
    /// 시나리오: 전부 추가 -> steps회 Step -> 3의 배수 ID 삭제 -> steps회 Step
    inline bool CheckEquivalence(std::size_t entityCount, int steps)
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
            ecs.Step(0.25f);
            oop.Step(0.25f);
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
            ecs.Step(0.25f);
            oop.Step(0.25f);
        }

        if (ecs.Size() != oop.Size())
        {
            std::printf("  [FAIL] 크기 불일치 ecs=%zu oop=%zu\n", ecs.Size(), oop.Size());
            return false;
        }

        for (const EntityId id : alive)
        {
            const BenchTransform* a = ecs.GetTransform(id);
            const BenchTransform* b = oop.GetTransform(id);
            if (a == nullptr || b == nullptr)
            {
                std::printf("  [FAIL] id=%u 한쪽에만 존재\n", id);
                return false;
            }
            if (std::abs(a->position.x - b->position.x) > 1e-5f ||
                std::abs(a->position.y - b->position.y) > 1e-5f ||
                std::abs(a->rotation.y - b->rotation.y) > 1e-5f)
            {
                std::printf("  [FAIL] id=%u 값 불일치 ecs=(%.6f,%.6f,%.6f) oop=(%.6f,%.6f,%.6f)\n",
                    id, a->position.x, a->position.y, a->rotation.y,
                    b->position.x, b->position.y, b->rotation.y);
                return false;
            }
        }

        // 삭제한 엔티티는 양쪽 모두 없어야 한다
        for (std::size_t i = 0; i < entityCount; ++i)
        {
            const EntityId id = static_cast<EntityId>(i + 1);
            if (id % 3 != 0)
                continue;
            if (ecs.GetTransform(id) != nullptr || oop.GetTransform(id) != nullptr)
            {
                std::printf("  [FAIL] id=%u 삭제됐어야 하는데 남아있다\n", id);
                return false;
            }
        }

        return true;
    }
}
