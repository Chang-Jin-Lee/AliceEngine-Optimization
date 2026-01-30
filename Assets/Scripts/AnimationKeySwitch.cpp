#include "AnimationKeySwitch.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(AnimationKeySwitch);

    void AnimationKeySwitch::Start()
    {
        auto go = gameObject();
        auto anim = go.GetAnimator();
        if (!anim.IsValid())
        {
            ALICE_LOG_WARN("[AnimationKeySwitch] Animator not valid. (Need SkinnedMesh + animations)");
            return;
        }

        ALICE_LOG_INFO("[AnimationKeySwitch] Ready. clips=%d (press 1/2/3)", anim.ClipCount());
        for (int i = 0; i < anim.ClipCount(); ++i)
            ALICE_LOG_INFO("  - [%d] %s", i, anim.ClipName(i));

        anim.Play();

        // 노티파이 타임 초기화
        m_prevTime = anim.GetTime();
    }

    void AnimationKeySwitch::Update(float /*deltaTime*/)
    {
        auto* input = Input();
        if (!input)
            return;

        auto go = gameObject();
        auto anim = go.GetAnimator();
        if (!anim.IsValid())
            return;

        if (input->GetKeyDown(KeyCode::Alpha1)) { anim.SetClip(0); anim.Play(); }
        if (input->GetKeyDown(KeyCode::Alpha2)) { anim.SetClip(1); anim.Play(); }
        if (input->GetKeyDown(KeyCode::Alpha3)) { anim.SetClip(2); anim.Play(); }

        // 몽타주 느낌: 특정 타임에 특정 함수를 호출
        UpdateNotifies();
    }

    void AnimationKeySwitch::UpdateNotifies()
    {
        auto go = gameObject();
        auto anim = go.GetAnimator();
        if (!anim.IsValid())
            return;

        const int clip = anim.GetClip();
        const double t = anim.GetTime();
        const double dt = t - m_prevTime;

        // 클립이 바뀌거나, 시간이 되감겼다면(루프/리셋) 기준을 리셋
        if (dt < 0.0)
        {
            m_prevTime = t;
            return;
        }

        // 아주 단순 예시:
        // - clip 0: 0.20초에 발소리, 0.55초에 공격 히트
        if (clip == 0)
        {
            if (m_prevTime < 0.20 && t >= 0.20) OnFootstep();
            if (m_prevTime < 0.55 && t >= 0.55) OnHit();
        }

        m_prevTime = t;
    }

    void AnimationKeySwitch::OnFootstep()
    {
        ALICE_LOG_INFO("[AnimationKeySwitch] Notify: Footstep");
    }

    void AnimationKeySwitch::OnHit()
    {
        ALICE_LOG_INFO("[AnimationKeySwitch] Notify: Hit");
    }
}






