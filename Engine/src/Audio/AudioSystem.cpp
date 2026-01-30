#include "Audio/AudioSystem.h"

#include <algorithm>

#include "Audio/SoundManager.h"
#include "Core/Helper.h"
#include "Core/Logger.h"
#include "Components/TransformComponent.h"
#include "Components/CameraComponent.h"

namespace Alice
{
    namespace
    {
        std::wstring ToKeyW(const std::string& key)
        {
            return WStringFromUtf8(key);
        }

        std::wstring MakeInstanceId(EntityId id)
        {
            return L"AudioSource#" + std::to_wstring(static_cast<std::uint64_t>(id));
        }

        std::wstring MakeSoundBoxId(EntityId id)
        {
            return L"SoundBox#" + std::to_wstring(static_cast<std::uint64_t>(id));
        }

        // 검사 위치(checkPos)를 인자로 받도록 수정
        bool IsInsideBox(const SoundBoxComponent& box, const TransformComponent* tr, const DirectX::XMFLOAT3& checkPos)
        {
            DirectX::XMFLOAT3 p = tr ? tr->position : DirectX::XMFLOAT3(0, 0, 0);
            DirectX::XMFLOAT3 s = tr ? tr->scale : DirectX::XMFLOAT3(1, 1, 1);

            float x0 = box.boundsMin.x * s.x + p.x;
            float x1 = box.boundsMax.x * s.x + p.x;
            float y0 = box.boundsMin.y * s.y + p.y;
            float y1 = box.boundsMax.y * s.y + p.y;
            float z0 = box.boundsMin.z * s.z + p.z;
            float z1 = box.boundsMax.z * s.z + p.z;

            const float minX = std::min(x0, x1);
            const float maxX = std::max(x0, x1);
            const float minY = std::min(y0, y1);
            const float maxY = std::max(y0, y1);
            const float minZ = std::min(z0, z1);
            const float maxZ = std::max(z0, z1);

            return (checkPos.x >= minX && checkPos.x <= maxX &&
                    checkPos.y >= minY && checkPos.y <= maxY &&
                    checkPos.z >= minZ && checkPos.z <= maxZ);
        }

        // 검사 위치(checkPos)를 인자로 받도록 수정
        float CenterWeight01(const SoundBoxComponent& box, const TransformComponent* tr, const DirectX::XMFLOAT3& checkPos)
        {
            DirectX::XMFLOAT3 p = tr ? tr->position : DirectX::XMFLOAT3(0, 0, 0);
            DirectX::XMFLOAT3 s = tr ? tr->scale : DirectX::XMFLOAT3(1, 1, 1);

            float x0 = box.boundsMin.x * s.x + p.x;
            float x1 = box.boundsMax.x * s.x + p.x;
            float y0 = box.boundsMin.y * s.y + p.y;
            float y1 = box.boundsMax.y * s.y + p.y;
            float z0 = box.boundsMin.z * s.z + p.z;
            float z1 = box.boundsMax.z * s.z + p.z;

            const float minX = std::min(x0, x1);
            const float maxX = std::max(x0, x1);
            const float minY = std::min(y0, y1);
            const float maxY = std::max(y0, y1);
            const float minZ = std::min(z0, z1);
            const float maxZ = std::max(z0, z1);

            const float cx = (minX + maxX) * 0.5f;
            const float cy = (minY + maxY) * 0.5f;
            const float cz = (minZ + maxZ) * 0.5f;
            const float ex = std::max(1e-6f, (maxX - minX) * 0.5f);
            const float ey = std::max(1e-6f, (maxY - minY) * 0.5f);
            const float ez = std::max(1e-6f, (maxZ - minZ) * 0.5f);

            const float nx = std::fabs(checkPos.x - cx) / ex;
            const float ny = std::fabs(checkPos.y - cy) / ey;
            const float nz = std::fabs(checkPos.z - cz) / ez;
            float u = std::max(nx, std::max(ny, nz));
            u = std::clamp(u, 0.0f, 1.0f);

            float w = 1.0f - u;
            w = std::pow(w, std::max(box.curve, 0.0001f));
            return std::clamp(w, 0.0f, 1.0f);
        }
    }

    void AudioSystem::Update(World& world, double)
    {
        // 리소스가 없어도 Sound::Update는 무조건 호출해야 FMOD가 돌아갑니다.

        if (m_resources)
        {
            // 1. 리스너 업데이트 및 위치 확보 (SoundBox 로직에서 사용)
            DirectX::XMFLOAT3 listenerPos{ 0, 0, 0 };
            UpdateListener(world, listenerPos);

            // 2. 오디오 소스 업데이트
            for (auto [id, src] : world.GetComponents<AudioSourceComponent>())
            {
                if (src.soundPath.empty()) continue;
                if (const auto* tr = world.GetComponent<TransformComponent>(id); tr && !tr->enabled)
                    continue;

                // Runtime 데이터 초기화
                Runtime& rt = m_runtime[id];
                if (rt.key.empty())
                {
                    rt.key = ToKeyW(src.soundKey.empty() ? src.soundPath : src.soundKey);
                    rt.instanceId = MakeInstanceId(id);
                }

                // 리소스 로드 (아직 안됐다면)
                if (!rt.loaded)
                {
                    const auto type = (src.type == AudioType::BGM) ? Sound::Type::BGM : Sound::Type::SFX;
                    rt.loaded = Sound::LoadAuto(*m_resources, rt.key, src.soundPath, type);
                }

                if (!rt.loaded) continue;

                // 재생 요청 처리
                if ((src.playOnStart && !rt.started) || src.requestPlay)
                {
                    src.requestPlay = false; // 플래그 즉시 리셋

                    if (src.is3D)
                    {
                        const auto* tr = world.GetComponent<TransformComponent>(id);
                        DirectX::XMFLOAT3 pos = tr ? tr->position : DirectX::XMFLOAT3{ 0,0,0 };
                        // Loop가 아닐 때는 instanceId를 비워서 Fire-and-forget (중첩 재생 허용)
                        Sound::Play3D(src.loop ? rt.instanceId : L"", rt.key, pos, src.volume, src.pitch, src.loop);
                        rt.playing3D = src.loop;
                    }
                    else
                    {
                        if (src.type == AudioType::BGM)
                        {
                            Sound::SetBGMVolume(src.volume);
                            Sound::PlayBGM(rt.key);
                        }
                        else
                        {
                            Sound::PlaySFX(rt.key, src.volume, src.pitch, src.loop);
                        }
                    }
                    rt.started = true;
                }

                // 정지 요청 처리
                if (src.requestStop)
                {
                    src.requestStop = false;

                    // [중요 수정] 정지 요청 시 자동 시작 옵션도 꺼야 다시 재생되지 않습니다.
                    src.playOnStart = false;

                    if (src.is3D)
                    {
                        Sound::Stop3D(rt.instanceId);
                        rt.playing3D = false;
                    }
                    else
                    {
                        if (src.type == AudioType::BGM) Sound::StopBGM();
                        else Sound::StopSfx(rt.key);
                    }
                    rt.started = false;
                }

                // 3D 위치 동기화 (재생 중인 루프 사운드만)
                if (src.is3D && rt.playing3D && src.loop)
                {
                    if (auto* tr = world.GetComponent<TransformComponent>(id))
                    {
                        Sound::Update3D(rt.instanceId, tr->position, src.volume, src.minDistance, src.maxDistance);
                    }
                }
            }

            // SoundBox 처리 (타겟 엔티티 또는 listener 위치 기준)
            for (auto [id, box] : world.GetComponents<SoundBoxComponent>())
            {
                if (box.soundPath.empty())
                    continue;

                const TransformComponent* boxTr = world.GetComponent<TransformComponent>(id);
                if (boxTr && !boxTr->enabled)
                    continue;

                SoundBoxRuntime& rt = m_soundBoxRuntime[id];
                if (rt.key.empty())
                {
                    rt.key = ToKeyW(box.soundKey.empty() ? box.soundPath : box.soundKey);
                    rt.instanceId = MakeSoundBoxId(id);
                }

                if (!rt.loaded)
                {
                    const auto type = (box.type == SoundBoxType::BGM) ? Sound::Type::BGM : Sound::Type::SFX;
                    if (Sound::LoadAuto(*m_resources, rt.key, box.soundPath, type))
                        rt.loaded = true;
                }

                if (!rt.loaded)
                    continue;

                // 반응할 위치(checkPos) 결정
                DirectX::XMFLOAT3 checkPos = listenerPos; // 기본값: 카메라(리스너)

                if (box.targetEntity != InvalidEntityId)
                {
                    // 타겟 엔티티가 설정되어 있다면 그 엔티티의 위치를 사용
                    if (const auto* targetTr = world.GetComponent<TransformComponent>(box.targetEntity))
                    {
                        if (targetTr->enabled)
                            checkPos = targetTr->position;
                    }
                }

                // 결정된 위치(checkPos)를 기준으로 박스 안인지 체크
                const bool inside = IsInsideBox(box, boxTr, checkPos);

                if (inside && !rt.wasInside && box.playOnEnter)
                {
                    // 소리는 박스 자신의 위치에서 나게 설정
                    const DirectX::XMFLOAT3 srcPos = boxTr ? boxTr->position : DirectX::XMFLOAT3(0, 0, 0);
                    Sound::Play3D(rt.instanceId, rt.key, srcPos, 0.0f, 1.0f, box.loop);
                }
                if (!inside && rt.wasInside && box.stopOnExit)
                {
                    Sound::Stop3D(rt.instanceId);
                }

                rt.wasInside = inside;

                if (inside)
                {
                    // 볼륨 가중치도 checkPos(타겟 위치) 기준으로 계산
                    const float w = CenterWeight01(box, boxTr, checkPos);
                    const float vol = box.edgeVolume + (box.centerVolume - box.edgeVolume) * w;
                    const DirectX::XMFLOAT3 srcPos = boxTr ? boxTr->position : DirectX::XMFLOAT3(0, 0, 0);
                    Sound::Update3D(rt.instanceId, srcPos, vol, box.minDistance, box.maxDistance);
                }
            }

        }

        // FMOD 시스템 업데이트 및 채널 정리 (항상 실행)
        // 리소스 유무와 무관하게 매 프레임 호출하여 채널 정리 및 시스템 업데이트 보장
        Sound::Update();
    }

	void AudioSystem::UpdateListener(World& world, DirectX::XMFLOAT3& outPos)
	{
		using namespace DirectX;

		const TransformComponent* targetTr = nullptr;

		// 1. AudioListenerComponent 찾기
		for (const auto& [id, listener] : world.GetComponents<AudioListenerComponent>())
		{
			if (listener.primary)
			{
				const auto* tr = world.GetComponent<TransformComponent>(id);
				if (tr && tr->enabled)
					targetTr = tr;
				break;
			}
		}

		// 2. 없으면 MainCamera 찾기
		if (!targetTr)
		{
			EntityId camId = world.GetMainCameraEntityId();
			if (camId != InvalidEntityId)
			{
				const auto* tr = world.GetComponent<TransformComponent>(camId);
				if (tr && tr->enabled)
					targetTr = tr;
			}
		}

		// 3. 리스너 설정
		if (targetTr)
		{
			XMMATRIX R = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&targetTr->rotation));
			XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), R);
			XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), R);

			outPos = targetTr->position; // 위치 출력
			Sound::SetListener(targetTr->position, XMFLOAT3(0, 0, 0), forward, up);
		}
		else
		{
			outPos = DirectX::XMFLOAT3{ 0, 0, 0 }; // 기본값
		}
	}

    
}

