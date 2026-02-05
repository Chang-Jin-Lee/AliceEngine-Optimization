#pragma once

// Script build precompiled header.
// This is force-included for Assets/Scripts to provide component types
// without per-file includes (autocomplete + compile).

#include "Runtime/Scripting/IScript.h"

// ECS / Core
#include "Runtime/ECS/Component.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"

// Rendering components
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraInputComponent.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/DecalComponent.h"
#include "Runtime/Rendering/Components/PointLightComponent.h"
#include "Runtime/Rendering/Components/SpotLightComponent.h"
#include "Runtime/Rendering/Components/RectLightComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/EffectComponent.h"
#include "Runtime/Rendering/Components/TrailEffectComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Rendering/Components/DebugDrawBoxComponent.h"

// Physics components
#include "Runtime/Physics/Components/Phy_SettingsComponent.h"
#include "Runtime/Physics/Components/Phy_RigidBodyComponent.h"
#include "Runtime/Physics/Components/Phy_ColliderComponent.h"
#include "Runtime/Physics/Components/Phy_MeshColliderComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Physics/Components/Phy_TerrainHeightFieldComponent.h"
#include "Runtime/Physics/Components/Phy_JointComponent.h"

// Audio components
#include "Runtime/Audio/Components/AudioSourceComponent.h"
#include "Runtime/Audio/Components/AudioListenerComponent.h"
#include "Runtime/Audio/Components/SoundBoxComponent.h"

// Gameplay components
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/HurtboxComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Animation/AnimBlueprintComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/Gameplay/Sockets/SocketAttachmentComponent.h"
#include "Runtime/Gameplay/Sockets/SocketPoseOutputComponent.h"

// Scripting component
#include "Runtime/Scripting/Components/ScriptComponent.h"

// UI components
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UIEffectComponent.h"
#include "Runtime/UI/UIAnimationComponent.h"
#include "Runtime/UI/UIShakeComponent.h"
#include "Runtime/UI/UIHover3DComponent.h"
#include "Runtime/UI/UIVitalComponent.h"
