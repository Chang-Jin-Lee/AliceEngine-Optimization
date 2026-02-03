#include "Runtime/Gameplay/Combat/AttackDriverSystem.h"

#include <functional>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#include <assimp/scene.h>

#include "Runtime/ECS/World.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Importing/FbxModel.h"

namespace Alice
{
    namespace
    {
        std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value)
        {
            return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
        }

        std::uint64_t HashClip(const AttackDriverClip& clip, const std::string& resolvedName, float startTime, float endTime)
        {
            std::uint64_t h = 0;
            h = HashCombine(h, std::hash<std::string>{}(resolvedName));
            h = HashCombine(h, std::hash<float>{}(startTime));
            h = HashCombine(h, std::hash<float>{}(endTime));
            h = HashCombine(h, std::hash<bool>{}(clip.enabled));
            h = HashCombine(h, std::hash<int>{}(static_cast<int>(clip.type)));
            h = HashCombine(h, std::hash<int>{}(static_cast<int>(clip.source)));
            return h;
        }

        struct ClipTimeState
        {
            std::string clipName;
            float currTime = 0.0f;
            float prevTime = 0.0f;
            float duration = 0.0f;
            float speed = 0.0f;
            bool loop = false;
            bool validPrev = false;
        };

        bool TryParseIndex(const std::string& key, int& outIdx)
        {
            if (key.empty()) return false;
            for (char c : key)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                    return false;
            }
            outIdx = std::atoi(key.c_str());
            return true;
        }

        const aiAnimation* ResolveClip(const SkinnedMeshRegistry* registry,
                                        const SkinnedMeshComponent* skinned,
                                        const std::string& clipName)
        {
            if (!registry || !skinned || skinned->meshAssetPath.empty() || clipName.empty())
                return nullptr;

            auto mesh = registry->Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                return nullptr;

            const aiScene* scene = mesh->sourceModel->GetScenePtr();
            if (!scene)
                return nullptr;

            const auto& names = mesh->sourceModel->GetAnimationNames();
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (names[i] == clipName && i < scene->mNumAnimations)
                    return scene->mAnimations[i];
            }

            // Try animation name lookup
            for (unsigned i = 0; i < scene->mNumAnimations; ++i)
            {
                const aiAnimation* anim = scene->mAnimations[i];
                if (anim && anim->mName.length > 0 && clipName == anim->mName.C_Str())
                    return anim;
            }

            // Try numeric index
            int idx = -1;
            if (TryParseIndex(clipName, idx))
            {
                if (idx >= 0 && static_cast<unsigned>(idx) < scene->mNumAnimations)
                    return scene->mAnimations[idx];
            }

            return nullptr;
        }

        float GetClipDurationSec(const aiAnimation* anim)
        {
            if (!anim)
                return 0.0f;
            const double tps = (anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0;
            if (tps <= 0.0)
                return 0.0f;
            return static_cast<float>(anim->mDuration / tps);
        }

        float NormalizeTime(float timeSec, float durationSec)
        {
            if (durationSec <= 0.0f)
                return timeSec;
            float t = std::fmod(timeSec, durationSec);
            if (t < 0.0f)
                t += durationSec;
            return t;
        }

        float GetPrevTimeSec(AttackDriverClipHistory& history,
                             const std::string& clipName,
                             float currTime,
                             bool& outValid)
        {
            if (clipName.empty())
            {
                history.clipName.clear();
                history.prevTimeSec = 0.0f;
                history.valid = false;
                outValid = false;
                return currTime;
            }

            if (!history.valid || history.clipName != clipName)
            {
                history.clipName = clipName;
                history.prevTimeSec = currTime;
                history.valid = true;
                outValid = false;
                return currTime;
            }

            outValid = true;
            return history.prevTimeSec;
        }

        void CommitPrevTimeSec(AttackDriverClipHistory& history,
                               const std::string& clipName,
                               float currTime)
        {
            if (clipName.empty())
            {
                history.clipName.clear();
                history.prevTimeSec = 0.0f;
                history.valid = false;
                return;
            }

            history.clipName = clipName;
            history.prevTimeSec = currTime;
            history.valid = true;
        }

        void ResetHistory(AttackDriverClipHistory& history)
        {
            history.clipName.clear();
            history.prevTimeSec = 0.0f;
            history.valid = false;
        }

        void ResetDriverHistories(AttackDriverComponent& driver)
        {
            ResetHistory(driver.prevBaseA);
            ResetHistory(driver.prevBaseB);
            ResetHistory(driver.prevUpperA);
            ResetHistory(driver.prevUpperB);
            ResetHistory(driver.prevAdditive);
            ResetHistory(driver.prevSkinned);
        }

        bool IsForward(float speed, float prevTime, float currTime)
        {
            if (std::abs(speed) > 0.0001f)
                return speed >= 0.0f;
            return currTime >= prevTime;
        }

        bool IntervalOverlap(float a0, float a1, float b0, float b1)
        {
            const float minA = (a0 < a1) ? a0 : a1;
            const float maxA = (a0 < a1) ? a1 : a0;
            return !(maxA < b0 || minA > b1);
        }

        bool WindowIntersects(float prevTime,
                              float currTime,
                              float startTime,
                              float endTime,
                              float durationSec,
                              bool loop,
                              bool forward,
                              bool validPrev)
        {
            if (!validPrev)
                return (currTime >= startTime && currTime <= endTime);

            if (!loop || durationSec <= 0.0f)
                return IntervalOverlap(prevTime, currTime, startTime, endTime);

            if (forward)
            {
                if (prevTime <= currTime)
                    return IntervalOverlap(prevTime, currTime, startTime, endTime);
                // Wrapped forward: [prev -> end], [0 -> curr]
                return IntervalOverlap(prevTime, durationSec, startTime, endTime) ||
                       IntervalOverlap(0.0f, currTime, startTime, endTime);
            }

            // Backward playback
            if (prevTime >= currTime)
                return IntervalOverlap(currTime, prevTime, startTime, endTime);
            // Wrapped backward: [curr -> end], [0 -> prev]
            return IntervalOverlap(currTime, durationSec, startTime, endTime) ||
                   IntervalOverlap(0.0f, prevTime, startTime, endTime);
        }

        bool IsWindowActive(const ClipTimeState& state, float startTime, float endTime)
        {
            if (state.clipName.empty())
                return false;

            float start = std::max(0.0f, startTime);
            float end = std::max(0.0f, endTime);
            if (end < start)
                std::swap(start, end);

            if (state.duration > 0.0f)
            {
                start = std::clamp(start, 0.0f, state.duration);
                end = std::clamp(end, 0.0f, state.duration);
            }

            const bool forward = IsForward(state.speed, state.prevTime, state.currTime);
            return WindowIntersects(state.prevTime, state.currTime, start, end, state.duration, state.loop, forward, state.validPrev);
        }

        std::string ResolveClipName(const AttackDriverClip& clip, const AdvancedAnimationComponent& anim)
        {
            switch (clip.source)
            {
            case AttackDriverClipSource::BaseA:
                return anim.base.clipA;
            case AttackDriverClipSource::BaseB:
                return anim.base.clipB;
            case AttackDriverClipSource::UpperA:
                return anim.upper.clipA;
            case AttackDriverClipSource::UpperB:
                return anim.upper.clipB;
            case AttackDriverClipSource::Additive:
                return anim.additive.clip;
            case AttackDriverClipSource::Explicit:
            default:
                return clip.clipName;
            }
        }

        bool HasNotifyTag(const AdvancedAnimationComponent& anim, std::uint64_t ownerTag)
        {
            if (ownerTag == 0)
                return false;

            for (const auto& kv : anim.notifies)
            {
                for (const auto& notify : kv.second)
                {
                    if (notify.ownerTag == ownerTag)
                        return true;
                }
            }
            return false;
        }

        bool HasAnyEnabledAttackClip(const AttackDriverComponent& driver, const AdvancedAnimationComponent& anim)
        {
            for (const auto& clip : driver.clips)
            {
                if (!clip.enabled || clip.type != AttackDriverNotifyType::Attack)
                    continue;

                const std::string resolved = ResolveClipName(clip, anim);
                if (!resolved.empty())
                    return true;
            }
            return false;
        }

        void SanitizeTimes(const AttackDriverClip& clip, float& outStart, float& outEnd)
        {
            outStart = std::max(0.0f, clip.startTimeSec);
            outEnd = std::max(0.0f, clip.endTimeSec);
            if (outEnd < outStart)
                std::swap(outStart, outEnd);
        }

        std::uint64_t HashClipList(const std::vector<AttackDriverClip>& clips,
                                   const AdvancedAnimationComponent& anim,
                                   bool attackOnly)
        {
            std::uint64_t h = 0;
            for (const auto& clip : clips)
            {
                if (attackOnly && clip.type != AttackDriverNotifyType::Attack)
                    continue;

                const std::string resolved = ResolveClipName(clip, anim);
                float startTime = 0.0f;
                float endTime = 0.0f;
                SanitizeTimes(clip, startTime, endTime);
                h = HashCombine(h, HashClip(clip, resolved, startTime, endTime));
            }
            return h;
        }

        bool TryResolveSkinnedClipName(const SkinnedMeshRegistry* registry,
            const SkinnedMeshComponent* skinned,
            int clipIndex,
            std::string& outName)
        {
            if (!registry || !skinned || skinned->meshAssetPath.empty())
                return false;

            auto mesh = registry->Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                return false;

            if (clipIndex < 0)
                return false;

            const auto& names = mesh->sourceModel->GetAnimationNames();
            if (static_cast<size_t>(clipIndex) < names.size() && !names[static_cast<size_t>(clipIndex)].empty())
            {
                outName = names[static_cast<size_t>(clipIndex)];
                return true;
            }

            const aiScene* scene = mesh->sourceModel->GetScenePtr();
            if (scene && static_cast<unsigned>(clipIndex) < scene->mNumAnimations)
            {
                const aiAnimation* a = scene->mAnimations[clipIndex];
                if (a && a->mName.length > 0)
                {
                    outName = a->mName.C_Str();
                    return true;
                }
                outName = "Anim" + std::to_string(clipIndex);
                return true;
            }

            return false;
        }

        void ResetDriverState(AttackDriverComponent& driver)
        {
            driver.attackActive = false;
            driver.dodgeActive = false;
            driver.guardActive = false;
            driver.attackCancelable = true;
        }

        void ApplyHealthState(World& world, EntityId entityId, const AttackDriverComponent& driver)
        {
            auto* health = world.GetComponent<HealthComponent>(entityId);
            if (!health)
                return;

            health->dodgeActive = driver.dodgeActive;
            health->guardActive = driver.guardActive;
        }

        void ApplyWindowState(AttackDriverComponent& driver,
                              AttackDriverNotifyType type,
                              bool active,
                              bool canBeInterrupted)
        {
            switch (type)
            {
            case AttackDriverNotifyType::Dodge:
                driver.dodgeActive = driver.dodgeActive || active;
                break;
            case AttackDriverNotifyType::Guard:
                driver.guardActive = driver.guardActive || active;
                break;
            case AttackDriverNotifyType::Attack:
            default:
                driver.attackActive = driver.attackActive || active;
                if (active && !canBeInterrupted)
                    driver.attackCancelable = false;
                break;
            }
        }

        bool IsClipWindowActiveSkinned(const AttackDriverClip& clip,
                                       const ClipTimeState& state)
        {
            if (!clip.enabled)
                return false;

            const std::string targetName =
                (clip.source == AttackDriverClipSource::Explicit) ? clip.clipName : state.clipName;

            if (targetName.empty() || targetName != state.clipName)
                return false;

            return IsWindowActive(state, clip.startTimeSec, clip.endTimeSec);
        }

        bool IsClipWindowActive(const AttackDriverClip& clip,
                                const ClipTimeState& baseA,
                                const ClipTimeState& baseB,
                                const ClipTimeState& upperA,
                                const ClipTimeState& upperB,
                                const ClipTimeState& additive)
        {
            if (!clip.enabled)
                return false;

            if (clip.source == AttackDriverClipSource::Explicit)
            {
                const std::string& name = clip.clipName;
                if (name.empty())
                    return false;

                if (baseA.clipName == name && IsWindowActive(baseA, clip.startTimeSec, clip.endTimeSec))
                    return true;
                if (baseB.clipName == name && IsWindowActive(baseB, clip.startTimeSec, clip.endTimeSec))
                    return true;
                if (upperA.clipName == name && IsWindowActive(upperA, clip.startTimeSec, clip.endTimeSec))
                    return true;
                if (upperB.clipName == name && IsWindowActive(upperB, clip.startTimeSec, clip.endTimeSec))
                    return true;
                if (additive.clipName == name && IsWindowActive(additive, clip.startTimeSec, clip.endTimeSec))
                    return true;

                return false;
            }

            switch (clip.source)
            {
            case AttackDriverClipSource::BaseA: return IsWindowActive(baseA, clip.startTimeSec, clip.endTimeSec);
            case AttackDriverClipSource::BaseB: return IsWindowActive(baseB, clip.startTimeSec, clip.endTimeSec);
            case AttackDriverClipSource::UpperA: return IsWindowActive(upperA, clip.startTimeSec, clip.endTimeSec);
            case AttackDriverClipSource::UpperB: return IsWindowActive(upperB, clip.startTimeSec, clip.endTimeSec);
            case AttackDriverClipSource::Additive: return IsWindowActive(additive, clip.startTimeSec, clip.endTimeSec);
            case AttackDriverClipSource::Explicit:
            default:
                return false;
            }
        }

        EntityId ResolveTraceEntity(World& world, AttackDriverComponent& driver, EntityId self)
        {
            if (driver.traceGuid == 0)
            {
                driver.traceCached = InvalidEntityId;
                return self;
            }

            if (driver.traceCached != InvalidEntityId)
            {
                if (const auto* idc = world.GetComponent<IDComponent>(driver.traceCached))
                {
                    if (idc->guid == driver.traceGuid)
                        return driver.traceCached;
                }
                driver.traceCached = InvalidEntityId;
            }

            if (driver.traceGuid != 0)
            {
                EntityId resolved = world.FindEntityByGuid(driver.traceGuid);
                if (resolved != InvalidEntityId)
                {
                    driver.traceCached = resolved;
                    return resolved;
                }
            }

            return self;
        }

        void ActivateTrace(World& world, EntityId traceId)
        {
            auto* trace = world.GetComponent<WeaponTraceComponent>(traceId);
            if (!trace)
                return;

            trace->attackInstanceId++;
            trace->active = true;
            trace->hasPrevBasis = false;
            trace->hasPrevShapes = false;
            trace->prevCentersWS.clear();
            trace->prevRotsWS.clear();
            trace->hitVictims.clear();
            trace->lastAttackInstanceId = trace->attackInstanceId;
        }

        void DeactivateTrace(World& world, EntityId traceId)
        {
            auto* trace = world.GetComponent<WeaponTraceComponent>(traceId);
            if (!trace)
                return;

            trace->active = false;
        }

        void LogStateChange(EntityId entityId, const char* label, bool prevState, bool currState)
        {
            if (prevState == currState)
                return;

            ALICE_LOG_INFO("[AttackDriver] entity=%llu %s=%s",
                static_cast<unsigned long long>(entityId),
                label,
                currState ? "ON" : "OFF");
        }
    }

    void AttackDriverSystem::PreUpdate(World& world)
    {
        for (auto&& [entityId, driver] : world.GetComponents<AttackDriverComponent>())
        {
            auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
            if (!anim || !anim->enabled || !anim->playing)
                continue;

            if (driver.notifyTag == 0)
            {
                driver.notifyTag = static_cast<std::uint64_t>(entityId);
            }

            const std::uint64_t currentHash = HashClipList(driver.clips, *anim, true);
            const bool wantsNotifies = HasAnyEnabledAttackClip(driver, *anim);
            const bool missingNotifies = wantsNotifies && !HasNotifyTag(*anim, driver.notifyTag);
            const bool needsRebuild = missingNotifies || (currentHash != driver.registeredHash);

            if (!needsRebuild)
                continue;

            anim->RemoveNotifiesByTag(driver.notifyTag);

            bool registeredAny = false;
            const std::uint32_t gen = world.GetEntityGeneration(entityId);
            for (const auto& clip : driver.clips)
            {
                if (!clip.enabled || clip.type != AttackDriverNotifyType::Attack)
                    continue;

                const std::string resolvedName = ResolveClipName(clip, *anim);
                if (resolvedName.empty())
                    continue;

                float startTime = 0.0f;
                float endTime = 0.0f;
                SanitizeTimes(clip, startTime, endTime);
                registeredAny = true;

                anim->AddNotify(resolvedName, startTime, [entityId, gen, &world]() {
                    if (!world.IsEntityValid(entityId, gen))
                        return;
                    auto* driverComp = world.GetComponent<AttackDriverComponent>(entityId);
                    if (!driverComp)
                        return;
                    if (driverComp->cancelAttackRequested)
                        return;
                    EntityId traceId = ResolveTraceEntity(world, *driverComp, entityId);
                    ActivateTrace(world, traceId);
                }, driver.notifyTag);

                anim->AddNotify(resolvedName, endTime, [entityId, gen, &world]() {
                    if (!world.IsEntityValid(entityId, gen))
                        return;
                    auto* driverComp = world.GetComponent<AttackDriverComponent>(entityId);
                    if (!driverComp)
                        return;
                    EntityId traceId = ResolveTraceEntity(world, *driverComp, entityId);
                    DeactivateTrace(world, traceId);
                }, driver.notifyTag);
            }

            if (!registeredAny)
            {
                EntityId traceId = ResolveTraceEntity(world, driver, entityId);
                DeactivateTrace(world, traceId);
            }

            driver.registeredHash = currentHash;
        }
    }

    void AttackDriverSystem::PostUpdate(World& world)
    {
        for (auto&& [entityId, driver] : world.GetComponents<AttackDriverComponent>())
        {
            const bool prevAttack = driver.attackActive;
            const bool prevDodge = driver.dodgeActive;
            const bool prevGuard = driver.guardActive;
            auto LogChanges = [&]() {
                LogStateChange(entityId, "Attack", prevAttack, driver.attackActive);
                LogStateChange(entityId, "Dodge", prevDodge, driver.dodgeActive);
                LogStateChange(entityId, "Guard", prevGuard, driver.guardActive);
            };

            EntityId traceId = ResolveTraceEntity(world, driver, entityId);
            auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
            if (!anim)
            {
                ResetDriverState(driver);
                ResetHistory(driver.prevBaseA);
                ResetHistory(driver.prevBaseB);
                ResetHistory(driver.prevUpperA);
                ResetHistory(driver.prevUpperB);
                ResetHistory(driver.prevAdditive);

                auto* skinnedAnim = world.GetComponent<SkinnedAnimationComponent>(entityId);
                if (!skinnedAnim || !skinnedAnim->playing)
                {
                    ResetHistory(driver.prevSkinned);
                    ApplyHealthState(world, entityId, driver);
                    LogChanges();
                    DeactivateTrace(world, traceId);
                    continue;
                }

                const auto* skinnedMesh = world.GetComponent<SkinnedMeshComponent>(entityId);
                std::string currentClipName;
                if (!TryResolveSkinnedClipName(m_registry, skinnedMesh, skinnedAnim->clipIndex, currentClipName))
                {
                    ResetHistory(driver.prevSkinned);
                    ApplyHealthState(world, entityId, driver);
                    LogChanges();
                    DeactivateTrace(world, traceId);
                    continue;
                }

                float durationSec = 0.0f;
                if (m_registry && skinnedMesh && !skinnedMesh->meshAssetPath.empty())
                {
                    if (auto mesh = m_registry->Find(skinnedMesh->meshAssetPath))
                    {
                        if (mesh && mesh->sourceModel)
                            durationSec = static_cast<float>(mesh->sourceModel->GetClipDurationSec(skinnedAnim->clipIndex));
                    }
                }

                const float rawTimeSec = static_cast<float>(skinnedAnim->timeSec);
                const float currTimeSec = (durationSec > 0.0f) ? NormalizeTime(rawTimeSec, durationSec) : rawTimeSec;

                ClipTimeState skinnedState{};
                skinnedState.clipName = currentClipName;
                skinnedState.currTime = currTimeSec;
                skinnedState.speed = skinnedAnim->speed;
                skinnedState.loop = (durationSec > 0.0f);
                skinnedState.duration = durationSec;
                skinnedState.prevTime = GetPrevTimeSec(driver.prevSkinned, currentClipName, currTimeSec, skinnedState.validPrev);
                if (durationSec > 0.0f)
                    skinnedState.prevTime = NormalizeTime(skinnedState.prevTime, durationSec);

                for (const auto& clip : driver.clips)
                {
                    if (IsClipWindowActiveSkinned(clip, skinnedState))
                        ApplyWindowState(driver, clip.type, true, clip.canBeInterrupted);
                }

                CommitPrevTimeSec(driver.prevSkinned, currentClipName, currTimeSec);

                if (driver.cancelAttackRequested)
                {
                    if (driver.attackCancelable)
                        driver.attackActive = false;
                    else
                        driver.cancelAttackRequested = false;
                }

                if (!driver.attackActive)
                    driver.cancelAttackRequested = false;

                ApplyHealthState(world, entityId, driver);
                LogChanges();

                auto* trace = world.GetComponent<WeaponTraceComponent>(traceId);
                if (driver.attackActive)
                {
                    if (trace && !trace->active)
                        ActivateTrace(world, traceId);
                }
                else
                {
                    if (trace && trace->active)
                        DeactivateTrace(world, traceId);
                }
                continue;
            }

            if (!anim->enabled || !anim->playing)
            {
                ResetDriverState(driver);
                ResetDriverHistories(driver);
                ApplyHealthState(world, entityId, driver);
                LogChanges();
                DeactivateTrace(world, traceId);
                continue;
            }

            ResetDriverState(driver);
            ResetHistory(driver.prevSkinned);

            const auto* skinnedMesh = world.GetComponent<SkinnedMeshComponent>(entityId);
            auto BuildState = [&](const std::string& clipName,
                                  float currTime,
                                  float speed,
                                  bool loop,
                                  AttackDriverClipHistory& history) -> ClipTimeState
            {
                ClipTimeState state{};
                state.clipName = clipName;
                state.speed = speed;
                state.loop = loop;
                state.duration = GetClipDurationSec(ResolveClip(m_registry, skinnedMesh, clipName));

                if (state.duration > 0.0f)
                {
                    if (state.loop)
                        currTime = NormalizeTime(currTime, state.duration);
                    else
                        currTime = std::clamp(currTime, 0.0f, state.duration);
                }

                state.currTime = currTime;
                state.prevTime = GetPrevTimeSec(history, clipName, state.currTime, state.validPrev);

                if (state.duration > 0.0f)
                {
                    if (state.loop)
                        state.prevTime = NormalizeTime(state.prevTime, state.duration);
                    else
                        state.prevTime = std::clamp(state.prevTime, 0.0f, state.duration);
                }

                return state;
            };

            ClipTimeState baseA = BuildState(anim->base.clipA, anim->base.timeA, anim->base.speedA, anim->base.loopA, driver.prevBaseA);
            ClipTimeState baseB = BuildState(anim->base.clipB, anim->base.timeB, anim->base.speedB, anim->base.loopB, driver.prevBaseB);
            ClipTimeState upperA = BuildState(anim->upper.clipA, anim->upper.timeA, anim->upper.speedA, anim->upper.loopA, driver.prevUpperA);
            ClipTimeState upperB = BuildState(anim->upper.clipB, anim->upper.timeB, anim->upper.speedB, anim->upper.loopB, driver.prevUpperB);
            ClipTimeState additive = BuildState(anim->additive.clip, anim->additive.time, anim->additive.speed, anim->additive.loop, driver.prevAdditive);

            for (const auto& clip : driver.clips)
            {
                if (IsClipWindowActive(clip, baseA, baseB, upperA, upperB, additive))
                    ApplyWindowState(driver, clip.type, true, clip.canBeInterrupted);
            }

            CommitPrevTimeSec(driver.prevBaseA, baseA.clipName, baseA.currTime);
            CommitPrevTimeSec(driver.prevBaseB, baseB.clipName, baseB.currTime);
            CommitPrevTimeSec(driver.prevUpperA, upperA.clipName, upperA.currTime);
            CommitPrevTimeSec(driver.prevUpperB, upperB.clipName, upperB.currTime);
            CommitPrevTimeSec(driver.prevAdditive, additive.clipName, additive.currTime);

            if (driver.cancelAttackRequested)
            {
                if (driver.attackCancelable)
                    driver.attackActive = false;
                else
                    driver.cancelAttackRequested = false;
            }

            if (!driver.attackActive)
                driver.cancelAttackRequested = false;

            ApplyHealthState(world, entityId, driver);
            LogChanges();

            auto* trace = world.GetComponent<WeaponTraceComponent>(traceId);
            if (driver.attackActive)
            {
                if (trace && !trace->active)
                    ActivateTrace(world, traceId);
            }
            else
            {
                if (trace && trace->active)
                    DeactivateTrace(world, traceId);
            }
        }
    }
}
