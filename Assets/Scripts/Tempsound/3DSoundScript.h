#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include <string>

namespace Alice
{
    class AudioEventBusScript;

    class DSoundScript : public IScript
    {
        ALICE_BODY(DSoundScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, busEntityName, std::string("AudioBus"));

        // Player (Z,X,C,V,B)
        ALICE_PROPERTY(std::string, zPath, std::string("Player/공격 1/Player_Attack_01.wav"));
        ALICE_PROPERTY(std::string, xPath, std::string("Player/공격 2/Player_Attack_02.wav"));
        ALICE_PROPERTY(std::string, cPath, std::string("Player/공격 3/Player_Attack_03.wav"));
        ALICE_PROPERTY(std::string, vPath, std::string("Player/강공격/Player_HeavyAttack_01.mp3"));
        ALICE_PROPERTY(std::string, bPath, std::string("Player/구르기/Player_Rolling_01.mp3"));

        // Boss (N,M,A,S,D)
        ALICE_PROPERTY(std::string, nPath, std::string("Boss/공격 1/Boss_Attack_01.mp3"));
        ALICE_PROPERTY(std::string, mPath, std::string("Boss/공격 2/Boss_Attack_02.wav"));
        ALICE_PROPERTY(std::string, aPath, std::string("Boss/공격 3/Boss_Attack_03.wav"));
        ALICE_PROPERTY(std::string, sPath, std::string("Boss/포효/Boss_Roaring_01.mp3"));
        ALICE_PROPERTY(std::string, dPath, std::string("Boss/피격/Boss_Hit_01.wav"));

        // BGM (F,G,H,J)
        ALICE_PROPERTY(std::string, fPath, std::string("BGM/BGM_Title.mp3"));
        ALICE_PROPERTY(std::string, gPath, std::string("BGM/BGM_Tutorial_01.mp3"));
        ALICE_PROPERTY(std::string, hPath, std::string("BGM/BGM_Boss_FirstPhase_01.mp3"));
        ALICE_PROPERTY(std::string, jPath, std::string("BGM/BGM_Boss_SecondPhase_01.mp3"));

    private:
        AudioEventBusScript* m_bus = nullptr;
    };
}
