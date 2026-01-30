#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

class IUIScript;

/// UI 스크립트 생성 함수 타입
using UIScriptCreateFunc = IUIScript* (*)();

/// UI 스크립트를 등록하고 생성하는 팩토리입니다.
/// - ScriptFactory와 동일한 패턴을 사용합니다.
class UIScriptFactory
{
public:
    /// UI 스크립트를 등록합니다.
    static void Register(const char* name, UIScriptCreateFunc func);

    /// 이름으로 새 UI 스크립트 인스턴스를 생성합니다. (없으면 nullptr)
    static std::unique_ptr<IUIScript> Create(const char* name);

    /// 현재 등록된 UI 스크립트 이름 목록을 반환합니다.
    static std::vector<std::string> GetRegisteredUIScriptNames();

private:
    static std::unordered_map<std::string, UIScriptCreateFunc>& GetRegistry();
};

/// 템플릿을 이용해 간단하게 UI 스크립트를 등록할 수 있게 합니다.
template <typename TUIScript>
class UIScriptRegistrar
{
public:
    explicit UIScriptRegistrar(const char* name)
    {
        UIScriptFactory::Register(name, []() -> IUIScript*
        {
            return new TUIScript();
        });
    }
};

// 매크로로 간단하게 UI 스크립트를 등록합니다.
// 사용 예:
//
//   class MyUIScript : public IUIScript { ... };
//   REGISTER_UI_SCRIPT(MyUIScript);
//
#define REGISTER_UI_SCRIPT(ScriptType) \
    static UIScriptRegistrar<ScriptType> s_uiscript_registrar_##ScriptType(#ScriptType);
