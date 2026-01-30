#pragma once

#include <string>
#include <vector>

#include "Core/Entity.h"
#include "Core/ScriptAPI.h"
#include "Core/World.h"
#include "Core/Logger.h"
#include "Rendering/SkinnedMeshRegistry.h"

// FbxModel은 전역 네임스페이스에 있습니다.
#include "3Dmodel/FbxModel.h"
#include "Components/AnimBlueprintComponent.h"
#include "Components/SocketComponent.h"
#include "Components/SocketPoseOutputComponent.h"

namespace Alice
{
    /// Unity 느낌의 엔티티 래퍼 (스크립트에서 GetComponent<> 사용)
    class GameObject
    {
    public:
        GameObject() = default;
        GameObject(World* world, EntityId id, ScriptServices* services)
            : m_world(world), m_id(id), m_services(services)
        {
            // SlotMap: 생성 시점의 generation 저장
            if (m_world && m_id != InvalidEntityId)
            {
                m_generation = m_world->GetEntityGeneration(m_id);
            }
        }

        /// SlotMap 기반 유효성 검사: generation이 일치하는지 확인
        bool IsValid() const
        {
            if (!m_world || m_id == InvalidEntityId)
                return false;

            // World에서 현재 generation과 저장된 generation 비교
            return m_world->IsEntityValid(m_id, m_generation);
        }

        EntityId id() const { return m_id; }

        template <typename T>
        T* GetComponent() const
        {
            // SlotMap: 유효성 체크 후 컴포넌트 접근
            if (!IsValid())
                return nullptr;

            return m_world->GetComponent<T>(m_id);
        }

        /// 현재 게임오브젝트에 T 타입 컴포넌트를 추가합니다. (World::AddComponent 래핑)
        /// - 유효하지 않은 GameObject(IsValid()==false) 에서 호출 시 아무 것도 하지 않습니다.
        template <typename T, typename... Args>
        T& AddComponent(Args&&... args) const
        {
            if (!IsValid() || !m_world)
            {
                ALICE_LOG_WARN("GameObject::AddComponent called on invalid GameObject.");
                // 크래시를 막기 위해 static dummy 반환 (사용 전 반드시 IsValid() 체크 권장)
                static T dummy{};
                return dummy;
            }
            return m_world->AddComponent<T>(m_id, std::forward<Args>(args)...);
        }

        /// 현재 게임오브젝트에서 T 타입 컴포넌트를 제거합니다.
        template <typename T>
        void RemoveComponent() const
        {
            if (!IsValid() || !m_world)
                return;
            m_world->RemoveComponent<T>(m_id);
        }

        /// 현재 게임오브젝트에 붙어있는 T 타입 컴포넌트들을 모두 반환합니다.
        template <typename T>
        std::vector<T*> GetComponents() const
        {
            if (!IsValid() || !m_world)
                return {};
            return m_world->GetComponents<T>(m_id);
        }

        // 게임 오브젝트를 즉시 파괴합니다.
        void destroy()
        {
            if (!IsValid())
                return;

            m_world->DestroyEntity(m_id);
            m_id = InvalidEntityId; // 파괴 후 무효화
            m_generation = 0; // generation도 초기화
        }

        // 게임 오브젝트를 지연 파괴합니다 (delay 초 후에 파괴)
        void destroy(float delay)
        {
            if (!IsValid())
                return;

            if (delay <= 0.0f)
            {
                // 지연 시간이 0 이하면 즉시 파괴
                destroy();
                return;
            }

            m_world->ScheduleDelayedDestruction(m_id, delay);
            // 지연 파괴는 예약만 하고, 실제 파괴는 UpdateDelayedDestruction에서 수행
            // 따라서 m_id는 아직 유효하지만, 파괴 예약이 되어있음
        }
        /// Animator 핸들(엔티티 단위)
        class Animator
        {
        public:
            Animator() = default;
            Animator(World* world, EntityId id, ScriptServices* services)
                : m_world(world), m_id(id), m_services(services)
            {
                // SlotMap: 생성 시점의 generation 저장
                if (m_world && m_id != InvalidEntityId)
                {
                    m_generation = m_world->GetEntityGeneration(m_id);
                }
            }

            bool IsValid() const
            {
                // SlotMap: 먼저 엔티티 유효성 체크
                if (!m_world || m_id == InvalidEntityId)
                    return false;

                if (!m_world->IsEntityValid(m_id, m_generation))
                    return false;

                if (!m_services || !m_services->skinnedRegistry)
                    return false;

                auto* skinned = m_world->GetComponent<SkinnedMeshComponent>(m_id);
                if (!skinned || skinned->meshAssetPath.empty())
                    return false;

                auto mesh = m_services->skinnedRegistry->Find(skinned->meshAssetPath);
                if (!mesh || !mesh->sourceModel)
                    return false;

                return !mesh->sourceModel->GetAnimationNames().empty();
            }

            int ClipCount() const
            {
                auto model = SourceModel();
                return model ? (int)model->GetAnimationNames().size() : 0;
            }

            const char* ClipName(int idx) const
            {
                auto model = SourceModel();
                if (!model) return "";
                const auto& n = model->GetAnimationNames();
                if (idx < 0 || idx >= (int)n.size()) return "";
                return n[(size_t)idx].c_str();
            }

            int GetClip() const
            {
                auto* a = AnimComp(false);
                return a ? a->clipIndex : 0;
            }

            void SetClip(int idx)
            {
                auto* a = AnimComp(true);
                if (!a) return;

                const int count = ClipCount();
                if (count <= 0) return;
                if (idx < 0) idx = 0;
                if (idx >= count) idx = count - 1;
                a->clipIndex = idx;
                a->timeSec = 0.0;
            }

            bool IsPlaying() const
            {
                auto* a = AnimComp(false);
                return a ? a->playing : false;
            }

            void Play()
            {
                auto* a = AnimComp(true);
                if (a) a->playing = true;
            }

            void Play(int idx, bool forceRestart = false)
            {
                auto* a = AnimComp(true);
                if (!a) return;

                // 이미 재생 중이고, 요청한 클립이 현재 클립과 같으며, 강제 재시작이 아니라면 아무것도 하지 않고 리턴 (애니메이션 끊김 방지)
                if (a->playing && a->clipIndex == idx && !forceRestart) return;
                SetClip(idx);
                a->playing = true;
            }

            void Stop()
            {
                auto* a = AnimComp(true);
                if (!a) return;
                a->playing = false;
                a->timeSec = 0.0;
            }

            float GetSpeed() const
            {
                auto* a = AnimComp(false);
                return a ? a->speed : 1.0f;
            }

            void SetSpeed(float s)
            {
                auto* a = AnimComp(true);
                if (!a) return;
                if (s < 0.0f) s = 0.0f;
                a->speed = s;
            }

            double GetTime() const
            {
                auto* a = AnimComp(false);
                return a ? a->timeSec : 0.0;
            }

            void SetTime(double t)
            {
                auto* a = AnimComp(true);
                if (!a) return;
                a->timeSec = (t < 0.0) ? 0.0 : t;
            }

            double ClipDurationSec(int idx) const
            {
                auto model = SourceModel();
                return model ? model->GetClipDurationSec(idx) : 0.0;
            }

            int BoneCount() const
            {
                auto model = SourceModel();
                return model ? (int)model->GetBoneNames().size() : 0;
            }

            const char* BoneName(int idx) const
            {
                auto model = SourceModel();
                if (!model) return "";
                const auto& names = model->GetBoneNames();
                if (idx < 0 || idx >= (int)names.size()) return "";
                return names[(size_t)idx].c_str();
            }

        private:
            std::shared_ptr<FbxModel> SourceModel() const
            {
                // SlotMap: 유효성 체크
                if (!m_world || m_id == InvalidEntityId)
                    return nullptr;

                if (!m_world->IsEntityValid(m_id, m_generation))
                    return nullptr;

                if (!m_services || !m_services->skinnedRegistry)
                    return nullptr;
                auto* skinned = m_world->GetComponent<SkinnedMeshComponent>(m_id);
                if (!skinned) return nullptr;
                auto mesh = m_services->skinnedRegistry->Find(skinned->meshAssetPath);
                if (!mesh) return nullptr;
                return mesh->sourceModel;
            }

            SkinnedAnimationComponent* AnimComp(bool create) const
            {
                // SlotMap: 유효성 체크
                if (!m_world || m_id == InvalidEntityId)
                    return nullptr;

                if (!m_world->IsEntityValid(m_id, m_generation))
                    return nullptr;

                if (auto* a = m_world->GetComponent<SkinnedAnimationComponent>(m_id))
                    return a;
                return create ? &m_world->AddComponent<SkinnedAnimationComponent>(m_id) : nullptr;
            }

            World* m_world = nullptr;
            EntityId m_id = InvalidEntityId;
            std::uint32_t m_generation = 0; // SlotMap: Animator도 generation 저장
            ScriptServices* m_services = nullptr;
        };

        Animator GetAnimator() const { return Animator(m_world, m_id, m_services); }

        /// AnimBlueprint 핸들 (FSM/파라미터/소켓)
        class AnimGraph
        {
        public:
            AnimGraph() = default;
            AnimGraph(World* world, EntityId id, ScriptServices* services)
                : m_world(world), m_id(id), m_services(services)
            {
                if (m_world && m_id != InvalidEntityId)
                {
                    m_generation = m_world->GetEntityGeneration(m_id);
                }
            }

            bool IsValid() const
            {
                if (!m_world || m_id == InvalidEntityId)
                    return false;
                if (!m_world->IsEntityValid(m_id, m_generation))
                    return false;
                return (m_world->GetComponent<AnimBlueprintComponent>(m_id) != nullptr);
            }

            void SetBlueprint(const char* path) const
            {
                auto* ab = GetOrCreate();
                if (!ab) return;
                ab->blueprintPath = (path ? path : "");
            }

            void SetBool(const char* name, bool v) const
            {
                auto* ab = GetOrCreate();
                if (!ab || !name) return;
                auto& pv = ab->params[name];
                pv.type = AnimParamType::Bool;
                pv.b = v;
            }

            void SetInt(const char* name, int v) const
            {
                auto* ab = GetOrCreate();
                if (!ab || !name) return;
                auto& pv = ab->params[name];
                pv.type = AnimParamType::Int;
                pv.i = v;
            }

            void SetFloat(const char* name, float v) const
            {
                auto* ab = GetOrCreate();
                if (!ab || !name) return;
                auto& pv = ab->params[name];
                pv.type = AnimParamType::Float;
                pv.f = v;
            }

            void SetTrigger(const char* name) const
            {
                auto* ab = GetOrCreate();
                if (!ab || !name) return;
                auto& pv = ab->params[name];
                pv.type = AnimParamType::Trigger;
                pv.trigger = true;
            }

            bool TryGetSocketWorld(const char* name, DirectX::XMFLOAT4X4& outWorld) const
            {
                if (!IsValid() || !name) return false;
                if (auto* poses = m_world->GetComponent<SocketPoseOutputComponent>(m_id))
                {
                    for (const auto& p : poses->poses)
                    {
                        if (p.name == name)
                        {
                            outWorld = p.world;
                            return true;
                        }
                    }
                }
                auto* sc = m_world->GetComponent<SocketComponent>(m_id);
                if (!sc) return false;
                for (const auto& s : sc->sockets)
                {
                    if (s.name == name)
                    {
                        outWorld = s.world;
                        return true;
                    }
                }
                return false;
            }

        private:
            AnimBlueprintComponent* GetOrCreate() const
            {
                if (!m_world || m_id == InvalidEntityId) return nullptr;
                if (!m_world->IsEntityValid(m_id, m_generation)) return nullptr;
                if (auto* ab = m_world->GetComponent<AnimBlueprintComponent>(m_id))
                    return ab;
                return m_world ? &m_world->AddComponent<AnimBlueprintComponent>(m_id) : nullptr;
            }

            World* m_world = nullptr;
            EntityId m_id = InvalidEntityId;
            std::uint32_t m_generation = 0;
            ScriptServices* m_services = nullptr;
        };

        AnimGraph GetAnimGraph() const { return AnimGraph(m_world, m_id, m_services); }

        /// 씬에서 "첫번째 SkinnedMesh" 엔티티를 찾습니다. (캐릭터 1인 게임용 간단 유틸)
        GameObject FindFirstSkinnedMesh() const
        {
            if (!m_world)
                return {};

            for (const auto& [id, comp] : m_world->GetComponents<SkinnedMeshComponent>())
            {
                if (id == InvalidEntityId) continue;
                if (comp.meshAssetPath.empty()) continue;
                // GameObject 생성 시 자동으로 generation이 저장됨
                return GameObject(m_world, id, m_services);
            }
            return {};
        }

    private:
        World* m_world = nullptr;
        EntityId m_id = InvalidEntityId;
        std::uint32_t m_generation = 0; // SlotMap: 엔티티 생성 시점의 generation 저장
        ScriptServices* m_services = nullptr;
    };
}


