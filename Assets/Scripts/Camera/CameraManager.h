#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    /// 씬의 "메인 카메라" 역할을 하는 엔티티에 붙이는 스크립트입니다.
    /// - CameraComponent가 없으면 자동으로 추가합니다.
    /// - primary=true로 만들어 엔진이 이 카메라를 사용하게 합니다.
    class CameraManager : public IScript
    {
        ALICE_BODY(CameraManager);

    public:
        const char* GetName() const override { return "CameraManager"; }

        void Awake() override;
		void Update(float deltaTime) override;
    };
}


