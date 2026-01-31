#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <rttr/type.h>
#include <rttr/variant.h>
#include <rttr/instance.h>
#include <rttr/property.h>
#include <rttr/variant_sequential_view.h>
#include <imgui.h>
#include <DirectXMath.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/ECS/Entity.h"

namespace Alice
{
    class World; // 전방 선언
    
    /// @note RTTR 기반으로 렌더링하는 유틸리티 클래스
    namespace ReflectionUI
    {
        /// UI 편집 이벤트 정보
        struct UIEditEvent
        {
            bool changed = false;              // 값이 변경되었는지
            bool activated = false;            // 편집이 시작되었는지 (IsItemActivated)
            bool deactivatedAfterEdit = false; // 편집이 끝났는지 (IsItemDeactivatedAfterEdit)
        };
        namespace Detail
        {
            /// @param obj 인스턴스
            /// @param label 렌더링할 라벨
            /// @param world World 포인터 (엔티티 참조 드래그 앤 드롭용, 선택적)
            /// @return UI 편집 이벤트 정보
            inline UIEditEvent RenderProperty(const rttr::property& prop, rttr::instance& obj, 
                                      const std::string& label = "", World* world = nullptr)
            {
                if (prop.get_metadata("BindWidget").is_valid())
                    return UIEditEvent{};

                rttr::type propType = prop.get_type();
                std::string propName = prop.get_name().to_string();
                std::string displayName = label.empty() ? propName : label;

                rttr::variant value = prop.get_value(obj);
                if (!value.is_valid())
                    return UIEditEvent{};

                UIEditEvent event{};

                if (propType == rttr::type::get<bool>())
                {
                    bool val = value.to_bool();
                    bool changed = ImGui::Checkbox(displayName.c_str(), &val);
                    event.activated = ImGui::IsItemActivated();
                    event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                    if (changed)
                    {
                        prop.set_value(obj, val);
                        event.changed = true;
                    }
                }
                else if (propType == rttr::type::get<int>())
                {
                    int val = value.to_int();
                    bool changed = ImGui::DragInt(displayName.c_str(), &val);
                    event.activated = ImGui::IsItemActivated();
                    event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                    if (changed)
                    {
                        prop.set_value(obj, val);
                        event.changed = true;
                    }
                }
                else if (propType == rttr::type::get<uint32_t>())
                {
                    // uint32_t는 비트마스크로 처리 가능하지만, 일단 일반 int로 표시
                    int val = static_cast<int>(value.to_uint32());
                    bool changed = ImGui::DragInt(displayName.c_str(), &val, 1.0f, 0, INT_MAX);
                    event.activated = ImGui::IsItemActivated();
                    event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                    if (changed)
                    {
                        prop.set_value(obj, static_cast<uint32_t>(val));
                        event.changed = true;
                    }
                }
                else if (propType == rttr::type::get<float>())
                {
                    float val = value.to_float();
                    bool changed = ImGui::DragFloat(displayName.c_str(), &val, 0.01f);
                    event.activated = ImGui::IsItemActivated();
                    event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                    if (changed)
                    {
                        prop.set_value(obj, val);
                        event.changed = true;
                    }
                }
                else if (propType == rttr::type::get<double>())
                {
                    float val = static_cast<float>(value.to_double());
                    bool changed = ImGui::DragFloat(displayName.c_str(), &val, 0.01f);
                    event.activated = ImGui::IsItemActivated();
                    event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                    if (changed)
                    {
                        prop.set_value(obj, static_cast<double>(val));
                        event.changed = true;
                    }
                }
                else if (propType == rttr::type::get<std::string>())
                {
                    std::string val = value.to_string();
                    char buffer[512] = {};
                    strncpy_s(buffer, val.c_str(), sizeof(buffer) - 1);
                    
                    // InputText 렌더링
                    bool changed = ImGui::InputText(displayName.c_str(), buffer, sizeof(buffer));
                    event.activated = ImGui::IsItemActivated();
                    event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                    if (changed)
                    {
                        prop.set_value(obj, std::string(buffer));
                        event.changed = true;
                    }
                    
                    // 드래그앤드롭 지원 감지
                    std::string propNameLower = propName;
                    std::transform(propNameLower.begin(), propNameLower.end(), propNameLower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    
                    // 파일 경로 필드 감지: "Path", "path", "Asset", "asset", "File", "file" 등이 포함된 경우
                    bool isPathField = propNameLower.find("path") != std::string::npos ||
                                       propNameLower.find("asset") != std::string::npos ||
                                       propNameLower.find("file") != std::string::npos ||
                                       propNameLower.find("scene") != std::string::npos;
                    
                    // 엔티티 참조 필드 감지: 
                    // - 메타데이터 "EntityRef" 존재
                    // - "target"과 "name"이 모두 포함된 경우 (targetPosition 같은 경우 제외)
                    // - "target"과 "id"가 모두 포함된 경우
                    bool isEntityRefField = prop.get_metadata("EntityRef") ||
                                           (propNameLower.find("target") != std::string::npos && 
                                            propNameLower.find("name") != std::string::npos) ||
                                           (propNameLower.find("target") != std::string::npos && 
                                            propNameLower.find("id") != std::string::npos);
                    
                    // 드롭 타겟 시작 (파일 경로 또는 엔티티 참조)
                    if ((isPathField || isEntityRefField) && ImGui::BeginDragDropTarget())
                    {
                        // 엔티티 참조 드롭 처리 (World가 제공된 경우, 우선 처리)
                        if (isEntityRefField && world)
                        {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
                            {
                                IM_ASSERT(payload->DataSize == sizeof(EntityId));
                                EntityId draggedId = *(const EntityId*)payload->Data;
                                
                                if (draggedId != InvalidEntityId)
                                {
                                    // 엔티티 이름 가져오기
                                    std::string entityName = world->GetEntityName(draggedId);
                                    if (entityName.empty())
                                    {
                                        entityName = "Entity " + std::to_string(static_cast<uint32_t>(draggedId));
                                    }
                                    
                                    prop.set_value(obj, entityName);
                                    event.changed = true;
                                }
                            }
                        }
                        
                        // 파일 경로 드롭 처리
                        if (isPathField)
                        {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
                            {
                                const char* pathStr = static_cast<const char*>(payload->Data);
                                std::filesystem::path droppedPath(pathStr);
                                
                                // 씬 경로 필드인 경우 .scene 파일만 허용
                                bool isSceneField = propNameLower.find("scene") != std::string::npos;
                                if (isSceneField)
                                {
                                    std::string ext = droppedPath.extension().string();
                                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                    if (ext != ".scene")
                                    {
                                        // .scene 파일이 아니면 무시
                                        ImGui::EndDragDropTarget();
                                        return event;
                                    }
                                }
                                
                                // 절대 경로를 상대 경로로 변환
                                std::string logicalPath = droppedPath.string();
                                
                                if (droppedPath.is_absolute())
                                {
                                    try {
                                        // ResourceManager를 통해 논리 경로로 변환 시도
                                        auto& rm = ResourceManager::Get();
                                        std::filesystem::path logical = rm.NormalizeResourcePathAbsoluteToLogical(droppedPath);
                                        
                                        // 논리 경로로 변환 성공 (Resource/... 형식)
                                        if (!logical.is_absolute())
                                        {
                                            logicalPath = logical.generic_string();
                                        }
                                        else
                                        {
                                            // 논리 경로 변환 실패 시 프로젝트 루트 기준 상대 경로로 변환 시도
                                            // 프로젝트 루트 구하기 (에디터 모드 기준: exeDir/../../..)
                                            wchar_t exePathW[MAX_PATH] = {};
                                            GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
                                            std::filesystem::path exePath = exePathW;
                                            std::filesystem::path exeDir = exePath.parent_path();
                                            std::filesystem::path projectRoot = exeDir.parent_path().parent_path().parent_path();
                                            
                                            try {
                                                std::filesystem::path relative = std::filesystem::relative(droppedPath, projectRoot);
                                                if (!relative.empty())
                                                {
                                                    const std::string result = relative.generic_string();
                                                    // Assets/, Resource/, Cooked/로 시작하는지 확인
                                                    if (result.find("Assets/") == 0 || 
                                                        result.find("Resource/") == 0 || 
                                                        result.find("Cooked/") == 0)
                                                    {
                                                        logicalPath = result;
                                                    }
                                                }
                                            }
                                            catch (...) {
                                                // relative() 실패 시 논리 경로 변환 결과 사용
                                            }
                                        }
                                    }
                                    catch (...) {
                                        // ResourceManager 접근 실패 시 원본 경로 사용
                                    }
                                }
                                else
                                {
                                    // 이미 상대 경로인 경우 그대로 사용 (Assets/, Resource/, Cooked/로 시작하는지 확인)
                                    const std::string s = droppedPath.generic_string();
                                    if (s.find("Assets/") == 0 || 
                                        s.find("Resource/") == 0 || 
                                        s.find("Cooked/") == 0)
                                    {
                                        logicalPath = s;
                                    }
                                }
                                
                                prop.set_value(obj, logicalPath);
                                event.changed = true;
                            }
                        }
                        
                        ImGui::EndDragDropTarget();
                    }
                }
                else if (propType.is_enumeration())
                {
                    auto enumType = propType.get_enumeration();
                    std::string currentName = enumType.value_to_name(value).to_string();
                    if (currentName.empty())
                        currentName = "<None>";

                    if (ImGui::BeginCombo(displayName.c_str(), currentName.c_str()))
                    {
                        for (auto& name : enumType.get_names())
                        {
                            bool selected = (name.to_string() == currentName);
                            if (ImGui::Selectable(name.to_string().c_str(), selected))
                            {
                                rttr::variant v = enumType.name_to_value(name);
                                if (v.is_valid())
                                {
                                    prop.set_value(obj, v);
                                    event.changed = true;
                                }
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else if (propType.is_sequential_container())
                {
                    // std::vector<T> 등: 각 요소를 트리 노드로 표시 (AdvancedAnimSocket 등)
                    rttr::variant_sequential_view view = value.create_sequential_view();
                    if (view.is_valid())
                    {
                        for (size_t i = 0; i < view.get_size(); ++i)
                        {
                            rttr::variant itemVal = view.get_value(i);
                            if (!itemVal.is_valid()) continue;
                            rttr::type itemType = itemVal.get_type();
                            std::string nodeLabel = displayName + "[" + std::to_string(i) + "]";
                            if (itemType.is_class())
                            {
                                if (ImGui::TreeNode(nodeLabel.c_str()))
                                {
                                    rttr::instance elemInst = itemVal;
                                    for (auto& subProp : itemType.get_properties())
                                    {
                                        UIEditEvent e = RenderProperty(subProp, elemInst, "", world);
                                        event.changed |= e.changed;
                                        event.activated |= e.activated;
                                        event.deactivatedAfterEdit |= e.deactivatedAfterEdit;
                                    }
                                    ImGui::TreePop();
                                }
                            }
                            else
                            {
                                ImGui::Text("%s: %s", nodeLabel.c_str(), itemVal.to_string().c_str());
                            }
                        }
                    }
                }
                else if (propType.is_class())
                {
                    // 프로퍼티 타입이 클래스인 경우 렌더링
                    rttr::type classType = propType;
                    std::string className = classType.get_name().to_string();

                    // XMFLOAT2 타입 렌더링
                    if (className == "XMFLOAT2")
                    {
                        DirectX::XMFLOAT2 v = value.get_value<DirectX::XMFLOAT2>();
                        event.activated = ImGui::IsItemActivated();
                        if (ImGui::DragFloat2(displayName.c_str(), &v.x, 0.1f))
                        {
                            prop.set_value(obj, v);
                            event.changed = true;
                        }
                        event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                    }
                    // XMFLOAT3 타입 렌더링
                    else if (className == "XMFLOAT3")
                    {
                        DirectX::XMFLOAT3 v = value.get_value<DirectX::XMFLOAT3>();
                        // "color" 또는 "Color"가 포함된 경우 색상 편집 컨트롤로 렌더링
                        bool isColor = propName.find("color") != std::string::npos || 
                                      propName.find("Color") != std::string::npos;
                        
                        bool changed = false;
                        if (isColor)
                        {
                            changed = ImGui::ColorEdit3(displayName.c_str(), &v.x);
                        }
                        else
                        {
                            changed = ImGui::DragFloat3(displayName.c_str(), &v.x, 0.1f);
                        }
                        
                        event.activated = ImGui::IsItemActivated();
                        event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                        
                        if (changed)
                        {
                            prop.set_value(obj, v);
                            event.changed = true;
                        }
                    }
                    // XMFLOAT4 타입 렌더링
                    else if (className == "XMFLOAT4")
                    {
                        rttr::instance inst = value;
                        DirectX::XMFLOAT4* float4 = inst.try_convert<DirectX::XMFLOAT4>();
                        if (float4)
                        {
                            bool isColor = propName.find("color") != std::string::npos || 
                                          propName.find("Color") != std::string::npos;
                            
                            if (isColor)
                            {
                                event.activated = ImGui::IsItemActivated();
                                if (ImGui::ColorEdit4(displayName.c_str(), &float4->x))
                                {
                                    prop.set_value(obj, *float4);
                                    event.changed = true;
                                }
                                event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                            }
                            else
                            {
                                event.activated = ImGui::IsItemActivated();
                                if (ImGui::DragFloat4(displayName.c_str(), &float4->x, 0.1f))
                                {
                                    prop.set_value(obj, *float4);
                                    event.changed = true;
                                }
                                event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                            }
                        }
                    }
                    else
                    {
                        // 트리 노드로 렌더링
                        if (ImGui::TreeNode(displayName.c_str()))
                        {
                            rttr::instance inst = value;
                            UIEditEvent subEvent{};
                            for (auto& subProp : classType.get_properties())
                            {
                                UIEditEvent e = RenderProperty(subProp, inst, "", world);
                                subEvent.changed |= e.changed;
                                subEvent.activated |= e.activated;
                                subEvent.deactivatedAfterEdit |= e.deactivatedAfterEdit;
                            }
                            event.changed |= subEvent.changed;
                            event.activated |= subEvent.activated;
                            event.deactivatedAfterEdit |= subEvent.deactivatedAfterEdit;
                            ImGui::TreePop();
                        }
                    }
                }

                return event;
            }

            /// @param prop 프로퍼티
            /// @param obj 인스턴스
            /// @param minVal 최소값
            /// @param maxVal 최대값
            /// @param label 렌더링할 라벨
            /// @return UI 편집 이벤트 정보
            inline UIEditEvent RenderPropertyWithRange(const rttr::property& prop, rttr::instance& obj,
                                               float minVal, float maxVal,
                                               const std::string& label = "", World* world = nullptr)
            {
                rttr::type propType = prop.get_type();
                if (propType != rttr::type::get<float>() && propType != rttr::type::get<double>())
                {
                    return RenderProperty(prop, obj, label, world);
                }

                std::string propName = prop.get_name().to_string();
                std::string displayName = label.empty() ? propName : label;

                rttr::variant value = prop.get_value(obj);
                if (!value.is_valid())
                    return UIEditEvent{};

                float val = propType == rttr::type::get<float>() ? 
                           value.to_float() : static_cast<float>(value.to_double());
                
                bool changed = ImGui::SliderFloat(displayName.c_str(), &val, minVal, maxVal);
                UIEditEvent event{};
                event.activated = ImGui::IsItemActivated();
                event.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                
                if (changed)
                {
                    if (propType == rttr::type::get<float>())
                        prop.set_value(obj, val);
                    else
                        prop.set_value(obj, static_cast<double>(val));
                    event.changed = true;
                }
                return event;
            }
        }

        /// @param obj 인스턴스
        /// @param filter 필터 함수
        /// @param world World 포인터 (엔티티 참조 드래그 앤 드롭용, 선택적)
        /// @return UI 편집 이벤트 정보 (모든 프로퍼티의 이벤트를 OR 연산)
        template<typename T>
        UIEditEvent RenderInspector(T& obj, const std::function<bool(const std::string&)>& filter = nullptr, World* world = nullptr)
        {
            rttr::type t = rttr::type::get(obj);
            rttr::instance inst = obj;

            UIEditEvent result{};
            for (auto& prop : t.get_properties())
            {
                std::string propName = prop.get_name().to_string();
                
                // roughness, metalness 프로퍼티는 자동으로 SliderFloat로 렌더링
                if (filter && !filter(propName))
                    continue;

                // 그 외 프로퍼티는 자동으로 렌더링
                UIEditEvent event;
                if (propName == "roughness" || propName == "metalness" || propName == "alpha")
                {
                    event = Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, "", world);
                }
                else if (propName == "normalStrength")
                {
                    // 노말맵 강도 조절: 0.0f ~ 5.0f 범위
                    event = Detail::RenderPropertyWithRange(prop, inst, 0.0f, 5.0f, "", world);
                }
                else if (propName == "toonPbrCut1" || propName == "toonPbrCut2" || propName == "toonPbrCut3" ||
                         propName == "toonPbrLevel1" || propName == "toonPbrLevel2" || propName == "toonPbrLevel3")
                {
                    event = Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, "", world);
                }
                else if (propName == "toonPbrStrength")
                {
                    event = Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, "", world);
                }
                else
                {
                    event = Detail::RenderProperty(prop, inst, "", world);
                }
                
                result.changed |= event.changed;
                result.activated |= event.activated;
                result.deactivatedAfterEdit |= event.deactivatedAfterEdit;
            }

            return result;
        }

        /// @param obj 인스턴스
        /// @param propName 프로퍼티 이름
        /// @param label 렌더링할 라벨
        /// @param world World 포인터 (엔티티 참조 드래그 앤 드롭용, 선택적)
        /// @return UI 편집 이벤트 정보
        // 프로퍼티 렌더링
        template<typename T>
        UIEditEvent RenderProperty(T& obj, const std::string& propName, const std::string& label = "", World* world = nullptr)
        {
            rttr::type t = rttr::type::get(obj);
            rttr::instance inst = obj;
            rttr::property prop = t.get_property(propName);
            
            if (!prop.is_valid())
            {
                OutputDebugStringA(("Type Not Registered: " + std::string(typeid(T).name()) + "\n").c_str());
                return UIEditEvent{};
            }

            return Detail::RenderProperty(prop, inst, label.empty() ? propName : label, world);
        }

        // 프로퍼티 렌더링
        /// @param obj 인스턴스
        /// @param labelMap 렌더링할 라벨 맵
        /// @param world World 포인터 (엔티티 참조 드래그 앤 드롭용, 선택적)
        /// @return UI 편집 이벤트 정보
        template<typename T>
        UIEditEvent RenderInspectorWithLabels(T& obj, const std::unordered_map<std::string, std::string>& labelMap, World* world = nullptr)
        {
            rttr::type t = rttr::type::get(obj);
            rttr::instance inst = obj;

            UIEditEvent result{};
            for (auto& prop : t.get_properties())
            {
                std::string propName = prop.get_name().to_string();
                std::string displayLabel = propName;
                
                auto it = labelMap.find(propName);
                if (it != labelMap.end())
                    displayLabel = it->second;

                // roughness, metalness는 자동으로 SliderFloat로 렌더링
                UIEditEvent event;
                if (propName == "roughness" || propName == "metalness" || propName == "alpha")
                {
                    event = Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, displayLabel, world);
                }
                else if (propName == "normalStrength")
                {
                    event = Detail::RenderPropertyWithRange(prop, inst, 0.0f, 5.0f, displayLabel, world);
                }
                else if (propName == "toonPbrCut1" || propName == "toonPbrCut2" || propName == "toonPbrCut3" ||
                         propName == "toonPbrLevel1" || propName == "toonPbrLevel2" || propName == "toonPbrLevel3")
                {
                    event = Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, displayLabel, world);
                }
                else if (propName == "toonPbrStrength")
                {
                    event = Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, displayLabel, world);
                }
                else
                {
                    event = Detail::RenderProperty(prop, inst, displayLabel, world);
                }
                
                result.changed |= event.changed;
                result.activated |= event.activated;
                result.deactivatedAfterEdit |= event.deactivatedAfterEdit;
            }

            return result;
        }
    }
}
