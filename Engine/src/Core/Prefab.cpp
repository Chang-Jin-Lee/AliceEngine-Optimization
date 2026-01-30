#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Core/Prefab.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Core/JsonRttr.h"
#include "Core/ResourceManager.h"

#include "Core/World.h"
#include "Components/ScriptComponent.h"
#include "Components/MaterialComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/CameraComponent.h"
#include "Components/CameraFollowComponent.h"
#include "Components/CameraSpringArmComponent.h"
#include "Components/CameraLookAtComponent.h"
#include "Components/CameraShakeComponent.h"
#include "Components/CameraBlendComponent.h"
#include "Components/CameraInputComponent.h"
#include "Components/SocketAttachmentComponent.h"
#include "Components/HurtboxComponent.h"
#include "Components/WeaponTraceComponent.h"
#include "Components/HealthComponent.h"
#include "Components/AttackDriverComponent.h"
#include "Components/SocketComponent.h"
#include "Components/AdvancedAnimationComponent.h"
#include "Core/SocketSerialization.h"
#include "Core/AttackDriverSerialization.h"
#include "Core/WeaponTraceSerialization.h"

#include "PhysX/Components/Phy_RigidBodyComponent.h"
#include "PhysX/Components/Phy_ColliderComponent.h"
#include "PhysX/Components/Phy_MeshColliderComponent.h"
#include "PhysX/Components/Phy_CCTComponent.h"
#include "PhysX/Components/Phy_TerrainHeightFieldComponent.h"
#include "PhysX/Components/Phy_SettingsComponent.h"
#include "PhysX/Components/Phy_JointComponent.h"

#include "AliceUI/UIWidgetComponent.h"
#include "AliceUI/UITransformComponent.h"
#include "AliceUI/UIImageComponent.h"
#include "AliceUI/UITextComponent.h"
#include "AliceUI/UIButtonComponent.h"
#include "AliceUI/UIGaugeComponent.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DirectXMath.h>

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
        }

        EntityId InstantiateFromFile(World& world, const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return InvalidEntityId;

            JsonRttr::json root;
            if (!JsonRttr::LoadJsonFile(path, root))
                return InvalidEntityId;
            if (!root.is_object())
                return InvalidEntityId;

            // 엔티티 생성
            EntityId entity = world.CreateEntity();

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
                    return InvalidEntityId;
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
                            return InvalidEntityId;
                        sc.defaultsApplied = true; // 프리팹이 값 주입 완료
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
                    return InvalidEntityId;
            }

            // SkinnedMesh
            auto itSM = root.find("SkinnedMesh");
            if (itSM != root.end() && itSM->is_object())
            {
                SkinnedMeshComponent tmp;
                rttr::instance instTmp = tmp;
                if (!JsonRttr::FromJsonObject(instTmp, *itSM))
                    return InvalidEntityId;

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
                    return InvalidEntityId;
            }

            // AdvancedAnimation
            auto itAA = root.find("AdvancedAnimation");
            if (itAA != root.end() && itAA->is_object())
            {
                AdvancedAnimationComponent& aa = world.AddComponent<AdvancedAnimationComponent>(entity);
                rttr::instance inst = aa;
                if (!JsonRttr::FromJsonObject(inst, *itAA))
                    return InvalidEntityId;
            }

            // Camera
            auto itC = root.find("Camera");
            if (itC != root.end() && itC->is_object())
            {
                CameraComponent& cc = world.AddComponent<CameraComponent>(entity);
                rttr::instance inst = cc;
                if (!JsonRttr::FromJsonObject(inst, *itC))
                    return InvalidEntityId;
            }

            // CameraFollow
            auto itCF = root.find("CameraFollow");
            if (itCF != root.end() && itCF->is_object())
            {
                CameraFollowComponent& cf = world.AddComponent<CameraFollowComponent>(entity);
                rttr::instance inst = cf;
                if (!JsonRttr::FromJsonObject(inst, *itCF))
                    return InvalidEntityId;
            }

            // CameraSpringArm
            auto itSpring = root.find("CameraSpringArm");
            if (itSpring != root.end() && itSpring->is_object())
            {
                CameraSpringArmComponent& sa = world.AddComponent<CameraSpringArmComponent>(entity);
                rttr::instance inst = sa;
                if (!JsonRttr::FromJsonObject(inst, *itSpring))
                    return InvalidEntityId;
            }

            // CameraLookAt
            auto itLA = root.find("CameraLookAt");
            if (itLA != root.end() && itLA->is_object())
            {
                CameraLookAtComponent& la = world.AddComponent<CameraLookAtComponent>(entity);
                rttr::instance inst = la;
                if (!JsonRttr::FromJsonObject(inst, *itLA))
                    return InvalidEntityId;
            }

            // CameraShake
            auto itCS = root.find("CameraShake");
            if (itCS != root.end() && itCS->is_object())
            {
                CameraShakeComponent& cs = world.AddComponent<CameraShakeComponent>(entity);
                rttr::instance inst = cs;
                if (!JsonRttr::FromJsonObject(inst, *itCS))
                    return InvalidEntityId;
            }

            // CameraBlend
            auto itCB = root.find("CameraBlend");
            if (itCB != root.end() && itCB->is_object())
            {
                CameraBlendComponent& cb = world.AddComponent<CameraBlendComponent>(entity);
                rttr::instance inst = cb;
                if (!JsonRttr::FromJsonObject(inst, *itCB))
                    return InvalidEntityId;
            }

            // CameraInput
            auto itCI = root.find("CameraInput");
            if (itCI != root.end() && itCI->is_object())
            {
                CameraInputComponent& ci = world.AddComponent<CameraInputComponent>(entity);
                rttr::instance inst = ci;
                if (!JsonRttr::FromJsonObject(inst, *itCI))
                    return InvalidEntityId;
            }

            // Socket (소켓 정의 목록)
            auto itSocket = root.find("Socket");
            if (itSocket != root.end() && itSocket->is_object())
            {
                SocketComponent& sc = world.AddComponent<SocketComponent>(entity);
                if (!SocketSerialization::JsonToSocketComponent(*itSocket, sc))
                    return InvalidEntityId;
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
                    return InvalidEntityId;
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
                    return InvalidEntityId;
            }

            // WeaponTrace
            auto itWT = root.find("WeaponTrace");
            if (itWT != root.end() && itWT->is_object())
            {
                WeaponTraceComponent& wt = world.AddComponent<WeaponTraceComponent>(entity);
                if (!WeaponTraceSerialization::JsonToWeaponTraceComponent(*itWT, wt))
                    return InvalidEntityId;
            }

            // Health
            auto itHealth = root.find("Health");
            if (itHealth != root.end() && itHealth->is_object())
            {
                HealthComponent& hc = world.AddComponent<HealthComponent>(entity);
                rttr::instance inst = hc;
                if (!JsonRttr::FromJsonObject(inst, *itHealth))
                    return InvalidEntityId;
            }

            // AttackDriver
            auto itAttackDriver = root.find("AttackDriver");
            if (itAttackDriver != root.end() && itAttackDriver->is_object())
            {
                AttackDriverComponent& ad = world.AddComponent<AttackDriverComponent>(entity);
                if (!AttackDriverSerialization::JsonToAttackDriverComponent(*itAttackDriver, ad))
                    return InvalidEntityId;
            }

            // AliceUI Components
            auto itUIWidget = root.find("UIWidget");
            if (itUIWidget != root.end() && itUIWidget->is_object())
            {
                UIWidgetComponent& comp = world.AddComponent<UIWidgetComponent>(entity);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIWidget))
                    return InvalidEntityId;
            }
            auto itUITransform = root.find("UITransform");
            if (itUITransform != root.end() && itUITransform->is_object())
            {
                UITransformComponent& comp = world.AddComponent<UITransformComponent>(entity);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUITransform))
                    return InvalidEntityId;
            }
            auto itUIImage = root.find("UIImage");
            if (itUIImage != root.end() && itUIImage->is_object())
            {
                UIImageComponent& comp = world.AddComponent<UIImageComponent>(entity);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIImage))
                    return InvalidEntityId;
            }
            auto itUIText = root.find("UIText");
            if (itUIText != root.end() && itUIText->is_object())
            {
                UITextComponent& comp = world.AddComponent<UITextComponent>(entity);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIText))
                    return InvalidEntityId;
            }
            auto itUIButton = root.find("UIButton");
            if (itUIButton != root.end() && itUIButton->is_object())
            {
                UIButtonComponent& comp = world.AddComponent<UIButtonComponent>(entity);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIButton))
                    return InvalidEntityId;
            }
            auto itUIGauge = root.find("UIGauge");
            if (itUIGauge != root.end() && itUIGauge->is_object())
            {
                UIGaugeComponent& comp = world.AddComponent<UIGaugeComponent>(entity);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIGauge))
                    return InvalidEntityId;
            }

            // Point Light
            auto itPL = root.find("PointLight");
            if (itPL != root.end() && itPL->is_object())
            {
                PointLightComponent& pl = world.AddComponent<PointLightComponent>(entity);
                rttr::instance inst = pl;
                if (!JsonRttr::FromJsonObject(inst, *itPL))
                    return InvalidEntityId;
            }

            // Spot Light
            auto itSL = root.find("SpotLight");
            if (itSL != root.end() && itSL->is_object())
            {
                SpotLightComponent& sl = world.AddComponent<SpotLightComponent>(entity);
                rttr::instance inst = sl;
                if (!JsonRttr::FromJsonObject(inst, *itSL))
                    return InvalidEntityId;
            }

            // Rect Light
            auto itRL = root.find("RectLight");
            if (itRL != root.end() && itRL->is_object())
            {
                RectLightComponent& rl = world.AddComponent<RectLightComponent>(entity);
                rttr::instance inst = rl;
                if (!JsonRttr::FromJsonObject(inst, *itRL))
                    return InvalidEntityId;
            }

            // PhysX Components
            auto itRB = root.find("RigidBody");
            if (itRB != root.end() && itRB->is_object())
            {
                Phy_RigidBodyComponent& rb = world.AddComponent<Phy_RigidBodyComponent>(entity);
                rttr::instance inst = rb;
                if (!JsonRttr::FromJsonObject(inst, *itRB))
                    return InvalidEntityId;
            }

            auto itCollider = root.find("Collider");
            if (itCollider != root.end() && itCollider->is_object())
            {
                Phy_ColliderComponent& col = world.AddComponent<Phy_ColliderComponent>(entity);
                rttr::instance inst = col;
                if (!JsonRttr::FromJsonObject(inst, *itCollider))
                    return InvalidEntityId;
            }

            auto itMeshCollider = root.find("MeshCollider");
            if (itMeshCollider != root.end() && itMeshCollider->is_object())
            {
                Phy_MeshColliderComponent& mc = world.AddComponent<Phy_MeshColliderComponent>(entity);
                rttr::instance inst = mc;
                if (!JsonRttr::FromJsonObject(inst, *itMeshCollider))
                    return InvalidEntityId;
            }

            auto itCCT = root.find("CharacterController");
            if (itCCT != root.end() && itCCT->is_object())
            {
                Phy_CCTComponent& cct = world.AddComponent<Phy_CCTComponent>(entity);
                rttr::instance inst = cct;
                if (!JsonRttr::FromJsonObject(inst, *itCCT))
                    return InvalidEntityId;
            }

            auto itTerrain = root.find("TerrainHeightField");
            if (itTerrain != root.end() && itTerrain->is_object())
            {
                Phy_TerrainHeightFieldComponent& terrain = world.AddComponent<Phy_TerrainHeightFieldComponent>(entity);
                rttr::instance inst = terrain;
                if (!JsonRttr::FromJsonObject(inst, *itTerrain))
                    return InvalidEntityId;
            }

            auto itJoint = root.find("Joint");
            if (itJoint != root.end() && itJoint->is_object())
            {
                Phy_JointComponent& joint = world.AddComponent<Phy_JointComponent>(entity);
                rttr::instance inst = joint;
                if (!JsonRttr::FromJsonObject(inst, *itJoint))
                    return InvalidEntityId;
            }

            auto itPhysicsSettings = root.find("PhysicsSceneSettings");
            if (itPhysicsSettings != root.end() && itPhysicsSettings->is_object())
            {
                Phy_SettingsComponent& ps = world.AddComponent<Phy_SettingsComponent>(entity);
                // 수동 역직렬화 사용 (중첩 배열 보장)
                if (!LoadPhysicsSceneSettings(ps, *itPhysicsSettings))
                    return InvalidEntityId;
            }

            return entity;
        }

        bool SaveToFile(const World& world,
                        EntityId entity,
                        const std::filesystem::path& path)
        {
            if (entity == InvalidEntityId) return false;

            const TransformComponent* t = world.GetComponent<TransformComponent>(entity);
            if (!t)
                return false;

            JsonRttr::json root = JsonRttr::json::object();
            root["version"] = 1;

            const std::string name = world.GetEntityName(entity);
            if (!name.empty())
                root["name"] = name;

            // Transform
            {
                rttr::instance inst = const_cast<TransformComponent&>(*t);
                root["Transform"] = JsonRttr::ToJsonObject(inst);
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
                root["Scripts"] = arr;
            }

            // Material
            if (const auto* mat = world.GetComponent<MaterialComponent>(entity); mat)
            {
                // 경로를 상대 경로로 변환하기 위해 복사본 생성
                MaterialComponent matCopy = *mat;
                matCopy.assetPath = NormalizePathToRelative(matCopy.assetPath);
                matCopy.albedoTexturePath = NormalizePathToRelative(matCopy.albedoTexturePath);
                
                rttr::instance inst = matCopy;
                root["Material"] = JsonRttr::ToJsonObject(inst);
            }

            // SkinnedMesh
            if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(entity); skinned)
            {
                // 경로를 상대 경로로 변환하기 위해 복사본 생성
                SkinnedMeshComponent skinnedCopy = *skinned;
                skinnedCopy.instanceAssetPath = NormalizePathToRelative(skinnedCopy.instanceAssetPath);
                skinnedCopy.meshAssetPath = NormalizePathToRelative(skinnedCopy.meshAssetPath);
                
                rttr::instance inst = skinnedCopy;
                root["SkinnedMesh"] = JsonRttr::ToJsonObject(inst);
            }

            // SkinnedAnimation
            if (const auto* anim = world.GetComponent<SkinnedAnimationComponent>(entity); anim)
            {
                rttr::instance inst = const_cast<SkinnedAnimationComponent&>(*anim);
                root["SkinnedAnimation"] = JsonRttr::ToJsonObject(inst);
            }

            // AdvancedAnimation
            if (const auto* advAnim = world.GetComponent<AdvancedAnimationComponent>(entity); advAnim)
            {
                rttr::instance inst = const_cast<AdvancedAnimationComponent&>(*advAnim);
                root["AdvancedAnimation"] = JsonRttr::ToJsonObject(inst);
            }

            // Camera
            if (const auto* cam = world.GetComponent<CameraComponent>(entity); cam)
            {
                rttr::instance inst = const_cast<CameraComponent&>(*cam);
                root["Camera"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraFollow
            if (const auto* follow = world.GetComponent<CameraFollowComponent>(entity); follow)
            {
                rttr::instance inst = const_cast<CameraFollowComponent&>(*follow);
                root["CameraFollow"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraSpringArm
            if (const auto* spring = world.GetComponent<CameraSpringArmComponent>(entity); spring)
            {
                rttr::instance inst = const_cast<CameraSpringArmComponent&>(*spring);
                root["CameraSpringArm"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraLookAt
            if (const auto* lookAt = world.GetComponent<CameraLookAtComponent>(entity); lookAt)
            {
                rttr::instance inst = const_cast<CameraLookAtComponent&>(*lookAt);
                root["CameraLookAt"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraShake
            if (const auto* shake = world.GetComponent<CameraShakeComponent>(entity); shake)
            {
                rttr::instance inst = const_cast<CameraShakeComponent&>(*shake);
                root["CameraShake"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraBlend
            if (const auto* blend = world.GetComponent<CameraBlendComponent>(entity); blend)
            {
                rttr::instance inst = const_cast<CameraBlendComponent&>(*blend);
                root["CameraBlend"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraInput
            if (const auto* input = world.GetComponent<CameraInputComponent>(entity); input)
            {
                rttr::instance inst = const_cast<CameraInputComponent&>(*input);
                root["CameraInput"] = JsonRttr::ToJsonObject(inst);
            }

            // Socket
            if (const auto* socketComp = world.GetComponent<SocketComponent>(entity); socketComp)
            {
                root["Socket"] = SocketSerialization::SocketComponentToJson(*socketComp);
            }

            // SocketAttachment
            if (const auto* socketAttach = world.GetComponent<SocketAttachmentComponent>(entity); socketAttach)
            {
                rttr::instance inst = const_cast<SocketAttachmentComponent&>(*socketAttach);
                JsonRttr::json obj = JsonRttr::ToJsonObject(inst);
                obj["ownerGuid"] = std::to_string(socketAttach->ownerGuid);
                root["SocketAttachment"] = obj;
            }

            // Hurtbox
            if (const auto* hurtbox = world.GetComponent<HurtboxComponent>(entity); hurtbox)
            {
                rttr::instance inst = const_cast<HurtboxComponent&>(*hurtbox);
                JsonRttr::json obj = JsonRttr::ToJsonObject(inst);
                obj["ownerGuid"] = std::to_string(hurtbox->ownerGuid);
                root["Hurtbox"] = obj;
            }

            // WeaponTrace
            if (const auto* weaponTrace = world.GetComponent<WeaponTraceComponent>(entity); weaponTrace)
            {
                root["WeaponTrace"] = WeaponTraceSerialization::WeaponTraceComponentToJson(*weaponTrace);
            }

            // Health
            if (const auto* health = world.GetComponent<HealthComponent>(entity); health)
            {
                rttr::instance inst = const_cast<HealthComponent&>(*health);
                root["Health"] = JsonRttr::ToJsonObject(inst);
            }

            // AttackDriver
            if (const auto* attackDriver = world.GetComponent<AttackDriverComponent>(entity); attackDriver)
            {
                root["AttackDriver"] = AttackDriverSerialization::AttackDriverComponentToJson(*attackDriver);
            }

            // AliceUI Components
            if (const auto* uiWidget = world.GetComponent<UIWidgetComponent>(entity); uiWidget)
            {
                rttr::instance inst = const_cast<UIWidgetComponent&>(*uiWidget);
                root["UIWidget"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiTransform = world.GetComponent<UITransformComponent>(entity); uiTransform)
            {
                rttr::instance inst = const_cast<UITransformComponent&>(*uiTransform);
                root["UITransform"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiImage = world.GetComponent<UIImageComponent>(entity); uiImage)
            {
                UIImageComponent copy = *uiImage;
                copy.texturePath = NormalizePathToRelative(copy.texturePath);
                rttr::instance inst = copy;
                root["UIImage"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiText = world.GetComponent<UITextComponent>(entity); uiText)
            {
                UITextComponent copy = *uiText;
                copy.fontPath = NormalizePathToRelative(copy.fontPath);
                rttr::instance inst = copy;
                root["UIText"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiButton = world.GetComponent<UIButtonComponent>(entity); uiButton)
            {
                UIButtonComponent copy = *uiButton;
                copy.normalTexture = NormalizePathToRelative(copy.normalTexture);
                copy.hoveredTexture = NormalizePathToRelative(copy.hoveredTexture);
                copy.pressedTexture = NormalizePathToRelative(copy.pressedTexture);
                copy.disabledTexture = NormalizePathToRelative(copy.disabledTexture);
                rttr::instance inst = copy;
                root["UIButton"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiGauge = world.GetComponent<UIGaugeComponent>(entity); uiGauge)
            {
                UIGaugeComponent copy = *uiGauge;
                copy.fillTexture = NormalizePathToRelative(copy.fillTexture);
                copy.backgroundTexture = NormalizePathToRelative(copy.backgroundTexture);
                rttr::instance inst = copy;
                root["UIGauge"] = JsonRttr::ToJsonObject(inst);
            }

            // Point Light
            if (const auto* point = world.GetComponent<PointLightComponent>(entity); point)
            {
                rttr::instance inst = const_cast<PointLightComponent&>(*point);
                root["PointLight"] = JsonRttr::ToJsonObject(inst);
            }

            // Spot Light
            if (const auto* spot = world.GetComponent<SpotLightComponent>(entity); spot)
            {
                rttr::instance inst = const_cast<SpotLightComponent&>(*spot);
                root["SpotLight"] = JsonRttr::ToJsonObject(inst);
            }

            // Rect Light
            if (const auto* rect = world.GetComponent<RectLightComponent>(entity); rect)
            {
                rttr::instance inst = const_cast<RectLightComponent&>(*rect);
                root["RectLight"] = JsonRttr::ToJsonObject(inst);
            }

            // PhysX Components
            if (const auto* rigidBody = world.GetComponent<Phy_RigidBodyComponent>(entity); rigidBody)
            {
                rttr::instance inst = const_cast<Phy_RigidBodyComponent&>(*rigidBody);
                root["RigidBody"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* collider = world.GetComponent<Phy_ColliderComponent>(entity); collider)
            {
                rttr::instance inst = const_cast<Phy_ColliderComponent&>(*collider);
                root["Collider"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* meshCollider = world.GetComponent<Phy_MeshColliderComponent>(entity); meshCollider)
            {
                rttr::instance inst = const_cast<Phy_MeshColliderComponent&>(*meshCollider);
                root["MeshCollider"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* cct = world.GetComponent<Phy_CCTComponent>(entity); cct)
            {
                rttr::instance inst = const_cast<Phy_CCTComponent&>(*cct);
                root["CharacterController"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* terrain = world.GetComponent<Phy_TerrainHeightFieldComponent>(entity); terrain)
            {
                rttr::instance inst = const_cast<Phy_TerrainHeightFieldComponent&>(*terrain);
                root["TerrainHeightField"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* physicsSettings = world.GetComponent<Phy_SettingsComponent>(entity); physicsSettings)
            {
                // 수동 직렬화 사용 (중첩 배열 보장)
                root["PhysicsSceneSettings"] = WritePhysicsSceneSettings(*physicsSettings);
            }

            if (const auto* joint = world.GetComponent<Phy_JointComponent>(entity); joint)
            {
                rttr::instance inst = const_cast<Phy_JointComponent&>(*joint);
                root["Joint"] = JsonRttr::ToJsonObject(inst);
            }

            return JsonRttr::SaveJsonFile(path, root, 4);
        }
    }
}



