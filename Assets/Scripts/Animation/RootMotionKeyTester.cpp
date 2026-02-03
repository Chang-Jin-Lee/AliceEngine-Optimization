#include "RootMotionKeyTester.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(RootMotionKeyTester);

    void RootMotionKeyTester::Start()
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        auto* anim = go.GetComponent<AdvancedAnimationComponent>();
        if (!anim)
            anim = &go.AddComponent<AdvancedAnimationComponent>();

        ALICE_LOG_INFO("[RootMotionKeyTester] Ready. Assign clip names for keys 1-5.");
    }

    void RootMotionKeyTester::Update(float /*deltaTime*/)
    {
        auto* input = Input();
        if (!input)
            return;

        if (input->GetKeyDown(KeyCode::Alpha1)) ApplyKey(1);
        if (input->GetKeyDown(KeyCode::Alpha2)) ApplyKey(2);
        if (input->GetKeyDown(KeyCode::Alpha3)) ApplyKey(3);
        if (input->GetKeyDown(KeyCode::Alpha4)) ApplyKey(4);
        if (input->GetKeyDown(KeyCode::Alpha5)) ApplyKey(5);
    }

    void RootMotionKeyTester::ApplyKey(int keyIndex)
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        auto* anim = go.GetComponent<AdvancedAnimationComponent>();
        if (!anim)
            anim = &go.AddComponent<AdvancedAnimationComponent>();

        const std::string clip = [&]() -> std::string
        {
            switch (keyIndex)
            {
            case 1: return Get_m_clipKey1();
            case 2: return Get_m_clipKey2();
            case 3: return Get_m_clipKey3();
            case 4: return Get_m_clipKey4();
            case 5: return Get_m_clipKey5();
            default: return "";
            }
        }();

        if (clip.empty())
        {
            ALICE_LOG_WARN("[RootMotionKeyTester] Key %d has empty clip name.", keyIndex);
            return;
        }

        anim->enabled = true;
        anim->playing = true;

        anim->base.enabled = true;
        anim->base.autoAdvance = true;
        anim->base.clipA = clip;
        anim->base.clipB = clip;
        anim->base.blend01 = 0.0f;
        anim->base.loopA = Get_m_loop();
        anim->base.loopB = Get_m_loop();
        anim->base.speedA = Get_m_playSpeed();
        anim->base.speedB = Get_m_playSpeed();

        if (Get_m_forceRestartOnKey())
        {
            anim->base.timeA = 0.0f;
            anim->base.timeB = 0.0f;
        }

        if (Get_m_forceDisableUpperAdditive())
        {
            anim->upper.enabled = false;
            anim->additive.enabled = false;
        }
    }
}
