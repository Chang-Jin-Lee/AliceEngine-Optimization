#include "Game/AnimBlueprintAsset.h"

#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include "json/json.hpp"

#include <fstream>

namespace Alice
{
    namespace
    {
        AnimBPParamType ParamTypeFromName(const std::string& s)
        {
            if (s == "Bool") return AnimBPParamType::Bool;
            if (s == "Int") return AnimBPParamType::Int;
            if (s == "Trigger") return AnimBPParamType::Trigger;
            return AnimBPParamType::Float;
        }

        AnimBPCmpOp CmpOpFromName(const std::string& s)
        {
            if (s == "==") return AnimBPCmpOp::EQ;
            if (s == "!=") return AnimBPCmpOp::NEQ;
            if (s == ">=") return AnimBPCmpOp::GTE;
            if (s == "<=") return AnimBPCmpOp::LTE;
            if (s == "<")  return AnimBPCmpOp::LT;
            if (s == "IsSet") return AnimBPCmpOp::IsSet;
            return AnimBPCmpOp::GT;
        }

        // Blend 그래프 JSON에서 "Clip 노드 2개 -> 하나의 Blend 노드" 패턴만 추출
        void ExtractSimpleStateBlend(const nlohmann::json& j,
                                     AnimBlueprintAsset& out)
        {
            if (!j.contains("blendGraphs") || !j["blendGraphs"].is_array())
                return;

            for (const auto& g : j["blendGraphs"])
            {
                const std::uint64_t stateNodeId = g.value("stateId", 0ULL);
                if (stateNodeId == 0)
                    continue;

                // pinId -> clipName, pinId -> blendNodeId 매핑
                std::unordered_map<std::uint64_t, std::string> pinToClip;
                std::unordered_map<std::uint64_t, std::uint64_t> pinToBlendNode;

                if (g.contains("nodes") && g["nodes"].is_array())
                {
                    for (const auto& n : g["nodes"])
                    {
                        const std::string type = n.value("type", "");
                        const std::uint64_t nodeId = n.value("id", 0ULL);

                        if (type == "Clip")
                        {
                            const std::string clip = n.value("clip", "");
                            if (clip.empty())
                                continue;

                            if (n.contains("outputs") && n["outputs"].is_array())
                            {
                                for (const auto& p : n["outputs"])
                                {
                                    std::uint64_t pinId = p.value("id", 0ULL);
                                    if (pinId != 0)
                                        pinToClip[pinId] = clip;
                                }
                            }
                        }
                        else if (type == "Blend")
                        {
                            if (n.contains("inputs") && n["inputs"].is_array())
                            {
                                for (const auto& p : n["inputs"])
                                {
                                    std::uint64_t pinId = p.value("id", 0ULL);
                                    if (pinId != 0)
                                        pinToBlendNode[pinId] = nodeId;
                                }
                            }
                        }
                    }
                }

                // Blend 노드별로 어떤 Clip 들이 들어오는지 수집
                std::unordered_map<std::uint64_t, std::vector<std::string>> blendNodeToClips;

                if (g.contains("links") && g["links"].is_array())
                {
                    for (const auto& l : g["links"])
                    {
                        std::uint64_t a = l.value("a", 0ULL);
                        std::uint64_t b = l.value("b", 0ULL);
                        if (a == 0 || b == 0)
                            continue;

                        // 한 쪽은 Clip 출력, 한 쪽은 Blend 입력인 경우만 인식
                        auto itClipA = pinToClip.find(a);
                        auto itClipB = pinToClip.find(b);
                        auto itBlendA = pinToBlendNode.find(a);
                        auto itBlendB = pinToBlendNode.find(b);

                        if (itClipA != pinToClip.end() && itBlendB != pinToBlendNode.end())
                        {
                            blendNodeToClips[itBlendB->second].push_back(itClipA->second);
                        }
                        else if (itClipB != pinToClip.end() && itBlendA != pinToBlendNode.end())
                        {
                            blendNodeToClips[itBlendA->second].push_back(itClipB->second);
                        }
                    }
                }

                // 2-way 블렌드만 AnimBPStateBlend 로 등록
                for (auto& kv : blendNodeToClips)
                {
                    auto& clips = kv.second;
                    if (clips.size() != 2)
                        continue;

                    AnimBPStateBlend sb;
                    sb.stateId = stateNodeId;
                    sb.clipA = clips[0];
                    sb.clipB = clips[1];
                    out.stateBlends.push_back(std::move(sb));
                    // 상태당 하나만 있으면 충분하므로 break
                    break;
                }
            }
        }
    }

    bool LoadAnimBlueprintAssetAuto(const ResourceManager& resources,
                                    const std::filesystem::path& logicalPath,
                                    AnimBlueprintAsset& out)
    {
        out = {};

        auto sp = resources.LoadSharedBinaryAuto(logicalPath);
        if (!sp)
        {
            ALICE_LOG_ERRORF("[AnimBP] Load FAILED: %s", logicalPath.generic_string().c_str());
            return false;
        }

        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(sp->begin(), sp->end());
        }
        catch (...)
        {
            ALICE_LOG_ERRORF("[AnimBP] JSON parse FAILED: %s", logicalPath.generic_string().c_str());
            return false;
        }

        out.targetMesh = j.value("targetMesh", std::string{});

        if (j.contains("params") && j["params"].is_array())
        {
            for (const auto& jp : j["params"])
            {
                AnimBPParam p;
                p.name = jp.value("name", "Param");
                p.type = ParamTypeFromName(jp.value("type", "Float"));
                p.b = jp.value("b", false);
                p.i = jp.value("i", 0);
                p.f = jp.value("f", 0.0f);
                p.trigger = jp.value("trigger", false);
                out.params.push_back(p);
            }
        }

        if (j.contains("fsm") && j["fsm"].contains("states"))
        {
            for (const auto& js : j["fsm"]["states"])
            {
                AnimBPState s;
                s.id = js.value("id", 0ULL);
                s.pinIn = js.value("pinIn", 0ULL);
                s.pinOut = js.value("pinOut", 0ULL);
                s.name = js.value("name", "State");
                s.clip = js.value("clip", "Idle");
                s.playRate = js.value("playRate", 1.0f);
                out.states.push_back(std::move(s));
            }
        }

        if (j.contains("fsm") && j["fsm"].contains("trans"))
        {
            for (const auto& jt : j["fsm"]["trans"])
            {
                AnimBPTransition t;
                t.id = jt.value("id", 0ULL);
                t.fromOut = jt.value("fromOut", 0ULL);
                t.toIn = jt.value("toIn", 0ULL);
                t.blendTime = jt.value("blendTime", 0.2f);
                t.canInterrupt = jt.value("canInterrupt", true);

                if (jt.contains("conds") && jt["conds"].is_array())
                {
                    for (const auto& jc : jt["conds"])
                    {
                        AnimBPCond c;
                        c.param = jc.value("param", "");
                        c.type = ParamTypeFromName(jc.value("type", "Float"));
                        c.op = CmpOpFromName(jc.value("op", ">"));
                        c.b = jc.value("b", false);
                        c.i = jc.value("i", 0);
                        c.f = jc.value("f", 0.0f);
                        t.conds.push_back(std::move(c));
                    }
                }

                out.transitions.push_back(std::move(t));
            }
        }

        // Blend 그래프에서 간단한 2-way 블렌드 정보 추출
        ExtractSimpleStateBlend(j, out);

        return !out.states.empty();
    }
}

