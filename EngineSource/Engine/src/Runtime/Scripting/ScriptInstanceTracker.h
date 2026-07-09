#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Alice
{
    class IScript;

    /// 살아있는 모든 IScript 인스턴스를 추적합니다.
    /// - 목적: 스크립트 DLL 언로드 전에 "인스턴스 0개" 불변식을 검사해
    ///   vtable 소실로 인한 누수/크래시를 원천 차단합니다.
    namespace ScriptInstanceTracker
    {
        void OnCreated(IScript* instance);
        void OnDestroyed(IScript* instance);

        std::size_t AliveCount();

        /// 살아있는 인스턴스들의 클래스 이름 목록.
        /// 주의: GetName()은 vtable을 사용하므로 DLL 언로드 전에만 호출할 것.
        std::vector<std::string> AliveNames();
    }
}
