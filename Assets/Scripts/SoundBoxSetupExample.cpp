#include "SoundBoxSetupExample.h"

#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Core/World.h"       // FindGameObject 사용을 위해 필요
#include "Core/Logger.h"      // 로그 출력을 위해 권장
#include "Components/SoundBoxComponent.h"

namespace Alice
{
	REGISTER_SCRIPT(SoundBoxSetupExample);

	void SoundBoxSetupExample::Start()
	{
		// 1. 컴포넌트가 없으면 추가하고 기본값을 세팅
		auto go = gameObject();
		auto* box = go.GetComponent<SoundBoxComponent>();
		if (!box)
			box = &go.AddComponent<SoundBoxComponent>();

		// 2. 프로퍼티 값 적용
		box->soundPath = soundPath;
		box->loop = loop;
		box->minDistance = minDistance;
		box->maxDistance = maxDistance;
		box->boundsMin = boundsMin;
		box->boundsMax = boundsMax;
		box->debugDraw = debugDraw;

		// 3. 타겟 이름으로 오브젝트 찾아서 설정
		if (!targetName.empty())
		{
			if (World* world = GetWorld()) // IScript::GetWorld() 사용
			{
				// 이름으로 게임 오브젝트 검색
				GameObject targetGo = world->FindGameObject(targetName);

				if (targetGo.IsValid())
				{
					// 유효하면 ID를 타겟으로 설정
					box->SetTarget(targetGo.id());
					ALICE_LOG_INFO("SoundBox Target Set: %s", targetName.c_str());
				}
				else
				{
					ALICE_LOG_WARN("SoundBox Target Not Found: %s", targetName.c_str());
				}
			}
		}
	}

	void SoundBoxSetupExample::Update(float dt)
	{
		// 에디터에서 값을 바꾸면 실시간으로 반영
		auto go = gameObject();
		if (auto* box = go.GetComponent<SoundBoxComponent>())
		{
			box->boundsMin = boundsMin;
			box->boundsMax = boundsMax;
			box->debugDraw = debugDraw;
		}
	}
}