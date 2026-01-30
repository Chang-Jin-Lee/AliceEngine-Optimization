//#pragma once
//#include <string>
//#include <vector>
//#include <unordered_map>
//#include <cstdint>
//
//#include <imgui.h>
//#include "imgui_node_editor.h"
//
//struct ID3D11Device;
//namespace Alice
//{
//   class ResourceManager;
//   namespace ed = ax::NodeEditor;
//   class AnimBlueprintEditor
//   {
//   public:
//       void Init();
//       void Shutdown();
//       void Draw(bool* pOpen = nullptr);
//
//       void SetResourceManager(class ResourceManager* resources) { m_resources = resources; }
//       void SetDevice(struct ID3D11Device* device) { m_device = device; }
//
//       // =========================================================
//       // [Params] (RTTR 기반)
//       // =========================================================
//       enum class ParamType { Bool, Int, Float, Trigger };
//       enum class CmpOp { EQ, NEQ, GT, LT, GTE, LTE, IsSet }; // Trigger는 IsSet만 사용
//   private:
//
//       struct AnimParam
//       {
//           std::string name = "Param";
//           ParamType type = ParamType::Float;
//
//           bool  b = false;
//           int   i = 0;
//           float f = 0.0f;
//
//           // Trigger는 누른 상태를 저장 (에디터 테스트용임)
//           bool trigger = false;
//       };
//
//       struct Cond
//       {
//           std::string param;      // 파라미터 이름 키
//           ParamType type = ParamType::Float;
//           CmpOp op = CmpOp::GT;
//
//           bool  b = false;
//           int   i = 0;
//           float f = 0.0f;
//       };
//
//       // =========================================================
//       // [FSM]
//       // =========================================================
//       struct State
//       {
//           ed::NodeId id{};
//           ed::PinId  in{};
//           ed::PinId  out{};
//
//           std::string name = "State";
//           std::string clip = "Idle";
//           float playRate = 1.0f;
//       };
//
//       struct Transition
//       {
//           ed::LinkId id{};
//           ed::PinId fromOut{};
//           ed::PinId toIn{};
//
//           float blendTime = 0.2f;
//           bool canInterrupt = true;
//
//           // 조건은 AND 모두 만족해야지 가능
//           std::vector<Cond> conds;
//       };
//
//       // =========================================================
//       // [BlendGraph] 상태별
//       // =========================================================
//       enum class BlendNodeType { Clip, Blend };
//
//       struct BPin
//       {
//           ed::PinId id{};
//           ed::PinKind kind{};
//           std::string name;
//       };
//
//       struct BNode
//       {
//           ed::NodeId id{};
//           BlendNodeType type = BlendNodeType::Clip;
//           std::string name = "Node";
//           std::string clip = "Idle";
//
//           std::vector<BPin> inputs;
//           std::vector<BPin> outputs;
//       };
//
//       struct BLink
//       {
//           ed::LinkId id{};
//           ed::PinId a{};
//           ed::PinId b{};
//       };
//
//       struct BlendGraph
//       {
//           ed::EditorContext* ctx = nullptr;
//           int nextId = 1;
//
//           BNode* inspect = nullptr;
//           std::vector<BNode> nodes;
//           std::vector<BLink> links;
//       };
//
//   private:
//       // ---------------------------------------------------------
//       // Context
//       // ---------------------------------------------------------
//       ed::EditorContext* m_FsmCtx = nullptr;
//       std::unordered_map<uintptr_t, BlendGraph> m_Graphs;
//
//       // ---------------------------------------------------------
//       // Data
//       // ---------------------------------------------------------
//       int m_NextId = 1;
//       std::vector<AnimParam> m_Params;
//       std::vector<State> m_States;
//       std::vector<Transition> m_Trans;
//
//       ed::NodeId m_OpenState{}; // 오른쪽 그래프 대상. 더블클릭/버튼으로 설정
//
//       // ---------------------------------------------------------
//       // UI State
//       // ---------------------------------------------------------
//       State* m_InspectState = nullptr;
//       Transition* m_InspectTrans = nullptr;
//
//       char m_File[260] = "AnimationBlueprintJsonFile";
//       char m_TargetMesh[260] = "";
//       std::vector<std::string> m_TargetClips;
//
//       ResourceManager* m_resources = nullptr;
//       ID3D11Device* m_device = nullptr;
//
//   private:
//       // =========================================================
//       // IDs / helpers 
//       // =========================================================
//       int NewId();
//
//       static uint64_t ToU64(ed::NodeId id);
//       static uint64_t ToU64(ed::PinId id);
//       static uint64_t ToU64(ed::LinkId id);
//
//       static ed::NodeId NodeFromU64(uint64_t v);
//       static ed::PinId  PinFromU64(uint64_t v);
//       static ed::LinkId LinkFromU64(uint64_t v);
//
//       uintptr_t Key(ed::NodeId id) const;
//
//       // =========================================================
//       // Params UI
//       // =========================================================
//       AnimParam* FindParam(const std::string& name);
//       void DrawParamsPanel();
//       void DrawParamValueUI(AnimParam& p);
//       void DrawCondValueUI(Cond& c);
//
//       // =========================================================
//       // FSM
//       // =========================================================
//       State& AddState(const char* name);
//       State* FindState(ed::NodeId id);
//       State* FindStateByPin(ed::PinId pin);
//       Transition* FindTrans(ed::LinkId id);
//
//       void DeleteState(ed::NodeId id);
//       void DeleteTrans(ed::LinkId id);
//       void DeleteSelectedFsm();
//
//       bool CanCreateTrans(ed::PinId a, ed::PinId b, ed::PinId& outFrom, ed::PinId& outTo) const;
//
//       void DrawFsmInspector();
//       void DrawFsmGraph();
//       void HandleCreateFsm();
//       void HandleDeleteFsm();
//       // 더블클릭은 ed::End() 이후에 GetDoubleClickedNode()가 갱신되므로 Post-End로 처리
//       void HandleDoubleClickOpenPostEnd();
//
//       // =========================================================
//       // BlendGraph
//       // =========================================================
//       BlendGraph& GetOrCreateGraph(ed::NodeId stateId);
//       void DestroyAllGraphs();
//
//       BNode& AddClipNode(BlendGraph& g);
//       BNode& AddBlendNode(BlendGraph& g);
//
//       void AddBPin(BlendGraph& g, BNode& n, ed::PinKind kind, const char* baseName);
//       void RemoveLastBPin(BlendGraph& g, BNode& n, ed::PinKind kind);
//       void CleanupBLinksByPin(BlendGraph& g, ed::PinId pin);
//
//       BNode* FindBNode(BlendGraph& g, ed::NodeId id);
//       BNode* FindBNodeByPin(BlendGraph& g, ed::PinId pin);
//       BPin*  FindBPin(BlendGraph& g, ed::PinId id);
//
//       bool CanCreateBLink(BlendGraph& g, ed::PinId a, ed::PinId b, ed::PinId& outStart, ed::PinId& outEnd) const;
//
//       void DeleteBNode(BlendGraph& g, ed::NodeId id);
//       void DeleteBLink(BlendGraph& g, ed::LinkId id);
//       void DeleteSelectedBlend(BlendGraph& g);
//
//       void HandleCreateBlend(BlendGraph& g);
//       void HandleDeleteBlend(BlendGraph& g);
//
//       void DrawBlendInspector(BlendGraph& g);
//       void DrawBlendGraph(BlendGraph& g);
//
//       // =========================================================
//       // Save / Load
//       // =========================================================
//       void SaveJson(const char* path);
//       bool LoadJson(const char* path);
//
//       // UI
//       void DrawToolbar();
//       void DrawTargetMeshToolbar();
//       void DrawClipSelector(const char* label, std::string& value);
//
//       bool LoadTargetClips();
//
//       // Selection helpers (이전 NodeEditor 버전 호환)
//       static void GetSelectedNodesVec(std::vector<ed::NodeId>& out);
//       static void GetSelectedLinksVec(std::vector<ed::LinkId>& out);
//
//       // Node placement
//       void QueuePlaceFsm(ed::NodeId id, bool useMouse);
//
//   private:
//       // Pending node placement
//       ed::NodeId m_PendingPlaceFsm{};
//       bool m_PendingPlaceFsmUseMouse = false;
//
//       ImVec2 m_FsmPopupPos = ImVec2(0, 0);
//       ImVec2 m_BlendPopupPos = ImVec2(0, 0);
//       ImGuiID m_FsmPopupVp = 0;
//       ImGuiID m_BlendPopupVp = 0;
//   };
//}
//
//
//
