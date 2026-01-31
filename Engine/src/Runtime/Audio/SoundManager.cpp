#include "Runtime/Audio/SoundManager.h"

#include <fmod.hpp>
#include <fmod_errors.h>
#include <map>
#include <vector>
#include <algorithm>

#include "Runtime/Foundation/Helper.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Resources/ResourceManager.h"

#pragma comment(lib, "fmod_vc.lib")

namespace
{
    FMOD::System* g_System = nullptr;
    FMOD::ChannelGroup* g_MasterGroup = nullptr;
    FMOD::ChannelGroup* g_BgmGroup = nullptr;
    FMOD::ChannelGroup* g_SfxGroup = nullptr;

    struct SoundData
    {
        FMOD::Sound* fmodSound = nullptr;
        // 현재는 Editor Mode가 vector로 데이터 소유함
        // 이후 미래에는 Game Mode (Chunk): DataBlob 같은 추상화 구조로 변경하여
        //        - Editor: vector 소유 (ownedBuffer)
        //        - Game: Chunk 메모리 참조만 (ptr + size, 소유권 없음)
        //        FMOD_OPENMEMORY_POINT 플래그 사용하여 Zero-copy 구현해야함 
        std::vector<std::uint8_t> memoryBuffer; // 메모리 로드 시 버퍼 유지용
    };
    std::map<std::wstring, SoundData> g_SoundBank;

    FMOD::Channel* g_ChannelBGM = nullptr;
    float g_VolBGM = 1.0f;
    std::wstring g_CurrentBGMKey;

    // [SFX 관리 구조체] - 데모 프로젝트와 동일
    struct SfxEntry
    {
        FMOD::Channel* channel = nullptr;
        bool loop = false;
    };

    // Loop SFX 관리: Key별로 하나의 채널만 유지 (인스턴스 1개)
    std::map<std::wstring, SfxEntry> g_SfxChannels;

    // One-Shot SFX 관리: 중첩 재생을 위한 채널 리스트입니다 (인스턴스 N개)
    // 여기가 미리 만들어 놓은 여러개의 인스턴스을 처리하는 벡터입니다.
    std::vector<FMOD::Channel*> g_ChannelsSFX;
    float g_VolSFX = 1.0f;
    float g_PitchSFX = 1.0f;

    struct Inst3D { FMOD::Channel* ch = nullptr; };
    std::map<std::wstring, Inst3D> g_Inst3D;

    int g_SoftwareChannels = 64;
    int g_ReserveVoices = 4; // BGM/3D 여유분

    inline bool Check(FMOD_RESULT r, const char* msg = "")
    {
        if (r != FMOD_OK)
        {
            ALICE_LOG_ERRORF("[FMOD Error] %s: %s", msg, FMOD_ErrorString(r));
            return false;
        }
        return true;
    }

    inline FMOD_VECTOR ToFmod(const DirectX::XMFLOAT3& v) { return FMOD_VECTOR{ v.x, v.y, v.z }; }
    inline FMOD_VECTOR ToFmod(DirectX::XMVECTOR v)
    {
        DirectX::XMFLOAT3 f3;
        DirectX::XMStoreFloat3(&f3, v);
        return FMOD_VECTOR{ f3.x, f3.y, f3.z };
    }

    // 재생이 끝난 채널을 벡터에서 제거
    // 소리가 여러개 동시에 나올때 안들리게 되는 현상을 막음 
    void CleanupSFX()
    {
        // 1. One-Shot 채널 청소
        if (!g_ChannelsSFX.empty())
        {
            auto it = std::remove_if(g_ChannelsSFX.begin(), g_ChannelsSFX.end(),
                [](FMOD::Channel* c) {
                    if (!c) return true;
                    bool playing = false;
                    FMOD_RESULT r = c->isPlaying(&playing);
                    // 에러가 났거나(유효하지 않음) 재생 중이 아니면 제거 대상
                    return (r != FMOD_OK) || !playing;
                });
            g_ChannelsSFX.erase(it, g_ChannelsSFX.end());
        }

        // 2. Loop SFX 채널 청소
        for (auto it = g_SfxChannels.begin(); it != g_SfxChannels.end();)
        {
            bool remove = false;
            if (it->second.channel)
            {
                bool playing = false;
                FMOD_RESULT r = it->second.channel->isPlaying(&playing);
                if (r != FMOD_OK || !playing)
                {
                    it->second.channel = nullptr;
                    if (!it->second.loop) remove = true;
                }
            }
            else
            {
                if (!it->second.loop) remove = true;
            }

            if (remove) it = g_SfxChannels.erase(it);
            else ++it;
        }

        // 3. 3D 인스턴스 청소
        std::erase_if(g_Inst3D, [](const auto& pair) {
            bool playing = false;
            return (pair.second.ch && pair.second.ch->isPlaying(&playing) == FMOD_OK) ? !playing : true;
        });
    }

    bool IsVirtual(FMOD::Channel* ch)
    {
        bool v = false;
        return ch && ch->isVirtual(&v) == FMOD_OK && v;
    }

    void StopOldestOneShot()
    {
        if (g_ChannelsSFX.empty()) return;
        if (g_ChannelsSFX.front()) g_ChannelsSFX.front()->stop();
        g_ChannelsSFX.erase(g_ChannelsSFX.begin());
    }

    void StopAllOneShot()
    {
        for (auto c : g_ChannelsSFX) if (c) c->stop();
        g_ChannelsSFX.clear();
    }

    void TrimOneShotLimit()
    {
        int limit = g_SoftwareChannels - g_ReserveVoices;
        if (limit < 1) limit = 1;

        int excess = (int)g_ChannelsSFX.size() - limit;
        if (excess <= 0) return;

        // 최대 excess개수 만큼만 정리
        for (int i = 0; i < excess; ++i)
            StopOldestOneShot();
    }

    bool CreateSoundFromMemory(const std::wstring& key, const std::vector<std::uint8_t>& bytes, Alice::Sound::Type type)
    {
        if (!g_System) return false;
        if (bytes.empty()) return false;

        FMOD_CREATESOUNDEXINFO exinfo{};
        exinfo.cbsize = sizeof(exinfo);
        exinfo.length = static_cast<unsigned int>(bytes.size());

        FMOD_MODE mode = FMOD_OPENMEMORY;
        mode |= (type == Alice::Sound::Type::BGM) ? (FMOD_CREATESTREAM | FMOD_LOOP_NORMAL) : (FMOD_CREATESAMPLE | FMOD_LOOP_OFF);
        if (type != Alice::Sound::Type::BGM) mode |= FMOD_3D; // SFX는 3D 지원

        FMOD::Sound* newSound = nullptr;
        FMOD_RESULT r = g_System->createSound(reinterpret_cast<const char*>(bytes.data()), mode, &exinfo, &newSound);
        if (!Check(r, "CreateSound") || !newSound) return false;

        SoundData data;
        data.fmodSound = newSound;
        data.memoryBuffer = bytes; // FMOD가 데이터를 참조하므로 버퍼 유지 필수
        g_SoundBank[key] = std::move(data);
        return true;
    }
}

namespace Alice::Sound
{
    bool Initialize()
    {
        if (g_System) return true;
        
        FMOD_RESULT r = FMOD::System_Create(&g_System);
        if (!Check(r, "System Create") || !g_System) return false;

        // 기본값이 낮으면 64~근처에서 새 소리가 가상화되어 안 들릴 수 있음
        g_SoftwareChannels = 64;
        Check(g_System->setSoftwareChannels(g_SoftwareChannels), "Set Software Channels");

        // 최대 채널 512, 초기화 플래그
        r = g_System->init(512, FMOD_INIT_NORMAL | FMOD_INIT_3D_RIGHTHANDED, nullptr);
        if (!Check(r, "System Init"))
        {
            g_System->release();
            g_System = nullptr;
            return false;
        }

        // 채널 그룹 생성
        Check(g_System->getMasterChannelGroup(&g_MasterGroup), "Get Master Group");
        Check(g_System->createChannelGroup("BGM", &g_BgmGroup), "Create BGM Group");
        Check(g_System->createChannelGroup("SFX", &g_SfxGroup), "Create SFX Group");

        // 그룹 계층 설정 (Master 하위에 BGM, SFX)
        if (g_MasterGroup)
        {
            if (g_BgmGroup) g_MasterGroup->addGroup(g_BgmGroup);
            if (g_SfxGroup) g_MasterGroup->addGroup(g_SfxGroup);
        }

        // 3D 설정 (거리 계수 1.0)
        g_System->set3DSettings(1.0f, 1.0f, 1.0f);

        ALICE_LOG_INFO("[SoundManager] Initialized FMOD System with ChannelGroups.");
        return true;
    }

    void Shutdown()
    {
        // 모든 소리 정지
        if (g_MasterGroup) g_MasterGroup->stop();

        StopBGM();
        StopAllSFX();

        for (auto& pair : g_Inst3D)
        {
            if (pair.second.ch)
            {
                pair.second.ch->stop();
                pair.second.ch = nullptr;
            }
        }
        g_Inst3D.clear();

        for (auto& pair : g_SfxChannels)
        {
            if (pair.second.channel)
            {
                pair.second.channel->stop();
                pair.second.channel = nullptr;
            }
        }
        g_SfxChannels.clear();
        g_ChannelsSFX.clear();

        // 사운드 해제
        for (auto& pair : g_SoundBank)
        {
            if (pair.second.fmodSound)
            {
                pair.second.fmodSound->release();
                pair.second.fmodSound = nullptr;
            }
        }
        g_SoundBank.clear();

        // 그룹 해제
        if (g_BgmGroup) { g_BgmGroup->release(); g_BgmGroup = nullptr; }
        if (g_SfxGroup) { g_SfxGroup->release(); g_SfxGroup = nullptr; }

        if (g_System)
        {
            g_System->close();
            g_System->release();
            g_System = nullptr;
        }

        ALICE_LOG_INFO("[SoundManager] Shutdown.");
    }

    void Update()
    {
        if (!g_System) return;
        
        // erase_if: 재생 중이지 않은 3D 인스턴스 정리
        std::erase_if(g_Inst3D, [](const auto& pair) {
            bool playing = false;
            return (pair.second.ch && pair.second.ch->isPlaying(&playing) == FMOD_OK) ? !playing : true;
        });

        g_System->update();
        CleanupSFX();
    }

    bool Load(const std::wstring& key, const std::wstring& path, Type type)
    {
        if (!g_System && !Initialize()) return false;

        if (g_SoundBank.find(key) != g_SoundBank.end())
            return true;

        FMOD_MODE mode = (type == Type::BGM) ? FMOD_CREATESTREAM : FMOD_DEFAULT;
        FMOD::Sound* newSound = nullptr;

        FMOD_RESULT r = g_System->createSound(Utf8FromWString(path).c_str(), mode, nullptr, &newSound);
        if (!Check(r) || !newSound) return false;

        g_SoundBank[key].fmodSound = newSound;
        return true;
    }

    bool LoadAuto(const ResourceManager& resources,
                  const std::wstring& key,
                  const std::filesystem::path& logicalPath,
                  Type type)
    {
        if (!g_System && !Initialize()) return false;
        if (key.empty()) return false;

        // 이미 로드됨
        if (g_SoundBank.contains(key)) return true;

        // ====================================================================
        // @details : 
        // 현재 구현 Editor Mode / Loose File 방식
        // - ResourceManager가 파일을 읽어서 vector를 할당
        // - SoundManager가 이 vector를 소유하여 메모리 주소 고정
        // - 장점: 안정적, 메모리 접근 위반 방지
        // - 단점: 파일 개수만큼 할당 발생, 메모리 파편화 우려
        // ====================================================================
        // @details :
        // 향후 개선 Game Mode / Chunk 시스템 지원
        // - ResourceManager가 Chunk 메모리의 포인터(ptr + offset + size)만 반환
        // - SoundManager는 데이터를 소유하지 않고 참조만 함
        // - FMOD_OPENMEMORY_POINT 플래그 사용하여 Zero-copy 구현
        // - 장점: 제로 카피, 로딩 속도 극대화, 메모리 효율
        // ====================================================================
        
        // 메모리 주소 고정을 위해 맵에 먼저 항목 생성함
        SoundData& data = g_SoundBank[key];

        // 고정된 버퍼에 데이터를 직접 로드 (메모리 주소가 변경되지 않음)
        // TODO: 향후 DataBlob 구조로 변경하여 Chunk 시스템 지원해야함 
        //       - Editor: resources.LoadBinaryAuto() -> vector 소유
        //       - Game: resources.LoadChunkData() -> ptr + size 참조만
        if (!resources.LoadBinaryAuto(logicalPath, data.memoryBuffer) || data.memoryBuffer.empty())
        {
            ALICE_LOG_ERRORF("[SoundManager] LoadAuto Failed: Path=\"%s\" Key=\"%ls\"", 
                logicalPath.string().c_str(), key.c_str());
            g_SoundBank.erase(key); // 실패 시 항목 제거
            return false;
        }

        // FMOD 사운드 생성 (고정된 메모리 주소 사용)
        FMOD_CREATESOUNDEXINFO exinfo{};
        exinfo.cbsize = sizeof(exinfo);
        exinfo.length = static_cast<unsigned int>(data.memoryBuffer.size());

        // 현재는 FMOD_OPENMEMORY: FMOD가 데이터를 복사함 (안전하지만 비효율)
        // 미래에는 FMOD_OPENMEMORY_POINT: FMOD가 참조만 하도록 해야함 (Chunk 시스템과 함께 사용)
        FMOD_MODE mode = FMOD_OPENMEMORY;
        mode |= (type == Type::BGM) ? (FMOD_CREATESTREAM | FMOD_LOOP_NORMAL) : (FMOD_CREATESAMPLE | FMOD_LOOP_OFF);
        if (type != Type::BGM) mode |= FMOD_3D; // SFX는 3D 지원

        FMOD::Sound* newSound = nullptr;
        // data.memoryBuffer.data()는 맵 내부의 메모리이므로 이동되거나 해제되지 않음
        // 마래에는 Chunk 시스템에서는 data.dataBlob.ptr을 사용
        FMOD_RESULT r = g_System->createSound(reinterpret_cast<const char*>(data.memoryBuffer.data()), mode, &exinfo, &newSound);
        
        if (!Check(r, "LoadAuto") || !newSound)
        {
            ALICE_LOG_ERRORF("[SoundManager] CreateSound Failed: Key=\"%ls\" Size=%zu", 
                key.c_str(), data.memoryBuffer.size());
            g_SoundBank.erase(key); // 실패 시 항목 제거
            return false;
        }

        data.fmodSound = newSound;

        ALICE_LOG_INFO("[SoundManager] Loaded: Key=\"%ls\" Path=\"%s\" Size=%zu", 
            key.c_str(), logicalPath.string().c_str(), data.memoryBuffer.size());
        return true;
    }

    void SetMasterVolume(float volume)
    {
        if (g_MasterGroup) g_MasterGroup->setVolume(std::clamp(volume, 0.0f, 1.0f));
    }

    void SetBGMVolume(float volume)
    {
        g_VolBGM = std::clamp(volume, 0.0f, 1.0f);
        if (g_BgmGroup) g_BgmGroup->setVolume(g_VolBGM);
        if (g_ChannelBGM) g_ChannelBGM->setVolume(g_VolBGM);
    }

    void SetSFXVolume(float volume)
    {
        g_VolSFX = std::clamp(volume, 0.0f, 1.0f);
        if (g_SfxGroup) g_SfxGroup->setVolume(g_VolSFX);
    }

    void PauseAll(bool pause)
    {
        if (g_MasterGroup) g_MasterGroup->setPaused(pause);
    }

    void PlayBGM(const std::wstring& key, float /*fadeTime*/)
    {
        if (!g_System) return;
        
        // 이미 재생 중이면 무시
        if (g_CurrentBGMKey == key && IsBGMPlaying()) return;

        StopBGM(0.0f); // 이전 BGM 정지

        if (!g_SoundBank.contains(key))
        {
            ALICE_LOG_WARN("[SoundManager] PlayBGM Failed: Key not found \"%ls\"", key.c_str());
            return;
        }

        for (int attempt = 0; attempt < 8; ++attempt)
        {
            FMOD::Channel* ch = nullptr;
            FMOD_RESULT r = g_System->playSound(g_SoundBank[key].fmodSound, g_BgmGroup, false, &ch);
            if (!Check(r, "PlayBGM") || !ch) return;

            ch->setMode(FMOD_2D);
            ch->setVolume(g_VolBGM);
            ch->setPriority(0);          // BGM 최우선

            g_System->update();          // virtual 판정이 바로 반영되게 1번 업데이트

            if (!IsVirtual(ch))
            {
                g_ChannelBGM = ch;
                g_CurrentBGMKey = key;
                return;
            }

            // virtual(무음)로 시작했으면 지금 프레임에서 해결하고 다시 시도
            ch->stop();
            StopOldestOneShot();         // 원샷 하나만 비워서 보이스 확보
        }
    }

    void PauseBGM(bool pause)
    {
        if (!g_ChannelBGM) return;
        g_ChannelBGM->setPaused(pause);
    }

    void StopBGM(float /*fadeTime*/)
    {
        if (g_ChannelBGM)
        {
            g_ChannelBGM->stop(); // FadeOut 구현 생략 (필요 시 DSP 사용)
            g_ChannelBGM = nullptr;
        }
        g_CurrentBGMKey.clear();
    }

    bool IsBGMPlaying()
    {
        if (!g_ChannelBGM) return false;
        bool playing = false;
        return (g_ChannelBGM->isPlaying(&playing) == FMOD_OK) && playing;
    }

    bool IsBGMPaused()
    {
        if (!g_ChannelBGM) return false;
        bool paused = false;
        return (g_ChannelBGM->getPaused(&paused) == FMOD_OK) && paused;
    }

    bool SetBGMTimeSeconds(float sec)
    {
        if (!g_ChannelBGM) return false;
        return (g_ChannelBGM->setPosition((unsigned int)(sec * 1000.0f), FMOD_TIMEUNIT_MS) == FMOD_OK);
    }

    float GetBGMTimeSeconds()
    {
        if (!g_ChannelBGM) return 0.0f;
        unsigned int ms = 0;
        if (g_ChannelBGM->getPosition(&ms, FMOD_TIMEUNIT_MS) != FMOD_OK) return 0.0f;
        return (float)ms / 1000.0f;
    }

    float GetBGMLengthSeconds()
    {
        if (!g_ChannelBGM || g_CurrentBGMKey.empty()) return 0.0f;
        auto it = g_SoundBank.find(g_CurrentBGMKey);
        if (it == g_SoundBank.end() || !it->second.fmodSound) return 0.0f;
        unsigned int ms = 0;
        if (it->second.fmodSound->getLength(&ms, FMOD_TIMEUNIT_MS) != FMOD_OK) return 0.0f;
        return (float)ms / 1000.0f;
    }

    std::wstring GetCurrentBGMKey()
    {
        return g_CurrentBGMKey;
    }

    // ================= SFX Logic (데모와 동일) =================
    void PlaySFX(const std::wstring& key, float volume, float pitch, bool loop)
    {
        if (!g_System) return;
        if (!g_SoundBank.contains(key)) return;

        FMOD::Sound* sound = g_SoundBank[key].fmodSound;
        if (!sound) return;

        volume = std::clamp(volume, 0.f, 1.f);
        pitch = std::clamp(pitch, 0.5f, 2.f);

        // 재생 전에 Sound 자체의 Mode를 변경
        FMOD_MODE mode;
        if (sound->getMode(&mode) == FMOD_OK)
        {
            if (loop) { mode |= FMOD_LOOP_NORMAL; mode &= ~FMOD_LOOP_OFF; }
            else      { mode |= FMOD_LOOP_OFF;    mode &= ~FMOD_LOOP_NORMAL; }
            sound->setMode(mode);
        }

        if (loop)
        {
            // Loop SFX에선 Map 사용 (1개를 인스턴스함)
            auto it = g_SfxChannels.find(key);
            // 이미 재생 중이면 속성만 업데이트
            if (it != g_SfxChannels.end() && it->second.channel)
            {
                bool playing = false;
                if (it->second.channel->isPlaying(&playing) == FMOD_OK && playing) {
                    it->second.channel->setVolume(volume * g_VolSFX);
                    it->second.channel->setPitch(pitch);
                    return; 
                }
            }
            
            // 새로 재생
            FMOD::Channel* ch = nullptr;
            g_System->playSound(sound, g_SfxGroup, false, &ch);
            if (ch) {
                ch->setVolume(volume * g_VolSFX);
                ch->setPitch(pitch);
                g_SfxChannels[key] = { ch, true };
            }
        }
        else
        {
            CleanupSFX();

            // 이 함수 한 번에서 "그 클릭이 안 들리면" 바로 재시도해서
            // 최종적으로는 이번 클릭에서 소리가 나게 만든다.
            FMOD::Channel* ch = nullptr;

            for (int attempt = 0; attempt < 8; ++attempt) // 무한루프 방지함. 최대 8번만
            {
                TrimOneShotLimit();

                ch = nullptr;
                FMOD_RESULT r = g_System->playSound(sound, g_SfxGroup, false, &ch);
                if (r != FMOD_OK || !ch)
                {
                    StopOldestOneShot();
                    continue;
                }

                ch->setMode(FMOD_2D);
                ch->setVolume(volume * g_VolSFX);
                ch->setPitch(pitch);
                // 원샷은 BGM보다 덜 중요. (새 클릭 보장은 우리가 "원샷끼리" 스틸로 해결)
                ch->setPriority(128);

                // 키 연타로 한 프레임에 여러 번 호출될 때도
                // 가상화 여부를 즉시 반영시키려고 update 1번 돌림
                g_System->update();

                if (!IsVirtual(ch))
                {
                    g_ChannelsSFX.push_back(ch);
                    return;
                }

                // 안 들리는 클릭 발생시 자리 확보 후 즉시 재시도
                ch->stop();

                // 한 번에 하나씩만 정리하면서 재시도 (안전)
                if (!g_ChannelsSFX.empty()) StopOldestOneShot();
                else StopAllOneShot();
            }

            // 원샷 전부 비우고 1번 더 재생 시도함. 이번 클릭 살리는 것
            StopAllOneShot();

            ch = nullptr;
            FMOD_RESULT r = g_System->playSound(sound, g_SfxGroup, false, &ch);
            if (r != FMOD_OK || !ch) return;

            ch->setMode(FMOD_2D);
            ch->setVolume(volume * g_VolSFX);
            ch->setPitch(pitch);
            // 원샷은 BGM보다 덜 중요. 새 클릭 보장은 우리가 원샷끼리 스틸로 해결
            ch->setPriority(128);

            g_ChannelsSFX.push_back(ch);
        }
    }

    bool IsSfxPlaying(const std::wstring& key)
    {
        if (auto it = g_SfxChannels.find(key); it != g_SfxChannels.end() && it->second.channel)
        {
            bool playing = false;
            return (it->second.channel->isPlaying(&playing) == FMOD_OK) && playing;
        }
        return false;
    }

    void StopSfx(const std::wstring& key)
    {
        // Loop 채널만 특정해서 끔 (OneShot은 보통 놔둠)
        if (auto it = g_SfxChannels.find(key); it != g_SfxChannels.end()) {
            if (it->second.channel) it->second.channel->stop();
            it->second.channel = nullptr;
        }
    }

    void StopAllSFX()
    {
        for (auto c : g_ChannelsSFX) if(c) c->stop();
        g_ChannelsSFX.clear();
        for (auto& [k, v] : g_SfxChannels) if(v.channel) v.channel->stop();
        g_SfxChannels.clear();
        for (auto& [k, v] : g_Inst3D) if(v.ch) v.ch->stop();
        g_Inst3D.clear();
    }

    void StopLastSFX()
    {
        if (g_ChannelsSFX.empty()) return;
        FMOD::Channel* ch = g_ChannelsSFX.back();
        if (ch) ch->stop();
        g_ChannelsSFX.pop_back();
    }

    void SetSFXPitch(float pitch)
    {
        g_PitchSFX = std::clamp(pitch, 0.5f, 2.0f);
    }

    void SetSfxVolume(const std::wstring& key, float volume)
    {
        if (auto it = g_SfxChannels.find(key); it != g_SfxChannels.end() && it->second.channel)
            it->second.channel->setVolume(std::clamp(volume, 0.0f, 1.0f) * g_VolSFX);
    }

    void SetSfxPitch(const std::wstring& key, float pitch)
    {
        if (auto it = g_SfxChannels.find(key); it != g_SfxChannels.end() && it->second.channel)
            it->second.channel->setPitch(std::clamp(pitch, 0.5f, 2.0f) * g_PitchSFX);
    }

    void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& vel,
                     const DirectX::XMFLOAT3& forward, const DirectX::XMFLOAT3& up)
    {
        if (!g_System) return;
        FMOD_VECTOR p = ToFmod(pos);
        FMOD_VECTOR v = ToFmod(vel);
        FMOD_VECTOR f = ToFmod(forward);
        FMOD_VECTOR u = ToFmod(up);
        g_System->set3DListenerAttributes(0, &p, &v, &f, &u);
    }

    void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& vel,
                     const DirectX::XMVECTOR& forward, const DirectX::XMVECTOR& up)
    {
        if (!g_System) return;
        FMOD_VECTOR p = ToFmod(pos);
        FMOD_VECTOR v = ToFmod(vel);
        FMOD_VECTOR f = ToFmod(forward);
        FMOD_VECTOR u = ToFmod(up);
        g_System->set3DListenerAttributes(0, &p, &v, &f, &u);
    }

    bool Play3D(const std::wstring& instanceId, const std::wstring& key,
                const DirectX::XMFLOAT3& pos, float volume, float pitch, bool loop)
    {
        if (!g_System) return false;

        // contains 사용
        if (!g_SoundBank.contains(key)) return false;

        // 이미 재생 중인 인스턴스 확인
        // Loop 사운드는 중복 재생 방지 (기존 것 유지)
        if (loop && g_Inst3D.contains(instanceId))
        {
            bool playing = false;
            if (g_Inst3D[instanceId].ch && g_Inst3D[instanceId].ch->isPlaying(&playing) == FMOD_OK && playing)
            {
                return true; // 이미 재생 중
            }
        }

        FMOD::Channel* channel = nullptr;
        FMOD_RESULT r = g_System->playSound(g_SoundBank[key].fmodSound, g_SfxGroup, true, &channel);
        
        if (!Check(r, "Play3D") || !channel) return false;

        FMOD_VECTOR p = ToFmod(pos);
        FMOD_VECTOR v = { 0, 0, 0 };
        channel->set3DAttributes(&p, &v);
        channel->setVolume(std::clamp(volume, 0.0f, 1.0f));
        channel->setPitch(std::clamp(pitch, 0.5f, 2.0f));
        channel->setMode(loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
        channel->set3DMinMaxDistance(1.0f, 50.0f); // 기본값
        
        // 3D는 중요도가 높으므로 우선순위를 높여도 괜찮지만
        // 여기서도 너무 많이 생성되면 2D 소리가 끊길 수 있으므로 기본값을 사용하는걸로 함
        // 필요하다면 다음처럼 우선순위를 나누세요  ch->setPriority(64); (중간 우선순위)
        
        channel->setPaused(false);

        // Loop나 추적이 필요한 사운드만 맵에 저장
        if (loop || !instanceId.empty())
        {
            g_Inst3D[instanceId] = { channel };
        }

        return true;
    }

    void Stop3D(const std::wstring& instanceId)
    {
        if (auto it = g_Inst3D.find(instanceId); it != g_Inst3D.end())
        {
            if (it->second.ch) it->second.ch->stop();
            g_Inst3D.erase(it);
        }
    }

    void Update3D(const std::wstring& instanceId,
                  const DirectX::XMFLOAT3& pos,
                  float volume,
                  float minDistance,
                  float maxDistance)
    {
        if (auto it = g_Inst3D.find(instanceId); it != g_Inst3D.end() && it->second.ch)
        {
            FMOD_VECTOR p = ToFmod(pos);
            FMOD_VECTOR v = { 0, 0, 0 };
            it->second.ch->set3DAttributes(&p, &v);
            it->second.ch->setVolume(std::clamp(volume, 0.0f, 1.0f));
            it->second.ch->set3DMinMaxDistance(std::max(0.1f, minDistance), std::max(minDistance, maxDistance));
        }
    }

    // 하위 호환성 (기존 코드용)
    bool Play3DInstance(const std::wstring& instanceId, const std::wstring& soundKey, bool loop)
    {
        return Play3D(instanceId, soundKey, DirectX::XMFLOAT3(0, 0, 0), 1.0f, 1.0f, loop);
    }

    void Stop3DInstance(const std::wstring& instanceId)
    {
        Stop3D(instanceId);
    }

    void Update3DInstance(const std::wstring& instanceId,
                          const DirectX::XMFLOAT3& pos,
                          float volume01,
                          float minDist,
                          float maxDist)
    {
        Update3D(instanceId, pos, volume01, minDist, maxDist);
    }
}

