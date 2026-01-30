#include "MaterialAlphaExample.h"

#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Components/MaterialComponent.h"
#include "Components/SkinnedMeshComponent.h"

#include <algorithm>

namespace Alice
{
    REGISTER_SCRIPT(MaterialAlphaExample);

    void MaterialAlphaExample::Start()
    {
        // SkinnedMesh 확인 (경고만 출력)
		if (!GetComponent<SkinnedMeshComponent>())
		{
			ALICE_LOG_WARN("[MaterialAlphaExample] SkinnedMeshComponent not found on this GameObject.");
		}

        // MaterialComponent 없으면 기본값으로 추가
        if (!GetComponent<MaterialComponent>())
        {
            AddComponent<MaterialComponent>();
            ALICE_LOG_INFO("[MaterialAlphaExample] MaterialComponent added to GameObject.");
        }

        ALICE_LOG_INFO("[MaterialAlphaExample] Controls:");
        ALICE_LOG_INFO("  - 1/2/3/4: Color preset (Red/Green/Blue/White)");
        ALICE_LOG_INFO("  - Q/E: Alpha -/+");
        ALICE_LOG_INFO("  - Z/X: Roughness -/+");
        ALICE_LOG_INFO("  - C/V: Metalness -/+");
        ALICE_LOG_INFO("  - T: Toggle Transparent");
        ALICE_LOG_INFO("  - R: Reset (alpha/roughness/metalness)");
    }

    void MaterialAlphaExample::Update(float /*deltaTime*/)
    {
        auto* input = Input();
        if (!input)
            return;

        auto* mat = GetComponent<MaterialComponent>();
        if (!mat)
            return;

        bool changed = false;
        const float alphaStep = Get_m_alphaStep();
        const float valueStep = Get_m_valueStep();

        if (input->GetKeyDown(KeyCode::Alpha1)) { mat->Set_color({ 1.0f, 0.0f, 0.0f }); changed = true; }
        if (input->GetKeyDown(KeyCode::Alpha1)) { mat->Set_color({ 1.0f, 0.0f, 0.0f }); changed = true; }
        if (input->GetKeyDown(KeyCode::Alpha2)) { mat->Set_color({ 0.0f, 1.0f, 0.0f }); changed = true; }
        if (input->GetKeyDown(KeyCode::Alpha3)) { mat->Set_color({ 0.0f, 0.0f, 1.0f }); changed = true; }
        if (input->GetKeyDown(KeyCode::Alpha4)) { mat->Set_color({ 1.0f, 1.0f, 1.0f }); changed = true; }

        if (input->GetKeyDown(KeyCode::Q))
        {
            mat->Set_alpha(std::clamp(mat->Get_alpha() - alphaStep, 0.0f, 1.0f));
            changed = true;
        }
        if (input->GetKeyDown(KeyCode::E))
        {
            mat->Set_alpha(std::clamp(mat->Get_alpha() + alphaStep, 0.0f, 1.0f));
            changed = true;
        }

        if (input->GetKeyDown(KeyCode::Z))
        {
            mat->Set_roughness(std::clamp(mat->Get_roughness() - valueStep, 0.0f, 1.0f));
            changed = true;
        }
        if (input->GetKeyDown(KeyCode::X))
        {
            mat->Set_roughness(std::clamp(mat->Get_roughness() + valueStep, 0.0f, 1.0f));
            changed = true;
        }

        if (input->GetKeyDown(KeyCode::C))
        {
            mat->Set_metalness(std::clamp(mat->Get_metalness() - valueStep, 0.0f, 1.0f));
            changed = true;
        }
        if (input->GetKeyDown(KeyCode::V))
        {
            mat->Set_metalness(std::clamp(mat->Get_metalness() + valueStep, 0.0f, 1.0f));
            changed = true;
        }

        if (input->GetKeyDown(KeyCode::T))
        {
            mat->Set_transparent(!mat->Get_transparent());
            changed = true;
        }

        if (input->GetKeyDown(KeyCode::R))
        {
            mat->Set_alpha(1.0f);
            mat->Set_roughness(0.5f);
            mat->Set_metalness(0.0f);
            changed = true;
        }

        if (changed && Get_m_autoTransparent())
        {
            if (mat->Get_alpha() < 0.999f)
            {
                mat->Set_transparent(true);
            }
        }
    }
}
