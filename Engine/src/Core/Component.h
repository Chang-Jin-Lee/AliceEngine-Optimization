#pragma once

namespace Alice
{
    /// 모든 컴포넌트가 구현해야 하는 가장 기본 인터페이스입니다.
    /// - SOLID 원칙의 인터페이스 분리를 위해, 공통 기능은 최소한으로 유지합니다.
    class IComponent
    {
    public:
        virtual ~IComponent() = default;
    };
}


