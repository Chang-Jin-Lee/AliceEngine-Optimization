#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class UIWorld;
class UIBase;
class UI_ScriptComponent;
class IUIScript;
struct UIScriptEntry;

/// 동적 UI 스크립트 DLL에서 사용할 함수 포인터 타입들
using DynamicUIScriptCountFunc = int (*)(void);
using DynamicUIScriptGetNameFunc = bool (*)(int index, char* outName, int maxLen);

/// 동적 UI 스크립트 생성 함수 타입 (동적 DLL에서 주입)
using DynamicUIScriptFactory = std::function<std::unique_ptr<IUIScript>(const std::string&)>;

/// UIWorld 내의 모든 UI_ScriptComponent를 갱신하는 시스템
class UIScriptSystem
{
public:
	/// 스크립트 생성 팩토리 등록 (동적 DLL 로더에서 주입 예상)
	static void SetFactory(DynamicUIScriptFactory factory);

	/// 동적 UI 스크립트 이름 목록 함수들 등록
	static void SetDynamicUIScriptFunctions(DynamicUIScriptCountFunc countFn, DynamicUIScriptGetNameFunc getNameFn);

	/// 현재 등록된 UI 스크립트 이름 목록을 반환합니다.
	static std::vector<std::string> GetRegisteredUIScriptNames();

	/// UIWorld 전체 Tick
	static void Tick(UIWorld& world, float dt);

private:
	static DynamicUIScriptFactory s_factory;
	static DynamicUIScriptCountFunc s_dynCount;
	static DynamicUIScriptGetNameFunc s_dynGetName;

	static void TickRoot(UIWorld& world, float dt);
	static void TickNode(UIWorld& world, UIBase* node, float dt);
	static void TickComponent(UI_ScriptComponent& comp, float dt);
	static void EnsureInstance(UI_ScriptComponent& comp);
	

	static void TickUIScriptEntry(UIWorld& world, UIBase* owner, UIScriptEntry& entry, float dt);
	static void EnsureUIScriptInstance(UIWorld& world, UIBase* owner, UIScriptEntry& entry);
};
