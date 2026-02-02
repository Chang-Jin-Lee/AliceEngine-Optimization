#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/Foundation/Delegate.h"
#include "Runtime/Input/InputTypes.h"

namespace Alice
{
    // 4개의 델리게이트 선언
    ALICE_DECLARE_DELEGATE_OneParam(FOnBossHPChanged, float);
    ALICE_DECLARE_DELEGATE_OneParam(FOnPoiseHPChanged, float);
    ALICE_DECLARE_DELEGATE_OneParam(FOnCharacterHPChanged, float);
    ALICE_DECLARE_DELEGATE_OneParam(FOnWeaponHPChanged, float);

    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.
    class BoxDeligateScript : public IScript
    {
        ALICE_BODY(BoxDeligateScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // 4개의 델리게이트
        FOnBossHPChanged OnBossHPChanged;
        FOnPoiseHPChanged OnPoiseHPChanged;
        FOnCharacterHPChanged OnCharacterHPChanged;
        FOnWeaponHPChanged OnWeaponHPChanged;

        // 4개의 HP 변수
        ALICE_PROPERTY(float, BossHP_Value, 100.0f);
        ALICE_PROPERTY(float, PoiseHP_Value, 100.0f);
        ALICE_PROPERTY(float, CharacterHP_Value, 100.0f);
        ALICE_PROPERTY(float, WeaponHP_Value, 100.0f);

        // HP 감소량
        ALICE_PROPERTY(float, SubHP, 5.0f);

        // 키 입력 처리용 (에디터에서 설정 가능)
        ALICE_PROPERTY(int, keyBossHP, static_cast<int>(KeyCode::Alpha1));
        ALICE_PROPERTY(int, keyPoiseHP, static_cast<int>(KeyCode::Alpha2));
        ALICE_PROPERTY(int, keyCharacterHP, static_cast<int>(KeyCode::Alpha3));
        ALICE_PROPERTY(int, keyWeaponHP, static_cast<int>(KeyCode::Alpha4));

        // --- 함수 리플렉션 예시 ---
        void ExampleFunction();
        ALICE_FUNC(ExampleFunction);

    private:
        ALICE_PROPERTY(float, TotalTime, 0.0f);
    };
}
