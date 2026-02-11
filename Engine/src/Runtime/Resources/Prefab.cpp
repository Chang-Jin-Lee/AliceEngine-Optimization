#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Runtime/Resources/Prefab.h"
#include "Runtime/ECS/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Importing/FbxAsset.h"

#include "Runtime/ECS/World.h"
#include "Runtime/Scripting/Components/ScriptComponent.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/DecalComponent.h"
#include "Runtime/Rendering/Components/PointLightComponent.h"
#include "Runtime/Rendering/Components/SpotLightComponent.h"
#include "Runtime/Rendering/Components/RectLightComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraInputComponent.h"
#include "Runtime/Gameplay/Sockets/SocketAttachmentComponent.h"
#include "Runtime/Gameplay/Combat/HurtboxComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Resources/Serialization/SocketSerialization.h"
#include "Runtime/Resources/Serialization/AttackDriverSerialization.h"
#include "Runtime/Resources/Serialization/WeaponTraceSerialization.h"

#include "Runtime/Physics/Components/Phy_RigidBodyComponent.h"
#include "Runtime/Physics/Components/Phy_ColliderComponent.h"
#include "Runtime/Physics/Components/Phy_MeshColliderComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Physics/Components/Phy_TerrainHeightFieldComponent.h"
#include "Runtime/Physics/Components/Phy_SettingsComponent.h"
#include "Runtime/Physics/Components/Phy_JointComponent.h"

#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UICheckboxComponent.h"
#include "Runtime/UI/UIGaugeComponent.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DirectXMath.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"

namespace Alice
{
    namespace Prefab
    {
        namespace
        {
            // 스키닝 메시가 아직 애니메이션 시스템과 연결되지 않았을 때 사용할
            // 1개짜리 항등 본 팔레트입니다.
            static DirectX::XMFLOAT4X4 g_IdentityBone(
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);

            static World* g_DefaultWorld = nullptr;
            static ResourceManager* g_DefaultResources = nullptr;
            static ResourceManager* ResolvePrefabResourceManager();

            // 프로젝트 루트 경로를 구하는 헬퍼 함수
            static std::filesystem::path GetProjectRoot()
            {
                wchar_t exePathW[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
                std::filesystem::path exePath = exePathW;
                std::filesystem::path exeDir = exePath.parent_path();
                // build/bin/Debug 또는 build/bin/Release 가 나옴. 프로젝트 루트임
                return exeDir.parent_path().parent_path().parent_path();
            }

            // 절대 경로를 상대 경로로 변환하는 헬퍼 함수
            static std::string NormalizePathToRelative(const std::string& path)
            {
                if (path.empty())
                    return path;

                std::filesystem::path p(path);
                
                // 이미 상대 경로이거나 Assets/ 또는 Resource/로 시작하면 그대로 반환
                if (!p.is_absolute())
                {
                    const std::string s = p.generic_string();
                    if (s.find("Assets/") == 0 || s.find("Resource/") == 0 || s.find("Cooked/") == 0)
                        return s;
                }

                // 절대 경로인 경우 프로젝트 루트 기준 상대 경로로 변환
                if (p.is_absolute())
                {
                    const std::filesystem::path projectRoot = GetProjectRoot();
                    try
                    {
                        std::filesystem::path relative = std::filesystem::relative(p, projectRoot);
                        if (!relative.empty())
                        {
                            const std::string result = relative.generic_string();
                            // Assets/ 또는 Resource/로 시작하는지 확인
                            if (result.find("Assets/") == 0 || result.find("Resource/") == 0 || result.find("Cooked/") == 0)
                                return result;
                        }
                    }
                    catch (...)
                    {
                        // relative() 실패 시 원본 반환
                    }
                }

                return path;
            }

            static void ResolveSkinnedMeshPathsForLoad(SkinnedMeshComponent& skinned)
            {
                if (skinned.instanceAssetPath.empty() && !skinned.meshAssetPath.empty())
                {
                    const std::filesystem::path inferred = std::filesystem::path("Assets/Fbx") / (skinned.meshAssetPath + ".fbxasset");
                    skinned.instanceAssetPath = inferred.generic_string();
                }

                if (!skinned.instanceAssetPath.empty())
                {
                    ResourceManager* resources = ResolvePrefabResourceManager();
                    if (resources)
                    {
                        FbxInstanceAsset asset{};
                        if (LoadFbxInstanceAssetAuto(*resources, skinned.instanceAssetPath, asset))
                        {
                            if (!asset.meshAssetPath.empty())
                                skinned.meshAssetPath = asset.meshAssetPath;
                        }
                    }
                }
            }

            static std::uint64_t ParseGuidOrZero(const JsonRttr::json& j)
            {
                if (j.is_string())
                {
                    try
                    {
                        return std::stoull(j.get<std::string>());
                    }
                    catch (...)
                    {
                        return 0;
                    }
                }
                if (j.is_number_unsigned() || j.is_number_integer())
                    return j.get<std::uint64_t>();
                return 0;
            }

            // Phy_SettingsComponent 수동 직렬화
            static JsonRttr::json WritePhysicsSceneSettings(const Phy_SettingsComponent& settings)
            {
                JsonRttr::json out = JsonRttr::json::object();
                
                // 기본 프로퍼티
                out["enablePhysics"] = settings.enablePhysics;
                out["enableGroundPlane"] = settings.enableGroundPlane;
                out["groundStaticFriction"] = settings.groundStaticFriction;
                out["groundDynamicFriction"] = settings.groundDynamicFriction;
                out["groundRestitution"] = settings.groundRestitution;
                out["groundLayerBits"] = settings.groundLayerBits;
                out["groundCollideMask"] = settings.groundCollideMask;
                out["groundQueryMask"] = settings.groundQueryMask;
                out["groundIgnoreLayers"] = settings.groundIgnoreLayers;
                out["groundIsTrigger"] = settings.groundIsTrigger;
                out["gravity"] = JsonRttr::json::array({ settings.gravity.x, settings.gravity.y, settings.gravity.z });
                out["fixedDt"] = settings.fixedDt;
                out["maxSubsteps"] = settings.maxSubsteps;
                out["filterRevision"] = settings.filterRevision;
                
                // layerCollideMatrix: 32x32 bool 배열
                out["layerCollideMatrix"] = JsonRttr::json::array();
                for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
                {
                    JsonRttr::json row = JsonRttr::json::array();
                    for (int col = 0; col < MAX_PHYSICS_LAYERS; ++col)
                    {
                        row.push_back(settings.layerCollideMatrix[i][col]);
                    }
                    out["layerCollideMatrix"].push_back(row);
                }
                
                // layerQueryMatrix: 32x32 bool 배열
                out["layerQueryMatrix"] = JsonRttr::json::array();
                for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
                {
                    JsonRttr::json row = JsonRttr::json::array();
                    for (int col = 0; col < MAX_PHYSICS_LAYERS; ++col)
                    {
                        row.push_back(settings.layerQueryMatrix[i][col]);
                    }
                    out["layerQueryMatrix"].push_back(row);
                }
                
                // layerNames: 32개 string 배열
                out["layerNames"] = JsonRttr::json::array();
                for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
                {
                    out["layerNames"].push_back(settings.layerNames[i]);
                }
                
                return out;
            }
            
            // Phy_SettingsComponent 수동 역직렬화
            static bool LoadPhysicsSceneSettings(Phy_SettingsComponent& settings, const JsonRttr::json& root)
            {
                if (!root.is_object()) return false;
                
                // 기본 프로퍼티
                if (root.contains("enablePhysics") && root["enablePhysics"].is_boolean())
                    settings.enablePhysics = root["enablePhysics"].get<bool>();

                if (root.contains("enableGroundPlane") && root["enableGroundPlane"].is_boolean())
                    settings.enableGroundPlane = root["enableGroundPlane"].get<bool>();

                if (root.contains("groundStaticFriction") && root["groundStaticFriction"].is_number())
                    settings.groundStaticFriction = root["groundStaticFriction"].get<float>();

                if (root.contains("groundDynamicFriction") && root["groundDynamicFriction"].is_number())
                    settings.groundDynamicFriction = root["groundDynamicFriction"].get<float>();

                if (root.contains("groundRestitution") && root["groundRestitution"].is_number())
                    settings.groundRestitution = root["groundRestitution"].get<float>();

                if (root.contains("groundLayerBits") && root["groundLayerBits"].is_number_unsigned())
                    settings.groundLayerBits = root["groundLayerBits"].get<uint32_t>();

                if (root.contains("groundCollideMask") && root["groundCollideMask"].is_number_unsigned())
                    settings.groundCollideMask = root["groundCollideMask"].get<uint32_t>();

                if (root.contains("groundQueryMask") && root["groundQueryMask"].is_number_unsigned())
                    settings.groundQueryMask = root["groundQueryMask"].get<uint32_t>();

                if (root.contains("groundIgnoreLayers") && root["groundIgnoreLayers"].is_number_unsigned())
                    settings.groundIgnoreLayers = root["groundIgnoreLayers"].get<uint32_t>();

                if (root.contains("groundIsTrigger") && root["groundIsTrigger"].is_boolean())
                    settings.groundIsTrigger = root["groundIsTrigger"].get<bool>();

                if (root.contains("gravity") && root["gravity"].is_array() && root["gravity"].size() == 3)
                {
                    settings.gravity.x = root["gravity"][0].get<float>();
                    settings.gravity.y = root["gravity"][1].get<float>();
                    settings.gravity.z = root["gravity"][2].get<float>();
                }

                if (root.contains("fixedDt") && root["fixedDt"].is_number())
                    settings.fixedDt = root["fixedDt"].get<float>();

                if (root.contains("maxSubsteps") && root["maxSubsteps"].is_number_unsigned())
                    settings.maxSubsteps = root["maxSubsteps"].get<uint32_t>();

                if (root.contains("filterRevision") && root["filterRevision"].is_number_unsigned())
                    settings.filterRevision = root["filterRevision"].get<uint32_t>();
                
                // layerCollideMatrix: 32x32 bool 배열
                if (root.contains("layerCollideMatrix") && root["layerCollideMatrix"].is_array())
                {
                    const auto& matrix = root["layerCollideMatrix"];
                    for (int i = 0; i < MAX_PHYSICS_LAYERS && i < static_cast<int>(matrix.size()); ++i)
                    {
                        if (matrix[i].is_array())
                        {
                            const auto& row = matrix[i];
                            for (int col = 0; col < MAX_PHYSICS_LAYERS && col < static_cast<int>(row.size()); ++col)
                            {
                                if (row[col].is_boolean())
                                    settings.layerCollideMatrix[i][col] = row[col].get<bool>();
                                else if (row[col].is_number_integer())
                                    settings.layerCollideMatrix[i][col] = (row[col].get<int>() != 0);
                            }
                        }
                    }
                }
                
                // layerQueryMatrix: 32x32 bool 배열
                if (root.contains("layerQueryMatrix") && root["layerQueryMatrix"].is_array())
                {
                    const auto& matrix = root["layerQueryMatrix"];
                    for (int i = 0; i < MAX_PHYSICS_LAYERS && i < static_cast<int>(matrix.size()); ++i)
                    {
                        if (matrix[i].is_array())
                        {
                            const auto& row = matrix[i];
                            for (int col = 0; col < MAX_PHYSICS_LAYERS && col < static_cast<int>(row.size()); ++col)
                            {
                                if (row[col].is_boolean())
                                    settings.layerQueryMatrix[i][col] = row[col].get<bool>();
                                else if (row[col].is_number_integer())
                                    settings.layerQueryMatrix[i][col] = (row[col].get<int>() != 0);
                            }
                        }
                    }
                }
                
                // layerNames: 32개 string 배열
                if (root.contains("layerNames") && root["layerNames"].is_array())
                {
                    const auto& names = root["layerNames"];
                    for (int i = 0; i < MAX_PHYSICS_LAYERS && i < static_cast<int>(names.size()); ++i)
                    {
                        if (names[i].is_string())
                            settings.layerNames[i] = names[i].get<std::string>();
                    }
                }
                
                return true;
            }

            static void CollectEntityTree(const World& world, EntityId root, std::vector<EntityId>& out)
            {
                out.push_back(root);
                std::vector<EntityId> children = world.GetChildren(root);
                std::sort(children.begin(), children.end());
                for (EntityId child : children)
                    CollectEntityTree(world, child, out);
            }

            static bool WriteEntityJson(const World& world, EntityId entity, JsonRttr::json& outEntity);
            static bool ApplyEntityFromJson(World& world, EntityId entity, const JsonRttr::json& root);

            static bool WriteEntityJson(const World& world, EntityId entity, JsonRttr::json& outEntity)
            {
                if (entity == InvalidEntityId)
                    return false;

                const TransformComponent* t = world.GetComponent<TransformComponent>(entity);
                if (!t)
                    return false;

                outEntity = JsonRttr::json::object();
                const std::string name = world.GetEntityName(entity);
                if (!name.empty())
                    outEntity["name"] = name;

                // Transform
                {
                    rttr::instance inst = const_cast<TransformComponent&>(*t);
                    outEntity["Transform"] = JsonRttr::ToJsonObject(inst);
                }

                // Scripts
                if (const auto* scripts = world.GetScripts(entity); scripts && !scripts->empty())
                {
                    JsonRttr::json arr = JsonRttr::json::array();
                    for (const auto& sc : *scripts)
                    {
                        JsonRttr::json s = JsonRttr::json::object();
                        s["name"] = sc.scriptName;
                        s["enabled"] = sc.enabled;

                        if (sc.instance)
                        {
                            rttr::instance inst = *sc.instance;
                            const rttr::type t = rttr::type::get_by_name(sc.scriptName);
                            s["props"] = JsonRttr::ToJsonObject(inst, t);
                        }

                        arr.push_back(s);
                    }
                    outEntity["Scripts"] = arr;
                }

                // Material
                if (const auto* mat = world.GetComponent<MaterialComponent>(entity); mat)
                {
                    MaterialComponent matCopy = *mat;
                    matCopy.assetPath = NormalizePathToRelative(matCopy.assetPath);
                    matCopy.albedoTexturePath = NormalizePathToRelative(matCopy.albedoTexturePath);
                    matCopy.emissiveTexturePath = NormalizePathToRelative(matCopy.emissiveTexturePath);

                    rttr::instance inst = matCopy;
                    outEntity["Material"] = JsonRttr::ToJsonObject(inst);
                }

                if (const auto* decal = world.GetComponent<DecalComponent>(entity); decal)
                {
                    DecalComponent decalCopy = *decal;
                    decalCopy.albedoTexturePath = NormalizePathToRelative(decalCopy.albedoTexturePath);
                    rttr::instance inst = decalCopy;
                    outEntity["Decal"] = JsonRttr::ToJsonObject(inst);
                }

                // SkinnedMesh
                if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(entity); skinned)
                {
                    SkinnedMeshComponent skinnedCopy = *skinned;
                    skinnedCopy.instanceAssetPath = NormalizePathToRelative(skinnedCopy.instanceAssetPath);
                    skinnedCopy.meshAssetPath = NormalizePathToRelative(skinnedCopy.meshAssetPath);

                    rttr::instance inst = skinnedCopy;
                    outEntity["SkinnedMesh"] = JsonRttr::ToJsonObject(inst);
                }

                // SkinnedAnimation
                if (const auto* anim = world.GetComponent<SkinnedAnimationComponent>(entity); anim)
                {
                    rttr::instance inst = const_cast<SkinnedAnimationComponent&>(*anim);
                    outEntity["SkinnedAnimation"] = JsonRttr::ToJsonObject(inst);
                }

                // AdvancedAnimation
                if (const auto* advAnim = world.GetComponent<AdvancedAnimationComponent>(entity); advAnim)
                {
                    rttr::instance inst = const_cast<AdvancedAnimationComponent&>(*advAnim);
                    JsonRttr::json obj = JsonRttr::ToJsonObject(inst);
                    obj["rootBoneLock"] = advAnim->rootBoneLock;
                    obj["rootMotionDriveCct"] = advAnim->rootMotionDriveCct;
                    outEntity["AdvancedAnimation"] = obj;
                }

                // Camera
                if (const auto* cam = world.GetComponent<CameraComponent>(entity); cam)
                {
                    rttr::instance inst = const_cast<CameraComponent&>(*cam);
                    outEntity["Camera"] = JsonRttr::ToJsonObject(inst);
                }

                // CameraFollow
                if (const auto* follow = world.GetComponent<CameraFollowComponent>(entity); follow)
                {
                    rttr::instance inst = const_cast<CameraFollowComponent&>(*follow);
                    outEntity["CameraFollow"] = JsonRttr::ToJsonObject(inst);
                }

                // CameraSpringArm
                if (const auto* spring = world.GetComponent<CameraSpringArmComponent>(entity); spring)
                {
                    rttr::instance inst = const_cast<CameraSpringArmComponent&>(*spring);
                    outEntity["CameraSpringArm"] = JsonRttr::ToJsonObject(inst);
                }

                // CameraLookAt
                if (const auto* lookAt = world.GetComponent<CameraLookAtComponent>(entity); lookAt)
                {
                    rttr::instance inst = const_cast<CameraLookAtComponent&>(*lookAt);
                    outEntity["CameraLookAt"] = JsonRttr::ToJsonObject(inst);
                }

                // CameraShake
                if (const auto* shake = world.GetComponent<CameraShakeComponent>(entity); shake)
                {
                    rttr::instance inst = const_cast<CameraShakeComponent&>(*shake);
                    outEntity["CameraShake"] = JsonRttr::ToJsonObject(inst);
                }

                // CameraBlend
                if (const auto* blend = world.GetComponent<CameraBlendComponent>(entity); blend)
                {
                    rttr::instance inst = const_cast<CameraBlendComponent&>(*blend);
                    outEntity["CameraBlend"] = JsonRttr::ToJsonObject(inst);
                }

                // CameraInput
                if (const auto* input = world.GetComponent<CameraInputComponent>(entity); input)
                {
                    rttr::instance inst = const_cast<CameraInputComponent&>(*input);
                    outEntity["CameraInput"] = JsonRttr::ToJsonObject(inst);
                }

                // Socket
                if (const auto* socketComp = world.GetComponent<SocketComponent>(entity); socketComp)
                {
                    outEntity["Socket"] = SocketSerialization::SocketComponentToJson(*socketComp);
                }

                // SocketAttachment
                if (const auto* socketAttach = world.GetComponent<SocketAttachmentComponent>(entity); socketAttach)
                {
                    rttr::instance inst = const_cast<SocketAttachmentComponent&>(*socketAttach);
                    JsonRttr::json obj = JsonRttr::ToJsonObject(inst);
                    obj["ownerGuid"] = std::to_string(socketAttach->ownerGuid);
                    outEntity["SocketAttachment"] = obj;
                }

                // Hurtbox
                if (const auto* hurtbox = world.GetComponent<HurtboxComponent>(entity); hurtbox)
                {
                    rttr::instance inst = const_cast<HurtboxComponent&>(*hurtbox);
                    JsonRttr::json obj = JsonRttr::ToJsonObject(inst);
                    obj["ownerGuid"] = std::to_string(hurtbox->ownerGuid);
                    outEntity["Hurtbox"] = obj;
                }

                // WeaponTrace
                if (const auto* weaponTrace = world.GetComponent<WeaponTraceComponent>(entity); weaponTrace)
                {
                    outEntity["WeaponTrace"] = WeaponTraceSerialization::WeaponTraceComponentToJson(*weaponTrace);
                }

                // Health
                if (const auto* health = world.GetComponent<HealthComponent>(entity); health)
                {
                    rttr::instance inst = const_cast<HealthComponent&>(*health);
                    outEntity["Health"] = JsonRttr::ToJsonObject(inst);
                }

                // AttackDriver
                if (const auto* attackDriver = world.GetComponent<AttackDriverComponent>(entity); attackDriver)
                {
                    outEntity["AttackDriver"] = AttackDriverSerialization::AttackDriverComponentToJson(*attackDriver);
                }

                // AliceUI Components
                if (const auto* uiWidget = world.GetComponent<UIWidgetComponent>(entity); uiWidget)
                {
                    rttr::instance inst = const_cast<UIWidgetComponent&>(*uiWidget);
                    outEntity["UIWidget"] = JsonRttr::ToJsonObject(inst);
                }
                if (const auto* uiTransform = world.GetComponent<UITransformComponent>(entity); uiTransform)
                {
                    rttr::instance inst = const_cast<UITransformComponent&>(*uiTransform);
                    outEntity["UITransform"] = JsonRttr::ToJsonObject(inst);
                }
                if (const auto* uiImage = world.GetComponent<UIImageComponent>(entity); uiImage)
                {
                    UIImageComponent copy = *uiImage;
                    copy.texturePath = NormalizePathToRelative(copy.texturePath);
                    rttr::instance inst = copy;
                    outEntity["UIImage"] = JsonRttr::ToJsonObject(inst);
                }
                if (const auto* uiText = world.GetComponent<UITextComponent>(entity); uiText)
                {
                    UITextComponent copy = *uiText;
                    copy.fontPath = NormalizePathToRelative(copy.fontPath);
                    rttr::instance inst = copy;
                    outEntity["UIText"] = JsonRttr::ToJsonObject(inst);
                }
                if (const auto* uiButton = world.GetComponent<UIButtonComponent>(entity); uiButton)
                {
                    UIButtonComponent copy = *uiButton;
                    copy.normalTexture = NormalizePathToRelative(copy.normalTexture);
                    copy.hoveredTexture = NormalizePathToRelative(copy.hoveredTexture);
                    copy.pressedTexture = NormalizePathToRelative(copy.pressedTexture);
                    copy.disabledTexture = NormalizePathToRelative(copy.disabledTexture);
                    rttr::instance inst = copy;
                    outEntity["UIButton"] = JsonRttr::ToJsonObject(inst);
                }
                if (const auto* uiGauge = world.GetComponent<UIGaugeComponent>(entity); uiGauge)
                {
                    UIGaugeComponent copy = *uiGauge;
                    copy.fillTexture = NormalizePathToRelative(copy.fillTexture);
                    copy.backgroundTexture = NormalizePathToRelative(copy.backgroundTexture);
                    rttr::instance inst = copy;
                    outEntity["UIGauge"] = JsonRttr::ToJsonObject(inst);
                }

            
                // Point Light
                if (const auto* point = world.GetComponent<PointLightComponent>(entity); point)
                {
                    rttr::instance inst = const_cast<PointLightComponent&>(*point);
                    outEntity["PointLight"] = JsonRttr::ToJsonObject(inst);
                }

                // Spot Light
                if (const auto* spot = world.GetComponent<SpotLightComponent>(entity); spot)
                {
                    rttr::instance inst = const_cast<SpotLightComponent&>(*spot);
                    outEntity["SpotLight"] = JsonRttr::ToJsonObject(inst);
                }

                // Rect Light
                if (const auto* rect = world.GetComponent<RectLightComponent>(entity); rect)
                {
                    rttr::instance inst = const_cast<RectLightComponent&>(*rect);
                    outEntity["RectLight"] = JsonRttr::ToJsonObject(inst);
                }

                if (const auto* computeEffect = world.GetComponent<ComputeEffectComponent>(entity); computeEffect)
                {
                    rttr::instance inst = const_cast<ComputeEffectComponent&>(*computeEffect);
                    outEntity["ComputeEffect"] = JsonRttr::ToJsonObject(inst);
                }

                if (const auto* unityVfx = world.GetComponent<UnityVfxComponent>(entity); unityVfx)
                {
                    rttr::instance inst = const_cast<UnityVfxComponent&>(*unityVfx);
                    outEntity["UnityVfx"] = JsonRttr::ToJsonObject(inst);
                }

                // PhysX Components
                if (const auto* rigidBody = world.GetComponent<Phy_RigidBodyComponent>(entity); rigidBody)
                {
                    rttr::instance inst = const_cast<Phy_RigidBodyComponent&>(*rigidBody);
                    outEntity["RigidBody"] = JsonRttr::ToJsonObject(inst);
                }

                if (const auto* collider = world.GetComponent<Phy_ColliderComponent>(entity); collider)
                {
                    rttr::instance inst = const_cast<Phy_ColliderComponent&>(*collider);
                    outEntity["Collider"] = JsonRttr::ToJsonObject(inst);
                }

                if (const auto* meshCollider = world.GetComponent<Phy_MeshColliderComponent>(entity); meshCollider)
                {
                    rttr::instance inst = const_cast<Phy_MeshColliderComponent&>(*meshCollider);
                    outEntity["MeshCollider"] = JsonRttr::ToJsonObject(inst);
                }

                if (const auto* cct = world.GetComponent<Phy_CCTComponent>(entity); cct)
                {
                    rttr::instance inst = const_cast<Phy_CCTComponent&>(*cct);
                    outEntity["CharacterController"] = JsonRttr::ToJsonObject(inst);
                }

                if (const auto* terrain = world.GetComponent<Phy_TerrainHeightFieldComponent>(entity); terrain)
                {
                    rttr::instance inst = const_cast<Phy_TerrainHeightFieldComponent&>(*terrain);
                    outEntity["TerrainHeightField"] = JsonRttr::ToJsonObject(inst);
                }

                if (const auto* physicsSettings = world.GetComponent<Phy_SettingsComponent>(entity); physicsSettings)
                {
                    outEntity["PhysicsSceneSettings"] = WritePhysicsSceneSettings(*physicsSettings);
                }

                if (const auto* joint = world.GetComponent<Phy_JointComponent>(entity); joint)
                {
                    rttr::instance inst = const_cast<Phy_JointComponent&>(*joint);
                    outEntity["Joint"] = JsonRttr::ToJsonObject(inst);
                }

                return true;
            }

            static bool ApplyEntityFromJson(World& world, EntityId entity, const JsonRttr::json& root)
            {
                if (entity == InvalidEntityId)
                    return false;

                const std::string name = root.value("name", std::string{});
                if (!name.empty())
                    world.SetEntityName(entity, name);

                // Transform
                TransformComponent& t = world.AddComponent<TransformComponent>(entity);
                auto itT = root.find("Transform");
                if (itT != root.end() && itT->is_object())
                {
                    rttr::instance inst = t;
                    if (!JsonRttr::FromJsonObject(inst, *itT))
                        return false;
                    if (itT->find("visible") == itT->end())
                    {
                        auto itLegacy = itT->find("renderEnabled");
                        if (itLegacy != itT->end())
                        {
                            if (itLegacy->is_boolean())
                                t.visible = itLegacy->get<bool>();
                            else if (itLegacy->is_number())
                                t.visible = (itLegacy->get<double>() != 0.0);
                        }
                    }
                }

                // Scripts (여러 개)
                auto itS = root.find("Scripts");
                if (itS != root.end() && itS->is_array())
                {
                    for (const auto& s : *itS)
                    {
                        if (!s.is_object()) continue;
                        const std::string sn = s.value("name", std::string{});
                        if (sn.empty()) continue;

                        ScriptComponent& sc = world.AddScript(entity, sn);
                        sc.enabled = s.value("enabled", true);

                        auto itP = s.find("props");
                        if (itP != s.end() && itP->is_object() && sc.instance)
                        {
                            rttr::instance inst = *sc.instance;
                            const rttr::type t = rttr::type::get_by_name(sc.scriptName);
                            if (!JsonRttr::FromJsonObject(inst, *itP, t))
                                return false;
                            sc.defaultsApplied = true;
                        }
                    }
                }

                // Material
                auto itM = root.find("Material");
                if (itM != root.end() && itM->is_object())
                {
                    MaterialComponent& mc = world.AddComponent<MaterialComponent>(entity, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
                    rttr::instance inst = mc;
                    if (!JsonRttr::FromJsonObject(inst, *itM))
                        return false;
                }

                auto itD = root.find("Decal");
                if (itD != root.end() && itD->is_object())
                {
                    DecalComponent& dc = world.AddComponent<DecalComponent>(entity);
                    rttr::instance inst = dc;
                    if (!JsonRttr::FromJsonObject(inst, *itD))
                        return false;
                }

                // SkinnedMesh
                auto itSM = root.find("SkinnedMesh");
                if (itSM != root.end() && itSM->is_object())
                {
                    SkinnedMeshComponent tmp;
                    rttr::instance instTmp = tmp;
                    if (!JsonRttr::FromJsonObject(instTmp, *itSM))
                        return false;

                    ResolveSkinnedMeshPathsForLoad(tmp);

                    if (!tmp.meshAssetPath.empty())
                    {
                        SkinnedMeshComponent& sm = world.AddComponent<SkinnedMeshComponent>(entity, tmp.meshAssetPath);
                        sm.instanceAssetPath = tmp.instanceAssetPath;
                        sm.boneMatrices = &g_IdentityBone;
                        sm.boneCount = 1;
                    }
                }

                // SkinnedAnimation
                auto itSA = root.find("SkinnedAnimation");
                if (itSA != root.end() && itSA->is_object())
                {
                    SkinnedAnimationComponent& sa = world.AddComponent<SkinnedAnimationComponent>(entity);
                    rttr::instance inst = sa;
                    if (!JsonRttr::FromJsonObject(inst, *itSA))
                        return false;
                }

                // AdvancedAnimation
                auto itAA = root.find("AdvancedAnimation");
                if (itAA != root.end() && itAA->is_object())
                {
                    AdvancedAnimationComponent& aa = world.AddComponent<AdvancedAnimationComponent>(entity);
                    rttr::instance inst = aa;
                    if (!JsonRttr::FromJsonObject(inst, *itAA))
                        return false;
                    if (auto it = itAA->find("rootBoneLock"); it != itAA->end())
                    {
                        if (it->is_boolean())
                            aa.rootBoneLock = it->get<bool>();
                        else if (it->is_number())
                            aa.rootBoneLock = (it->get<double>() != 0.0);
                    }
                    if (auto it = itAA->find("rootMotionDriveCct"); it != itAA->end())
                    {
                        if (it->is_boolean())
                            aa.rootMotionDriveCct = it->get<bool>();
                        else if (it->is_number())
                            aa.rootMotionDriveCct = (it->get<double>() != 0.0);
                    }
                }

                // Camera
                auto itC = root.find("Camera");
                if (itC != root.end() && itC->is_object())
                {
                    CameraComponent& cc = world.AddComponent<CameraComponent>(entity);
                    rttr::instance inst = cc;
                    if (!JsonRttr::FromJsonObject(inst, *itC))
                        return false;
                }

                // CameraFollow
                auto itCF = root.find("CameraFollow");
                if (itCF != root.end() && itCF->is_object())
                {
                    CameraFollowComponent& cf = world.AddComponent<CameraFollowComponent>(entity);
                    rttr::instance inst = cf;
                    if (!JsonRttr::FromJsonObject(inst, *itCF))
                        return false;
                }

                // CameraSpringArm
                auto itSpring = root.find("CameraSpringArm");
                if (itSpring != root.end() && itSpring->is_object())
                {
                    CameraSpringArmComponent& sa = world.AddComponent<CameraSpringArmComponent>(entity);
                    rttr::instance inst = sa;
                    if (!JsonRttr::FromJsonObject(inst, *itSpring))
                        return false;
                }

                // CameraLookAt
                auto itLA = root.find("CameraLookAt");
                if (itLA != root.end() && itLA->is_object())
                {
                    CameraLookAtComponent& la = world.AddComponent<CameraLookAtComponent>(entity);
                    rttr::instance inst = la;
                    if (!JsonRttr::FromJsonObject(inst, *itLA))
                        return false;
                }

                // CameraShake
                auto itCS = root.find("CameraShake");
                if (itCS != root.end() && itCS->is_object())
                {
                    CameraShakeComponent& cs = world.AddComponent<CameraShakeComponent>(entity);
                    rttr::instance inst = cs;
                    if (!JsonRttr::FromJsonObject(inst, *itCS))
                        return false;
                }

                // CameraBlend
                auto itCB = root.find("CameraBlend");
                if (itCB != root.end() && itCB->is_object())
                {
                    CameraBlendComponent& cb = world.AddComponent<CameraBlendComponent>(entity);
                    rttr::instance inst = cb;
                    if (!JsonRttr::FromJsonObject(inst, *itCB))
                        return false;
                }

                // CameraInput
                auto itCI = root.find("CameraInput");
                if (itCI != root.end() && itCI->is_object())
                {
                    CameraInputComponent& ci = world.AddComponent<CameraInputComponent>(entity);
                    rttr::instance inst = ci;
                    if (!JsonRttr::FromJsonObject(inst, *itCI))
                        return false;
                }

                // Socket (소켓 정의 목록)
                auto itSocket = root.find("Socket");
                if (itSocket != root.end() && itSocket->is_object())
                {
                    SocketComponent& sc = world.AddComponent<SocketComponent>(entity);
                    if (!SocketSerialization::JsonToSocketComponent(*itSocket, sc))
                        return false;
                }

                // SocketAttachment
                auto itSAc = root.find("SocketAttachment");
                if (itSAc != root.end() && itSAc->is_object())
                {
                    SocketAttachmentComponent& sa = world.AddComponent<SocketAttachmentComponent>(entity);
                    if (auto itGuid = itSAc->find("ownerGuid"); itGuid != itSAc->end())
                        sa.ownerGuid = ParseGuidOrZero(*itGuid);

                    JsonRttr::json copy = *itSAc;
                    copy.erase("ownerGuid");
                    rttr::instance inst = sa;
                    if (!JsonRttr::FromJsonObject(inst, copy))
                        return false;
                }

                // Hurtbox
                auto itHB = root.find("Hurtbox");
                if (itHB != root.end() && itHB->is_object())
                {
                    HurtboxComponent& hb = world.AddComponent<HurtboxComponent>(entity);
                    if (auto itGuid = itHB->find("ownerGuid"); itGuid != itHB->end())
                        hb.ownerGuid = ParseGuidOrZero(*itGuid);

                    JsonRttr::json copy = *itHB;
                    copy.erase("ownerGuid");
                    rttr::instance inst = hb;
                    if (!JsonRttr::FromJsonObject(inst, copy))
                        return false;
                }

                // WeaponTrace
                auto itWT = root.find("WeaponTrace");
                if (itWT != root.end() && itWT->is_object())
                {
                    WeaponTraceComponent& wt = world.AddComponent<WeaponTraceComponent>(entity);
                    if (!WeaponTraceSerialization::JsonToWeaponTraceComponent(*itWT, wt))
                        return false;
                }

                // Health
                auto itHealth = root.find("Health");
                if (itHealth != root.end() && itHealth->is_object())
                {
                    HealthComponent& hc = world.AddComponent<HealthComponent>(entity);
                    rttr::instance inst = hc;
                    if (!JsonRttr::FromJsonObject(inst, *itHealth))
                        return false;
                }

                // AttackDriver
                auto itAttackDriver = root.find("AttackDriver");
                if (itAttackDriver != root.end() && itAttackDriver->is_object())
                {
                    AttackDriverComponent& ad = world.AddComponent<AttackDriverComponent>(entity);
                    if (!AttackDriverSerialization::JsonToAttackDriverComponent(*itAttackDriver, ad))
                        return false;
                }

                // AliceUI Components
                auto itUIWidget = root.find("UIWidget");
                if (itUIWidget != root.end() && itUIWidget->is_object())
                {
                    UIWidgetComponent& comp = world.AddComponent<UIWidgetComponent>(entity);
                    rttr::instance inst = comp;
                    if (!JsonRttr::FromJsonObject(inst, *itUIWidget))
                        return false;
                }


                auto itUITransform = root.find("UITransform");
                if (itUITransform != root.end() && itUITransform->is_object())
                {
                    UITransformComponent& comp = world.AddComponent<UITransformComponent>(entity);
                    rttr::instance inst = comp;
                    if (!JsonRttr::FromJsonObject(inst, *itUITransform))
                        return false;
                }
                auto itUIImage = root.find("UIImage");
                if (itUIImage != root.end() && itUIImage->is_object())
                {
                    UIImageComponent& comp = world.AddComponent<UIImageComponent>(entity);
                    rttr::instance inst = comp;
                    if (!JsonRttr::FromJsonObject(inst, *itUIImage))
                        return false;
                }
                auto itUIText = root.find("UIText");
                if (itUIText != root.end() && itUIText->is_object())
                {
                    UITextComponent& comp = world.AddComponent<UITextComponent>(entity);
                    rttr::instance inst = comp;
                    if (!JsonRttr::FromJsonObject(inst, *itUIText))
                        return false;
                }
                auto itUIButton = root.find("UIButton");
                if (itUIButton != root.end() && itUIButton->is_object())
                {
                    UIButtonComponent& comp = world.AddComponent<UIButtonComponent>(entity);
                    rttr::instance inst = comp;
                    if (!JsonRttr::FromJsonObject(inst, *itUIButton))
                        return false;
                }
                auto itUIGauge = root.find("UIGauge");
                if (itUIGauge != root.end() && itUIGauge->is_object())
                {
                    UIGaugeComponent& comp = world.AddComponent<UIGaugeComponent>(entity);
                    rttr::instance inst = comp;
                    if (!JsonRttr::FromJsonObject(inst, *itUIGauge))
                        return false;
                }

                // Point Light
                auto itPL = root.find("PointLight");
                if (itPL != root.end() && itPL->is_object())
                {
                    PointLightComponent& pl = world.AddComponent<PointLightComponent>(entity);
                    rttr::instance inst = pl;
                    if (!JsonRttr::FromJsonObject(inst, *itPL))
                        return false;
                }

                // Spot Light
                auto itSL = root.find("SpotLight");
                if (itSL != root.end() && itSL->is_object())
                {
                    SpotLightComponent& sl = world.AddComponent<SpotLightComponent>(entity);
                    rttr::instance inst = sl;
                    if (!JsonRttr::FromJsonObject(inst, *itSL))
                        return false;
                }
                // Rect Light
                auto itRL = root.find("RectLight");
                if (itRL != root.end() && itRL->is_object())
                {
                    RectLightComponent& rl = world.AddComponent<RectLightComponent>(entity);
                    rttr::instance inst = rl;
                    if (!JsonRttr::FromJsonObject(inst, *itRL))
                        return false;
                }

                auto itComputeEffect = root.find("ComputeEffect");
                if (itComputeEffect != root.end() && itComputeEffect->is_object())
                {
                    ComputeEffectComponent& ce = world.AddComponent<ComputeEffectComponent>(entity);
                    rttr::instance inst = ce;
                    if (!JsonRttr::FromJsonObject(inst, *itComputeEffect))
                        return false;
                }

                auto itUnityVfx = root.find("UnityVfx");
                if (itUnityVfx != root.end() && itUnityVfx->is_object())
                {
                    UnityVfxComponent& uv = world.AddComponent<UnityVfxComponent>(entity);
                    rttr::instance inst = uv;
                    if (!JsonRttr::FromJsonObject(inst, *itUnityVfx))
                        return false;
                }

                // PhysX Components
                auto itRB = root.find("RigidBody");
                if (itRB != root.end() && itRB->is_object())
                {
                    Phy_RigidBodyComponent& rb = world.AddComponent<Phy_RigidBodyComponent>(entity);
                    rttr::instance inst = rb;
                    if (!JsonRttr::FromJsonObject(inst, *itRB))
                        return false;
                }

                auto itCollider = root.find("Collider");
                if (itCollider != root.end() && itCollider->is_object())
                {
                    Phy_ColliderComponent& col = world.AddComponent<Phy_ColliderComponent>(entity);
                    rttr::instance inst = col;
                    if (!JsonRttr::FromJsonObject(inst, *itCollider))
                        return false;
                }

                auto itMeshCollider = root.find("MeshCollider");
                if (itMeshCollider != root.end() && itMeshCollider->is_object())
                {
                    Phy_MeshColliderComponent& mc = world.AddComponent<Phy_MeshColliderComponent>(entity);
                    rttr::instance inst = mc;
                    if (!JsonRttr::FromJsonObject(inst, *itMeshCollider))
                        return false;
                }

                auto itCCT = root.find("CharacterController");
                if (itCCT != root.end() && itCCT->is_object())
                {
                    Phy_CCTComponent& cct = world.AddComponent<Phy_CCTComponent>(entity);
                    rttr::instance inst = cct;
                    if (!JsonRttr::FromJsonObject(inst, *itCCT))
                        return false;
                }

                auto itTerrain = root.find("TerrainHeightField");
                if (itTerrain != root.end() && itTerrain->is_object())
                {
                    Phy_TerrainHeightFieldComponent& terrain = world.AddComponent<Phy_TerrainHeightFieldComponent>(entity);
                    rttr::instance inst = terrain;
                    if (!JsonRttr::FromJsonObject(inst, *itTerrain))
                        return false;
                }

                auto itJoint = root.find("Joint");
                if (itJoint != root.end() && itJoint->is_object())
                {
                    Phy_JointComponent& joint = world.AddComponent<Phy_JointComponent>(entity);
                    rttr::instance inst = joint;
                    if (!JsonRttr::FromJsonObject(inst, *itJoint))
                        return false;
                }

                auto itPhysicsSettings = root.find("PhysicsSceneSettings");
                if (itPhysicsSettings != root.end() && itPhysicsSettings->is_object())
                {
                    Phy_SettingsComponent& ps = world.AddComponent<Phy_SettingsComponent>(entity);
                    if (!LoadPhysicsSceneSettings(ps, *itPhysicsSettings))
                        return false;
                }

                return true;
            }

            static ResourceManager* ResolvePrefabResourceManager()
            {
                if (g_DefaultResources)
                    return g_DefaultResources;
                return ResourceManager::GetPtr();
            }

            static bool LoadPrefabJsonAuto(const std::filesystem::path& logicalPath, JsonRttr::json& out)
            {
                out = JsonRttr::json{};

                ResourceManager* resources = ResolvePrefabResourceManager();
                if (!resources)
                {
                    ALICE_LOG_ERRORF("[Prefab] LoadText failed: ResourceManager is null. logical=\"%s\"",
                                     logicalPath.generic_string().c_str());
                    return false;
                }

                std::string text;
                if (!resources->LoadText(logicalPath, text) || text.empty())
                {
                    ALICE_LOG_ERRORF("[Prefab] LoadText failed: \"%s\"", logicalPath.generic_string().c_str());
                    return false;
                }

                try
                {
                    out = JsonRttr::json::parse(text);
                }
                catch (...)
                {
                    ALICE_LOG_ERRORF("[Prefab] JSON parse failed: \"%s\"", logicalPath.generic_string().c_str());
                    return false;
                }

                return true;
            }

            static EntityId InstantiateFromJsonInternal(World& world, const JsonRttr::json& root)
            {
                if (!root.is_object())
                    return InvalidEntityId;

                auto itEntities = root.find("entities");
                if (itEntities != root.end() && itEntities->is_array())
                {
                    std::unordered_map<std::uint64_t, EntityId> guidToEntity;
                    std::vector<std::pair<EntityId, std::uint64_t>> pendingParents;
                    std::vector<EntityId> createdEntities;

                    for (const auto& e : *itEntities)
                    {
                        if (!e.is_object())
                            continue;

                        EntityId id = world.CreateEntity();
                        createdEntities.push_back(id);

                        if (!ApplyEntityFromJson(world, id, e))
                        {
                            for (EntityId created : createdEntities)
                                world.DestroyEntity(created);
                            return InvalidEntityId;
                        }

                        std::uint64_t guid = 0;
                        if (auto itGuid = e.find("guid"); itGuid != e.end())
                            guid = ParseGuidOrZero(*itGuid);
                        if (guid != 0)
                            guidToEntity[guid] = id;

                        if (auto itParent = e.find("_parentGuid"); itParent != e.end())
                        {
                            std::uint64_t parentGuid = ParseGuidOrZero(*itParent);
                            if (parentGuid != 0)
                                pendingParents.push_back({ id, parentGuid });
                        }
                    }

                    for (const auto& [childId, parentGuid] : pendingParents)
                    {
                        auto it = guidToEntity.find(parentGuid);
                        if (it != guidToEntity.end())
                            world.SetParent(childId, it->second, false);
                    }

                    EntityId rootEntity = InvalidEntityId;
                    if (auto itRootGuid = root.find("rootGuid"); itRootGuid != root.end())
                    {
                        std::uint64_t rootGuid = ParseGuidOrZero(*itRootGuid);
                        if (rootGuid != 0)
                        {
                            auto it = guidToEntity.find(rootGuid);
                            if (it != guidToEntity.end())
                                rootEntity = it->second;
                        }
                    }

                    if (rootEntity == InvalidEntityId && !createdEntities.empty())
                    {
                        std::unordered_set<EntityId> hasParent;
                        for (const auto& [childId, parentGuid] : pendingParents)
                        {
                            if (guidToEntity.find(parentGuid) != guidToEntity.end())
                                hasParent.insert(childId);
                        }
                        for (EntityId id : createdEntities)
                        {
                            if (hasParent.find(id) == hasParent.end())
                            {
                                rootEntity = id;
                                break;
                            }
                        }
                        if (rootEntity == InvalidEntityId)
                            rootEntity = createdEntities.front();
                    }

                    return rootEntity;
                }

                EntityId entity = world.CreateEntity();
                if (!ApplyEntityFromJson(world, entity, root))
                {
                    world.DestroyEntity(entity);
                    return InvalidEntityId;
                }

                return entity;
            }
        }

        EntityId InstantiateFromFile(World& world, const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return InvalidEntityId;

            JsonRttr::json root;
            if (!JsonRttr::LoadJsonFile(path, root))
                return InvalidEntityId;

            return InstantiateFromJsonInternal(world, root);
        }

        void SetDefaultWorld(World* world)
        {
            g_DefaultWorld = world;
        }

        void SetDefaultResources(ResourceManager* resources)
        {
            g_DefaultResources = resources;
        }

        EntityId InstantiateFromFileAuto(const std::filesystem::path& logicalPath)
        {
            if (!g_DefaultWorld)
            {
                ALICE_LOG_ERRORF("[Prefab] InstantiateFromFileAuto failed: default world is null.");
                return InvalidEntityId;
            }

            JsonRttr::json root;
            if (!LoadPrefabJsonAuto(logicalPath, root))
                return InvalidEntityId;

            return InstantiateFromJsonInternal(*g_DefaultWorld, root);
        }

        bool SaveToFile(const World& world,
                        EntityId entity,
                        const std::filesystem::path& path)
        {
            if (entity == InvalidEntityId)
                return false;

            std::vector<EntityId> tree;
            CollectEntityTree(world, entity, tree);
            if (tree.empty())
                return false;

            JsonRttr::json root = JsonRttr::json::object();

            if (tree.size() == 1)
            {
                if (!WriteEntityJson(world, entity, root))
                    return false;
                root["version"] = 1;
                return JsonRttr::SaveJsonFile(path, root, 4);
            }

            root["version"] = 2;
            if (const auto* idComp = world.GetComponent<IDComponent>(entity); idComp)
                root["rootGuid"] = std::to_string(idComp->guid);

            std::unordered_map<EntityId, std::uint64_t> guidByEntity;
            guidByEntity.reserve(tree.size());
            for (EntityId id : tree)
            {
                if (const auto* idComp = world.GetComponent<IDComponent>(id); idComp)
                    guidByEntity[id] = idComp->guid;
            }

            JsonRttr::json entities = JsonRttr::json::array();
            for (EntityId id : tree)
            {
                JsonRttr::json e;
                if (!WriteEntityJson(world, id, e))
                    return false;

                auto itGuid = guidByEntity.find(id);
                if (itGuid != guidByEntity.end() && itGuid->second != 0)
                    e["guid"] = std::to_string(itGuid->second);

                EntityId parentId = world.GetParent(id);
                if (parentId != InvalidEntityId)
                {
                    auto itParent = guidByEntity.find(parentId);
                    if (itParent != guidByEntity.end() && itParent->second != 0)
                        e["_parentGuid"] = std::to_string(itParent->second);
                }

                entities.push_back(e);
            }

            root["entities"] = entities;
            return JsonRttr::SaveJsonFile(path, root, 4);
        }
    }
}



