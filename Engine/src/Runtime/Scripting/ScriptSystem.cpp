#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Runtime/Scripting/ScriptSystem.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Scripting/Components/ScriptComponent.h"
#include "Runtime/Input/InputSystem.h"
#include "Runtime/Resources/Scene.h"
#include "Runtime/Resources/SceneFile.h"
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Foundation/Logger.h"

namespace Alice
{
    // === ScriptSystem 구현 ===

    void ScriptSystem::SetServices(InputSystem* input,
                                   SceneManager* scenes,
                                   ResourceManager* resources,
                                   SkinnedMeshRegistry* skinnedRegistry)
    {
        m_input = input;
        m_scenes = scenes;
        m_resources = resources;
        m_skinnedRegistry = skinnedRegistry;

        m_services.input = this;
        m_services.scene = this;
        m_services.skinnedRegistry = m_skinnedRegistry;
        m_services.resources = m_resources;
    }

    void ScriptSystem::BeginInputFrame()
    {
        // 키보드 상태 갱신
        m_prevKeys = m_currKeys;
        m_currKeys.fill(false);

        // 마우스 버튼 상태 갱신
        m_prevMouseButtons = m_currMouseButtons;
        m_currMouseButtons.fill(false);

        if (!m_input)
            return;

        // 키보드 상태 읽기
        for (std::size_t i = 0; i < m_currKeys.size(); ++i)
        {
            const auto key = static_cast<KeyCode>(i);
            const auto dx = ToDxKey(key);
            if (dx == DirectX::Keyboard::Keys::None)
                continue;
            m_currKeys[i] = m_input->IsKeyDown(dx);
        }

        // 마우스 버튼 상태 읽기
        for (std::size_t i = 0; i < m_currMouseButtons.size(); ++i)
        {
            m_currMouseButtons[i] = m_input->IsMouseButtonDown(static_cast<int>(i));
        }
    }

    DirectX::Keyboard::Keys ScriptSystem::ToDxKey(KeyCode k)
    {
        using K = DirectX::Keyboard::Keys;
        switch (k)
        {
        case KeyCode::Alpha0: return K::D0;
        case KeyCode::Alpha1: return K::D1;
        case KeyCode::Alpha2: return K::D2;
        case KeyCode::Alpha3: return K::D3;
        case KeyCode::Alpha4: return K::D4;
        case KeyCode::Alpha5: return K::D5;
        case KeyCode::Alpha6: return K::D6;
        case KeyCode::Alpha7: return K::D7;
        case KeyCode::Alpha8: return K::D8;
        case KeyCode::Alpha9: return K::D9;

        case KeyCode::A: return K::A; case KeyCode::B: return K::B; case KeyCode::C: return K::C;
        case KeyCode::D: return K::D; case KeyCode::E: return K::E; case KeyCode::F: return K::F;
        case KeyCode::G: return K::G; case KeyCode::H: return K::H; case KeyCode::I: return K::I;
        case KeyCode::J: return K::J; case KeyCode::K: return K::K; case KeyCode::L: return K::L;
        case KeyCode::M: return K::M; case KeyCode::N: return K::N; case KeyCode::O: return K::O;
        case KeyCode::P: return K::P; case KeyCode::Q: return K::Q; case KeyCode::R: return K::R;
        case KeyCode::S: return K::S; case KeyCode::T: return K::T; case KeyCode::U: return K::U;
        case KeyCode::V: return K::V; case KeyCode::W: return K::W; case KeyCode::X: return K::X;
        case KeyCode::Y: return K::Y; case KeyCode::Z: return K::Z;

        case KeyCode::Up: return K::Up;
        case KeyCode::Down: return K::Down;
        case KeyCode::Left: return K::Left;
        case KeyCode::Right: return K::Right;

        case KeyCode::Space: return K::Space;
        case KeyCode::Enter: return K::Enter;
        case KeyCode::Escape: return K::Escape;
        case KeyCode::Tab: return K::Tab;
        case KeyCode::Backspace: return K::Back;

        case KeyCode::LeftShift: return K::LeftShift;
        case KeyCode::RightShift: return K::RightShift;
        case KeyCode::LeftCtrl: return K::LeftControl;
        case KeyCode::RightCtrl: return K::RightControl;
        case KeyCode::LeftAlt: return K::LeftAlt;
        case KeyCode::RightAlt: return K::RightAlt;

        case KeyCode::F1: return K::F1;
        case KeyCode::F2: return K::F2;
        case KeyCode::F3: return K::F3;
        case KeyCode::F4: return K::F4;
        case KeyCode::F5: return K::F5;
        case KeyCode::F6: return K::F6;
        case KeyCode::F7: return K::F7;
        case KeyCode::F8: return K::F8;
        case KeyCode::F9: return K::F9;
        case KeyCode::F10: return K::F10;
        case KeyCode::F11: return K::F11;
        case KeyCode::F12: return K::F12;

        default: return K::None;
        }
    }

    bool ScriptSystem::GetKeyInternal(KeyCode key) const
    {
        const std::size_t i = static_cast<std::size_t>(key);
        if (i >= m_currKeys.size())
            return false;
        return m_currKeys[i];
    }

    bool ScriptSystem::GetKey(KeyCode key) const { return GetKeyInternal(key); }
    bool ScriptSystem::GetKeyDown(KeyCode key) const
    {
        const bool now = GetKeyInternal(key);
        const std::size_t i = static_cast<std::size_t>(key);
        const bool prev = (i < m_prevKeys.size()) ? m_prevKeys[i] : false;
        return now && !prev;
    }
    bool ScriptSystem::GetKeyUp(KeyCode key) const
    {
        const bool now = GetKeyInternal(key);
        const std::size_t i = static_cast<std::size_t>(key);
        const bool prev = (i < m_prevKeys.size()) ? m_prevKeys[i] : false;
        return !now && prev;
    }

    // 마우스 버튼 상태 확인 (내부 헬퍼)
    bool ScriptSystem::GetMouseButtonInternal(MouseCode button) const
    {
        const std::size_t i = static_cast<std::size_t>(button);
        if (i >= m_currMouseButtons.size())
            return false;
        return m_currMouseButtons[i];
    }

    bool ScriptSystem::GetMouseButton(MouseCode button) const
    {
        return GetMouseButtonInternal(button);
    }

    bool ScriptSystem::GetMouseButtonDown(MouseCode button) const
    {
        const bool now = GetMouseButtonInternal(button);
        const std::size_t i = static_cast<std::size_t>(button);
        const bool prev = (i < m_prevMouseButtons.size()) ? m_prevMouseButtons[i] : false;
        return now && !prev;
    }

    bool ScriptSystem::GetMouseButtonUp(MouseCode button) const
    {
        const bool now = GetMouseButtonInternal(button);
        const std::size_t i = static_cast<std::size_t>(button);
        const bool prev = (i < m_prevMouseButtons.size()) ? m_prevMouseButtons[i] : false;
        return !now && prev;
    }

    std::pair<float, float> ScriptSystem::GetMousePosition() const
    {
        if (!m_input)
            return std::make_pair(0.0f, 0.0f);

        POINT pos = m_input->GetMousePosition();
        return std::make_pair(static_cast<float>(pos.x), static_cast<float>(pos.y));
    }

    float ScriptSystem::GetMouseDeltaX() const
    {
        if (!m_input)
            return 0.0f;

        POINT delta = m_input->GetMouseDelta();
        return static_cast<float>(delta.x);
    }

    float ScriptSystem::GetMouseDeltaY() const
    {
        if (!m_input)
            return 0.0f;

        POINT delta = m_input->GetMouseDelta();
        return static_cast<float>(delta.y);
    }

    float ScriptSystem::GetMouseScrollDelta() const
    {
        if (!m_input)
            return 0.0f;

        return m_input->GetMouseScrollDelta();
    }

    std::string ScriptSystem::GetResolvedPath(const char* filename) const
    {
        if (!filename || !filename[0])
            return "";

        std::string path = filename;

        // 만약 입력값에 이미 경로나 슬래시가 포함되어 있다면 그대로 쓸 수도 있겠지만,
        // 여기서는 요청하신 대로 "파일명만 들어온다"고 가정하고 무조건 경로를 붙입니다.
        if (m_editorMode)
        {
            // 에디터 실행 중: 실행 파일 위치 기준 한 단계 상위의 원본 소스 폴더 참조
            // 예: "../Assets/Scenes/Stage1.scene"
            return "../Assets/Scenes/" + path;
        }
        else
        {
            // 빌드된 게임 실행 중: 실행 파일 옆의 배포된 폴더 참조
            // 예: "Assets/Scenes/Stage1.scene"
            return "Assets/Scenes/" + path;
        }
    }

    void ScriptSystem::SwitchTo(const char* sceneName)
    {
        // "코드 씬 이름" 전환용: 여기서 경로/확장자 붙이면 SceneFactory에서 못 찾는다.
        // SceneManager::SwitchTo()가 내부에서 처리하므로 그대로 전달
        m_pendingSwitch = (sceneName ? sceneName : "");
    }

    void ScriptSystem::LoadSceneFile(const char* scenePathUtf8)
    {
        m_pendingSceneFile = GetResolvedPath(scenePathUtf8);

        std::filesystem::path p = m_pendingSceneFile;
        if (p.extension() != ".scene") p += ".scene";
        m_pendingSceneFile = p.string().c_str();
    }

    bool ScriptSystem::LoadSceneFileRequest(const char* scenePathUtf8)
    {
        if (!m_scenes || !scenePathUtf8) return false;

        std::string pathStr = scenePathUtf8;
        std::filesystem::path p = pathStr;
        
        // 이미 Assets/, Resource/, Cooked/로 시작하는 논리 경로인 경우 그대로 사용
        const std::string genericPath = p.generic_string();
        if (genericPath.find("Assets/") == 0 || 
            genericPath.find("Resource/") == 0 || 
            genericPath.find("Cooked/") == 0)
        {
            // 논리 경로는 그대로 사용
            if (p.extension() != ".scene") p += ".scene";
            return m_scenes->LoadSceneFileRequest(p);
        }
        
        // 그 외의 경우 GetResolvedPath 사용 (파일명만 들어온 경우)
        std::string resolvedPath = GetResolvedPath(scenePathUtf8);
        p = resolvedPath;
        if (p.extension() != ".scene") p += ".scene";

        return m_scenes->LoadSceneFileRequest(p);
    }

    void ScriptSystem::EnsureServicesBound(World& world)
    {
        for (auto& [entityId, list] : world.GetAllScriptsInWorld())
        {
            for (auto& comp : list)
            {
                if (!comp.instance) continue;

                // .meta 기본값 1회 주입 (씬/프리팹에서 props가 이미 들어간 경우 defaultsApplied=true로 막습니다)
                if (!comp.defaultsApplied && m_resources && !comp.scriptName.empty())
                {
                    const std::filesystem::path metaLogical = std::filesystem::path("Assets/Scripts") / (comp.scriptName + ".meta");
                    auto bytes = m_resources->LoadSharedBinaryAuto(metaLogical);
                    if (bytes && !bytes->empty())
                    {
                        try
                        {
                            auto root = JsonRttr::json::parse(bytes->begin(), bytes->end());
                            auto itP = root.find("props");
                            if (itP != root.end() && itP->is_object())
                            {
                                rttr::instance inst = *comp.instance;
                                const rttr::type t = rttr::type::get_by_name(comp.scriptName);
                                JsonRttr::FromJsonObject(inst, *itP, t);
                            }
                        }
                        catch (...) {}
                    }
                    comp.defaultsApplied = true;
                }

                comp.instance->SetContext(&world, entityId);
                comp.instance->SetServices(&m_services);
            }
        }
    }

    void ScriptSystem::CallUpdate(World& world, float deltaTime)
    {
        auto& allScripts = world.GetAllScriptsInWorld();
        for (auto it = allScripts.begin(); it != allScripts.end(); ++it)
        {
            EntityId entityId = it->first;
            auto& list = it->second;

            // 벡터를 순회할 때 size를 매번 체크하며 인덱스로 접근
            for (size_t i = 0; i < list.size(); ++i)
            {
                auto& comp = list[i];

                if (!comp.instance) continue;

                // Update 도중 RemoveComponent가 호출되어 현재 인덱스가 삭제될 수 있음
                // 하지만 여기서는 간단히 null 체크나 enabled 체크로 넘어감
                // 더 엄격하게 하려면 삭제된 요소를 건너뛰는 로직을 나중에 넣어야함.
                // 일단은 이렇게 처리해둠. 문제 생기면 그때 수정.

                comp.instance->SetContext(&world, entityId);
                comp.instance->SetServices(&m_services);

                if (!comp.awoken)
                {
                    comp.awoken = true;
                    comp.wasEnabled = comp.enabled;

                    comp.instance->Awake();

                    // Awake 도중 스크립트가 삭제되었을 수도 있으므로 체크
                    if (i >= list.size()) break;
                    if (!list[i].instance) continue; // 삭제된 경우

                    if (comp.enabled) comp.instance->OnEnable();
                }

                // 중간에 삭제되었는지 다시 확인
                if (i >= list.size()) break;

                // 참조 다시 획득 벡터 재할당 가능성을 배제할 순 없음
                auto& currentComp = list[i];

                if (currentComp.enabled != currentComp.wasEnabled)
                {
                    if (currentComp.enabled) 
                        currentComp.instance->OnEnable();
                    else 
                        currentComp.instance->OnDisable();

                    currentComp.wasEnabled = currentComp.enabled;
                }

                if (!currentComp.enabled)
                    continue;

                if (!currentComp.started)
                {
                    currentComp.started = true;
                    currentComp.instance->Start();
                }

                // Start 등에서 벡터가 재할당되었을 수 있으므로 다시 참조 갱신
                if (i < list.size() && list[i].instance)
                {
                    list[i].instance->Update(deltaTime);
                }
            }
        }
    }

    void ScriptSystem::CallFixedUpdate(World& world, float fixedDt)
    {
        auto& allScripts = world.GetAllScriptsInWorld();
        for (auto it = allScripts.begin(); it != allScripts.end(); ++it)
        {
            auto& list = it->second;
            for (size_t i = 0; i < list.size(); ++i)
            {
                if (i >= list.size()) break;
                auto& comp = list[i];

                if (!comp.instance || !comp.enabled) continue;
                comp.instance->FixedUpdate(fixedDt);
            }
        }
    }

    void ScriptSystem::CallLateUpdate(World& world, float deltaTime)
    {
        auto& allScripts = world.GetAllScriptsInWorld();
        for (auto it = allScripts.begin(); it != allScripts.end(); ++it)
        {
            auto& list = it->second;

            for (size_t i = 0; i < list.size(); ++i)
            {
                if (i >= list.size()) break;
                auto& comp = list[i];
                if (!comp.instance || !comp.enabled) continue;
                comp.instance->LateUpdate(deltaTime);
            }
        }
    }

    void ScriptSystem::CallPostCombatUpdate(World& world, float deltaTime)
    {
        auto& allScripts = world.GetAllScriptsInWorld();
        for (auto it = allScripts.begin(); it != allScripts.end(); ++it)
        {
            EntityId entityId = it->first;
            auto& list = it->second;

            for (size_t i = 0; i < list.size(); ++i)
            {
                if (i >= list.size()) break;
                auto& comp = list[i];
                if (!comp.instance || !comp.enabled) continue;

                comp.instance->SetContext(&world, entityId);
                comp.instance->SetServices(&m_services);
                comp.instance->PostCombatUpdate(deltaTime);
            }
        }
    }

    bool ScriptSystem::HasPendingSceneRequests() const
    {
        return !m_pendingSwitch.empty() || !m_pendingSceneFile.empty();
    }

    void ScriptSystem::CommitSceneRequests(World& world)
    {
        // 기존 로직 그대로 사용 (단, 이제 엔진이 안전 지점에서 호출)
        ProcessSceneRequests(world);
    }

    void ScriptSystem::ProcessSceneRequests(World& world)
    {
        if (m_pendingSwitch.empty() && m_pendingSceneFile.empty())
            return;

        // 1) 코드 씬 전환
        if (m_scenes)
        {
            if (!m_pendingSwitch.empty())
            {
                const std::string name = std::exchange(m_pendingSwitch, {});
                if (!m_scenes->SwitchTo(name.c_str()))
                    ALICE_LOG_WARN("ScriptSystem: SceneManager::SwitchTo failed. name=\"%s\"", name.c_str());
            }
        }

        // 2) .scene 파일 로드
        {
            if (!m_pendingSceneFile.empty())
            {
                const std::string path = std::exchange(m_pendingSceneFile, {});
                const bool ok = (m_resources)
                    ? SceneFile::LoadAuto(world, *m_resources, std::filesystem::path(path))
                    : SceneFile::Load(world, std::filesystem::path(path));
                onTrimVideoMemory.Execute();
                if (!ok)
                    ALICE_LOG_ERRORF("ScriptSystem: SceneFile::Load failed. path=\"%s\"", path.c_str());
                else
                    onAfterSceneLoaded.Execute();
            }
        }
    }

    void ScriptSystem::Tick(World& world, float deltaTime)
    {
        BeginInputFrame();
        EnsureServicesBound(world);

        // Awake/OnEnable/Start/Update
        CallUpdate(world, deltaTime);

        // FixedUpdate
        m_fixedAcc += deltaTime;
        while (m_fixedAcc >= m_fixedDt)
        {
            CallFixedUpdate(world, m_fixedDt);
            m_fixedAcc -= m_fixedDt;
        }

        // LateUpdate
        CallLateUpdate(world, deltaTime);

        // 지연 파괴 업데이트
        world.UpdateDelayedDestruction(deltaTime);

        // (중요) 씬 요청 커밋은 여기서 하지 않는다.
        // Engine::Update()의 안전 지점에서 CommitSceneRequests()를 호출한다.
        // ProcessSceneRequests(world);
    }

    void ScriptSystem::PostCombatUpdate(World& world, float deltaTime)
    {
        EnsureServicesBound(world);
        CallPostCombatUpdate(world, deltaTime);
    }

    void ScriptSystem::OnApplicationQuit(World& world)
    {
        EnsureServicesBound(world);
        for (auto& [entityId, list] : world.GetAllScriptsInWorld())
        {
            (void)entityId;
            for (auto& comp : list)
            {
                if (!comp.instance) continue;
                comp.instance->OnApplicationQuit();
            }
        }
    }
}
