#pragma once

namespace Alice
{
    /// 모든 시스템이 따라야 하는 공통 인터페이스입니다.
    /// - 예: 렌더 시스템, 물리 시스템, 애니메이션 시스템 등
    class ISystem
    {
    public:
        virtual ~ISystem() = default;

        /// 시스템 초기화 단계에서 한 번 호출됩니다.
        virtual void Initialize() = 0;

        /// 매 프레임마다 호출되어 갱신 로직을 수행합니다.
        /// \param deltaTime 이전 프레임에서 경과한 시간(초)
        virtual void Update(float deltaTime) = 0;
    };
}


