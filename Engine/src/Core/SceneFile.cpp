#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Core/SceneFile.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Core/JsonRttr.h"
#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include "Core/ThreadSafety.h"
#include "Components/IDComponent.h"
#include <random>

#include <cmath>
#include <fstream>
#include <string>
#include <algorithm>
#include <unordered_set>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Core/World.h"
#include "Components/ScriptComponent.h"
#include "Components/ComputeEffectComponent.h"
#include "Components/EffectComponent.h"
#include "Components/TrailEffectComponent.h"
#include "Components/DebugDrawBoxComponent.h"
#include "Components/HealthComponent.h"
#include "Components/AttackDriverComponent.h"
#include "Components/AnimBlueprintComponent.h"
#include "Components/SocketComponent.h"
#include "Components/PostProcessVolumeComponent.h"
#include "Core/SocketSerialization.h"
#include "Core/AttackDriverSerialization.h"
#include "Core/WeaponTraceSerialization.h"
#include "PhysX/Components/Phy_SettingsComponent.h"
#include "PhysX/Components/Phy_JointComponent.h"
#include "PhysX/Components/Phy_MeshColliderComponent.h"

#include "AliceUI/UIWidgetComponent.h"
#include "AliceUI/UITransformComponent.h"
#include "AliceUI/UIImageComponent.h"
#include "AliceUI/UITextComponent.h"
#include "AliceUI/UIButtonComponent.h"
#include "AliceUI/UIGaugeComponent.h"
#include "AliceUI/UIEffectComponent.h"
#include "AliceUI/UIAnimationComponent.h"
#include "AliceUI/UIShakeComponent.h"
#include "AliceUI/UIHover3DComponent.h"
#include "AliceUI/UIVitalComponent.h"

#include "UI/UIWorldManager.h"

#include <wrl/client.h>
#include <dxgi.h>
#include <dxgi1_3.h>

namespace Alice
{
    namespace
    {
        // GUID 생성 함수
        static std::uint64_t NewGuid()
        {
            static std::mt19937_64 rng{ std::random_device{}() };
            static std::uniform_int_distribution<std::uint64_t> dist;
            return dist(rng);
        }

        // GUID 파싱 (JSON string 또는 number)
        static std::uint64_t ParseGuid(const JsonRttr::json& j)
        {
            if (j.is_string())
            {
                try
                {
                    return std::stoull(j.get<std::string>());
                }
                catch (...)
                {
                    return NewGuid();
                }
            }
            else if (j.is_number_unsigned())
            {
                return j.get<std::uint64_t>();
            }
            return NewGuid();
        }

        // GUID 파싱 (잘못된 값은 0)
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

        static void ReadStringArray(const JsonRttr::json& j, std::vector<std::string>& out)
        {
            out.clear();

            if (j.is_string())
            {
                out.push_back(j.get<std::string>());
                return;
            }

            if (!j.is_array())
                return;

            for (const auto& item : j)
            {
                if (item.is_string())
                {
                    out.push_back(item.get<std::string>());
                    continue;
                }

                if (item.is_number() || item.is_boolean())
                {
                    out.push_back(item.dump());
                    continue;
                }

                if (item.is_object())
                {
                    auto itName = item.find("name");
                    if (itName != item.end() && itName->is_string())
                        out.push_back(itName->get<std::string>());
                }
            }
        }

        static std::string NormalizePathToRelative(const std::string& path);

        static bool ParseUIAnimProperty(const JsonRttr::json& jval, UIAnimProperty& out)
        {
            if (jval.is_number_integer())
            {
                const int v = jval.get<int>();
                if (v < 0 || v > static_cast<int>(UIAnimProperty::VitalAmplitude))
                    return false;
                out = static_cast<UIAnimProperty>(v);
                return true;
            }
            if (!jval.is_string())
                return false;

            const std::string name = jval.get<std::string>();
            if (name == "PositionX") { out = UIAnimProperty::PositionX; return true; }
            if (name == "PositionY") { out = UIAnimProperty::PositionY; return true; }
            if (name == "ScaleX") { out = UIAnimProperty::ScaleX; return true; }
            if (name == "ScaleY") { out = UIAnimProperty::ScaleY; return true; }
            if (name == "Rotation") { out = UIAnimProperty::Rotation; return true; }
            if (name == "ImageAlpha") { out = UIAnimProperty::ImageAlpha; return true; }
            if (name == "TextAlpha") { out = UIAnimProperty::TextAlpha; return true; }
            if (name == "GlobalAlpha") { out = UIAnimProperty::GlobalAlpha; return true; }
            if (name == "OutlineThickness") { out = UIAnimProperty::OutlineThickness; return true; }
            if (name == "RadialFill") { out = UIAnimProperty::RadialFill; return true; }
            if (name == "GlowStrength") { out = UIAnimProperty::GlowStrength; return true; }
            if (name == "VitalAmplitude") { out = UIAnimProperty::VitalAmplitude; return true; }
            return false;
        }

        static bool LoadUIAnimationComponent(UIAnimationComponent& comp, const JsonRttr::json& j)
        {
            if (!j.is_object())
                return false;

            comp.playOnStart = j.value("playOnStart", false);
            comp.tracks.clear();

            auto itTracks = j.find("tracks");
            if (itTracks == j.end() || !itTracks->is_array())
                return true;

            for (const auto& jt : *itTracks)
            {
                if (!jt.is_object())
                    continue;

                UIAnimTrack t;
                t.name = jt.value("name", std::string{});
                t.curvePath = jt.value("curvePath", std::string{});
                t.duration = jt.value("duration", t.duration);
                t.delay = jt.value("delay", t.delay);
                t.from = jt.value("from", t.from);
                t.to = jt.value("to", t.to);
                t.loop = jt.value("loop", t.loop);
                t.pingPong = jt.value("pingPong", t.pingPong);
                t.useNormalizedTime = jt.value("useNormalizedTime", t.useNormalizedTime);
                t.additive = jt.value("additive", t.additive);

                auto itProp = jt.find("property");
                if (itProp != jt.end())
                {
                    UIAnimProperty prop = t.property;
                    if (ParseUIAnimProperty(*itProp, prop))
                        t.property = prop;
                }

                comp.tracks.push_back(t);
            }

            return true;
        }

        static const char* ToStringUIAnimProperty(UIAnimProperty prop)
        {
            switch (prop)
            {
            case UIAnimProperty::PositionX: return "PositionX";
            case UIAnimProperty::PositionY: return "PositionY";
            case UIAnimProperty::ScaleX: return "ScaleX";
            case UIAnimProperty::ScaleY: return "ScaleY";
            case UIAnimProperty::Rotation: return "Rotation";
            case UIAnimProperty::ImageAlpha: return "ImageAlpha";
            case UIAnimProperty::TextAlpha: return "TextAlpha";
            case UIAnimProperty::GlobalAlpha: return "GlobalAlpha";
            case UIAnimProperty::OutlineThickness: return "OutlineThickness";
            case UIAnimProperty::RadialFill: return "RadialFill";
            case UIAnimProperty::GlowStrength: return "GlowStrength";
            case UIAnimProperty::VitalAmplitude: return "VitalAmplitude";
            default: return "PositionX";
            }
        }

        static JsonRttr::json SaveUIAnimationComponent(const UIAnimationComponent& comp)
        {
            JsonRttr::json j = JsonRttr::json::object();
            j["playOnStart"] = comp.playOnStart;
            JsonRttr::json tracks = JsonRttr::json::array();

            for (const auto& t : comp.tracks)
            {
                JsonRttr::json jt = JsonRttr::json::object();
                jt["name"] = t.name;
                jt["property"] = ToStringUIAnimProperty(t.property);
                jt["curvePath"] = NormalizePathToRelative(t.curvePath);
                jt["duration"] = t.duration;
                jt["delay"] = t.delay;
                jt["from"] = t.from;
                jt["to"] = t.to;
                jt["loop"] = t.loop;
                jt["pingPong"] = t.pingPong;
                jt["useNormalizedTime"] = t.useNormalizedTime;
                jt["additive"] = t.additive;
                tracks.push_back(jt);
            }

            j["tracks"] = tracks;
            return j;
        }

        template<typename T>
        static bool ReadRttrArray(const JsonRttr::json& j, std::vector<T>& out)
        {
            out.clear();

            if (j.is_null())
                return true;

            if (j.is_object())
            {
                T value{};
                rttr::instance inst = value;
                if (!JsonRttr::FromJsonObject(inst, j)) return false;
                out.push_back(std::move(value));
                return true;
            }

            if (!j.is_array())
                return false;

            for (const auto& item : j)
            {
                if (!item.is_object())
                {
                    // 잘못된 항목은 스킵 (이전 데이터 호환용)
                    continue;
                }

                T value{};
                rttr::instance inst = value;
                if (!JsonRttr::FromJsonObject(inst, item)) return false;
                out.push_back(std::move(value));
            }

            return true;
        }

        // 스키닝 메시가 아직 애니메이션 시스템과 연결되지 않았을 때 사용할
        // 1개짜리 항등 본 팔레트입니다. (정적인 메시처럼 렌더링되도록 함)
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
        // Assets/ 또는 Resource/로 시작하는 경로는 그대로 유지함
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
                        // 상대 경로 변환이 실패하거나 예상과 다른 경우 원본 반환
                    }
                }
                catch (...)
                {
                    // relative() 실패 시 원본 반환
                }
            }

            return path;
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
            
            if (root.contains("maxSubsteps") && root["maxSubsteps"].is_number_integer())
                settings.maxSubsteps = root["maxSubsteps"].get<int>();
            
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
                            // bool 또는 0/1 정수 모두 처리
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
                            // bool 또는 0/1 정수 모두 처리
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

        static bool WriteEntity(JsonRttr::json& outEntity, const World& world, EntityId id)
        {
            outEntity = JsonRttr::json::object();
            // 엔티티 id는 저장하지 않음 (로드 시 재사용되지 않으므로 혼란 방지)

            const std::string name = world.GetEntityName(id);
            if (!name.empty())
                outEntity["name"] = name;
            
            // GUID 저장
            if (const auto* idComp = world.GetComponent<IDComponent>(id); idComp)
            {
                // uint64는 JSON에서 string으로 저장 (호환성)
                outEntity["guid"] = std::to_string(idComp->guid);
            }
            
            // Parent 관계 저장 (GUID 기반)
            EntityId parentId = world.GetParent(id);
            if (parentId != InvalidEntityId)
            {
                if (const auto* parentIdComp = world.GetComponent<IDComponent>(parentId); parentIdComp)
                {
                    outEntity["_parentGuid"] = std::to_string(parentIdComp->guid);
                }
            }
            
            if (const auto* transform = world.GetComponent<TransformComponent>(id); transform)
            {
                rttr::instance inst = const_cast<TransformComponent&>(*transform);
                outEntity["Transform"] = JsonRttr::ToJsonObject(inst);
            }

            
            if (const auto* scripts = world.GetScripts(id); scripts && !scripts->empty())
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

            
            if (const auto* mat = world.GetComponent<MaterialComponent>(id); mat)
            {
                // 경로를 상대 경로로 변환하기 위해 복사본 생성
                MaterialComponent matCopy = *mat;
                matCopy.assetPath = NormalizePathToRelative(matCopy.assetPath);
                matCopy.albedoTexturePath = NormalizePathToRelative(matCopy.albedoTexturePath);
                
                rttr::instance inst = matCopy;
                outEntity["Material"] = JsonRttr::ToJsonObject(inst);
            }

            
            if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(id); skinned)
            {
                // 경로를 상대 경로로 변환하기 위해 복사본 생성
                SkinnedMeshComponent skinnedCopy = *skinned;
                skinnedCopy.instanceAssetPath = NormalizePathToRelative(skinnedCopy.instanceAssetPath);
                // meshAssetPath는 이미 상대 경로일 가능성이 높지만 안전을 위해 변환
                skinnedCopy.meshAssetPath = NormalizePathToRelative(skinnedCopy.meshAssetPath);
                
                rttr::instance inst = skinnedCopy;
                outEntity["SkinnedMesh"] = JsonRttr::ToJsonObject(inst);
            }

            
            if (const auto* anim = world.GetComponent<SkinnedAnimationComponent>(id); anim)
            {
                rttr::instance inst = const_cast<SkinnedAnimationComponent&>(*anim);
                outEntity["SkinnedAnimation"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* advAnim = world.GetComponent<AdvancedAnimationComponent>(id); advAnim)
            {
                rttr::instance inst = const_cast<AdvancedAnimationComponent&>(*advAnim);
                outEntity["AdvancedAnimation"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* animBp = world.GetComponent<AnimBlueprintComponent>(id); animBp)
            {
                AnimBlueprintComponent copy = *animBp;
                copy.blueprintPath = NormalizePathToRelative(copy.blueprintPath);
                rttr::instance inst = copy;
                outEntity["AnimBlueprint"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* socketComp = world.GetComponent<SocketComponent>(id); socketComp)
            {
                outEntity["Socket"] = SocketSerialization::SocketComponentToJson(*socketComp);
            }

            if (const auto* audio = world.GetComponent<AudioSourceComponent>(id); audio)
            {
                AudioSourceComponent copy = *audio;
                copy.soundPath = NormalizePathToRelative(copy.soundPath);
                rttr::instance inst = copy;
                outEntity["AudioSource"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* listener = world.GetComponent<AudioListenerComponent>(id); listener)
            {
                rttr::instance inst = const_cast<AudioListenerComponent&>(*listener);
                outEntity["AudioListener"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* sb = world.GetComponent<SoundBoxComponent>(id); sb)

            {
                SoundBoxComponent copy = *sb;
                copy.soundPath = NormalizePathToRelative(copy.soundPath);
                rttr::instance inst = copy;
                outEntity["SoundBox"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* dbgBox = world.GetComponent<DebugDrawBoxComponent>(id); dbgBox)
            {
                rttr::instance inst = const_cast<DebugDrawBoxComponent&>(*dbgBox);
                outEntity["DebugDrawBox"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* socketAttach = world.GetComponent<SocketAttachmentComponent>(id); socketAttach)
            {
                rttr::instance inst = const_cast<SocketAttachmentComponent&>(*socketAttach);
                JsonRttr::json obj = JsonRttr::ToJsonObject(inst);
                obj["ownerGuid"] = std::to_string(socketAttach->ownerGuid);
                outEntity["SocketAttachment"] = obj;
            }

            if (const auto* hurtbox = world.GetComponent<HurtboxComponent>(id); hurtbox)
            {
                rttr::instance inst = const_cast<HurtboxComponent&>(*hurtbox);
                JsonRttr::json obj = JsonRttr::ToJsonObject(inst);
                obj["ownerGuid"] = std::to_string(hurtbox->ownerGuid);
                outEntity["Hurtbox"] = obj;
            }

            if (const auto* weaponTrace = world.GetComponent<WeaponTraceComponent>(id); weaponTrace)
            {
                outEntity["WeaponTrace"] = WeaponTraceSerialization::WeaponTraceComponentToJson(*weaponTrace);
            }

            if (const auto* health = world.GetComponent<HealthComponent>(id); health)
            {
                rttr::instance inst = const_cast<HealthComponent&>(*health);
                outEntity["Health"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* attackDriver = world.GetComponent<AttackDriverComponent>(id); attackDriver)
            {
                outEntity["AttackDriver"] = AttackDriverSerialization::AttackDriverComponentToJson(*attackDriver);
            }

            // === AliceUI Components ===
            if (const auto* uiWidget = world.GetComponent<UIWidgetComponent>(id); uiWidget)
            {
                rttr::instance inst = const_cast<UIWidgetComponent&>(*uiWidget);
                outEntity["UIWidget"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiTransform = world.GetComponent<UITransformComponent>(id); uiTransform)
            {
                rttr::instance inst = const_cast<UITransformComponent&>(*uiTransform);
                outEntity["UITransform"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiImage = world.GetComponent<UIImageComponent>(id); uiImage)
            {
                UIImageComponent copy = *uiImage;
                copy.texturePath = NormalizePathToRelative(copy.texturePath);
                rttr::instance inst = copy;
                outEntity["UIImage"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiText = world.GetComponent<UITextComponent>(id); uiText)
            {
                UITextComponent copy = *uiText;
                copy.fontPath = NormalizePathToRelative(copy.fontPath);
                rttr::instance inst = copy;
                outEntity["UIText"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiButton = world.GetComponent<UIButtonComponent>(id); uiButton)
            {
                UIButtonComponent copy = *uiButton;
                copy.normalTexture = NormalizePathToRelative(copy.normalTexture);
                copy.hoveredTexture = NormalizePathToRelative(copy.hoveredTexture);
                copy.pressedTexture = NormalizePathToRelative(copy.pressedTexture);
                copy.disabledTexture = NormalizePathToRelative(copy.disabledTexture);
                rttr::instance inst = copy;
                outEntity["UIButton"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiGauge = world.GetComponent<UIGaugeComponent>(id); uiGauge)
            {
                UIGaugeComponent copy = *uiGauge;
                copy.fillTexture = NormalizePathToRelative(copy.fillTexture);
                copy.backgroundTexture = NormalizePathToRelative(copy.backgroundTexture);
                rttr::instance inst = copy;
                outEntity["UIGauge"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* uiEffect = world.GetComponent<UIEffectComponent>(id); uiEffect)
            {
                rttr::instance inst = const_cast<UIEffectComponent&>(*uiEffect);
                outEntity["UIEffect"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiAnim = world.GetComponent<UIAnimationComponent>(id); uiAnim)
            {
                outEntity["UIAnimation"] = SaveUIAnimationComponent(*uiAnim);
            }
            if (const auto* uiShake = world.GetComponent<UIShakeComponent>(id); uiShake)
            {
                rttr::instance inst = const_cast<UIShakeComponent&>(*uiShake);
                outEntity["UIShake"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiHover = world.GetComponent<UIHover3DComponent>(id); uiHover)
            {
                rttr::instance inst = const_cast<UIHover3DComponent&>(*uiHover);
                outEntity["UIHover3D"] = JsonRttr::ToJsonObject(inst);
            }
            if (const auto* uiVital = world.GetComponent<UIVitalComponent>(id); uiVital)
            {
                rttr::instance inst = const_cast<UIVitalComponent&>(*uiVital);
                outEntity["UIVital"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* cam = world.GetComponent<CameraComponent>(id); cam)
            {
                rttr::instance inst = const_cast<CameraComponent&>(*cam);
                outEntity["Camera"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* follow = world.GetComponent<CameraFollowComponent>(id); follow)
            {
                rttr::instance inst = const_cast<CameraFollowComponent&>(*follow);
                outEntity["CameraFollow"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* spring = world.GetComponent<CameraSpringArmComponent>(id); spring)
            {
                rttr::instance inst = const_cast<CameraSpringArmComponent&>(*spring);
                outEntity["CameraSpringArm"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* lookAt = world.GetComponent<CameraLookAtComponent>(id); lookAt)
            {
                rttr::instance inst = const_cast<CameraLookAtComponent&>(*lookAt);
                outEntity["CameraLookAt"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* shake = world.GetComponent<CameraShakeComponent>(id); shake)
            {
                rttr::instance inst = const_cast<CameraShakeComponent&>(*shake);
                outEntity["CameraShake"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* blend = world.GetComponent<CameraBlendComponent>(id); blend)
            {
                rttr::instance inst = const_cast<CameraBlendComponent&>(*blend);
                outEntity["CameraBlend"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* input = world.GetComponent<CameraInputComponent>(id); input)
            {
                rttr::instance inst = const_cast<CameraInputComponent&>(*input);
                outEntity["CameraInput"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* point = world.GetComponent<PointLightComponent>(id); point)
            {
                rttr::instance inst = const_cast<PointLightComponent&>(*point);
                outEntity["PointLight"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* spot = world.GetComponent<SpotLightComponent>(id); spot)
            {
                rttr::instance inst = const_cast<SpotLightComponent&>(*spot);
                outEntity["SpotLight"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* rect = world.GetComponent<RectLightComponent>(id); rect)
            {
                rttr::instance inst = const_cast<RectLightComponent&>(*rect);
                outEntity["RectLight"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* computeEffect = world.GetComponent<ComputeEffectComponent>(id); computeEffect)
            {
                rttr::instance inst = const_cast<ComputeEffectComponent&>(*computeEffect);
                outEntity["ComputeEffect"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* postProcessVolume = world.GetComponent<PostProcessVolumeComponent>(id); postProcessVolume)
            {
                rttr::instance inst = const_cast<PostProcessVolumeComponent&>(*postProcessVolume);
                outEntity["PostProcessVolume"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* effect = world.GetComponent<EffectComponent>(id); effect)
            {
                rttr::instance inst = const_cast<EffectComponent&>(*effect);
                outEntity["Effect"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* trail = world.GetComponent<TrailEffectComponent>(id); trail)
            {
                rttr::instance inst = const_cast<TrailEffectComponent&>(*trail);
                outEntity["TrailEffect"] = JsonRttr::ToJsonObject(inst);
            }

            // PhysX Components
            if (const auto* rigidBody = world.GetComponent<Phy_RigidBodyComponent>(id); rigidBody)
            {
                rttr::instance inst = const_cast<Phy_RigidBodyComponent&>(*rigidBody);
                outEntity["RigidBody"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* collider = world.GetComponent<Phy_ColliderComponent>(id); collider)
            {
                rttr::instance inst = const_cast<Phy_ColliderComponent&>(*collider);
                outEntity["Collider"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* meshCollider = world.GetComponent<Phy_MeshColliderComponent>(id); meshCollider)
            {
                rttr::instance inst = const_cast<Phy_MeshColliderComponent&>(*meshCollider);
                outEntity["MeshCollider"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* cct = world.GetComponent<Phy_CCTComponent>(id); cct)
            {
                rttr::instance inst = const_cast<Phy_CCTComponent&>(*cct);
                outEntity["CharacterController"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* terrain = world.GetComponent<Phy_TerrainHeightFieldComponent>(id); terrain)
            {
                rttr::instance inst = const_cast<Phy_TerrainHeightFieldComponent&>(*terrain);
                outEntity["TerrainHeightField"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* physicsSettings = world.GetComponent<Phy_SettingsComponent>(id); physicsSettings)
            {
                // 수동 직렬화 사용 (중첩 배열 보장)
                outEntity["PhysicsSceneSettings"] = WritePhysicsSceneSettings(*physicsSettings);
            }

            if (const auto* joint = world.GetComponent<Phy_JointComponent>(id); joint)
            {
                rttr::instance inst = const_cast<Phy_JointComponent&>(*joint);
                outEntity["Joint"] = JsonRttr::ToJsonObject(inst);
            }

            return true;
        }

        static bool ApplyEntity(World& world, const JsonRttr::json& e, std::unordered_map<std::uint64_t, EntityId>& guidToEntity, std::vector<std::pair<EntityId, std::uint64_t>>& pendingParents)
        {
            if (!e.is_object()) return false;

            const EntityId id = world.CreateEntity();

            const std::string name = e.value("name", std::string{});
            if (!name.empty())
                world.SetEntityName(id, name);

            // IDComponent: GUID 로드 또는 생성
            auto* idComp = world.GetComponent<IDComponent>(id);
            if (!idComp)
            {
                // IDComponent가 없으면 생성
                idComp = &world.AddComponent<IDComponent>(id);
            }
            
            if (auto itGuid = e.find("guid"); itGuid != e.end())
            {
                auto parsed = ParseGuid(*itGuid);
                if (parsed != 0) idComp->guid = parsed; // 실패면 덮어쓰지 않기
                else idComp->guid = NewGuid(); // ParseGuid 실패 시 새 GUID 생성
            }
            else
            {
                idComp->guid = NewGuid();
            }
            guidToEntity[idComp->guid] = id;

            // Parent GUID 저장 (나중에 연결)
            if (auto itParentGuid = e.find("_parentGuid"); itParentGuid != e.end())
            {
                std::uint64_t parentGuid = ParseGuid(*itParentGuid);
                pendingParents.push_back({ id, parentGuid });
            }

            // Transform
            TransformComponent& t = world.AddComponent<TransformComponent>(id);
            auto itT = e.find("Transform");
            if (itT != e.end())
            {
                rttr::instance inst = t;
                if (!JsonRttr::FromJsonObject(inst, *itT)) return false;
                if (itT->is_object() && itT->find("visible") == itT->end())
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
                // scale (0,0,0) 방지: 물리/렌더에서 0 나누기 등 오류 방지
                const float eps = 1e-6f;
                if (t.scale.x == 0.f && t.scale.y == 0.f && t.scale.z == 0.f)
                {
                    t.scale.x = t.scale.y = t.scale.z = 1.f;
                }
                else
                {
                    if (std::abs(t.scale.x) < eps) t.scale.x = (t.scale.x >= 0.f) ? eps : -eps;
                    if (std::abs(t.scale.y) < eps) t.scale.y = (t.scale.y >= 0.f) ? eps : -eps;
                    if (std::abs(t.scale.z) < eps) t.scale.z = (t.scale.z >= 0.f) ? eps : -eps;
                }
            }

            // Scripts (여러 개)
            auto itS = e.find("Scripts");
            if (itS != e.end() && itS->is_array())
            {
                for (const auto& s : *itS)
                {
                    if (!s.is_object()) continue;
                    const std::string name = s.value("name", std::string{});
                    if (name.empty()) continue;

                    ScriptComponent& sc = world.AddScript(id, name);
                    sc.enabled = s.value("enabled", true);

                    auto itP = s.find("props");
                    if (itP != s.end() && itP->is_object() && sc.instance)
                    {
                        rttr::instance inst = *sc.instance;
                        const rttr::type t = rttr::type::get_by_name(sc.scriptName);
                        if (!JsonRttr::FromJsonObject(inst, *itP, t)) return false;
                        sc.defaultsApplied = true; // 씬이 값 주입 완료
                    }
                }
            }
            else
            {
                // Script (레거시 단일)
                auto itLegacy = e.find("Script");
                if (itLegacy != e.end() && itLegacy->is_object())
                {
                    const std::string name = itLegacy->value("name", std::string{});
                    const bool enabled = itLegacy->value("enabled", true);
                    if (!name.empty())
                    {
                        ScriptComponent& sc = world.AddScript(id, name);
                        sc.enabled = enabled;
                    }
                }
            }

            // Material
            auto itM = e.find("Material");
            if (itM != e.end() && itM->is_object())
            {
                MaterialComponent& mc = world.AddComponent<MaterialComponent>(id, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
                rttr::instance inst = mc;
                if (!JsonRttr::FromJsonObject(inst, *itM)) return false;
            }

            // SkinnedMesh
            auto itSM = e.find("SkinnedMesh");
            if (itSM != e.end() && itSM->is_object())
            {
                SkinnedMeshComponent tmp;
                rttr::instance instTmp = tmp;
                if (!JsonRttr::FromJsonObject(instTmp, *itSM)) return false;

                if (!tmp.meshAssetPath.empty())
                {
                    SkinnedMeshComponent& sm = world.AddComponent<SkinnedMeshComponent>(id, tmp.meshAssetPath);
                    sm.instanceAssetPath = tmp.instanceAssetPath;
                    sm.boneMatrices = &g_IdentityBone;
                    sm.boneCount = 1;
                }
            }

            // SkinnedAnimation (선택)
            auto itSA = e.find("SkinnedAnimation");
            if (itSA != e.end() && itSA->is_object())
            {
                SkinnedAnimationComponent& sa = world.AddComponent<SkinnedAnimationComponent>(id);
                rttr::instance inst = sa;
                if (!JsonRttr::FromJsonObject(inst, *itSA)) return false;
            }

            // AdvancedAnimation (선택)
            auto itAA = e.find("AdvancedAnimation");
            if (itAA != e.end() && itAA->is_object())
            {
                AdvancedAnimationComponent& aa = world.AddComponent<AdvancedAnimationComponent>(id);
                JsonRttr::json copy = *itAA;

                if (auto itChains = copy.find("ikChains"); itChains != copy.end())
                {
                    if (!ReadRttrArray(*itChains, aa.ikChains)) return false;
                    copy.erase("ikChains");
                }

                if (auto itSockets = copy.find("sockets"); itSockets != copy.end())
                {
                    if (!ReadRttrArray(*itSockets, aa.sockets)) return false;
                    copy.erase("sockets");
                }

                rttr::instance inst = aa;
                if (!JsonRttr::FromJsonObject(inst, copy)) return false;
            }

            // AnimBlueprint (선택)
            auto itAnimBp = e.find("AnimBlueprint");
            if (itAnimBp != e.end() && itAnimBp->is_object())
            {
                AnimBlueprintComponent& ab = world.AddComponent<AnimBlueprintComponent>(id);
                rttr::instance inst = ab;
                if (!JsonRttr::FromJsonObject(inst, *itAnimBp)) return false;
            }

            // Socket (선택)
            auto itSocket = e.find("Socket");
            if (itSocket != e.end() && itSocket->is_object())
            {
                SocketComponent& sc = world.AddComponent<SocketComponent>(id);
                if (!SocketSerialization::JsonToSocketComponent(*itSocket, sc)) return false;
            }

            // Camera (선택)
            auto itC = e.find("Camera");
            if (itC != e.end() && itC->is_object())
            {
                CameraComponent& cc = world.AddComponent<CameraComponent>(id);
                rttr::instance inst = cc;
                if (!JsonRttr::FromJsonObject(inst, *itC)) return false;
            }

            // CameraFollow (선택)
            auto itCF = e.find("CameraFollow");
            if (itCF != e.end() && itCF->is_object())
            {
                CameraFollowComponent& cf = world.AddComponent<CameraFollowComponent>(id);
                rttr::instance inst = cf;
                if (!JsonRttr::FromJsonObject(inst, *itCF)) return false;
            }

            // CameraSpringArm (선택)
            auto itSpring = e.find("CameraSpringArm");
            if (itSpring != e.end() && itSpring->is_object())
            {
                CameraSpringArmComponent& sa = world.AddComponent<CameraSpringArmComponent>(id);
                rttr::instance inst = sa;
                if (!JsonRttr::FromJsonObject(inst, *itSpring)) return false;
            }

            // CameraLookAt (선택)
            auto itLA = e.find("CameraLookAt");
            if (itLA != e.end() && itLA->is_object())
            {
                CameraLookAtComponent& la = world.AddComponent<CameraLookAtComponent>(id);
                rttr::instance inst = la;
                if (!JsonRttr::FromJsonObject(inst, *itLA)) return false;
            }

            // CameraShake (선택)
            auto itCS = e.find("CameraShake");
            if (itCS != e.end() && itCS->is_object())
            {
                CameraShakeComponent& cs = world.AddComponent<CameraShakeComponent>(id);
                rttr::instance inst = cs;
                if (!JsonRttr::FromJsonObject(inst, *itCS)) return false;
            }

            // CameraBlend (선택)
            auto itCB = e.find("CameraBlend");
            if (itCB != e.end() && itCB->is_object())
            {
                CameraBlendComponent& cb = world.AddComponent<CameraBlendComponent>(id);
                rttr::instance inst = cb;
                if (!JsonRttr::FromJsonObject(inst, *itCB)) return false;
            }

            // CameraInput (선택)
            auto itCI = e.find("CameraInput");
            if (itCI != e.end() && itCI->is_object())
            {
                CameraInputComponent& ci = world.AddComponent<CameraInputComponent>(id);
                rttr::instance inst = ci;
                if (!JsonRttr::FromJsonObject(inst, *itCI)) return false;
            }

            // Point Light 선택
            auto itPL = e.find("PointLight");
            if (itPL != e.end() && itPL->is_object())
            {
                PointLightComponent& pl = world.AddComponent<PointLightComponent>(id);
                rttr::instance inst = pl;
                if (!JsonRttr::FromJsonObject(inst, *itPL)) return false;
            }

            // Spot Light 선택
            auto itSL = e.find("SpotLight");
            if (itSL != e.end() && itSL->is_object())
            {
                SpotLightComponent& sl = world.AddComponent<SpotLightComponent>(id);
                rttr::instance inst = sl;
                if (!JsonRttr::FromJsonObject(inst, *itSL)) return false;
            }

            // Rect Light 선택
            auto itRL = e.find("RectLight");
            if (itRL != e.end() && itRL->is_object())
            {
                RectLightComponent& rl = world.AddComponent<RectLightComponent>(id);
                rttr::instance inst = rl;
                if (!JsonRttr::FromJsonObject(inst, *itRL)) return false;
            }

            // ComputeEffect 선택
            auto itCE = e.find("ComputeEffect");
            if (itCE != e.end() && itCE->is_object())
            {
                ComputeEffectComponent& ce = world.AddComponent<ComputeEffectComponent>(id);
                rttr::instance inst = ce;
                if (!JsonRttr::FromJsonObject(inst, *itCE)) return false;
            }

            // PostProcessVolume 선택
            auto itPPV = e.find("PostProcessVolume");
            if (itPPV != e.end() && itPPV->is_object())
            {
                PostProcessVolumeComponent& ppv = world.AddComponent<PostProcessVolumeComponent>(id);
                rttr::instance inst = ppv;
                if (!JsonRttr::FromJsonObject(inst, *itPPV)) return false;
            }

            // Effect 선택
            auto itEffect = e.find("Effect");
            if (itEffect != e.end() && itEffect->is_object())
            {
                EffectComponent& ec = world.AddComponent<EffectComponent>(id);
                rttr::instance inst = ec;
                if (!JsonRttr::FromJsonObject(inst, *itEffect)) return false;
            }

            // TrailEffect 선택
            auto itTrail = e.find("TrailEffect");
            if (itTrail != e.end() && itTrail->is_object())
            {
                TrailEffectComponent& te = world.AddComponent<TrailEffectComponent>(id);
                rttr::instance inst = te;
                if (!JsonRttr::FromJsonObject(inst, *itTrail)) return false;
            }

            // PhysX Components
            auto itRB = e.find("RigidBody");
            if (itRB != e.end() && itRB->is_object())
            {
                Phy_RigidBodyComponent& rb = world.AddComponent<Phy_RigidBodyComponent>(id);
                rttr::instance inst = rb;
                if (!JsonRttr::FromJsonObject(inst, *itRB)) return false;
            }

            auto itCollider = e.find("Collider");
            if (itCollider != e.end() && itCollider->is_object())
            {
                Phy_ColliderComponent& col = world.AddComponent<Phy_ColliderComponent>(id);
                rttr::instance inst = col;
                if (!JsonRttr::FromJsonObject(inst, *itCollider)) return false;
            }

            auto itMeshCollider = e.find("MeshCollider");
            if (itMeshCollider != e.end() && itMeshCollider->is_object())
            {
                Phy_MeshColliderComponent& mc = world.AddComponent<Phy_MeshColliderComponent>(id);
                rttr::instance inst = mc;
                if (!JsonRttr::FromJsonObject(inst, *itMeshCollider)) return false;
            }

            auto itCCT = e.find("CharacterController");
            if (itCCT != e.end() && itCCT->is_object())
            {
                Phy_CCTComponent& cct = world.AddComponent<Phy_CCTComponent>(id);
                rttr::instance inst = cct;
                if (!JsonRttr::FromJsonObject(inst, *itCCT)) return false;
            }

            auto itTerrain = e.find("TerrainHeightField");
            if (itTerrain != e.end() && itTerrain->is_object())
            {
                Phy_TerrainHeightFieldComponent& terrain = world.AddComponent<Phy_TerrainHeightFieldComponent>(id);
                rttr::instance inst = terrain;
                if (!JsonRttr::FromJsonObject(inst, *itTerrain)) return false;
            }

            auto itJoint = e.find("Joint");
            if (itJoint != e.end() && itJoint->is_object())
            {
                Phy_JointComponent& joint = world.AddComponent<Phy_JointComponent>(id);
                rttr::instance inst = joint;
                if (!JsonRttr::FromJsonObject(inst, *itJoint)) return false;
            }

            auto itPhysicsSettings = e.find("PhysicsSceneSettings");
            if (itPhysicsSettings != e.end() && itPhysicsSettings->is_object())
            {
                Phy_SettingsComponent& ps = world.AddComponent<Phy_SettingsComponent>(id);
                // 수동 역직렬화 사용 (중첩 배열 보장)
                if (!LoadPhysicsSceneSettings(ps, *itPhysicsSettings)) return false;
            }

            // AudioSource (선택)
            auto itAS = e.find("AudioSource");
            if (itAS != e.end() && itAS->is_object())
            {
                AudioSourceComponent& asc = world.AddComponent<AudioSourceComponent>(id);
                rttr::instance inst = asc;
                if (!JsonRttr::FromJsonObject(inst, *itAS)) return false;
            }

            // AudioListener (선택)
            auto itAL = e.find("AudioListener");
            if (itAL != e.end() && itAL->is_object())
            {
                AudioListenerComponent& alc = world.AddComponent<AudioListenerComponent>(id);
                rttr::instance inst = alc;
                if (!JsonRttr::FromJsonObject(inst, *itAL)) return false;
            }

            // SoundBox (선택)
            auto itSB = e.find("SoundBox");
            if (itSB != e.end() && itSB->is_object())
            {
                SoundBoxComponent& sb = world.AddComponent<SoundBoxComponent>(id);
                rttr::instance inst = sb;
                if (!JsonRttr::FromJsonObject(inst, *itSB)) return false;
            }

            // DebugDrawBox (선택)
            auto itDbg = e.find("DebugDrawBox");
            if (itDbg != e.end() && itDbg->is_object())
            {
                DebugDrawBoxComponent& dd = world.AddComponent<DebugDrawBoxComponent>(id);
                rttr::instance inst = dd;
                if (!JsonRttr::FromJsonObject(inst, *itDbg)) return false;
            }

            // SocketAttachment (선택)
            auto itSocketAttach = e.find("SocketAttachment");
            if (itSocketAttach != e.end() && itSocketAttach->is_object())
            {
                SocketAttachmentComponent& sa = world.AddComponent<SocketAttachmentComponent>(id);
                if (auto itGuid = itSocketAttach->find("ownerGuid"); itGuid != itSocketAttach->end())
                    sa.ownerGuid = ParseGuidOrZero(*itGuid);

                JsonRttr::json copy = *itSocketAttach;
                copy.erase("ownerGuid");
                rttr::instance inst = sa;
                if (!JsonRttr::FromJsonObject(inst, copy)) return false;
            }

            // Hurtbox (선택)
            auto itHurtbox = e.find("Hurtbox");
            if (itHurtbox != e.end() && itHurtbox->is_object())
            {
                HurtboxComponent& hb = world.AddComponent<HurtboxComponent>(id);
                if (auto itGuid = itHurtbox->find("ownerGuid"); itGuid != itHurtbox->end())
                    hb.ownerGuid = ParseGuidOrZero(*itGuid);

                JsonRttr::json copy = *itHurtbox;
                copy.erase("ownerGuid");
                rttr::instance inst = hb;
                if (!JsonRttr::FromJsonObject(inst, copy)) return false;
            }

            // WeaponTrace (선택)
            auto itWeaponTrace = e.find("WeaponTrace");
            if (itWeaponTrace != e.end() && itWeaponTrace->is_object())
            {
                WeaponTraceComponent& wt = world.AddComponent<WeaponTraceComponent>(id);
                if (!WeaponTraceSerialization::JsonToWeaponTraceComponent(*itWeaponTrace, wt)) return false;
            }

            // Health (선택)
            auto itHealth = e.find("Health");
            if (itHealth != e.end() && itHealth->is_object())
            {
                HealthComponent& hc = world.AddComponent<HealthComponent>(id);
                rttr::instance inst = hc;
                if (!JsonRttr::FromJsonObject(inst, *itHealth)) return false;
            }

            // AttackDriver (선택)
            auto itAttackDriver = e.find("AttackDriver");
            if (itAttackDriver != e.end() && itAttackDriver->is_object())
            {
                AttackDriverComponent& ad = world.AddComponent<AttackDriverComponent>(id);
                if (!AttackDriverSerialization::JsonToAttackDriverComponent(*itAttackDriver, ad))
                    return false;
            }

            // === AliceUI Components ===
            auto itUIWidget = e.find("UIWidget");
            if (itUIWidget != e.end() && itUIWidget->is_object())
            {
                UIWidgetComponent& comp = world.AddComponent<UIWidgetComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIWidget)) return false;
            }
            auto itUITransform = e.find("UITransform");
            if (itUITransform != e.end() && itUITransform->is_object())
            {
                UITransformComponent& comp = world.AddComponent<UITransformComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUITransform)) return false;
            }
            auto itUIImage = e.find("UIImage");
            if (itUIImage != e.end() && itUIImage->is_object())
            {
                UIImageComponent& comp = world.AddComponent<UIImageComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIImage)) return false;
            }
            auto itUIText = e.find("UIText");
            if (itUIText != e.end() && itUIText->is_object())
            {
                UITextComponent& comp = world.AddComponent<UITextComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIText)) return false;
            }
            auto itUIButton = e.find("UIButton");
            if (itUIButton != e.end() && itUIButton->is_object())
            {
                UIButtonComponent& comp = world.AddComponent<UIButtonComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIButton)) return false;
            }
            auto itUIGauge = e.find("UIGauge");
            if (itUIGauge != e.end() && itUIGauge->is_object())
            {
                UIGaugeComponent& comp = world.AddComponent<UIGaugeComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIGauge)) return false;
            }

            auto itUIEffect = e.find("UIEffect");
            if (itUIEffect != e.end() && itUIEffect->is_object())
            {
                UIEffectComponent& comp = world.AddComponent<UIEffectComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIEffect)) return false;
            }
            auto itUIAnimation = e.find("UIAnimation");
            if (itUIAnimation != e.end() && itUIAnimation->is_object())
            {
                UIAnimationComponent& comp = world.AddComponent<UIAnimationComponent>(id);
                if (!LoadUIAnimationComponent(comp, *itUIAnimation)) return false;
            }
            auto itUIShake = e.find("UIShake");
            if (itUIShake != e.end() && itUIShake->is_object())
            {
                UIShakeComponent& comp = world.AddComponent<UIShakeComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIShake)) return false;
            }
            auto itUIHover = e.find("UIHover3D");
            if (itUIHover != e.end() && itUIHover->is_object())
            {
                UIHover3DComponent& comp = world.AddComponent<UIHover3DComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIHover)) return false;
            }
            auto itUIVital = e.find("UIVital");
            if (itUIVital != e.end() && itUIVital->is_object())
            {
                UIVitalComponent& comp = world.AddComponent<UIVitalComponent>(id);
                rttr::instance inst = comp;
                if (!JsonRttr::FromJsonObject(inst, *itUIVital)) return false;
            }

            return true;
        }

        static bool LoadFromRoot(World& world, const JsonRttr::json& root)
        {
            auto itEntities = root.find("entities");
            if (itEntities == root.end() || !itEntities->is_array())
                return false;

            world.Clear();

            // 2-pass 로드: GUID 기반 parent 복원
            std::unordered_map<std::uint64_t, EntityId> guidToEntity;
            std::vector<std::pair<EntityId, std::uint64_t>> pendingParents;

            // PASS 1: 엔티티 생성 + 컴포넌트 복원 + GUID 맵 생성
            size_t entityIndex = 0;
            for (const auto& e : *itEntities)
            {
                if (!ApplyEntity(world, e, guidToEntity, pendingParents))
                {
                    const std::string name = e.value("name", std::string{});
                    ALICE_LOG_ERRORF("[SceneFile] ApplyEntity FAILED at entity index %zu name=\"%s\"", entityIndex, name.c_str());
                    return false;
                }
                ++entityIndex;
            }

            // PASS 2: parent 연결 (keepWorld=false, 로드이므로)
            for (const auto& [childId, parentGuid] : pendingParents)
            {
                auto it = guidToEntity.find(parentGuid);
                if (it != guidToEntity.end())
                {
                    world.SetParent(childId, it->second, false);
                }
            }

            return true;
        }

        static bool LoadFromBytes(World& world,
                                  const std::uint8_t* bytes,
                                  std::size_t size,
                                  const std::string& debugName)
        {
            if (!bytes || size == 0)
            {
                ALICE_LOG_ERRORF("[SceneFile] LoadFromBytes FAILED: empty buffer. name=\"%s\"", debugName.c_str());
                return false;
            }

            JsonRttr::json root;
            try
            {
                root = JsonRttr::json::parse(bytes, bytes + size);
            }
            catch (...)
            {
                ALICE_LOG_ERRORF("[SceneFile] JSON parse FAILED. name=\"%s\" bytes=%zu", debugName.c_str(), size);
                return false;
            }

            return LoadFromRoot(world, root);
        }
    }

    namespace SceneFile
    {
        bool Save(const World& world, const std::filesystem::path& path)
        {
            JsonRttr::json root = JsonRttr::json::object();
            root["version"] = 1;
            
            // Scene 이름 저장 (파일명 기반, 확장자 제외)
            std::string sceneName = path.stem().string();
            root["sceneName"] = sceneName;
            
            root["entities"] = JsonRttr::json::array();

            std::unordered_set<EntityId> entitySet;
            const auto& transforms = world.GetComponents<TransformComponent>();
            for (const auto& [id, transform] : transforms)
            {
                (void)transform;
                entitySet.insert(id);
            }

            const auto& uiWidgets = world.GetComponents<UIWidgetComponent>();
            for (const auto& [id, widget] : uiWidgets)
            {
                (void)widget;
                entitySet.insert(id);
            }

            std::vector<EntityId> entityList(entitySet.begin(), entitySet.end());
            std::sort(entityList.begin(), entityList.end());
            for (EntityId id : entityList)
            {
                JsonRttr::json e;
                if (!WriteEntity(e, world, id)) return false;
                root["entities"].push_back(e);
            }

            if (!JsonRttr::SaveJsonFile(path, root, 4)) return false;

            return true;
        }

        bool SaveToJsonString(const World& world, std::string& out)
        {
            JsonRttr::json root = JsonRttr::json::object();
            root["version"] = 1;
            root["entities"] = JsonRttr::json::array();

            std::unordered_set<EntityId> entitySet;
            const auto& transforms = world.GetComponents<TransformComponent>();
            for (const auto& [id, transform] : transforms)
            {
                (void)transform;
                entitySet.insert(id);
            }

            const auto& uiWidgets = world.GetComponents<UIWidgetComponent>();
            for (const auto& [id, widget] : uiWidgets)
            {
                (void)widget;
                entitySet.insert(id);
            }

            std::vector<EntityId> entityList(entitySet.begin(), entitySet.end());
            std::sort(entityList.begin(), entityList.end());
            for (EntityId id : entityList)
            {
                JsonRttr::json e;
                if (!WriteEntity(e, world, id)) return false;
                root["entities"].push_back(e);
            }

            out = root.dump(4);
            return true;
        }

        bool LoadFromJsonString(World& world, const std::string& json)
        {
            ThreadSafety::AssertMainThread();
            JsonRttr::json root;
            try
            {
                root = JsonRttr::json::parse(json);
            }
            catch (...)
            {
                ALICE_LOG_ERRORF("[SceneFile] LoadFromJsonString: JSON parse failed.");
                return false;
            }
            world.Clear();
            return LoadFromRoot(world, root);
        }

        bool Load(World& world, const std::filesystem::path& path)
        {
            ThreadSafety::AssertMainThread();
            // 레거시 빈 씬(텍스트 헤더만 존재) 자동 처리:
            // - 예전 포맷으로 생성된 "# AliceRenderer scene" 파일은 JSON이 아니므로 파싱에 실패합니다.
            // - 이 경우 기본 엔티티 1개를 넣어 JSON 씬으로 즉시 업그레이드합니다.
            {
                std::ifstream ifs(path);
                if (!ifs.is_open()) return false;

                std::string firstLine;
                std::getline(ifs, firstLine);
                if (firstLine.rfind("# AliceRenderer scene", 0) == 0)
                {
                    world.Clear();
                    const EntityId e = world.CreateEntity();
                    world.AddComponent<TransformComponent>(e);
                    world.AddComponent<MaterialComponent>(e, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
                    Save(world, path);
                    return true;
                }
            }

            JsonRttr::json root;
            if (!JsonRttr::LoadJsonFile(path, root)) return false;
            return LoadFromRoot(world, root);
        }

        bool LoadAuto(World& world, const ResourceManager& resources, const std::filesystem::path& logicalPath, UIWorldManager* uiWorldManager)
        {
            (void)uiWorldManager;
            // (1) 에디터: 실제 파일
            // (2) 게임  : Assets/... 는 Metas/Chunks 로 패킹되어 있으므로, 바이트 로드 후 JSON 파싱
            const std::filesystem::path resolved = resources.Resolve(logicalPath);
            const std::string resolvedStr = resolved.generic_string();

            // Metas/Chunks 로 매핑된 경우: chunk 파일(.alice)이므로 직접 파일 파싱하면 안 됨
            if (resolved.extension() == ".alice")
            {
                auto sp = resources.LoadSharedBinaryAuto(logicalPath);
                if (!sp)
                {
                    ALICE_LOG_ERRORF("[SceneFile] LoadAuto FAILED: chunk load failed. logical=\"%s\" resolved=\"%s\"",
                                     logicalPath.generic_string().c_str(),
                                     resolvedStr.c_str());
                    return false;
                }

                ALICE_LOG_INFO("[SceneFile] LoadAuto: metas bytes loaded. logical=\"%s\" bytes=%zu resolved=\"%s\"",
                               logicalPath.generic_string().c_str(),
                               sp->size(),
                               resolvedStr.c_str());
                // .alice 파일의 경우 World는 바이트에서 로드
                if (!LoadFromBytes(world, sp->data(), sp->size(), logicalPath.generic_string())) return false;
                return true;
            }

            // 일반 파일: resolved 경로로 로드
            ALICE_LOG_INFO("[SceneFile] LoadAuto: file load. logical=\"%s\" resolved=\"%s\"",
                           logicalPath.generic_string().c_str(),
                           resolvedStr.c_str());
            return Load(world, resolved, nullptr);
        }
        
        bool Save(const World& world, const std::filesystem::path& path, UIWorldManager* uiWorldManager)
        {
            (void)uiWorldManager;
            // World 저장
            if (!Save(world, path)) return false;
            return true;
        }
        
        bool Load(World& world, const std::filesystem::path& path, UIWorldManager* uiWorldManager)
        {
            (void)uiWorldManager;
            // World 로드
            if (!Load(world, path)) return false;
            return true;
        }
    }
}
