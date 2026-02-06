#include "WeaponAnimationController.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(WeaponAnimationController);

    namespace
    {
        void CopyLayer(const AdvancedAnimLayer& src,
                       AdvancedAnimLayer& dst,
                       const std::string& prefixFrom,
                       const std::string& prefixTo)
        {
            auto remap = [&](const std::string& name) -> std::string
            {
                if (name.empty() || prefixFrom.empty())
                    return name;
                if (name.rfind(prefixFrom, 0) == 0)
                    return prefixTo + name.substr(prefixFrom.size());
                return name;
            };

            dst.enabled = src.enabled;
            dst.autoAdvance = src.autoAdvance;
            dst.clipA = remap(src.clipA);
            dst.clipB = remap(src.clipB);
            dst.timeA = src.timeA;
            dst.timeB = src.timeB;
            dst.speedA = src.speedA;
            dst.speedB = src.speedB;
            dst.loopA = src.loopA;
            dst.loopB = src.loopB;
            dst.blend01 = src.blend01;
            dst.layerAlpha = src.layerAlpha;
        }

        void CopyAdditive(const AdvancedAnimAdditive& src,
                          AdvancedAnimAdditive& dst,
                          const std::string& prefixFrom,
                          const std::string& prefixTo)
        {
            auto remap = [&](const std::string& name) -> std::string
            {
                if (name.empty() || prefixFrom.empty())
                    return name;
                if (name.rfind(prefixFrom, 0) == 0)
                    return prefixTo + name.substr(prefixFrom.size());
                return name;
            };

            dst.enabled = src.enabled;
            dst.autoAdvance = src.autoAdvance;
            dst.clip = remap(src.clip);
            dst.refClip = remap(src.refClip);
            dst.time = src.time;
            dst.speed = src.speed;
            dst.loop = src.loop;
            dst.alpha = src.alpha;
        }
    }

    void WeaponAnimationController::LateUpdate(float /*deltaTime*/)
    {
        if (!GetWorld())
            return;
        if (!Get_m_syncEnabled())
            return;

        auto* dstAnim = GetComponent<AdvancedAnimationComponent>();
        if (!dstAnim)
            return;

        const EntityId srcId = ResolveSourceEntity();
        if (srcId == InvalidEntityId)
            return;

        auto* srcAnim = GetWorld()->GetComponent<AdvancedAnimationComponent>(srcId);
        if (!srcAnim)
            return;

        CopyFromSource(*srcAnim, *dstAnim);
    }

    EntityId WeaponAnimationController::ResolveSourceEntity()
    {
        auto* world = GetWorld();
        if (!world)
            return InvalidEntityId;

        if (Get_m_sourceGuid() != 0)
        {
            EntityId id = world->FindEntityByGuid(Get_m_sourceGuid());
            if (id != InvalidEntityId)
            {
                m_cachedSourceId = id;
                m_loggedMissing = false;
                return id;
            }
        }

        // If cached id is still valid and name matches, use it.
        if (m_cachedSourceId != InvalidEntityId)
        {
            const std::string name = world->GetEntityName(m_cachedSourceId);
            if (!name.empty() && name == Get_m_sourceEntityName())
            {
                m_loggedMissing = false;
                return m_cachedSourceId;
            }
        }

        if (!Get_m_sourceEntityName().empty())
        {
            GameObject go = world->FindGameObject(Get_m_sourceEntityName());
            if (go.IsValid())
            {
                m_cachedSourceId = go.id();
                m_loggedMissing = false;
                return m_cachedSourceId;
            }
        }

        if (Get_m_logMissingOnce() && !m_loggedMissing)
        {
            ALICE_LOG_WARN("[WeaponAnimationController] Source entity not found. guid=%llu name=\"%s\"",
                static_cast<unsigned long long>(Get_m_sourceGuid()),
                Get_m_sourceEntityName().c_str());
            m_loggedMissing = true;
        }

        return InvalidEntityId;
    }

    void WeaponAnimationController::CopyFromSource(const AdvancedAnimationComponent& src,
                                                   AdvancedAnimationComponent& dst)
    {
        const std::string& prefixFrom = Get_m_sourceClipPrefix();
        const std::string& prefixTo = Get_m_targetClipPrefix();

        dst.playing = src.playing;
        CopyLayer(src.base, dst.base, prefixFrom, prefixTo);
        CopyLayer(src.upper, dst.upper, prefixFrom, prefixTo);
        CopyAdditive(src.additive, dst.additive, prefixFrom, prefixTo);
        dst.procedural = src.procedural;
        dst.aim = src.aim;
    }
}
