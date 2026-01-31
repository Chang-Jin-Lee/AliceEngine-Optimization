#pragma once

// Windows.h의 min/max 매크로 충돌 방지 (RTTR 헤더와의 충돌 방지)
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <rttr/type.h>
#include <rttr/variant.h>
#include <rttr/instance.h>
#include <rttr/property.h>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>

#include "Runtime/Resources/Serialization/JsonRttr.h"

namespace Alice
{
    /// @note RTTR 기반으로 직렬화하는 유틸리티 클래스
    namespace ReflectionSerializer
    {
        /// @param path 파일 경로
        /// @param obj 인스턴스
        /// @return 저장 성공 여부
        template<typename T>
        bool Save(const std::filesystem::path& path, const T& obj)
        {
            // 템플릿 파라미터로 타입 가져오기
            rttr::type t = rttr::type::get<T>();
            if (!t.is_valid())
            {
                // 실패하면 객체로부터 타입 가져오기 시도
                t = rttr::type::get(obj);
                if (!t.is_valid())
                    return false;
            }
            
            rttr::instance inst = const_cast<T&>(obj);

            const JsonRttr::json j = JsonRttr::ToJsonObject(inst);
            if (!JsonRttr::SaveJsonFile(path, j, 4)) return false;
            return true;
        }

        /// @param path 파일 경로
        /// @param obj 인스턴스
        /// @return 로드 성공 여부
        template<typename T>
        bool Load(const std::filesystem::path& path, T& obj)
        {
            // 템플릿 파라미터로 타입 가져오기
            rttr::type t = rttr::type::get<T>();
            if (!t.is_valid())
            {
                // 실패하면 객체로부터 타입 가져오기 시도
                t = rttr::type::get(obj);
                if (!t.is_valid())
                    return false;
            }
            
            rttr::instance inst = obj;

            JsonRttr::json j;
            if (!JsonRttr::LoadJsonFile(path, j)) return false;

            if (!JsonRttr::FromJsonObject(inst, j)) return false;

            return true;
        }

        /// 필요한 경우: 특정 프로퍼티만 저장
        template<typename T, typename Filter>
        bool SaveFiltered(const std::filesystem::path& path, const T& obj, const Filter& filter)
        {
            rttr::type t = rttr::type::get<T>();
            if (!t.is_valid())
            {
                t = rttr::type::get(obj);
                if (!t.is_valid())
                    return false;
            }
            
            rttr::instance inst = const_cast<T&>(obj);

            JsonRttr::json j = JsonRttr::json::object();
            for (const auto& prop : t.get_properties())
            {
                std::string propName = prop.get_name().to_string();
                if (!filter(propName)) continue;

                rttr::variant value = prop.get_value(inst);
                if (!value.is_valid()) continue;

                j[propName] = JsonRttr::ToJsonVariant(value);
            }

            if (!JsonRttr::SaveJsonFile(path, j, 4)) return false;
            return true;
        }
    }
}
