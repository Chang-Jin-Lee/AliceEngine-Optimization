#pragma once

#include "Runtime/ECS/Entity.h"
#include "EcsVsOop/Workload.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Alice::Bench
{
    /// 컴포넌트 종류 태그.
    /// dynamic_cast는 RTTI 탐색 비용이 커서 OOP 쪽을 실제보다 불리하게 만든다.
    /// 상용 엔진도 타입 ID 비교를 쓰므로 여기서도 그렇게 한다.
    enum class BehaviourKind : std::uint8_t { Transform, Decal, Animation };

    class Behaviour
    {
    public:
        virtual ~Behaviour() = default;
        virtual BehaviourKind Kind() const noexcept = 0;
        virtual void Update(float dt) noexcept = 0;
    };

    class TransformBehaviour final : public Behaviour
    {
    public:
        BenchTransform data{};
        BehaviourKind Kind() const noexcept override { return BehaviourKind::Transform; }
        void Update(float dt) noexcept override { StepTransform(data, dt); }
    };

    class DecalBehaviour final : public Behaviour
    {
    public:
        BenchDecal data{};
        BehaviourKind Kind() const noexcept override { return BehaviourKind::Decal; }
        void Update(float dt) noexcept override { StepDecal(data, dt); }
    };

    class AnimationBehaviour final : public Behaviour
    {
    public:
        BenchAnimation data{};
        BehaviourKind Kind() const noexcept override { return BehaviourKind::Animation; }
        void Update(float dt) noexcept override { StepAnimation(data, dt); }
    };

    /// 객체 하나가 컴포넌트 소유권을 개별 힙 할당으로 들고 있는 형태.
    class GameObject
    {
    public:
        explicit GameObject(EntityId id) noexcept : m_id(id) {}

        EntityId Id() const noexcept { return m_id; }

        template <typename T>
        void Attach()
        {
            m_behaviours.push_back(std::make_unique<T>());
        }

        /// 종류 태그를 선형 비교한다. Unity의 GetComponent와 같은 접근이다.
        Behaviour* Find(BehaviourKind kind) const noexcept
        {
            for (const auto& b : m_behaviours)
            {
                if (b->Kind() == kind)
                    return b.get();
            }
            return nullptr;
        }

        void Update(float dt) noexcept
        {
            for (const auto& b : m_behaviours)
                b->Update(dt);
        }

    private:
        EntityId m_id;
        std::vector<std::unique_ptr<Behaviour>> m_behaviours;
    };

    class OopBackend
    {
    public:
        void Add(EntityId id)
        {
            auto object = std::make_unique<GameObject>(id);
            object->Attach<TransformBehaviour>();
            object->Attach<DecalBehaviour>();
            object->Attach<AnimationBehaviour>();

            m_index[id] = m_objects.size();
            m_objects.push_back(std::move(object));
        }

        /// swap-and-pop. ECS와 같은 O(1)로 맞춰 복잡도 차이를 제거한다.
        void Remove(EntityId id)
        {
            const auto it = m_index.find(id);
            if (it == m_index.end())
                return;

            const std::size_t removed = it->second;
            const std::size_t last = m_objects.size() - 1;

            if (removed != last)
            {
                m_objects[removed] = std::move(m_objects[last]);
                m_index[m_objects[removed]->Id()] = removed;
            }

            m_objects.pop_back();
            m_index.erase(it);
        }

        void Step(float dt)
        {
            for (const auto& object : m_objects)
                object->Update(dt);
        }

        const BenchTransform* GetTransform(EntityId id) const
        {
            const auto it = m_index.find(id);
            if (it == m_index.end())
                return nullptr;

            Behaviour* found = m_objects[it->second]->Find(BehaviourKind::Transform);
            if (found == nullptr)
                return nullptr;
            return &static_cast<TransformBehaviour*>(found)->data;
        }

        std::size_t Size() const { return m_objects.size(); }

        void Clear()
        {
            m_objects.clear();
            m_index.clear();
        }

    private:
        std::vector<std::unique_ptr<GameObject>> m_objects;
        std::unordered_map<EntityId, std::size_t> m_index;
    };
}
