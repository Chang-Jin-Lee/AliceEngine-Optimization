#include "3DSoundScript.h"

#include "AudioEventBusScript.h"
#include "Runtime/Input/Input.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Scripting/ScriptFactory.h"

namespace Alice
{
    namespace
    {
        AudioEventBusScript* FindBus(World& world, const std::string& name)
        {
            if (name.empty())
                return nullptr;

            GameObject go = world.FindGameObject(name);
            if (!go.IsValid())
                return nullptr;

            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;

            for (auto& sc : *scripts)
            {
                if (sc.scriptName == "AudioEventBusScript" && sc.instance)
                    return static_cast<AudioEventBusScript*>(sc.instance.get());
            }

            return nullptr;
        }
    }

    REGISTER_SCRIPT(DSoundScript);

    void DSoundScript::Start()
    {
        if (auto* world = GetWorld())
        {
            m_bus = FindBus(*world, Get_busEntityName());
            if (!m_bus)
            {
                ALICE_LOG_WARN("[DSound] Bus not found: %s", Get_busEntityName().c_str());
            }
        }
    }

    void DSoundScript::Update(float)
    {
        auto* input = Input();
        if (!input)
            return;

        if (!m_bus)
        {
            if (auto* world = GetWorld())
                m_bus = FindBus(*world, Get_busEntityName());
            if (!m_bus)
                return;
        }

        auto requestPlayer = [this](const std::string& path)
        {
            if (!path.empty())
                m_bus->RequestPlayerSfx(path);
        };
        auto requestBoss = [this](const std::string& path)
        {
            if (!path.empty())
                m_bus->RequestBossSfx(path);
        };
        auto requestBgm = [this](const std::string& path)
        {
            if (!path.empty())
                m_bus->RequestBgm(path);
        };

        if (input->GetKeyDown(KeyCode::Z)) requestPlayer(Get_zPath());
        if (input->GetKeyDown(KeyCode::X)) requestPlayer(Get_xPath());
        if (input->GetKeyDown(KeyCode::C)) requestPlayer(Get_cPath());
        if (input->GetKeyDown(KeyCode::V)) requestPlayer(Get_vPath());
        if (input->GetKeyDown(KeyCode::B)) requestPlayer(Get_bPath());

        if (input->GetKeyDown(KeyCode::N)) requestBoss(Get_nPath());
        if (input->GetKeyDown(KeyCode::M)) requestBoss(Get_mPath());
        if (input->GetKeyDown(KeyCode::A)) requestBoss(Get_aPath());
        if (input->GetKeyDown(KeyCode::S)) requestBoss(Get_sPath());
        if (input->GetKeyDown(KeyCode::D)) requestBoss(Get_dPath());

        if (input->GetKeyDown(KeyCode::F)) requestBgm(Get_fPath());
        if (input->GetKeyDown(KeyCode::G)) requestBgm(Get_gPath());
        if (input->GetKeyDown(KeyCode::H)) requestBgm(Get_hPath());
        if (input->GetKeyDown(KeyCode::J)) requestBgm(Get_jPath());
    }
}
