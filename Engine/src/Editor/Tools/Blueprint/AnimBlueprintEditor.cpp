//#include "Editor/Tools/Blueprint/AnimBlueprintEditor.h"
//
//#include <imgui.h>
//#include <rttr/registration>
//#include "ThirdParty/json/json.hpp"
//
//#include <fstream>
//#include <cstdio>
//#include <algorithm>
//#include <cfloat>
//#include <filesystem>
//
//#include "Runtime/Resources/ResourceManager.h"
//#include "Runtime/Foundation/Helper.h"
//#include "Runtime/Foundation/Logger.h"
//#include "Runtime/Importing/FbxModel.h"
//
//using json = nlohmann::json;
//
//namespace Alice
//{
//    namespace ed = ax::NodeEditor;
//    // =========================================================
//    // RTTR 등록 (enum name<->value 변환에 사용)
//    // =========================================================
//    RTTR_REGISTRATION
//    {
//        using namespace rttr;
//
//        registration::enumeration<AnimBlueprintEditor::ParamType>("ParamType")
//        (
//            value("Bool",    AnimBlueprintEditor::ParamType::Bool),
//            value("Int",     AnimBlueprintEditor::ParamType::Int),
//            value("Float",   AnimBlueprintEditor::ParamType::Float),
//            value("Trigger", AnimBlueprintEditor::ParamType::Trigger)
//        );
//
//        registration::enumeration<AnimBlueprintEditor::CmpOp>("CmpOp")
//        (
//            value("==",     AnimBlueprintEditor::CmpOp::EQ),
//            value("!=",     AnimBlueprintEditor::CmpOp::NEQ),
//            value(">",      AnimBlueprintEditor::CmpOp::GT),
//            value("<",      AnimBlueprintEditor::CmpOp::LT),
//            value(">=",     AnimBlueprintEditor::CmpOp::GTE),
//            value("<=",     AnimBlueprintEditor::CmpOp::LTE),
//            value("IsSet",  AnimBlueprintEditor::CmpOp::IsSet)
//        );
//    }
//
//    // enum <-> string (RTTR)
//    template<typename E>
//    static std::string EnumName(E v)
//    {
//        auto e = rttr::type::get<E>().get_enumeration();
//        auto sv = e.value_to_name(v);          // rttr::string_view
//        return sv.empty() ? std::string{} : std::string(sv.data(), sv.size());
//    }
//
//    template<typename E>
//    static E EnumFromName(const std::string& s, E fallback)
//    {
//        auto e = rttr::type::get<E>().get_enumeration();
//        auto v = e.name_to_value(s);
//        return v.is_valid() ? v.get_value<E>() : fallback;
//    }
//
//    // =========================================================
//    // Selection helpers (이전 NodeEditor 버전 호환)
//    // =========================================================
//    void AnimBlueprintEditor::GetSelectedNodesVec(std::vector<ed::NodeId>& out)
//    {
//        const int cap = std::max(0, ed::GetSelectedObjectCount());
//        out.resize((size_t)cap);
//        const int n = ed::GetSelectedNodes(cap > 0 ? out.data() : nullptr, cap);
//        out.resize((size_t)std::max(0, n));
//    }
//
//    void AnimBlueprintEditor::GetSelectedLinksVec(std::vector<ed::LinkId>& out)
//    {
//        const int cap = std::max(0, ed::GetSelectedObjectCount());
//        out.resize((size_t)cap);
//        const int n = ed::GetSelectedLinks(cap > 0 ? out.data() : nullptr, cap);
//        out.resize((size_t)std::max(0, n));
//    }
//
//    // 노드안에서 줄바꿈 되는 그거 헬퍼
//    static void NodeHr(float w)
//    {
//        auto* dl = ImGui::GetWindowDrawList();
//        ImVec2 p = ImGui::GetCursorScreenPos();
//        dl->AddLine(p, ImVec2(p.x + w, p.y), ImGui::GetColorU32(ImGuiCol_Separator));
//        ImGui::Dummy(ImVec2(w, 1)); // 아이템 제출 + 줄바꿈
//    }
//
//    // =========================================================
//    // IDs
//    // =========================================================
//    int AnimBlueprintEditor::NewId() { return m_NextId++; }
//
//    uint64_t AnimBlueprintEditor::ToU64(ed::NodeId id) { return (uint64_t)id.Get(); }
//    uint64_t AnimBlueprintEditor::ToU64(ed::PinId  id) { return (uint64_t)id.Get(); }
//    uint64_t AnimBlueprintEditor::ToU64(ed::LinkId id) { return (uint64_t)id.Get(); }
//
//    ed::NodeId AnimBlueprintEditor::NodeFromU64(uint64_t v) { return ed::NodeId((uintptr_t)v); }
//    ed::PinId  AnimBlueprintEditor::PinFromU64(uint64_t v)  { return ed::PinId((uintptr_t)v); }
//    ed::LinkId AnimBlueprintEditor::LinkFromU64(uint64_t v) { return ed::LinkId((uintptr_t)v); }
//
//    uintptr_t AnimBlueprintEditor::Key(ed::NodeId id) const { return (uintptr_t)id.Get(); }
//
//    // =========================================================
//    // Init / Shutdown
//    // =========================================================
//    void AnimBlueprintEditor::Init()
//    {
//        if (m_FsmCtx) return;
//
//        ed::Config cfg;
//        cfg.SettingsFile = "AnimFSM.layout";
//        m_FsmCtx = ed::CreateEditor(&cfg);
//
//        // 기본 파라미터
//        AnimParam p;
//        p.name = "Speed";
//        p.type = ParamType::Float;
//        p.f = 0.0f;
//        m_Params.push_back(p);
//
//        // 기본 상태 2개
//        State& a = AddState("Idle"); a.clip = "Idle";
//        State& b = AddState("Run");  b.clip = "Run";
//
//        m_OpenState = a.id;
//
//        // 기본 그래프 상태별로
//        GetOrCreateGraph(a.id);
//        GetOrCreateGraph(b.id);
//    }
//
//    void AnimBlueprintEditor::Shutdown()
//    {
//        DestroyAllGraphs();
//        if (m_FsmCtx) { ed::DestroyEditor(m_FsmCtx); m_FsmCtx = nullptr; }
//
//        m_Params.clear();
//        m_States.clear();
//        m_Trans.clear();
//        m_InspectState = nullptr;
//        m_InspectTrans = nullptr;
//        m_OpenState = {};
//        m_NextId = 1;
//    }
//
//    // =========================================================
//    // Params
//    // =========================================================
//    AnimBlueprintEditor::AnimParam* AnimBlueprintEditor::FindParam(const std::string& name)
//    {
//        for (auto& p : m_Params)
//            if (p.name == name) return &p;
//        return nullptr;
//    }
//
//    void AnimBlueprintEditor::DrawParamValueUI(AnimParam& p)
//    {
//        // 타입에 맞는 값만 편집 Trigger는 버튼 눌렀을 때 함
//        if (p.type == ParamType::Bool)   ImGui::Checkbox("Value", &p.b);
//        if (p.type == ParamType::Int)    ImGui::DragInt("Value", &p.i, 1.0f);
//        if (p.type == ParamType::Float)  ImGui::DragFloat("Value", &p.f, 0.01f);
//
//        if (p.type == ParamType::Trigger)
//        {
//            if (ImGui::Button(p.trigger ? "Triggered" : "Fire"))
//                p.trigger = true;
//
//            ImGui::SameLine();
//            if (ImGui::Button("Reset"))
//                p.trigger = false;
//        }
//    }
//
//    void AnimBlueprintEditor::DrawCondValueUI(Cond& c)
//    {
//        if (c.type == ParamType::Bool)   ImGui::Checkbox("V", &c.b);
//        if (c.type == ParamType::Int)    ImGui::DragInt("V", &c.i, 1.0f);
//        if (c.type == ParamType::Float)  ImGui::DragFloat("V", &c.f, 0.01f);
//
//        // Trigger는 값 없음
//    }
//
//    void AnimBlueprintEditor::DrawParamsPanel()
//    {
//        ImGui::BeginChild("Params", ImVec2(0, 190), true);
//        ImGui::TextUnformatted("Parameters");
//        ImGui::Separator();
//
//        if (ImGui::Button("+ Param"))
//        {
//            AnimParam p;
//            p.name = "Param" + std::to_string((int)m_Params.size() + 1);
//            p.type = ParamType::Bool;
//            m_Params.push_back(p);
//        }
//        ImGui::SameLine();
//        if (ImGui::Button("Reset Triggers"))
//        {
//            for (auto& p : m_Params) if (p.type == ParamType::Trigger) p.trigger = false;
//        }
//
//        ImGui::Separator();
//
//        for (int i = 0; i < (int)m_Params.size(); ++i)
//        {
//            auto& p = m_Params[(size_t)i];
//            ImGui::PushID(i);
//
//            char nameBuf[64]{};
//            std::snprintf(nameBuf, sizeof(nameBuf), "%s", p.name.c_str());
//            ImGui::SetNextItemWidth(140);
//            if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
//                p.name = nameBuf;
//
//            ImGui::SameLine();
//
//            // ParamType combo RTTR enum 이름을 사용함
//            ImGui::SetNextItemWidth(90);
//            std::string cur = EnumName(p.type);
//            if (ImGui::BeginCombo("##type", cur.c_str()))
//            {
//                auto en = rttr::type::get<ParamType>().get_enumeration();
//                for (auto sv : en.get_names())
//                {
//                    std::string name = sv.empty() ? std::string{} : std::string(sv.data(), sv.size());
//                    auto v = en.name_to_value(sv).get_value<ParamType>(); // sv 그대로 써도 됨
//                    bool sel = (v == p.type);
//                    if (ImGui::Selectable(name.c_str(), sel))
//                        p.type = v;
//                }
//                ImGui::EndCombo();
//            }
//
//            ImGui::SameLine();
//            if (ImGui::SmallButton("X"))
//            {
//                // 파라미터 삭제 시, 조건에서 참조하는 것들은 남겨둠 나중에 정리를 해야하는데 일단은 이렇게 두자. 돌아간다.
//                // 나중에 더 복잡한 블루프린트가 필요할 때 그 때 바꿔보자
//                m_Params.erase(m_Params.begin() + i);
//                ImGui::PopID();
//                break;
//            }
//
//            // 값 편집
//            DrawParamValueUI(p);
//
//            ImGui::Separator();
//            ImGui::PopID();
//        }
//
//        ImGui::EndChild();
//    }
//
//    // =========================================================
//    // FSM
//    // =========================================================
//    void AnimBlueprintEditor::QueuePlaceFsm(ed::NodeId id, bool useMouse)
//    {
//        m_PendingPlaceFsm = id;
//        m_PendingPlaceFsmUseMouse = useMouse;
//    }
//
//    AnimBlueprintEditor::State& AnimBlueprintEditor::AddState(const char* name)
//    {
//        State s;
//        s.id  = ed::NodeId((uintptr_t)NewId());
//        s.in  = ed::PinId((uintptr_t)NewId());
//        s.out = ed::PinId((uintptr_t)NewId());
//        s.name = name;
//        m_States.push_back(std::move(s));
//        return m_States.back();
//    }
//
//    AnimBlueprintEditor::State* AnimBlueprintEditor::FindState(ed::NodeId id)
//    {
//        for (auto& s : m_States) if (s.id == id) return &s;
//        return nullptr;
//    }
//
//    AnimBlueprintEditor::State* AnimBlueprintEditor::FindStateByPin(ed::PinId pin)
//    {
//        for (auto& s : m_States) if (s.in == pin || s.out == pin) return &s;
//        return nullptr;
//    }
//
//    AnimBlueprintEditor::Transition* AnimBlueprintEditor::FindTrans(ed::LinkId id)
//    {
//        for (auto& t : m_Trans) if (t.id == id) return &t;
//        return nullptr;
//    }
//
//    bool AnimBlueprintEditor::CanCreateTrans(ed::PinId a, ed::PinId b, ed::PinId& outFrom, ed::PinId& outTo) const
//    {
//        if (!a || !b || a == b) return false;
//
//        const State* sa = nullptr;
//        const State* sb = nullptr;
//
//        for (auto& s : m_States)
//        {
//            if (s.in == a || s.out == a) sa = &s;
//            if (s.in == b || s.out == b) sb = &s;
//        }
//        if (!sa || !sb || sa == sb) return false;
//
//        bool aOut = (sa->out == a);
//        bool bOut = (sb->out == b);
//
//        // out -> in 만 허용
//        if (aOut && !bOut) { outFrom = a; outTo = b; return true; }
//        if (bOut && !aOut) { outFrom = b; outTo = a; return true; }
//        return false;
//    }
//
//    void AnimBlueprintEditor::DeleteTrans(ed::LinkId id)
//    {
//        for (int i = (int)m_Trans.size() - 1; i >= 0; --i)
//            if (m_Trans[(size_t)i].id == id) { m_Trans.erase(m_Trans.begin() + i); return; }
//    }
//
//    void AnimBlueprintEditor::DeleteState(ed::NodeId id)
//    {
//        State* s = FindState(id);
//        if (!s) return;
//
//        // 연결된 Transition 제거
//        for (int i = (int)m_Trans.size() - 1; i >= 0; --i)
//        {
//            auto& t = m_Trans[(size_t)i];
//            if (t.fromOut == s->out || t.toIn == s->in)
//                m_Trans.erase(m_Trans.begin() + i);
//        }
//
//        // 상태별 그래프도 삭제
//        m_Graphs.erase(Key(id));
//
//        for (int i = (int)m_States.size() - 1; i >= 0; --i)
//            if (m_States[(size_t)i].id == id)
//            {
//                if (m_OpenState == id) m_OpenState = {};
//                m_States.erase(m_States.begin() + i);
//                return;
//            }
//    }
//
//    void AnimBlueprintEditor::DeleteSelectedFsm()
//    {
//        std::vector<ed::LinkId> links;
//        GetSelectedLinksVec(links);
//        for (auto id : links) DeleteTrans(id);
//
//        std::vector<ed::NodeId> nodes;
//        GetSelectedNodesVec(nodes);
//        for (auto id : nodes) DeleteState(id);
//    }
//
//    void AnimBlueprintEditor::HandleCreateFsm()
//    {
//        if (!ed::BeginCreate()) return;
//
//        ed::PinId a{}, b{};
//        if (ed::QueryNewLink(&a, &b))
//        {
//            ed::PinId from{}, to{};
//            if (CanCreateTrans(a, b, from, to))
//            {
//                if (ed::AcceptNewItem())
//                {
//                    Transition t;
//                    t.id = ed::LinkId((uintptr_t)NewId());
//                    t.fromOut = from;
//                    t.toIn = to;
//
//                    // 기본 조건 하나 파라미터가 있으면 실행 될 것
//                    if (!m_Params.empty())
//                    {
//                        Cond c;
//                        c.param = m_Params[0].name;
//                        c.type = m_Params[0].type;
//                        c.op = (c.type == ParamType::Bool) ? CmpOp::EQ : CmpOp::GT;
//                        if (c.type == ParamType::Float) c.f = 0.1f;
//                        if (c.type == ParamType::Trigger) c.op = CmpOp::IsSet;
//                        t.conds.push_back(c);
//                    }
//
//                    m_Trans.push_back(std::move(t));
//                }
//            }
//            else ed::RejectNewItem();
//        }
//
//        ed::EndCreate();
//    }
//
//    void AnimBlueprintEditor::HandleDeleteFsm()
//    {
//        if (!ed::BeginDelete()) return;
//
//        ed::LinkId lid{};
//        while (ed::QueryDeletedLink(&lid))
//            if (ed::AcceptDeletedItem()) DeleteTrans(lid);
//
//        ed::NodeId nid{};
//        while (ed::QueryDeletedNode(&nid))
//            if (ed::AcceptDeletedItem()) DeleteState(nid);
//
//        ed::EndDelete();
//    }
//
//    void AnimBlueprintEditor::HandleDoubleClickOpenPostEnd()
//    {
//        // 이 버전 NodeEditor는 hovered API가 없고,
//        // double-click 결과는 ed::End() 이후에 GetDoubleClickedNode()로만 얻는게 안전함.
//        ed::NodeId dc = ed::GetDoubleClickedNode();
//        if (!dc) return;
//
//        State* s = FindState(dc);
//        if (!s) return;
//
//        m_OpenState = s->id;
//        GetOrCreateGraph(s->id);
//    }
//
//    void AnimBlueprintEditor::DrawFsmInspector()
//    {
//        ImGui::BeginChild("FSM_Inspector", ImVec2(0, 220), true);
//        ImGui::TextUnformatted("FSM Inspector");
//        ImGui::Separator();
//
//        m_InspectState = nullptr;
//        m_InspectTrans = nullptr;
//
//        std::vector<ed::NodeId> nodes;
//        GetSelectedNodesVec(nodes);
//        if (nodes.size() == 1)
//            m_InspectState = FindState(nodes[0]);
//
//        std::vector<ed::LinkId> links;
//        GetSelectedLinksVec(links);
//        if (links.size() == 1)
//            m_InspectTrans = FindTrans(links[0]);
//
//        if (m_InspectState)
//        {
//            char nbuf[128]{};
//            std::snprintf(nbuf, sizeof(nbuf), "%s", m_InspectState->name.c_str());
//
//            if (ImGui::InputText("Name", nbuf, sizeof(nbuf))) m_InspectState->name = nbuf;
//            DrawClipSelector("Clip", m_InspectState->clip);
//            ImGui::DragFloat("PlayRate", &m_InspectState->playRate, 0.01f, 0.1f, 3.0f);
//
//            if (ImGui::Button("Open Graph"))
//            {
//                m_OpenState = m_InspectState->id;
//                GetOrCreateGraph(m_OpenState);
//            }
//            ImGui::SameLine();
//            if (ImGui::Button("Delete State"))
//                DeleteState(m_InspectState->id);
//        }
//        else if (m_InspectTrans)
//        {
//            ImGui::DragFloat("BlendTime", &m_InspectTrans->blendTime, 0.01f, 0.0f, 5.0f);
//            ImGui::Checkbox("CanInterrupt", &m_InspectTrans->canInterrupt);
//
//            ImGui::Separator();
//            ImGui::TextUnformatted("Conditions (AND)");
//
//            if (ImGui::Button("+ Cond") && !m_Params.empty())
//            {
//                Cond c;
//                c.param = m_Params[0].name;
//                c.type = m_Params[0].type;
//                c.op = (c.type == ParamType::Bool) ? CmpOp::EQ : CmpOp::GT;
//                m_InspectTrans->conds.push_back(c);
//            }
//
//            for (int i = 0; i < (int)m_InspectTrans->conds.size(); ++i)
//            {
//                Cond& c = m_InspectTrans->conds[(size_t)i];
//                ImGui::PushID(i);
//
//                // Param 선택
//                const char* curName = c.param.c_str();
//                if (ImGui::BeginCombo("Param", curName))
//                {
//                    for (auto& p : m_Params)
//                    {
//                        bool sel = (p.name == c.param);
//                        if (ImGui::Selectable(p.name.c_str(), sel))
//                        {
//                            c.param = p.name;
//                            c.type = p.type;
//
//                            // 타입 바뀌면 op도 안전한 값으로
//                            if (c.type == ParamType::Trigger) c.op = CmpOp::IsSet;
//                            else if (c.type == ParamType::Bool) c.op = CmpOp::EQ;
//                            else c.op = CmpOp::GT;
//                        }
//                    }
//                    ImGui::EndCombo();
//                }
//
//                ImGui::SameLine();
//
//                // Op 선택 타입별로 제한을 둠
//                std::string opName = EnumName(c.op);
//                if (ImGui::BeginCombo("Op", opName.c_str()))
//                {
//                    // Trigger는 IsSet만 함
//                    if (c.type == ParamType::Trigger)
//                    {
//                        if (ImGui::Selectable("IsSet", c.op == CmpOp::IsSet))
//                            c.op = CmpOp::IsSet;
//                    }
//                    else if (c.type == ParamType::Bool)
//                    {
//                        if (ImGui::Selectable("==", c.op == CmpOp::EQ))  c.op = CmpOp::EQ;
//                        if (ImGui::Selectable("!=", c.op == CmpOp::NEQ)) c.op = CmpOp::NEQ;
//                    }
//                    else
//                    {
//                        if (ImGui::Selectable("==", c.op == CmpOp::EQ))  c.op = CmpOp::EQ;
//                        if (ImGui::Selectable("!=", c.op == CmpOp::NEQ)) c.op = CmpOp::NEQ;
//                        if (ImGui::Selectable(">",  c.op == CmpOp::GT))  c.op = CmpOp::GT;
//                        if (ImGui::Selectable("<",  c.op == CmpOp::LT))  c.op = CmpOp::LT;
//                        if (ImGui::Selectable(">=", c.op == CmpOp::GTE)) c.op = CmpOp::GTE;
//                        if (ImGui::Selectable("<=", c.op == CmpOp::LTE)) c.op = CmpOp::LTE;
//                    }
//                    ImGui::EndCombo();
//                }
//
//                // 값 Trigger는 없음. 이후에 설정
//                if (c.type != ParamType::Trigger)
//                {
//                    ImGui::SameLine();
//                    DrawCondValueUI(c);
//                }
//
//                ImGui::SameLine();
//                if (ImGui::SmallButton("X"))
//                {
//                    m_InspectTrans->conds.erase(m_InspectTrans->conds.begin() + i);
//                    ImGui::PopID();
//                    break;
//                }
//
//                ImGui::PopID();
//            }
//
//            ImGui::Separator();
//            if (ImGui::Button("Delete Transition"))
//                DeleteTrans(m_InspectTrans->id);
//        }
//        else
//        {
//            ImGui::TextUnformatted("Select State/Transition.");
//        }
//
//        ImGui::EndChild();
//    }
//
//    void AnimBlueprintEditor::DrawFsmGraph()
//    {
//        ImGui::BeginChild("FSM_Graph", ImVec2(0, 0), true);
//
//        ed::Begin("Anim FSM");
//
//        // 새 노드 배치 다음 프레임에 보이게 강제로 위치/포커스함
//        if (m_PendingPlaceFsm)
//        {
//            ImVec2 screenPos{};
//
//            if (m_PendingPlaceFsmUseMouse)
//            {
//                screenPos = ImGui::GetMousePos();
//            }
//            else
//            {
//                // 현재 child(=FSM_Graph) 중앙
//                ImVec2 wp = ImGui::GetWindowPos();
//                ImVec2 ws = ImGui::GetContentRegionAvail();
//                screenPos = ImVec2(wp.x + ws.x * 0.5f, wp.y + ws.y * 0.5f);
//            }
//
//            ed::SetNodePosition(m_PendingPlaceFsm, ed::ScreenToCanvas(screenPos));
//            ed::SelectNode(m_PendingPlaceFsm, false);
//            ed::NavigateToSelection(true);
//
//            m_PendingPlaceFsm = {};
//            m_PendingPlaceFsmUseMouse = false;
//        }
//
//        for (auto& s : m_States)
//        {
//            ed::BeginNode(s.id);
//
//            ImGui::TextUnformatted(s.name.c_str());
//            float w = std::max(
//                ImGui::CalcTextSize(s.name.c_str()).x,
//                ImGui::CalcTextSize((std::string("Clip: ") + s.clip).c_str()).x
//            );
//            NodeHr(w);
//
//            ed::BeginPin(s.in, ed::PinKind::Input);
//            ImGui::TextUnformatted("-> In");
//            ed::EndPin();
//
//            ed::BeginPin(s.out, ed::PinKind::Output);
//            ImGui::TextUnformatted("Out ->");
//            ed::EndPin();
//
//            ed::EndNode();
//        }
//
//        for (auto& t : m_Trans)
//            ed::Link(t.id, t.fromOut, t.toIn);
//
//        HandleCreateFsm();
//        HandleDeleteFsm();
//
//        //const bool openFsmMenu = ed::ShowBackgroundContextMenu();
//
//        //ed::End();
//
//        //ed::Suspend();
//        //if (openFsmMenu)
//        //{
//        //    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
//        //    ImGui::OpenPopup("fsm_bg");
//        //}
//        //if (ImGui::BeginPopup("fsm_bg"))
//        //{
//        //    if (ImGui::MenuItem("Add State"))
//        //    {
//        //        auto& s = AddState("State");
//        //        QueuePlaceFsm(s.id, true);
//        //    }
//        //    if (ImGui::MenuItem("Delete Selected")) DeleteSelectedFsm();
//        //    ImGui::EndPopup();
//        //}
//        //ed::Resume();
//
//        //// 더블클릭은 End 이후에 처리 (이 버전은 hovered API 없음)
//        //HandleDoubleClickOpenPostEnd();
//
//        //ImGui::EndChild();
//
//        // 우클릭 감지 (NodeEditor가 알려줌)
//        const bool openFsmMenu = ed::ShowBackgroundContextMenu();
//
//        // 팝업은 Begin~End 사이에서 Suspend/Resume로 감싸서 그린다
//        ed::Suspend();
//        {
//            if (openFsmMenu)
//            {
//                m_FsmPopupPos = ImGui::GetMousePos();
//                m_FsmPopupVp = ImGui::GetIO().MouseHoveredViewport; // 마우스가 있는 뷰포트
//                ImGui::OpenPopup("fsm_bg");
//            }
//
//            if (ImGui::BeginPopup("fsm_bg"))
//            {
//                // 팝업 위치/뷰포트는 "열린 프레임"에만 세팅해도 되는데
//                // ImGui가 이미 잡아준 pos가 틀어지는 케이스가 있어서, 그냥 항상 고정해도 안전합니다.
//                if (m_FsmPopupVp) ImGui::SetNextWindowViewport(m_FsmPopupVp);
//                ImGui::SetNextWindowPos(m_FsmPopupPos, ImGuiCond_Always);
//
//                if (ImGui::MenuItem("Add State"))
//                {
//                    auto& s = AddState("State");
//                    QueuePlaceFsm(s.id, true);
//                }
//                if (ImGui::MenuItem("Delete Selected"))
//                    DeleteSelectedFsm();
//
//                ImGui::EndPopup();
//            }
//        }
//        ed::Resume();
//
//        ed::End();
//
//        // 더블클릭 처리는 End() 이후 유지
//        HandleDoubleClickOpenPostEnd();
//
//        ImGui::EndChild();
//    }
//
//    // =========================================================
//    // BlendGraph
//    // =========================================================
//    AnimBlueprintEditor::BlendGraph& AnimBlueprintEditor::GetOrCreateGraph(ed::NodeId stateId)
//    {
//        uintptr_t k = Key(stateId);
//        auto it = m_Graphs.find(k);
//        if (it != m_Graphs.end()) return it->second;
//
//        BlendGraph g;
//
//        ed::Config cfg;
//        cfg.SettingsFile = "AnimBlend.layout";
//        g.ctx = ed::CreateEditor(&cfg);
//
//        // 기본: Clip 하나 + Output(포즈 결과)
//        AddClipNode(g);
//
//        m_Graphs.emplace(k, std::move(g));
//        return m_Graphs[k];
//    }
//
//    void AnimBlueprintEditor::DestroyAllGraphs()
//    {
//        for (auto& kv : m_Graphs)
//            if (kv.second.ctx) ed::DestroyEditor(kv.second.ctx);
//        m_Graphs.clear();
//    }
//
//    AnimBlueprintEditor::BNode& AnimBlueprintEditor::AddClipNode(BlendGraph& g)
//    {
//        BNode n;
//        n.id = ed::NodeId((uintptr_t)g.nextId++);
//        n.type = BlendNodeType::Clip;
//        n.name = "Clip";
//        n.clip = "Idle";
//        AddBPin(g, n, ed::PinKind::Output, "Pose");
//        g.nodes.push_back(std::move(n));
//        return g.nodes.back();
//    }
//
//    AnimBlueprintEditor::BNode& AnimBlueprintEditor::AddBlendNode(BlendGraph& g)
//    {
//        BNode n;
//        n.id = ed::NodeId((uintptr_t)g.nextId++);
//        n.type = BlendNodeType::Blend;
//        n.name = "Blend";
//        AddBPin(g, n, ed::PinKind::Input,  "A");
//        AddBPin(g, n, ed::PinKind::Input,  "B");
//        AddBPin(g, n, ed::PinKind::Output, "Out");
//        g.nodes.push_back(std::move(n));
//        return g.nodes.back();
//    }
//
//    void AnimBlueprintEditor::AddBPin(BlendGraph& g, BNode& n, ed::PinKind kind, const char* baseName)
//    {
//        BPin p;
//        p.id = ed::PinId((uintptr_t)g.nextId++);
//        p.kind = kind;
//
//        int idx = 1;
//        if (kind == ed::PinKind::Input)  idx = (int)n.inputs.size() + 1;
//        if (kind == ed::PinKind::Output) idx = (int)n.outputs.size() + 1;
//
//        p.name = std::string(baseName) + " " + std::to_string(idx);
//
//        if (kind == ed::PinKind::Input)  n.inputs.push_back(std::move(p));
//        else                             n.outputs.push_back(std::move(p));
//    }
//
//    void AnimBlueprintEditor::CleanupBLinksByPin(BlendGraph& g, ed::PinId pin)
//    {
//        for (int i = (int)g.links.size() - 1; i >= 0; --i)
//        {
//            auto& l = g.links[(size_t)i];
//            if (l.a == pin || l.b == pin) g.links.erase(g.links.begin() + i);
//        }
//    }
//
//    void AnimBlueprintEditor::RemoveLastBPin(BlendGraph& g, BNode& n, ed::PinKind kind)
//    {
//        if (kind == ed::PinKind::Input)
//        {
//            if (n.inputs.empty()) return;
//            CleanupBLinksByPin(g, n.inputs.back().id);
//            n.inputs.pop_back();
//        }
//        else
//        {
//            if (n.outputs.empty()) return;
//            CleanupBLinksByPin(g, n.outputs.back().id);
//            n.outputs.pop_back();
//        }
//    }
//
//    AnimBlueprintEditor::BNode* AnimBlueprintEditor::FindBNode(BlendGraph& g, ed::NodeId id)
//    {
//        for (auto& n : g.nodes) if (n.id == id) return &n;
//        return nullptr;
//    }
//
//    AnimBlueprintEditor::BNode* AnimBlueprintEditor::FindBNodeByPin(BlendGraph& g, ed::PinId pin)
//    {
//        for (auto& n : g.nodes)
//        {
//            for (auto& p : n.inputs)  if (p.id == pin) return &n;
//            for (auto& p : n.outputs) if (p.id == pin) return &n;
//        }
//        return nullptr;
//    }
//
//    AnimBlueprintEditor::BPin* AnimBlueprintEditor::FindBPin(BlendGraph& g, ed::PinId id)
//    {
//        for (auto& n : g.nodes)
//        {
//            for (auto& p : n.inputs)  if (p.id == id) return &p;
//            for (auto& p : n.outputs) if (p.id == id) return &p;
//        }
//        return nullptr;
//    }
//
//    bool AnimBlueprintEditor::CanCreateBLink(BlendGraph& g, ed::PinId a, ed::PinId b, ed::PinId& outStart, ed::PinId& outEnd) const
//    {
//        if (!a || !b || a == b) return false;
//
//        const BPin* pa = nullptr;
//        const BPin* pb = nullptr;
//
//        for (auto& n : g.nodes)
//        {
//            for (auto& p : n.inputs)  { if (p.id == a) pa = &p; if (p.id == b) pb = &p; }
//            for (auto& p : n.outputs) { if (p.id == a) pa = &p; if (p.id == b) pb = &p; }
//        }
//
//        if (!pa || !pb) return false;
//        if (pa->kind == pb->kind) return false;
//
//        if (pa->kind == ed::PinKind::Output) { outStart = a; outEnd = b; }
//        else                                  { outStart = b; outEnd = a; }
//
//        auto* na = const_cast<AnimBlueprintEditor*>(this)->FindBNodeByPin(g, outStart);
//        auto* nb = const_cast<AnimBlueprintEditor*>(this)->FindBNodeByPin(g, outEnd);
//        if (!na || !nb || na == nb) return false;
//
//        // 입력 핀 1개당 링크 1개
//        for (auto& l : g.links) if (l.b == outEnd) return false;
//
//        return true;
//    }
//
//    void AnimBlueprintEditor::DeleteBLink(BlendGraph& g, ed::LinkId id)
//    {
//        for (int i = (int)g.links.size() - 1; i >= 0; --i)
//            if (g.links[(size_t)i].id == id) { g.links.erase(g.links.begin() + i); return; }
//    }
//
//    void AnimBlueprintEditor::DeleteBNode(BlendGraph& g, ed::NodeId id)
//    {
//        BNode* n = FindBNode(g, id);
//        if (!n) return;
//
//        for (auto& p : n->inputs)  CleanupBLinksByPin(g, p.id);
//        for (auto& p : n->outputs) CleanupBLinksByPin(g, p.id);
//
//        for (int i = (int)g.nodes.size() - 1; i >= 0; --i)
//            if (g.nodes[(size_t)i].id == id)
//            {
//                if (g.inspect == &g.nodes[(size_t)i]) g.inspect = nullptr;
//                g.nodes.erase(g.nodes.begin() + i);
//                return;
//            }
//    }
//
//    void AnimBlueprintEditor::DeleteSelectedBlend(BlendGraph& g)
//    {
//        std::vector<ed::LinkId> links;
//        GetSelectedLinksVec(links);
//        for (auto id : links) DeleteBLink(g, id);
//
//        std::vector<ed::NodeId> nodes;
//        GetSelectedNodesVec(nodes);
//        for (auto id : nodes) DeleteBNode(g, id);
//    }
//
//    void AnimBlueprintEditor::HandleCreateBlend(BlendGraph& g)
//    {
//        if (!ed::BeginCreate()) return;
//
//        ed::PinId a{}, b{};
//        if (ed::QueryNewLink(&a, &b))
//        {
//            ed::PinId start{}, end{};
//            if (CanCreateBLink(g, a, b, start, end))
//            {
//                if (ed::AcceptNewItem())
//                {
//                    BLink l;
//                    l.id = ed::LinkId((uintptr_t)g.nextId++);
//                    l.a = start;
//                    l.b = end;
//                    g.links.push_back(std::move(l));
//                }
//            }
//            else ed::RejectNewItem();
//        }
//
//        ed::EndCreate();
//    }
//
//    void AnimBlueprintEditor::HandleDeleteBlend(BlendGraph& g)
//    {
//        if (!ed::BeginDelete()) return;
//
//        ed::LinkId lid{};
//        while (ed::QueryDeletedLink(&lid))
//            if (ed::AcceptDeletedItem()) DeleteBLink(g, lid);
//
//        ed::NodeId nid{};
//        while (ed::QueryDeletedNode(&nid))
//            if (ed::AcceptDeletedItem()) DeleteBNode(g, nid);
//
//        ed::EndDelete();
//    }
//
//    void AnimBlueprintEditor::DrawBlendInspector(BlendGraph& g)
//    {
//        ImGui::BeginChild("Blend_Inspector", ImVec2(0, 180), true);
//        ImGui::TextUnformatted("BlendGraph Inspector");
//        ImGui::Separator();
//
//        std::vector<ed::NodeId> nodes;
//        GetSelectedNodesVec(nodes);
//
//        if (nodes.size() == 1) g.inspect = FindBNode(g, nodes[0]);
//        else if (nodes.empty()) g.inspect = nullptr;
//
//        if (!g.inspect)
//        {
//            ImGui::TextUnformatted("Select a node.");
//            ImGui::EndChild();
//            return;
//        }
//
//        ImGui::Text("Node: %s", g.inspect->name.c_str());
//        if (g.inspect->type == BlendNodeType::Clip)
//        {
//            DrawClipSelector("Clip", g.inspect->clip);
//        }
//
//        ImGui::Separator();
//        if (ImGui::Button("+ In"))  AddBPin(g, *g.inspect, ed::PinKind::Input, "In");
//        ImGui::SameLine();
//        if (ImGui::Button("- In"))  RemoveLastBPin(g, *g.inspect, ed::PinKind::Input);
//
//        if (ImGui::Button("+ Out")) AddBPin(g, *g.inspect, ed::PinKind::Output, "Out");
//        ImGui::SameLine();
//        if (ImGui::Button("- Out")) RemoveLastBPin(g, *g.inspect, ed::PinKind::Output);
//
//        ImGui::EndChild();
//    }
//
//    void AnimBlueprintEditor::DrawBlendGraph(BlendGraph& g)
//    {
//        ImGui::BeginChild("Blend_Graph", ImVec2(0, 0), true);
//
//        ed::Begin("Anim Blend Graph");
//
//        for (auto& n : g.nodes)
//        {
//            ed::BeginNode(n.id);
//            ImGui::TextUnformatted(n.name.c_str());
//            //ImGui::Separator();
//            float w = std::max(
//                ImGui::CalcTextSize(n.name.c_str()).x,
//                ImGui::CalcTextSize((std::string("Clip: ") + n.clip).c_str()).x
//            );
//            NodeHr(w);
//
//            if (n.type == BlendNodeType::Clip)
//                ImGui::Text("Clip: %s", n.clip.c_str());
//
//            for (auto& p : n.inputs)
//            {
//                ed::BeginPin(p.id, ed::PinKind::Input);
//                ImGui::Text("-> %s", p.name.c_str());
//                ed::EndPin();
//            }
//
//            for (auto& p : n.outputs)
//            {
//                ed::BeginPin(p.id, ed::PinKind::Output);
//                ImGui::Text("%s ->", p.name.c_str());
//                ed::EndPin();
//            }
//
//            ed::EndNode();
//        }
//
//        for (auto& l : g.links)
//            ed::Link(l.id, l.a, l.b);
//
//        HandleCreateBlend(g);
//        HandleDeleteBlend(g);
//
//        /*const bool openBlendMenu = ed::ShowBackgroundContextMenu();
//
//        ed::End();
//
//        ed::Suspend();
//        if (openBlendMenu)
//        {
//            ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
//            ImGui::OpenPopup("blend_bg");
//        }
//        if (ImGui::BeginPopup("blend_bg"))
//        {
//            if (ImGui::MenuItem("Add Clip"))  AddClipNode(g);
//            if (ImGui::MenuItem("Add Blend")) AddBlendNode(g);
//            if (ImGui::MenuItem("Delete Selected")) DeleteSelectedBlend(g);
//            ImGui::EndPopup();
//        }
//        ed::Resume();*/
//        //ImGui::EndChild();
//
//        const bool openBlendMenu = ed::ShowBackgroundContextMenu();
//        if (openBlendMenu)
//        {
//            m_BlendPopupPos = ImGui::GetMousePos();
//            ImGui::OpenPopup("blend_bg");
//        }
//
//        ed::Suspend();
//        {
//            if (openBlendMenu)
//            {
//                m_BlendPopupPos = ImGui::GetMousePos();
//                m_BlendPopupVp = ImGui::GetIO().MouseHoveredViewport;
//                ImGui::OpenPopup("blend_bg");
//            }
//
//            if (ImGui::BeginPopup("blend_bg"))
//            {
//                if (m_BlendPopupVp) ImGui::SetNextWindowViewport(m_BlendPopupVp);
//                ImGui::SetNextWindowPos(m_BlendPopupPos, ImGuiCond_Always);
//
//                if (ImGui::MenuItem("Add Clip"))  AddClipNode(g);
//                if (ImGui::MenuItem("Add Blend")) AddBlendNode(g);
//                if (ImGui::MenuItem("Delete Selected")) DeleteSelectedBlend(g);
//
//                ImGui::EndPopup();
//            }
//        }
//        ed::Resume();
//        ed::End();
//
//        ImGui::EndChild();
//    }
//
//    // =========================================================
//    // Save / Load
//    // =========================================================
//    static json JVec2(const ImVec2& v) { return json::array({ v.x, v.y }); }
//    static ImVec2 V2(const json& a)    { return ImVec2(a[0].get<float>(), a[1].get<float>()); }
//
//    void AnimBlueprintEditor::SaveJson(const char* path)
//    {
//        json j;
//
//        // ---------------- Params ----------------
//        for (auto& p : m_Params)
//        {
//            json jp;
//            jp["name"] = p.name;
//            jp["type"] = EnumName(p.type);
//            jp["b"] = p.b;
//            jp["i"] = p.i;
//            jp["f"] = p.f;
//            jp["trigger"] = p.trigger;
//            j["params"].push_back(jp);
//        }
//
//        // ---------------- FSM ----------------
//        ed::SetCurrentEditor(m_FsmCtx);
//        for (auto& s : m_States)
//        {
//            json js;
//            js["id"] = ToU64(s.id);
//            js["pinIn"] = ToU64(s.in);
//            js["pinOut"] = ToU64(s.out);
//            js["name"] = s.name;
//            js["clip"] = s.clip;
//            js["playRate"] = s.playRate;
//
//            ImVec2 pos = ed::GetNodePosition(s.id);
//            if (pos.x != FLT_MAX && pos.y != FLT_MAX)
//                js["pos"] = JVec2(pos);
//            j["fsm"]["states"].push_back(js);
//        }
//
//        for (auto& t : m_Trans)
//        {
//            json jt;
//            jt["id"] = ToU64(t.id);
//            jt["fromOut"] = ToU64(t.fromOut);
//            jt["toIn"] = ToU64(t.toIn);
//            jt["blendTime"] = t.blendTime;
//            jt["canInterrupt"] = t.canInterrupt;
//
//            for (auto& c : t.conds)
//            {
//                json jc;
//                jc["param"] = c.param;
//                jc["type"] = EnumName(c.type);
//                jc["op"] = EnumName(c.op);
//                jc["b"] = c.b;
//                jc["i"] = c.i;
//                jc["f"] = c.f;
//                jt["conds"].push_back(jc);
//            }
//
//            j["fsm"]["trans"].push_back(jt);
//        }
//
//        j["fsm"]["openState"] = m_OpenState ? ToU64(m_OpenState) : 0;
//        ed::SetCurrentEditor(nullptr);
//
//        // ---------------- Target Mesh ----------------
//        if (m_TargetMesh[0] != '\0')
//            j["targetMesh"] = std::string(m_TargetMesh);
//
//        // ---------------- BlendGraphs ----------------
//        for (auto& kv : m_Graphs)
//        {
//            uint64_t stateId = (uint64_t)kv.first;
//            BlendGraph& g = kv.second;
//
//            json gg;
//            gg["stateId"] = stateId;
//            gg["nextId"] = g.nextId;
//
//            ed::SetCurrentEditor(g.ctx);
//            for (auto& n : g.nodes)
//            {
//                json jn;
//                jn["id"] = ToU64(n.id);
//                jn["type"] = (n.type == BlendNodeType::Clip) ? "Clip" : "Blend";
//                jn["name"] = n.name;
//                jn["clip"] = n.clip;
//
//                ImVec2 pos = ed::GetNodePosition(n.id);
//                if (pos.x != FLT_MAX && pos.y != FLT_MAX)
//                    jn["pos"] = JVec2(pos);
//
//                for (auto& p : n.inputs)
//                    jn["inputs"].push_back({ {"id", ToU64(p.id)}, {"name", p.name} });
//
//                for (auto& p : n.outputs)
//                    jn["outputs"].push_back({ {"id", ToU64(p.id)}, {"name", p.name} });
//
//                gg["nodes"].push_back(jn);
//            }
//
//            for (auto& l : g.links)
//                gg["links"].push_back({ {"id", ToU64(l.id)}, {"a", ToU64(l.a)}, {"b", ToU64(l.b)} });
//
//            ed::SetCurrentEditor(nullptr);
//
//            j["blendGraphs"].push_back(gg);
//        }
//
//        std::filesystem::path filename = path;
//        filename.replace_extension(".json");
//        std::filesystem::path logicalOrRelative = std::filesystem::path("Assets/Anim") / filename;
//        std::filesystem::path result = ResourceManager::Get().Resolve(logicalOrRelative);
//
//        if (auto parent = result.parent_path(); !parent.empty())
//            std::filesystem::create_directories(parent);
//
//        std::ofstream ofs(result);
//        if (ofs.is_open()) ofs << j.dump(2);
//    }
//
//    bool AnimBlueprintEditor::LoadJson(const char* path)
//    {
//        std::filesystem::path filename = path;
//        filename.replace_extension(".json");
//        std::filesystem::path logicalOrRelative = std::filesystem::path("Assets/Anim") / filename;
//        std::filesystem::path result = ResourceManager::Get().Resolve(logicalOrRelative);
//
//        std::ifstream ifs(result);
//        if (!ifs.is_open()) return false;
//
//        json j;
//        ifs >> j;
//
//        // 컨텍스트/데이터 전부 리셋(깔끔)
//        DestroyAllGraphs();
//        if (m_FsmCtx) { ed::DestroyEditor(m_FsmCtx); m_FsmCtx = nullptr; }
//
//        m_Params.clear();
//        m_States.clear();
//        m_Trans.clear();
//        m_InspectState = nullptr;
//        m_InspectTrans = nullptr;
//        m_OpenState = {};
//        m_NextId = 1;
//        m_TargetMesh[0] = '\0';
//        m_TargetClips.clear();
//
//        // FSM ctx 재생성
//        ed::Config cfg;
//        cfg.SettingsFile = "AnimFSM.layout";
//        m_FsmCtx = ed::CreateEditor(&cfg);
//
//        // ---------------- Params ----------------
//        if (j.contains("params"))
//        {
//            for (auto& jp : j["params"])
//            {
//                AnimParam p;
//                p.name = jp.value("name", "Param");
//                p.type = EnumFromName<ParamType>(jp.value("type", "Float"), ParamType::Float);
//                p.b = jp.value("b", false);
//                p.i = jp.value("i", 0);
//                p.f = jp.value("f", 0.0f);
//                p.trigger = jp.value("trigger", false);
//                m_Params.push_back(p);
//            }
//        }
//
//        if (j.contains("targetMesh"))
//        {
//            std::string tm = j.value("targetMesh", std::string{});
//            std::snprintf(m_TargetMesh, sizeof(m_TargetMesh), "%s", tm.c_str());
//            LoadTargetClips();
//        }
//
//        // ---------------- FSM states ----------------
//        ed::SetCurrentEditor(m_FsmCtx);
//
//        if (j.contains("fsm") && j["fsm"].contains("states"))
//        {
//            for (auto& js : j["fsm"]["states"])
//            {
//                State s;
//                s.id  = NodeFromU64(js["id"].get<uint64_t>());
//                s.in  = PinFromU64(js["pinIn"].get<uint64_t>());
//                s.out = PinFromU64(js["pinOut"].get<uint64_t>());
//                s.name = js.value("name", "State");
//                s.clip = js.value("clip", "Idle");
//                s.playRate = js.value("playRate", 1.0f);
//
//                m_States.push_back(s);
//
//                // 위치 복원
//                if (js.contains("pos")) ed::SetNodePosition(s.id, V2(js["pos"]));
//
//                // 다음 ID 갱신(충돌 방지)
//                m_NextId = std::max(m_NextId, (int)ToU64(s.id) + 1);
//                m_NextId = std::max(m_NextId, (int)ToU64(s.in) + 1);
//                m_NextId = std::max(m_NextId, (int)ToU64(s.out) + 1);
//            }
//        }
//
//        // ---------------- FSM transitions ----------------
//        if (j.contains("fsm") && j["fsm"].contains("trans"))
//        {
//            for (auto& jt : j["fsm"]["trans"])
//            {
//                Transition t;
//                t.id = LinkFromU64(jt["id"].get<uint64_t>());
//                t.fromOut = PinFromU64(jt["fromOut"].get<uint64_t>());
//                t.toIn = PinFromU64(jt["toIn"].get<uint64_t>());
//                t.blendTime = jt.value("blendTime", 0.2f);
//                t.canInterrupt = jt.value("canInterrupt", true);
//
//                if (jt.contains("conds"))
//                {
//                    for (auto& jc : jt["conds"])
//                    {
//                        Cond c;
//                        c.param = jc.value("param", "");
//                        c.type = EnumFromName<ParamType>(jc.value("type", "Float"), ParamType::Float);
//                        c.op = EnumFromName<CmpOp>(jc.value("op", ">"), CmpOp::GT);
//                        c.b = jc.value("b", false);
//                        c.i = jc.value("i", 0);
//                        c.f = jc.value("f", 0.0f);
//                        t.conds.push_back(c);
//                    }
//                }
//
//                m_Trans.push_back(t);
//                m_NextId = std::max(m_NextId, (int)ToU64(t.id) + 1);
//            }
//        }
//
//        uint64_t open = 0;
//        if (j.contains("fsm")) open = j["fsm"].value("openState", 0ULL);
//        m_OpenState = open ? NodeFromU64(open) : ed::NodeId();
//
//        ed::SetCurrentEditor(nullptr);
//
//        // ---------------- BlendGraphs ----------------
//        if (j.contains("blendGraphs"))
//        {
//            for (auto& gg : j["blendGraphs"])
//            {
//                uint64_t sid = gg["stateId"].get<uint64_t>();
//                ed::NodeId stateId = NodeFromU64(sid);
//
//                BlendGraph g;
//                ed::Config gcfg;
//                gcfg.SettingsFile = "AnimBlend.layout";
//                g.ctx = ed::CreateEditor(&gcfg);
//
//                g.nextId = gg.value("nextId", 1);
//
//                ed::SetCurrentEditor(g.ctx);
//
//                if (gg.contains("nodes"))
//                {
//                    for (auto& jn : gg["nodes"])
//                    {
//                        BNode n;
//                        n.id = NodeFromU64(jn["id"].get<uint64_t>());
//                        n.type = (jn.value("type", "Clip") == "Blend") ? BlendNodeType::Blend : BlendNodeType::Clip;
//                        n.name = jn.value("name", "Node");
//                        n.clip = jn.value("clip", "Idle");
//
//                        if (jn.contains("inputs"))
//                            for (auto& pi : jn["inputs"])
//                                n.inputs.push_back({ PinFromU64(pi["id"].get<uint64_t>()), ed::PinKind::Input, pi.value("name", "In") });
//
//                        if (jn.contains("outputs"))
//                            for (auto& po : jn["outputs"])
//                                n.outputs.push_back({ PinFromU64(po["id"].get<uint64_t>()), ed::PinKind::Output, po.value("name", "Out") });
//
//                        g.nodes.push_back(std::move(n));
//
//                        if (jn.contains("pos"))
//                            ed::SetNodePosition(g.nodes.back().id, V2(jn["pos"]));
//                    }
//                }
//
//                if (gg.contains("links"))
//                {
//                    for (auto& jl : gg["links"])
//                    {
//                        BLink l;
//                        l.id = LinkFromU64(jl["id"].get<uint64_t>());
//                        l.a = PinFromU64(jl["a"].get<uint64_t>());
//                        l.b = PinFromU64(jl["b"].get<uint64_t>());
//                        g.links.push_back(l);
//                    }
//                }
//
//                ed::SetCurrentEditor(nullptr);
//
//                m_Graphs.emplace(Key(stateId), std::move(g));
//            }
//        }
//
//        return true;
//    }
//
//    // =========================================================
//    // Toolbar / Draw
//    // =========================================================
//    void AnimBlueprintEditor::DrawToolbar()
//    {
//        ImGui::SetNextItemWidth(240);
//        ImGui::InputText("File", m_File, sizeof(m_File));
//
//        ImGui::SameLine();
//        if (ImGui::Button("Save")) SaveJson(m_File);
//
//        ImGui::SameLine();
//        if (ImGui::Button("Load")) LoadJson(m_File);
//
//        ImGui::SameLine();
//        if (ImGui::Button("Add State"))
//        {
//            auto& s = AddState("State");
//            QueuePlaceFsm(s.id, false);
//        }
//
//        DrawTargetMeshToolbar();
//    }
//
//    void AnimBlueprintEditor::DrawTargetMeshToolbar()
//    {
//        ImGui::Separator();
//        ImGui::SetNextItemWidth(320);
//        ImGui::InputText("Target Mesh", m_TargetMesh, sizeof(m_TargetMesh));
//
//        ImGui::SameLine();
//        if (ImGui::Button("Load Clips"))
//        {
//            if (!LoadTargetClips())
//            {
//                ALICE_LOG_WARN("AnimBlueprint: Failed to load clips for Target Mesh.");
//            }
//        }
//
//        if (!m_TargetClips.empty())
//        {
//            ImGui::SameLine();
//            ImGui::Text("Clips: %d", (int)m_TargetClips.size());
//        }
//    }
//
//    void AnimBlueprintEditor::DrawClipSelector(const char* label, std::string& value)
//    {
//        if (m_TargetClips.empty())
//        {
//            char buf[128] = {};
//            std::snprintf(buf, sizeof(buf), "%s", value.c_str());
//            if (ImGui::InputText(label, buf, sizeof(buf)))
//                value = buf;
//            return;
//        }
//
//        int current = -1;
//        for (int i = 0; i < (int)m_TargetClips.size(); ++i)
//        {
//            if (m_TargetClips[(size_t)i] == value)
//            {
//                current = i;
//                break;
//            }
//        }
//
//        if (ImGui::BeginCombo(label, (current >= 0) ? m_TargetClips[(size_t)current].c_str() : value.c_str()))
//        {
//            for (int i = 0; i < (int)m_TargetClips.size(); ++i)
//            {
//                bool selected = (i == current);
//                if (ImGui::Selectable(m_TargetClips[(size_t)i].c_str(), selected))
//                    value = m_TargetClips[(size_t)i];
//                if (selected) ImGui::SetItemDefaultFocus();
//            }
//            ImGui::EndCombo();
//        }
//    }
//
//    bool AnimBlueprintEditor::LoadTargetClips()
//    {
//        if (!m_resources || !m_device)
//            return false;
//        if (m_TargetMesh[0] == '\0')
//            return false;
//
//        const std::filesystem::path logicalPath = m_TargetMesh;
//        const std::filesystem::path resolved = m_resources->Resolve(logicalPath);
//
//        FbxModel model;
//        bool ok = false;
//
//        if (resolved.extension() == ".alice")
//        {
//            auto sp = m_resources->LoadSharedBinaryAuto(logicalPath);
//            if (!sp) return false;
//            ok = model.LoadFromMemory(m_device, sp->data(), sp->size(),
//                                      logicalPath.filename().string(),
//                                      L"");
//        }
//        else
//        {
//            ok = model.Load(m_device, resolved.wstring());
//        }
//
//        if (!ok) return false;
//
//        m_TargetClips = model.GetAnimationNames();
//        return !m_TargetClips.empty();
//    }
//
//    void AnimBlueprintEditor::Draw(bool* pOpen)
//    {
//        if (!m_FsmCtx) return;
//
//        if (!ImGui::Begin("Animation Blueprint (FSM + BlendGraph)", pOpen))
//        {
//            ImGui::End();
//            return;
//        }
//
//        DrawToolbar();
//        ImGui::Separator();
//
//        // 좌/우 분할
//        float leftW = 560.0f;
//
//        // ---------------- LEFT ----------------
//        ImGui::BeginChild("Left", ImVec2(leftW, 0), true);
//
//        DrawParamsPanel();
//
//        ed::SetCurrentEditor(m_FsmCtx);
//        DrawFsmInspector();
//        DrawFsmGraph();
//        ed::SetCurrentEditor(nullptr);
//
//        ImGui::EndChild();
//
//        ImGui::SameLine();
//
//        // ---------------- RIGHT ----------------
//        ImGui::BeginChild("Right", ImVec2(0, 0), true);
//
//        ImGui::TextUnformatted("Blend Graph (double-click State to open)");
//        ImGui::Separator();
//
//        if (!m_OpenState)
//        {
//            ImGui::TextUnformatted("No State opened.");
//            ImGui::EndChild();
//            ImGui::End();
//            return;
//        }
//
//        BlendGraph& g = GetOrCreateGraph(m_OpenState);
//
//        ed::SetCurrentEditor(g.ctx);
//        DrawBlendInspector(g);
//        DrawBlendGraph(g);
//        ed::SetCurrentEditor(nullptr);
//
//        ImGui::EndChild();
//
//        ImGui::End();
//    }
//}
