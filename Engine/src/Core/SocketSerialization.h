#pragma once

#include "Components/SocketComponent.h"
#include "Core/JsonRttr.h"

namespace Alice
{
    namespace SocketSerialization
    {
        using Json = JsonRttr::json;

        inline Json Float3ToJson(const DirectX::XMFLOAT3& v)
        {
            Json j = Json::object();
            j["x"] = v.x;
            j["y"] = v.y;
            j["z"] = v.z;
            return j;
        }

        inline bool JsonToFloat3(const Json& j, DirectX::XMFLOAT3& out)
        {
            if (!j.is_object())
                return false;

            bool any = false;
            if (auto it = j.find("x"); it != j.end() && it->is_number())
            {
                out.x = static_cast<float>(it->get<double>());
                any = true;
            }
            if (auto it = j.find("y"); it != j.end() && it->is_number())
            {
                out.y = static_cast<float>(it->get<double>());
                any = true;
            }
            if (auto it = j.find("z"); it != j.end() && it->is_number())
            {
                out.z = static_cast<float>(it->get<double>());
                any = true;
            }
            return any;
        }

        inline Json SocketDefToJson(const SocketDef& s)
        {
            Json j = Json::object();
            j["name"] = s.name;
            j["parentBone"] = s.parentBone;
            j["position"] = Float3ToJson(s.position);
            j["rotation"] = Float3ToJson(s.rotation);
            j["scale"] = Float3ToJson(s.scale);
            return j;
        }

        inline bool JsonToSocketDef(const Json& j, SocketDef& out)
        {
            if (!j.is_object())
                return false;

            if (auto it = j.find("name"); it != j.end() && it->is_string())
                out.name = it->get<std::string>();
            if (auto it = j.find("parentBone"); it != j.end() && it->is_string())
                out.parentBone = it->get<std::string>();
            if (auto it = j.find("position"); it != j.end())
                JsonToFloat3(*it, out.position);
            if (auto it = j.find("rotation"); it != j.end())
                JsonToFloat3(*it, out.rotation);
            if (auto it = j.find("scale"); it != j.end())
                JsonToFloat3(*it, out.scale);

            return true;
        }

        inline Json SocketComponentToJson(const SocketComponent& sc)
        {
            Json root = Json::object();
            Json arr = Json::array();
            for (const auto& s : sc.sockets)
            {
                arr.push_back(SocketDefToJson(s));
            }
            root["sockets"] = std::move(arr);
            return root;
        }

        inline bool JsonToSocketComponent(const Json& j, SocketComponent& sc)
        {
            if (!j.is_object())
                return false;

            auto it = j.find("sockets");
            if (it == j.end())
                return true;
            if (!it->is_array())
                return false;

            sc.sockets.clear();
            for (const auto& item : *it)
            {
                if (!item.is_object())
                    continue;

                SocketDef s{};
                if (!JsonToSocketDef(item, s))
                    continue;

                sc.sockets.push_back(std::move(s));
            }

            return true;
        }
    }
}
