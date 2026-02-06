#include "DecalDemoController.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Rendering/Components/DecalComponent.h"
#include "Runtime/ECS/World.h"

#include <algorithm>
#include <cmath>

namespace Alice
{
    REGISTER_SCRIPT(DecalDemoController);

    void DecalDemoController::Start()
    {
        m_timeSec = 0.0f;

        if (!GetComponent<DecalComponent>())
        {
            AddComponent<DecalComponent>();
        }
    }

    void DecalDemoController::Update(float deltaTime)
    {
        m_timeSec += deltaTime;

        if (auto* t = transform())
        {
            t->rotation.y += Get_m_spinSpeed() * deltaTime;
        }

        auto* decal = GetComponent<DecalComponent>();
        if (!decal)
            return;

        if (std::abs(Get_m_uvScrollSpeed()) > 0.0f)
        {
            decal->uvOffset.x += Get_m_uvScrollSpeed() * deltaTime;
            float wrapped = std::fmod(decal->uvOffset.x, 1.0f);
            if (wrapped < 0.0f)
                wrapped += 1.0f;
            decal->uvOffset.x = wrapped;
        }

        if (Get_m_pulseOpacity())
        {
            const float t = 0.5f * (std::sin(m_timeSec * Get_m_opacityPulseSpeed()) + 1.0f);
            const float minOpacity = std::clamp(Get_m_opacityMin(), 0.0f, 1.0f);
            decal->opacity = minOpacity + (1.0f - minOpacity) * t;
        }
    }
}
