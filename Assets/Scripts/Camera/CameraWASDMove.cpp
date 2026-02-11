#include "CameraWASDMove.h"

#include <cmath>

#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Input/Input.h"
#include "Runtime/Scripting/ScriptFactory.h"

#include <DirectXMath.h>

namespace Alice
{
    REGISTER_SCRIPT(CameraWASDMove);

    void CameraWASDMove::Update(float deltaTime)
    {
        auto go = gameObject();
        auto* input = Input();
        if (!go.IsValid() || !input)
            return;

        auto* transform = go.GetComponent<TransformComponent>();
        if (!transform)
            return;

        float inputX = 0.0f;
        float inputZ = 0.0f;

        if (input->GetKey(KeyCode::W)) inputZ += 1.0f;
        if (input->GetKey(KeyCode::S)) inputZ -= 1.0f;
        if (input->GetKey(KeyCode::D)) inputX += 1.0f;
        if (input->GetKey(KeyCode::A)) inputX -= 1.0f;

        // Move by current camera yaw only; this keeps vertical position unchanged.
        const float yawRad = DirectX::XMConvertToRadians(transform->rotation.y);
        const float sinY = std::sin(yawRad);
        const float cosY = std::cos(yawRad);

        const float speed = std::max(0.0f, Get_m_moveSpeed());
        const float dragSpeed = std::max(0.0f, Get_m_dragMoveSpeed());
        const float dt = std::max(0.0f, deltaTime);

        float moveWorldX = 0.0f;
        float moveWorldZ = 0.0f;

        const float keyLenSq = inputX * inputX + inputZ * inputZ;
        if (keyLenSq > 0.000001f)
        {
            const float invLen = 1.0f / std::sqrt(keyLenSq);
            const float keyX = inputX * invLen;
            const float keyZ = inputZ * invLen;

            moveWorldX += (keyX * cosY + keyZ * sinY) * speed * dt;
            moveWorldZ += (-keyX * sinY + keyZ * cosY) * speed * dt;
        }

        // Hold right mouse button and drag to translate camera position.
        if (input->GetMouseButton(MouseCode::Right))
        {
            const float dragRight = input->GetMouseDeltaX() * dragSpeed;
            const float dragForward = -input->GetMouseDeltaY() * dragSpeed;

            moveWorldX += (dragRight * cosY + dragForward * sinY);
            moveWorldZ += (-dragRight * sinY + dragForward * cosY);
        }

        transform->position.x += moveWorldX;
        transform->position.z += moveWorldZ;
    }
}
