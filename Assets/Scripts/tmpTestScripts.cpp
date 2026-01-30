#include "tmpTestScripts.h"
#include "UI/UIScriptFactory.h"
#include "UI/UIBase.h"
#include "UI/UITransform.h"
#include "UI/UI_ImageComponent.h"
#include "Core/Logger.h"

// 클래스 정의 후에 등록! (중요)
// 이 정적 변수는 이 파일이 링크될 때 자동으로 초기화됩니다
REGISTER_UI_SCRIPT(MyUIScript);

void MyUIScript::OnAdded(UIBase& owner)
{
    // 컴포넌트가 추가될 때 호출
    // Owner는 이미 설정되어 있습니다
    ALICE_LOG_INFO("[MyUIScript] OnAdded called for UI ID: %lu", OwnerID);
}

void MyUIScript::OnStart()
{
    // 초기화 로직
    ALICE_LOG_INFO("[MyUIScript] OnStart called for UI ID: %lu", OwnerID);
    
    if (!Owner)
    {
        ALICE_LOG_WARN("[MyUIScript] Owner is null!");
        return;
    }

    // 예시: Transform 컴포넌트 가져오기
    //auto* transform = Owner->TryGetComponent<UITransform>();
    //if (transform)
    //{
    //    ALICE_LOG_INFO("[MyUIScript] Transform found: pos=(%.2f, %.2f), size=(%.2f, %.2f)",
    //        transform->m_translation.x, transform->m_translation.y,
    //        transform->m_size.x, transform->m_size.y);
    //}
    //else
    //{
    //    ALICE_LOG_WARN("[MyUIScript] Transform not found!");
    //}

    // ============================================================
    // 이미지 경로 설정 예시: Assets/Resource/Image/Yuuka.png
    // ============================================================
    
    // SetImagePath()는 ImageComponent가 없으면 자동으로 추가합니다
    // wide string을 받습니다 (L"..." 형식)
    ALICE_LOG_INFO("[MyUIScript] Calling SetImagePath...");
    auto tmpImage =  Owner->TryGetComponent<UI_ImageComponent>();
    bool success = tmpImage->SetImagePath(L"D:\\4q__AliceRenderer\\Resource\\Image\\Yuuka.png");
    if (success)
    {
        ALICE_LOG_INFO("[MyUIScript] Image loaded successfully: Yuuka.png");
    }
    else
    {
        ALICE_LOG_WARN("[MyUIScript] Failed to load image: Yuuka.png");
    }
}

void MyUIScript::Update(float dt)
{
    
}

void MyUIScript::OnRemoved()
{
    // 컴포넌트가 제거될 때 호출
    ALICE_LOG_INFO("[MyUIScript] OnRemoved called for UI ID: %lu", OwnerID);

}
