#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// 특정 본 소켓을 따라가는 예시 스크립트
    class SocketFollower : public IScript
    {
        ALICE_BODY(SocketFollower);

    public:
        void Start() override;
        void Update(float deltaTime) override;

    public:
        // 따라갈 타겟 이름 / 소켓 이름 / 회전·스케일 여부를 모두 인스펙터에서 조절
        ALICE_PROPERTY(std::string, targetName,  std::string("Rapi"));
        ALICE_PROPERTY(std::string, socketName,  std::string("RightHand"));
        ALICE_PROPERTY(bool,        followRotation, true);
        ALICE_PROPERTY(bool,        followScale,    false);

    private:
        EntityId m_targetId = InvalidEntityId;
    };
}

