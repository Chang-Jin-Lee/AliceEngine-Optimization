#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// 소켓 정의를 추가하는 예시
    class SocketSetupExample : public IScript
    {
        ALICE_BODY(SocketSetupExample);

    public:
        void Start() override;

    public:
        // 인스펙터에서 이름/본/오프셋을 바로 수정할 수 있도록 모두 프로퍼티로 노출
        ALICE_PROPERTY(std::string, socketName, std::string("RightHandSocket"));
        ALICE_PROPERTY(std::string, parentBone, std::string("手先.R"));
        ALICE_PROPERTY(DirectX::XMFLOAT3, position, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, rotation, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, scale,    DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
    };
}

