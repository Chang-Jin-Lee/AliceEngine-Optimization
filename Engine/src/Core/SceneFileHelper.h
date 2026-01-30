#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <rttr/type.h>
#include <rttr/variant.h>
#include <rttr/instance.h>
#include <rttr/property.h>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

namespace Alice
{
    // SceneFile 유틸리티 클래스
    namespace SceneFileHelper
    {
        // prefix_propertyName: value
        /// @param ofs 출력 스트림
        /// @param component 컴포넌트
        /// @param prefix 접두사
        /// @return 컴포넌트 저장
        template<typename T>
        void SaveComponent(std::ofstream& ofs, const T& component, const std::string& prefix)
        {
            rttr::type t = rttr::type::get(component);
            rttr::instance inst = const_cast<T&>(component);

            for (auto& prop : t.get_properties())
            {
                std::string propName = prop.get_name().to_string();
                rttr::variant value = prop.get_value(inst);
                
                if (!value.is_valid())
                    continue;

                std::string fullKey = prefix + "_" + propName;

                ofs << fullKey << ": ";

                const rttr::type valueType = value.get_type();

                if (valueType.is_arithmetic())
                {
                    if (valueType == rttr::type::get<bool>())
                        ofs << (value.to_bool() ? "1" : "0");
                    else if (valueType == rttr::type::get<int>())
                        ofs << value.to_int();
                    else if (valueType == rttr::type::get<float>())
                        ofs << value.to_float();
                    else if (valueType == rttr::type::get<double>())
                        ofs << value.to_double();
                    else
                        ofs << value.to_string();
                }
                else if (valueType == rttr::type::get<std::string>())
                {
                    std::string strVal = value.to_string();
                    if (!strVal.empty())
                        ofs << strVal;
                }
                else if (valueType.is_class())
                {
                    // XMFLOAT3, XMFLOAT4 타입 저장
                    rttr::instance subInst = value;
                    rttr::type subType = subInst.get_type();
                    bool first = true;

                    for (auto& subProp : subType.get_properties())
                    {
                        if (!first) ofs << " ";
                        first = false;
                        rttr::variant subValue = subProp.get_value(subInst);
                        
                        if (subValue.is_valid() && subValue.get_type().is_arithmetic())
                        {
                            if (subValue.get_type() == rttr::type::get<bool>())
                                ofs << (subValue.to_bool() ? "1" : "0");
                            else if (subValue.get_type() == rttr::type::get<int>())
                                ofs << subValue.to_int();
                            else if (subValue.get_type() == rttr::type::get<float>())
                                ofs << subValue.to_float();
                            else if (subValue.get_type() == rttr::type::get<double>())
                                ofs << subValue.to_double();
                        }
                    }
                }
                else
                {
                    ofs << value.to_string();
                }

                ofs << "\n";
            }
        }

        // prefix_propertyName: value
        template<typename T>
        bool LoadComponentProperty(const std::string& key, const std::string& valueStr, T& component, const std::string& prefix)
        {
            // prefix_propertyName: value
            if (key.find(prefix + "_") != 0)
                return false;

            std::string propName = key.substr(prefix.length() + 1);

            rttr::type t = rttr::type::get(component);
            rttr::instance inst = component;
            rttr::property prop = t.get_property(propName);
            
            if (!prop.is_valid())
                return false;

            rttr::type propType = prop.get_type();

            if (propType.is_arithmetic())
            {
                try {
                    if (propType == rttr::type::get<bool>())
                        prop.set_value(inst, std::stoi(valueStr) != 0);
                    else if (propType == rttr::type::get<int>())
                        prop.set_value(inst, std::stoi(valueStr));
                    else if (propType == rttr::type::get<float>())
                        prop.set_value(inst, std::stof(valueStr));
                    else if (propType == rttr::type::get<double>())
                        prop.set_value(inst, std::stod(valueStr));
                    return true;
                }
                catch (...)
                {
                    return false;
                }
            }
            else if (propType == rttr::type::get<std::string>())
            {
                prop.set_value(inst, valueStr);
                return true;
            }
            else if (propType.is_class())
            {
                // XMFLOAT3, XMFLOAT4 타입 로드
                std::istringstream iss(valueStr);
                std::vector<std::string> tokens;
                std::string token;
                while (iss >> token)
                    tokens.push_back(token);

                rttr::variant newInst = propType.create();
                if (newInst.is_valid())
                {
                    rttr::instance subInst = newInst;
                    int index = 0;
                    for (auto& subProp : propType.get_properties())
                    {
                        if (index >= static_cast<int>(tokens.size()))
                            break;

                        const std::string& valStr = tokens[index];
                        rttr::type subType = subProp.get_type();

                        try {
                            if (subType.is_arithmetic())
                            {
                                if (subType == rttr::type::get<bool>())
                                    subProp.set_value(subInst, std::stoi(valStr) != 0);
                                else if (subType == rttr::type::get<int>())
                                    subProp.set_value(subInst, std::stoi(valStr));
                                else if (subType == rttr::type::get<float>())
                                    subProp.set_value(subInst, std::stof(valStr));
                                else if (subType == rttr::type::get<double>())
                                    subProp.set_value(subInst, std::stod(valStr));
                            }
                        }
                        catch (...)
                        {
                            // 예외 처리
                        }
                        ++index;
                    }
                    prop.set_value(inst, newInst);
                    return true;
                }
            }

            return false;
        }
    }
}
