#pragma once
#include "UI/IUIScript.h"
#include <DirectXMath.h>

/// UI 스크립트 예시 클래스
/// 
/// 사용 예시:
/// 1. 에디터에서 UIObject를 선택
/// 2. Inspector에서 "Add UI Script" → "MyUIScript" 선택
/// 3. 스크립트가 자동으로 실행됩니다
/// 

class MyUIScript : public IUIScript
{
public:
    /// UI_ScriptComponent가 추가될 때 1회 호출
    void OnAdded(UIBase& owner) override;

    /// 컴포넌트가 제거될 때 호출
    void OnRemoved() override;

    /// 초기화 (첫 Update 전에 1회 호출)
    void OnStart() override;

    /// 매 프레임 호출
    void Update(float dt) override;


    //UI_ImageComponent* GetImageComponent();

    //bool SetImagePath(const std::wstring& path);

private:

};
