#pragma once

#include <DirectXMath.h>
#include <string>
#include <cstdint>

// Joint 컴포넌트 (엔티티 <-> 엔티티)
// - targetName 으로 상대 엔티티를 찾고, 두 액터를 연결
// - PhysicsSystem에서 실제 Joint를 생성/관리

enum class Phy_JointType : uint8_t
{
    Fixed,
    Revolute,
    Prismatic,
    Distance,
    Spherical,
    D6,
};

struct Phy_JointFrame
{
    // 로컬 프레임 (라디안 기준)
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
};

struct Phy_RevoluteJointSettings
{
    bool enableLimit = false;
    float lowerLimit = -3.14159265f;
    float upperLimit =  3.14159265f;
    float limitStiffness = 0.0f;
    float limitDamping = 0.0f;
    float limitRestitution = -1.0f;
    float limitBounceThreshold = -1.0f;

    bool enableDrive = false;
    float driveVelocity = 0.0f;
    float driveForceLimit = 0.0f; // <= 0 => infinite
    bool driveFreeSpin = false;
    bool driveLimitsAreForces = true;
};

struct Phy_PrismaticJointSettings
{
    bool enableLimit = false;
    float lowerLimit = -1.0f;
    float upperLimit =  1.0f;
    float limitStiffness = 0.0f;
    float limitDamping = 0.0f;
    float limitRestitution = -1.0f;
    float limitBounceThreshold = -1.0f;
};

struct Phy_DistanceJointSettings
{
    float minDistance = 0.0f;
    float maxDistance = 0.0f;
    float tolerance = 0.0f;

    bool enableMinDistance = false;
    bool enableMaxDistance = true;

    bool enableSpring = false;
    float stiffness = 0.0f;
    float damping = 0.0f;
};

struct Phy_SphericalJointSettings
{
    bool enableLimit = false;
    float yLimitAngle = 1.5707963f;
    float zLimitAngle = 1.5707963f;
    float limitStiffness = 0.0f;
    float limitDamping = 0.0f;
    float limitRestitution = -1.0f;
    float limitBounceThreshold = -1.0f;
};

enum class Phy_D6Motion : uint8_t
{
    Locked,
    Limited,
    Free,
};

struct Phy_D6JointDriveSettings
{
    float stiffness = 0.0f;
    float damping = 0.0f;
    float forceLimit = 0.0f; // <= 0 => infinite
    bool isAcceleration = false;
};

struct Phy_D6LinearLimitSettings
{
    float lower = -1.0f;
    float upper =  1.0f;
    float stiffness = 0.0f;
    float damping = 0.0f;
    float restitution = -1.0f;
    float bounceThreshold = -1.0f;
};

struct Phy_D6TwistLimitSettings
{
    float lower = -3.14159265f;
    float upper =  3.14159265f;
    float stiffness = 0.0f;
    float damping = 0.0f;
    float restitution = -1.0f;
    float bounceThreshold = -1.0f;
};

struct Phy_D6SwingLimitSettings
{
    float yAngle = 1.5707963f;
    float zAngle = 1.5707963f;
    float stiffness = 0.0f;
    float damping = 0.0f;
    float restitution = -1.0f;
    float bounceThreshold = -1.0f;
};

struct Phy_D6JointSettings
{
    bool driveLimitsAreForces = true;

    Phy_D6Motion motionX = Phy_D6Motion::Locked;
    Phy_D6Motion motionY = Phy_D6Motion::Locked;
    Phy_D6Motion motionZ = Phy_D6Motion::Locked;
    Phy_D6Motion motionTwist = Phy_D6Motion::Locked;
    Phy_D6Motion motionSwing1 = Phy_D6Motion::Locked;
    Phy_D6Motion motionSwing2 = Phy_D6Motion::Locked;

    Phy_D6LinearLimitSettings linearLimitX{};
    Phy_D6LinearLimitSettings linearLimitY{};
    Phy_D6LinearLimitSettings linearLimitZ{};
    Phy_D6TwistLimitSettings twistLimit{};
    Phy_D6SwingLimitSettings swingLimit{};

    Phy_D6JointDriveSettings driveX{};
    Phy_D6JointDriveSettings driveY{};
    Phy_D6JointDriveSettings driveZ{};
    Phy_D6JointDriveSettings driveSwing{};
    Phy_D6JointDriveSettings driveTwist{};
    Phy_D6JointDriveSettings driveSlerp{};

    Phy_JointFrame drivePose{};
    DirectX::XMFLOAT3 driveLinearVelocity = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 driveAngularVelocity = { 0.0f, 0.0f, 0.0f };
};

// Joint 컴포넌트 (엔티티 단위 1개)
struct Phy_JointComponent
{
    Phy_JointType type = Phy_JointType::Fixed;

    // 연결할 상대 엔티티 이름 (World::FindGameObject 사용)
    std::string targetName;

    // 로컬 프레임
    Phy_JointFrame frameA{};
    Phy_JointFrame frameB{};

    bool collideConnected = false;
    float breakForce = 0.0f;
    float breakTorque = 0.0f;

    // 타입별 설정
    Phy_RevoluteJointSettings revolute{};
    Phy_PrismaticJointSettings prismatic{};
    Phy_DistanceJointSettings distance{};
    Phy_SphericalJointSettings spherical{};
    Phy_D6JointSettings d6{};

    // 내부 핸들 (PhysicsSystem이 관리)
    //  직접 접근 금지: PhysicsSystem::ValidateAndGetJoint()를 통해 접근하세요
    IPhysicsJoint* jointHandle = nullptr; // 타입 안전 핸들 (worldEpoch 검증 + IsValid() 강제)
};
