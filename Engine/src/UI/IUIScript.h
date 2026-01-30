#pragma once

#include <string>

class UIBase;
class UI_ImageComponent;

/// UI 전용 스크립트 베이스 인터페이스
class IUIScript
{
public:
	virtual ~IUIScript() = default;

	/// 스크립트가 붙은 UI의 정보
	UIBase* Owner{ nullptr };
	unsigned long OwnerID{ 0 };

	/// UI_ScriptComponent가 추가될 때 1회 호출
	virtual void OnAdded(UIBase& /*owner*/) {}

	/// UI_ScriptComponent가 파괴되거나 제거될 때 호출
	virtual void OnRemoved() {}

	/// 필요하다면 1회 초기화용
	virtual void OnStart() {};

	/// 매 프레임 호출 (UIScriptSystem::Tick에서 호출)
	virtual void Update(float /*dt*/) {}


};
