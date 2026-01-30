#pragma once

#include <memory>
#include <string>
#include "IUIComponent.h"
#include "IUIScript.h"

/// UIWorld에 부착되는 스크립트 컴포넌트
class UI_ScriptComponent : public IUIComponent
{
public:
	UI_ScriptComponent() = default;
	~UI_ScriptComponent() override;
	
	// 복사/이동 방지 (컴포넌트는 unique_ptr로 관리되므로 복사/이동 불필요)
	UI_ScriptComponent(const UI_ScriptComponent&) = delete;
	UI_ScriptComponent& operator=(const UI_ScriptComponent&) = delete;
	UI_ScriptComponent(UI_ScriptComponent&&) = delete;
	UI_ScriptComponent& operator=(UI_ScriptComponent&&) = delete;

	// 스크립트 이름(동적 생성에 사용)
	std::string scriptName;

	// 실행 여부
	bool enabled{ true };

	// 라이프사이클 플래그
	bool awoken{ false };
	bool started{ false };

	// 실제 스크립트 인스턴스
	std::unique_ptr<IUIScript> instance;

	// 소멸 시 OnRemoved 호출
	void OnRemoved();
};
