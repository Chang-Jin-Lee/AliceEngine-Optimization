#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Alice
{
    class IScript;

    // === 간단한 리플렉션/팩토리 ===

    using ScriptCreateFunc = IScript* (*)();

    // 동적 스크립트 DLL(라이브 코딩용)에서 사용할 함수 포인터 타입들입니다.
    using DynamicScriptCreateFunc   = IScript* (*)(const char* name);
    using DynamicScriptCountFunc    = int (*)(void);
    using DynamicScriptGetNameFunc  = bool (*)(int index, char* outName, int maxLen);

    /// 문자열 이름으로 스크립트를 생성하는 간단한 팩토리입니다.
    /// - SceneFactory 와 동일한 패턴을 사용합니다.
    class ScriptFactory
    {
    public:
        static void Register(const char* name, ScriptCreateFunc func);

        /// 이름으로 새 스크립트 인스턴스를 생성합니다. (없으면 nullptr)
        static std::unique_ptr<IScript> Create(const char* name);

        /// 현재 등록된 스크립트 이름 목록을 반환합니다.
        static std::vector<std::string> GetRegisteredScriptNames();
    };

    /// 동적 스크립트 DLL 쪽에서 가져온 함수 포인터를 등록합니다.
    /// - createFn: 이름으로 스크립트를 생성
    /// - countFn : 등록된 스크립트 개수
    /// - getNameFn: 인덱스로 스크립트 이름 얻기
    void SetDynamicScriptFunctions(DynamicScriptCreateFunc   createFn,
                                   DynamicScriptCountFunc    countFn,
                                   DynamicScriptGetNameFunc  getNameFn);

    /// 템플릿을 이용해 간단하게 스크립트를 등록할 수 있게 합니다.
    template <typename TScript>
    class ScriptRegistrar
    {
    public:
        explicit ScriptRegistrar(const char* name)
        {
            ScriptFactory::Register(name, []() -> IScript*
            {
                return new TScript();
            });
        }
    };

    // 매크로로 간단하게 스크립트를 등록합니다.
    // 사용 예:
    //
    //   class Rotator : public IScript { ... };
    //   REGISTER_SCRIPT(Rotator);
    //
    #define REGISTER_SCRIPT(ScriptType) \
        static Alice::ScriptRegistrar<ScriptType> s_script_registrar_##ScriptType(#ScriptType);
}
