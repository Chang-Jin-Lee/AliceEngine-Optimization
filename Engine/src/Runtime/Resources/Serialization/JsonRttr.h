#pragma once

// RTTR <-> nlohmann::json 변환 유틸
// - 목적: 컴포넌트의 프로퍼티를 RTTR로 열거해서 JSON으로 저장/로드
// - 원칙: 짧고 단순하게, 실패는 즉시 false, 성공은 마지막 return true

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <rttr/type.h>
#include <rttr/enumeration.h>
#include <rttr/instance.h>
#include <rttr/property.h>
#include <rttr/variant.h>
#include <rttr/variant_associative_view.h>
#include <rttr/variant_sequential_view.h>

#include "ThirdParty/json/json.hpp"
#include "Runtime/Foundation/Logger.h"

namespace Alice
{
    namespace JsonRttr
    {
        using json = nlohmann::json;

        inline bool EnsureParentDir(const std::filesystem::path& path)
        {
            const auto parent = path.parent_path();
            if (parent.empty()) return true;

            if (std::filesystem::exists(parent)) return true;

            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            return true;
        }

        inline bool LoadJsonFile(const std::filesystem::path& path, json& out)
        {
            out = json{};

            std::ifstream ifs(path);
            if (!ifs.is_open()) return false;

            try
            {
                ifs >> out;
            }
            catch (...)
            {
                return false;
            }

            return true;
        }

        inline bool SaveJsonFile(const std::filesystem::path& path, const json& j, int indent = 4)
        {
            EnsureParentDir(path);

            std::ofstream ofs(path);
            if (!ofs.is_open()) return false;

            // 한글 경로 오류 해결
            try
            {
                ofs << j.dump(indent);
            }
            catch (...)
            {
                ALICE_LOG_ERRORF("JsonRttr 파일의 inline bool SaveJsonFile(const std::filesystem::path& path, const json& j, int indent = 4) 부분에 uft8 에러가 났습니다. 저장 경로에 힌글이 있습니다. 다른 글자로 대체합니다");
                ofs << j.dump(indent, ' ', false, json::error_handler_t::replace);
            }
            
            return true;
        }

        inline json ToJsonVariant(const rttr::variant& v);
        inline bool FromJsonToProperty(rttr::instance obj, const rttr::property& prop, const json& jval);

        inline json ToJsonObject(rttr::instance obj)
        {
            json j = json::object();

            const rttr::type t = obj.get_type();
            for (const auto& prop : t.get_properties())
            {
                // Unity 룰:
                // - public  : 기본 저장/노출
                // - private : SerializeField 메타데이터가 있을 때만 저장/노출
                if (prop.get_access_level() != rttr::access_levels::public_access &&
                    !prop.get_metadata("SerializeField").is_valid())
                    continue;
                if (prop.get_metadata("BindWidget").is_valid())
                    continue;

                rttr::variant value = prop.get_value(obj);
                if (!value.is_valid()) continue;

                j[prop.get_name().to_string()] = ToJsonVariant(value);
            }

            return j;
        }

        // 타입을 "명시"해서 프로퍼티를 열거합니다.
        // - IScript*처럼 베이스 타입으로 인스턴스를 만들면 obj.get_type()은 베이스로 나옵니다.
        // - Editor/Serializer에서 scriptName으로 type을 알 수 있으므로 이걸 사용합니다.
        inline json ToJsonObject(rttr::instance obj, const rttr::type& t)
        {
            json j = json::object();
            if (!t.is_valid())
                return j;

            for (const auto& prop : t.get_properties())
            {
                if (prop.get_access_level() != rttr::access_levels::public_access &&
                    !prop.get_metadata("SerializeField").is_valid())
                    continue;
                if (prop.get_metadata("BindWidget").is_valid())
                    continue;

                rttr::variant value = prop.get_value(obj);
                if (!value.is_valid()) continue;
                j[prop.get_name().to_string()] = ToJsonVariant(value);
            }
            return j;
        }

        inline bool FromJsonObject(rttr::instance obj, const json& j)
        {
            if (!j.is_object()) return false;

            const rttr::type t = obj.get_type();
            for (const auto& prop : t.get_properties())
            {
                if (prop.get_access_level() != rttr::access_levels::public_access &&
                    !prop.get_metadata("SerializeField").is_valid())
                    continue;
                if (prop.get_metadata("BindWidget").is_valid())
                    continue;

                const std::string key = prop.get_name().to_string();
                auto it = j.find(key);
                if (it == j.end())
                    continue;

                if (!FromJsonToProperty(obj, prop, *it))
                {
                    ALICE_LOG_ERRORF("[JsonRttr] Failed to set property \"%s\" on type \"%s\" (json type: %s)",
                                     key.c_str(),
                                     t.get_name().to_string().c_str(),
                                     it->type_name());
                    return false;
                }
            }

            return true;
        }

        inline bool FromJsonObject(rttr::instance obj, const json& j, const rttr::type& t)
        {
            if (!j.is_object()) return false;
            if (!t.is_valid()) return true;

            for (const auto& prop : t.get_properties())
            {
                if (prop.get_access_level() != rttr::access_levels::public_access &&
                    !prop.get_metadata("SerializeField").is_valid())
                    continue;
                if (prop.get_metadata("BindWidget").is_valid())
                    continue;

                const std::string key = prop.get_name().to_string();
                auto it = j.find(key);
                if (it == j.end())
                    continue;

                if (!FromJsonToProperty(obj, prop, *it))
                {
                    ALICE_LOG_ERRORF("[JsonRttr] Failed to set property \"%s\" on type \"%s\" (json type: %s)",
                                     key.c_str(),
                                     t.get_name().to_string().c_str(),
                                     it->type_name());
                    return false;
                }
            }
            return true;
        }

        inline json ToJsonSequential(const rttr::variant_sequential_view& view)
        {
            json arr = json::array();
            for (std::size_t i = 0; i < view.get_size(); ++i)
            {
                rttr::variant item = view.get_value(i);
                arr.push_back(ToJsonVariant(item));
            }
            return arr;
        }

        inline json ToJsonAssociative(const rttr::variant_associative_view& view)
        {
            // 키가 문자열이면 object, 아니면 array of [k,v]
            const rttr::type keyType = view.get_key_type();
            if (keyType == rttr::type::get<std::string>())
            {
                json obj = json::object();
                for (auto it = view.begin(); it != view.end(); ++it)
                {
                    const std::string k = it.get_key().to_string();
                    obj[k] = ToJsonVariant(it.get_value());
                }
                return obj;
            }

            json arr = json::array();
            for (auto it = view.begin(); it != view.end(); ++it)
            {
                json pair = json::array();
                pair.push_back(ToJsonVariant(it.get_key()));
                pair.push_back(ToJsonVariant(it.get_value()));
                arr.push_back(pair);
            }
            return arr;
        }

        inline json ToJsonVariant(const rttr::variant& v)
        {
            if (!v.is_valid()) return nullptr;

            const rttr::type t = v.get_type();

            if (t.is_arithmetic())
            {
                if (t == rttr::type::get<bool>())
                    return v.to_bool();
                if (t == rttr::type::get<int>())
                    return v.to_int();
                if (t == rttr::type::get<std::uint32_t>())
                    return static_cast<std::uint32_t>(v.to_int64());
                if (t == rttr::type::get<std::int64_t>())
                    return static_cast<std::int64_t>(v.to_int64());
                if (t == rttr::type::get<std::uint64_t>())
                    return static_cast<std::uint64_t>(v.to_int64());
                if (t == rttr::type::get<float>())
                    return v.to_double();
                if (t == rttr::type::get<double>())
                    return v.to_double();

                return v.to_string();
            }

            if (t.is_enumeration())
            {
                // 기본은 문자열 이름
                return v.to_string();
            }

            if (t == rttr::type::get<std::string>())
                return v.to_string();

            if (t.is_sequential_container())
                return ToJsonSequential(v.create_sequential_view());

            if (t.is_associative_container())
                return ToJsonAssociative(v.create_associative_view());

            if (t.is_class())
            {
                rttr::instance inst = v;
                return ToJsonObject(inst);
            }

            return v.to_string();
        }

        inline bool SetArithmetic(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            const rttr::type t = prop.get_type();

            if (t == rttr::type::get<bool>())
            {
                if (!jval.is_boolean() && !jval.is_number()) return false;
                const bool b = jval.is_boolean() ? jval.get<bool>() : (jval.get<double>() != 0.0);
                prop.set_value(obj, b);
                return true;
            }

            if (t == rttr::type::get<int>())
            {
                if (!jval.is_number()) return false;
                prop.set_value(obj, static_cast<int>(jval.get<double>()));
                return true;
            }

            if (t == rttr::type::get<std::uint32_t>())
            {
                if (!jval.is_number()) return false;
                const double v = jval.get<double>();
                if (v < 0.0) return false;
                prop.set_value(obj, static_cast<std::uint32_t>(v));
                return true;
            }

            if (t == rttr::type::get<float>())
            {
                if (!jval.is_number()) return false;
                prop.set_value(obj, static_cast<float>(jval.get<double>()));
                return true;
            }

            if (t == rttr::type::get<double>())
            {
                if (!jval.is_number()) return false;
                prop.set_value(obj, jval.get<double>());
                return true;
            }

            if (t == rttr::type::get<std::int64_t>())
            {
                if (!jval.is_number()) return false;
                prop.set_value(obj, static_cast<std::int64_t>(jval.get<double>()));
                return true;
            }

            if (t == rttr::type::get<std::uint64_t>())
            {
                if (!jval.is_number()) return false;
                const double v = jval.get<double>();
                if (v < 0.0) return false;
                prop.set_value(obj, static_cast<std::uint64_t>(v));
                return true;
            }

            // 나머지 산술형은 문자열로라도 시도(최소 안전)
            prop.set_value(obj, rttr::variant(jval.dump()));
            return true;
        }

        inline bool SetString(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            if (jval.is_string())
            {
                prop.set_value(obj, jval.get<std::string>());
                return true;
            }
            if (jval.is_number() || jval.is_boolean())
            {
                prop.set_value(obj, jval.dump());
                return true;
            }
            if (jval.is_object())
            {
                auto itName = jval.find("name");
                if (itName != jval.end() && itName->is_string())
                {
                    prop.set_value(obj, itName->get<std::string>());
                    return true;
                }
                prop.set_value(obj, std::string{});
                return true;
            }
            if (jval.is_array() || jval.is_null())
            {
                prop.set_value(obj, std::string{});
                return true;
            }
            return false;
        }

        inline bool SetEnum(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            if (!jval.is_string() && !jval.is_number_integer()) return false;

            const rttr::enumeration e = prop.get_type().get_enumeration();
            if (jval.is_string())
            {
                rttr::variant ev = e.name_to_value(jval.get<std::string>());
                if (!ev.is_valid())
                    return false;
                prop.set_value(obj, ev);
                return true;
            }

            // 숫자는 그대로 set_value()로 넣고, RTTR 변환에 맡깁니다.
            if (!prop.set_value(obj, jval.get<int>())) return false;

            return true;
        }

        inline bool SetClass(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            if (!jval.is_object())  return false;

            rttr::variant child = prop.get_value(obj);
            if (!child.is_valid())
            {
                child = prop.get_type().create();
                if (!child.is_valid()) return false;
            }

            rttr::instance childInst = child;
            if (!FromJsonObject(childInst, jval)) return false;

            prop.set_value(obj, child);
            return true;
        }

        inline bool SetSequentialItem(rttr::variant_sequential_view& view, size_t index, const json& jitem)
        {
            if (index >= view.get_size()) return false;

            rttr::variant item = view.get_value(index);
            if (!item.is_valid()) return false;

            rttr::type itemType = item.get_type();

            // 중첩 배열 처리 (예: std::array<std::array<bool, 32>, 32>)
            if (itemType.is_sequential_container() && jitem.is_array())
            {
                // 중첩 배열 요소를 가져와서 수정
                // RTTR의 variant_sequential_view는 원본 배열을 참조하므로,
                // nestedView를 통해 수정하면 원본이 수정될 수 있지만,
                // std::array의 경우 안전하게 하기 위해 다시 설정
                rttr::variant_sequential_view nestedView = item.create_sequential_view();
                if (nestedView.is_valid())
                {
                    // 중첩 배열의 모든 요소를 JSON에서 로드
                    size_t nestedIndex = 0;
                    for (const auto& nestedItem : jitem)
                    {
                        if (nestedIndex >= nestedView.get_size()) break;
                        if (!SetSequentialItem(nestedView, nestedIndex, nestedItem))
                            return false; // 재귀 호출 실패 시 즉시 반환
                        ++nestedIndex;
                    }
                    
                    // nestedView를 통해 중첩 배열이 수정되었으므로,
                    // 수정된 item을 원래 배열에 다시 설정
                    // std::array의 경우 전체 배열을 다시 설정해야 변경사항이 반영됨
                    if (!view.set_value(index, item)) return false;
                    return true;
                }
            }

            // 기본 타입 처리
            // bool은 boolean 또는 0/1 정수로 저장될 수 있음
            if (itemType == rttr::type::get<bool>())
            {
                bool value = false;
                if (jitem.is_boolean())
                    value = jitem.get<bool>();
                else if (jitem.is_number())
                    value = (jitem.get<double>() != 0.0);
                else
                    return false; // bool 또는 숫자가 아니면 실패
                if (!view.set_value(index, value)) return false;
                return true;
            }
            if (itemType == rttr::type::get<int>() && jitem.is_number())
            {
                if (!view.set_value(index, static_cast<int>(jitem.get<double>()))) return false;
                return true;
            }
            if (itemType == rttr::type::get<float>() && jitem.is_number())
            {
                if (!view.set_value(index, static_cast<float>(jitem.get<double>()))) return false;
                return true;
            }
            if (itemType == rttr::type::get<std::string>())
            {
                if (jitem.is_string())
                {
                    if (!view.set_value(index, jitem.get<std::string>())) return false;
                    return true;
                }
                if (jitem.is_number() || jitem.is_boolean())
                {
                    if (!view.set_value(index, jitem.dump())) return false;
                    return true;
                }
                if (jitem.is_object())
                {
                    auto itName = jitem.find("name");
                    if (itName != jitem.end() && itName->is_string())
                    {
                        if (!view.set_value(index, itName->get<std::string>())) return false;
                        return true;
                    }
                    if (!view.set_value(index, std::string{})) return false;
                    return true;
                }
                if (jitem.is_array() || jitem.is_null())
                {
                    if (!view.set_value(index, std::string{})) return false;
                    return true;
                }
            }

            // 클래스 타입(예: AdvancedAnimSocket): JSON 객체로 역직렬화
            // 새 인스턴스를 생성해 채운 뒤 set_value로 넣어야 저장된 필드가 제대로 반영됨
            if (itemType.is_class() && jitem.is_object())
            {
                rttr::variant newElem = itemType.create();
                if (!newElem.is_valid()) return false;
                rttr::instance inst = newElem;
                if (!FromJsonObject(inst, jitem)) return false;
                if (!view.set_value(index, newElem)) return false;
                return true;
            }

            return false;
        }

        inline bool SetSequential(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            if (!jval.is_array()) return false;

            rttr::variant var = prop.get_value(obj);
            if (!var.is_valid()) return false;

            rttr::variant_sequential_view view = var.create_sequential_view();
            if (!view.is_valid()) return false;

            // 동적 컨테이너(std::vector 등): JSON 배열 크기만큼 확장 후 채움 (소켓 등 저장 복원용)
            if (view.is_dynamic() && view.get_size() < jval.size())
            {
                if (!view.set_size(jval.size()))
                    return false;
            }

            // JSON 배열의 각 요소를 컨테이너에 설정
            size_t index = 0;
            for (const auto& jitem : jval)
            {
                // 배열 크기 체크 (고정 크기 배열의 경우)
                if (index >= view.get_size()) break;
                
                // SetSequentialItem의 반환값 확인
                if (!SetSequentialItem(view, index, jitem))
                    return false; // 역직렬화 실패 시 즉시 반환
                ++index;
            }

            if (!prop.set_value(obj, var)) return false;
            return true;
        }

        inline bool FromJsonToProperty(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            if (!prop.is_valid()) return false;

            const rttr::type t = prop.get_type();

            if (t.is_arithmetic())
                return SetArithmetic(obj, prop, jval);

            if (t.is_enumeration())
                return SetEnum(obj, prop, jval);

            if (t == rttr::type::get<std::string>())
                return SetString(obj, prop, jval);

            if (t.is_sequential_container())
                return SetSequential(obj, prop, jval);

            if (t.is_class())
                return SetClass(obj, prop, jval);

            return true;
        }
    }
}


