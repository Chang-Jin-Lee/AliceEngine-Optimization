#pragma once

#include "Runtime/ECS/Components/ComponentStorage.h"
#include "Runtime/ECS/Entity.h"
#include "EcsVsOop/Workload.h"

#include <cstddef>

namespace Alice::Bench
{
    /// 스파스셋 백엔드.
    /// 엔진이 실제로 쓰는 ComponentStorage<T>를 그대로 인스턴스화한다.
    class EcsBackend
    {
    public:
        void Add(EntityId id)
        {
            m_transforms.Add(id, BenchTransform{});
            m_decals.Add(id, BenchDecal{});
            m_animations.Add(id, BenchAnimation{});
        }

        void Remove(EntityId id)
        {
            m_transforms.Remove(id);
            m_decals.Remove(id);
            m_animations.Remove(id);
        }

        /// dense 배열을 인덱스 순으로 훑는다. 세 컴포넌트를 각각 한 번씩 순회한다.
        void Step(float dt)
        {
            for (auto&& entry : m_transforms.GetView())
                StepTransform(entry.second, dt);
            for (auto&& entry : m_decals.GetView())
                StepDecal(entry.second, dt);
            for (auto&& entry : m_animations.GetView())
                StepAnimation(entry.second, dt);
        }

        const BenchTransform* GetTransform(EntityId id) const { return m_transforms.Get(id); }
        std::size_t Size() const { return m_transforms.Size(); }

        void Clear()
        {
            m_transforms.Clear();
            m_decals.Clear();
            m_animations.Clear();
        }

    private:
        ComponentStorage<BenchTransform> m_transforms;
        ComponentStorage<BenchDecal>     m_decals;
        ComponentStorage<BenchAnimation> m_animations;
    };
}
