#include "AudioDemoScript.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Foundation/Helper.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/Audio/Components/AudioSourceComponent.h"
#include "Runtime/Audio/SoundManager.h"

namespace Alice
{
    REGISTER_SCRIPT(AudioDemoScript);

    void AudioDemoScript::Start()
    {
        ALICE_LOG_INFO("[AudioDemo] Started. Press 1-6 to Play, 7-9 to Stop.");
    }

    void AudioDemoScript::Update(float)
    {
        auto* input = Input();
        if (!input) return;

        // 1번키: BGM 재생 (컴포넌트 제어 방식)
        if (input->GetKeyDown(KeyCode::Alpha1))
        {
            auto go = gameObject();
            auto* audio = go.GetComponent<AudioSourceComponent>();
            if (!audio)
                audio = &go.AddComponent<AudioSourceComponent>();

            audio->soundPath = bgmPath;
            audio->type = AudioType::BGM;
            audio->is3D = false;
            audio->loop = true;
            audio->volume = 0.5f;
            audio->requestPlay = true; // AudioSystem이 다음 프레임에 처리함
            ALICE_LOG_INFO("[AudioDemo] Cmd: Play BGM (%s)", bgmPath.c_str());
        }

        // 2~6번키: VFX 재생 (Direct Fire-and-Forget 방식)
        // ECS를 거치지 않고 SoundManager를 바로 사용 (UI음, 단순 효과음 등)
        auto* resources = Resources();
        if (!resources) return; // ResourceManager가 없으면 VFX 재생 불가

        if (input->GetKeyDown(KeyCode::Alpha2))
        {
            std::wstring key = WStringFromUtf8(vfxPath1);
            // 리소스 로드 시도 (없으면 자동 로드)
            Sound::LoadAuto(*resources, key, vfxPath1, Sound::Type::SFX);
            Sound::PlaySFX(key, 1.0f, 1.0f, false); // loop=false -> 폴리포니 허용
            ALICE_LOG_INFO("[AudioDemo] Cmd: Play VFX 1 (%s)", vfxPath1.c_str());
        }
        if (input->GetKeyDown(KeyCode::Alpha3))
        {
            std::wstring key = WStringFromUtf8(vfxPath2);
            Sound::LoadAuto(*resources, key, vfxPath2, Sound::Type::SFX);
            Sound::PlaySFX(key, 1.0f, 1.0f, false);
            ALICE_LOG_INFO("[AudioDemo] Cmd: Play VFX 2 (%s)", vfxPath2.c_str());
        }
        if (input->GetKeyDown(KeyCode::Alpha4))
        {
            std::wstring key = WStringFromUtf8(vfxPath3);
            Sound::LoadAuto(*resources, key, vfxPath3, Sound::Type::SFX);
            Sound::PlaySFX(key, 1.0f, 1.0f, false);
            ALICE_LOG_INFO("[AudioDemo] Cmd: Play VFX 3 (%s)", vfxPath3.c_str());
        }
        if (input->GetKeyDown(KeyCode::Alpha5))
        {
            std::wstring key = WStringFromUtf8(vfxPath4);
            Sound::LoadAuto(*resources, key, vfxPath4, Sound::Type::SFX);
            Sound::PlaySFX(key, 1.0f, 1.0f, false);
            ALICE_LOG_INFO("[AudioDemo] Cmd: Play VFX 4 (%s)", vfxPath4.c_str());
        }
        if (input->GetKeyDown(KeyCode::Alpha6))
        {
            std::wstring key = WStringFromUtf8(vfxPath5);
            Sound::LoadAuto(*resources, key, vfxPath5, Sound::Type::SFX);
            Sound::PlaySFX(key, 1.0f, 1.0f, false);
            ALICE_LOG_INFO("[AudioDemo] Cmd: Play VFX 5 (%s)", vfxPath5.c_str());
        }

        // 7번키: BGM 끄기
        if (input->GetKeyDown(KeyCode::Alpha7))
        {
            auto go = gameObject();
            if (auto* audio = go.GetComponent<AudioSourceComponent>())
            {
                audio->requestStop = true;
            }
            // 컴포넌트가 없어도 직접 정지 시도
            Sound::StopBGM();
            ALICE_LOG_INFO("[AudioDemo] Cmd: Stop BGM");
        }

        // 8번키: 진행중인 모든 VFX 끄기
        if (input->GetKeyDown(KeyCode::Alpha8))
        {
            Sound::StopAllSFX();
            ALICE_LOG_INFO("[AudioDemo] Cmd: Stop All SFX");
        }

        // 9번키: 모든 소리 끄기 (패닉 버튼)
        if (input->GetKeyDown(KeyCode::Alpha9))
        {
            Sound::StopBGM();
            Sound::StopAllSFX();
            ALICE_LOG_INFO("[AudioDemo] Cmd: Stop ALL");
        }
    }
}
