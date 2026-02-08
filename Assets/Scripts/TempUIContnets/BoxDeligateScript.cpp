#include "BoxDeligateScript.h"
#include <algorithm>
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Foundation/Delegate.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Input/Input.h"

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(BoxDeligateScript);

    void BoxDeligateScript::Start()
    {
        // 모든 델리게이트에 초기값 전달 (바인딩되기 전에도 초기값 설정)
        // 바인딩된 델리게이트가 있으면 초기값을 받을 수 있음
        if (OnBossHPChanged.IsBound())
        {
            OnBossHPChanged.Execute(BossHP_Value);
        }
        if (OnPoiseHPChanged.IsBound())
        {
            OnPoiseHPChanged.Execute(PoiseHP_Value);
        }
        if (OnCharacterHPChanged.IsBound())
        {
            OnCharacterHPChanged.Execute(CharacterHP_Value);
        }
        if (OnWeaponHPChanged.IsBound())
        {
            OnWeaponHPChanged.Execute(WeaponHP_Value);
        }
        if (OnTextValueChanged.IsBound())
        {
            OnTextValueChanged.Execute(TextValue);
        }
    }

    void BoxDeligateScript::Update(float deltaTime)
    {
        auto* input = Input();
        if (!input)
            return;

        // 키 입력 처리
        auto toKey = [](int v) { return static_cast<KeyCode>(v); };

        // Alpha1: BossHP 감소
        if (input->GetKeyDown(toKey(Get_keyBossHP())))
        {
            BossHP_Value = std::max(0.0f, BossHP_Value - Get_SubHP());
            if (OnBossHPChanged.IsBound())
            {
                OnBossHPChanged.Execute(BossHP_Value);
            }
            ALICE_LOG_INFO("[BoxDeligateScript] BossHP: %f", BossHP_Value);
        }

        // Alpha2: PoiseHP 감소
        if (input->GetKeyDown(toKey(Get_keyPoiseHP())))
        {
            PoiseHP_Value = std::max(0.0f, PoiseHP_Value - Get_SubHP());
            if (OnPoiseHPChanged.IsBound())
            {
                OnPoiseHPChanged.Execute(PoiseHP_Value);
            }
            ALICE_LOG_INFO("[BoxDeligateScript] PoiseHP: %f", PoiseHP_Value);
        }

        // Alpha3: CharacterHP 감소
        if (input->GetKeyDown(toKey(Get_keyCharacterHP())))
        {
            CharacterHP_Value = std::max(0.0f, CharacterHP_Value - Get_SubHP());
            if (OnCharacterHPChanged.IsBound())
            {
                OnCharacterHPChanged.Execute(CharacterHP_Value);
            }
            ALICE_LOG_INFO("[BoxDeligateScript] CharacterHP: %f", CharacterHP_Value);
        }

        // Alpha4: WeaponHP 감소
        if (input->GetKeyDown(toKey(Get_keyWeaponHP())))
        {
            WeaponHP_Value = std::max(0.0f, WeaponHP_Value - Get_SubHP());
            if (OnWeaponHPChanged.IsBound())
            {
                OnWeaponHPChanged.Execute(WeaponHP_Value);
            }
            ALICE_LOG_INFO("[BoxDeligateScript] WeaponHP: %f", WeaponHP_Value);
        }

        // Alpha5: TextValue 감소
        if (input->GetKeyDown(toKey(Get_keyTextValue())))
        {
            TextValue = std::max(0.0f, TextValue - Get_SubHP());
            if (OnTextValueChanged.IsBound())
            {
                OnTextValueChanged.Execute(TextValue);
            }
            ALICE_LOG_INFO("[BoxDeligateScript] TextValue: %f", TextValue);
        }
    }

    void BoxDeligateScript::ExampleFunction()
    {
        // 리플렉션으로 등록된 함수 예시입니다.
        // 이 함수는 에디터에서 호출할 수 있습니다.
        
    }
}
