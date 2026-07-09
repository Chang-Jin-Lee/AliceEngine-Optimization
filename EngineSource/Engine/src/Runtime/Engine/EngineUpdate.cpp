#include "Runtime/Engine/EngineImpl.h"
#include "Runtime/ECS/Components/TransformComponent.h"

namespace Alice
{
	void Engine::Impl::UpdateFrame()
	{
		float dt = 0.0f;
		UpdateTimerAndInput(dt);

		const bool updateFromScene = UpdateShouldUpdateFromScene();
		bool sceneChangedThisFrame = false;

		UpdateHandlePlayStartReset();

		if (updateFromScene)
		{
			UpdateSceneAndScript(dt);

			sceneChangedThisFrame = UpdateCommitPendingSceneChanges(dt);

			if (!sceneChangedThisFrame)
			{
				UpdateAttackDriver();

				UpdateEnsurePhysicsWorldIfNeeded();

				const float physicsDt = UpdateResolvePhysicsDelta(dt);

				UpdatePhysicsBridge(physicsDt);
				UpdatePhysicsSim(physicsDt);

				UpdateAnimationAndSockets(dt);
				UpdateCombat(dt);

				UpdateCameraSystems(dt);
				UpdateSyncPrimaryCameraFromWorld();
			}
		}
		else
		{
			// 에디터 자유 카메라는 게임 시간 정지(StopDeltaTime)의 영향을 받지 않아야 한다.
			UpdateEditorFreeCam(m_unscaledDeltaTime);
		}

		UpdateApplyFinalCameraLookAt();
		UpdateUI(dt);
	}

	// =========================
	// Update helpers
	void Engine::Impl::UpdateTimerAndInput(float& outDt)
	{
		m_timer.Tick();
		m_unscaledDeltaTime = m_timer.DeltaTime();
		m_gameDeltaTime = m_stopGameDeltaTime ? 0.0f : m_unscaledDeltaTime;
		outDt = m_gameDeltaTime;
		m_inputSystem.Update(m_unscaledDeltaTime);
		m_animUpdatedThisFrame = false;
	}

	bool Engine::Impl::UpdateShouldUpdateFromScene() const
	{
		if (!m_editorMode)
			return true;
		if (!m_isPlaying)
			return false;
		// 일시정지 중에는 Step 버튼이 준 한 프레임만 진행한다.
		return !m_isPaused || m_stepOneFrame;
	}

	void Engine::Impl::UpdateSceneAndScript(float dt)
	{
		if (m_sceneManager) m_sceneManager->Update(dt);
		m_scriptSystem.Tick(m_world, dt);
	}

	bool Engine::Impl::UpdateCommitPendingSceneChanges(float /*dt*/)
	{
		bool sceneChangedThisFrame = false;

		if (m_scriptSystem.HasPendingSceneRequests() ||
			(m_sceneManager && m_sceneManager->HasPendingSceneChange()))
		{
			if (m_physicsSystem)
				m_physicsSystem->SetPhysicsWorld(nullptr);

			if (auto pwShared = m_world.GetPhysicsWorldShared())
				pwShared->Flush();

			m_physAccum = 0.0f;
			m_physicsEventQueue.clear();

			if (m_scriptSystem.HasPendingSceneRequests())
				m_scriptSystem.CommitSceneRequests(m_world);

			if (m_sceneManager && m_sceneManager->HasPendingSceneChange())
				m_sceneManager->CommitPendingSceneChange(m_world);

			// 씬 로드/전환 시 UI 시간(gTime.x) 리셋 → DieLine 등 시간 기반 쉐이더가 다시 정상 재생되도록
			m_aliceUIRenderer.ResetTime();

			// 새 씬은 새로운 스킨드메시 키를 들여올 수 있으므로,
			// 온디맨드 임포트 시도 캐시를 초기화해 이번 씬에서 다시 시도할 수 있게 한다.
			m_onDemandMeshAttempted.clear();

			sceneChangedThisFrame = true;
			m_skipPhysicsNextFrame = true;
		}

		return sceneChangedThisFrame;
	}

	void Engine::Impl::UpdateAttackDriver()
	{
		m_attackDriverSystem.PreUpdate(m_world);
	}

	void Engine::Impl::UpdateHandlePlayStartReset()
	{
		const bool playJustStarted = (m_editorMode && m_isPlaying && !m_prevIsPlaying);
		if (playJustStarted)
		{
			m_skipPhysicsNextFrame = true;
			m_physAccum = 0.0f;
			m_aliceUIRenderer.ResetTime();
			// 이전 세션이 남긴 게임 시간 정지 상태로 Play가 시작되지 않게 한다.
			m_stopGameDeltaTime = false;
		}

		// Stop 시: ESC 일시정지 메뉴 등 게임 스크립트가 걸어둔 시간 정지가
		// 에디터로 새어 들어와 카메라/시뮬레이션이 굳지 않도록 해제한다.
		const bool playJustStopped = (m_editorMode && !m_isPlaying && m_prevIsPlaying);
		if (playJustStopped)
		{
			m_stopGameDeltaTime = false;
		}
	}

	float Engine::Impl::UpdateResolvePhysicsDelta(float dt)
	{
		float physicsDt = dt;
		if (m_skipPhysicsNextFrame)
		{
			physicsDt = 0.0f;
			m_physAccum = 0.0f;
			m_skipPhysicsNextFrame = false;
		}
		return physicsDt;
	}

	void Engine::Impl::UpdateEnsurePhysicsWorldIfNeeded()
	{
		if (m_physicsSystem && !m_world.GetPhysicsWorld())
		{
			const auto& settingsMap = m_world.GetComponents<Phy_SettingsComponent>();
			if (!settingsMap.empty())
			{
				const auto& settings = settingsMap.begin()->second;
				if (settings.enablePhysics)
					RefreshPhysicsForCurrentWorld();
			}
		}
	}

	void Engine::Impl::UpdatePhysicsBridge(float dt)
	{
		if (m_physicsSystem)
			m_physicsSystem->Update(dt);

	}

	void Engine::Impl::UpdatePhysicsSim(float dt)
	{
		TickPhysics(dt);
	}

	void Engine::Impl::UpdateAnimationAndSockets(float dt)
	{
		m_advancedAnimSystem.Update(m_world, static_cast<double>(dt));
		m_skinnedAnimSystem.Update(m_world, static_cast<double>(dt));
		m_attackDriverSystem.PostUpdate(m_world);
		if (m_physicsSystem)
			m_physicsSystem->ApplyRootMotionDeltas(dt);
		m_socketWorldUpdateSystem.Update(m_world);
		m_socketAttachmentSystem.Update(m_world);
		m_animUpdatedThisFrame = true;

		m_combatSystem.BeginFrame(m_world);

		m_weaponTraceSystem.Update(m_world, dt, &m_combatHitQueue);
		// Script-side combat resolution (same-frame hit processing)
		m_world.SetFrameCombatHits(&m_combatHitQueue);
		m_scriptSystem.PostCombatUpdate(m_world, dt);
		m_world.SetFrameCombatHits(nullptr);
	}

	void Engine::Impl::UpdateCombat(float dt)
	{
		ProcessPhysicsEvents();
		ProcessCombatHits();
		m_combatSystem.Update(m_world, dt);
	}

	void Engine::Impl::UpdateCameraSystems(float dt)
	{
		m_cameraSystem.Update(m_world, m_inputSystem, dt);
	}

	void Engine::Impl::UpdateSyncPrimaryCameraFromWorld()
	{
		EntityId camId = InvalidEntityId;
		for (const auto& [id, cam] : m_world.GetComponents<CameraComponent>())
		{
			if (cam.GetPrimary()) { camId = id; break; }
			if (camId == InvalidEntityId) camId = id;
		}

		if (camId == InvalidEntityId) return;

		auto* camComp = m_world.GetComponent<CameraComponent>(camId);
		if (!camComp) return;

		const Camera& sourceCamera = camComp->GetCamera();

		const float defaultAspect =
			static_cast<float>(m_width == 0 ? 1u : m_width) /
			static_cast<float>(m_height == 0 ? 1u : m_height);
		const float aspect = (camComp->useAspectOverride && camComp->aspectOverride > 0.0f)
			? camComp->aspectOverride : defaultAspect;

		m_camera.SetPerspective(
			sourceCamera.GetFovYRadians(), aspect,
			sourceCamera.GetNearPlane(), sourceCamera.GetFarPlane());

		m_camera.SetPosition(sourceCamera.GetPosition());
		m_camera.SetRotation(sourceCamera.GetRotationQuat());
		m_camera.SetScale(sourceCamera.GetScale());

		m_cameraPosition = sourceCamera.GetPosition();
		const DirectX::XMFLOAT3 rot = sourceCamera.GetRotation();
		m_cameraYawRadians = rot.y;
		m_cameraPitchRadians = rot.x;
	}

	void Engine::Impl::UpdateEditorFreeCam(float dt)
	{
		using namespace DirectX;

		auto& input = m_inputSystem;

		// Editor + Stopped 상태에서만 이 함수가 호출됩니다.
		// F: 선택 오브젝트 앞으로 카메라를 이동(포커스)합니다.
		if (input.IsKeyPressed(Keyboard::F))
		{
			if (m_selectedEntity != InvalidEntityId)
			{
				const TransformComponent* selectedTransform = m_world.GetComponent<TransformComponent>(m_selectedEntity);
				if (selectedTransform)
				{
					const XMMATRIX targetWorld = m_world.ComputeWorldMatrix(m_selectedEntity);
					XMFLOAT3 targetPos{};
					XMStoreFloat3(&targetPos, targetWorld.r[3]);

					const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0.0f);
					const XMVECTOR camForward = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), camRot));

					const float sx = std::fabs(selectedTransform->scale.x);
					const float sy = std::fabs(selectedTransform->scale.y);
					const float sz = std::fabs(selectedTransform->scale.z);
					const float maxScale = (std::max)((std::max)(sx, sy), sz);
					const float focusDistance = std::clamp(maxScale * 2.0f, 1.5f, 20.0f);

					const XMVECTOR targetPosV = XMLoadFloat3(&targetPos);
					const XMVECTOR newCamPos = XMVectorSubtract(targetPosV, XMVectorScale(camForward, focusDistance));
					XMStoreFloat3(&m_cameraPosition, newCamPos);
				}
				else
				{
					// 선택 엔티티가 이미 삭제된 경우 선택 상태 정리
					m_selectedEntity = InvalidEntityId;
				}
			}
		}

		if (!input.IsRightButtonDown())
			return;

		// 우클릭 비행 모드 중 휠 스크롤로 이동 속도를 조절한다 (Unity 에디터와 동일).
		{
			const float wheel = input.GetMouseScrollDelta(); // 1노치 = ±120
			if (wheel != 0.0f)
			{
				const float notches = wheel / 120.0f;
				m_cameraMoveSpeed *= std::pow(1.1f, notches);
				m_cameraMoveSpeed = std::clamp(m_cameraMoveSpeed, 0.1f, 100.0f);
			}
		}

		XMVECTOR moveDir = XMVectorZero();

		if (input.IsKeyDown(Keyboard::W)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, 0, 1, 0));
		if (input.IsKeyDown(Keyboard::S)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, 0, -1, 0));
		if (input.IsKeyDown(Keyboard::D)) moveDir = XMVectorAdd(moveDir, XMVectorSet(1, 0, 0, 0));
		if (input.IsKeyDown(Keyboard::A)) moveDir = XMVectorAdd(moveDir, XMVectorSet(-1, 0, 0, 0));
		if (input.IsKeyDown(Keyboard::E)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, 1, 0, 0));
		if (input.IsKeyDown(Keyboard::Q)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, -1, 0, 0));

		if (!XMVector3Equal(moveDir, XMVectorZero()))
		{
			const XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0);
			const XMVECTOR worldDir = XMVector3Normalize(XMVector3TransformNormal(moveDir, rotMat));
			const XMVECTOR currentPos = XMLoadFloat3(&m_cameraPosition);

			XMStoreFloat3(&m_cameraPosition,
				XMVectorAdd(currentPos, XMVectorScale(worldDir, m_cameraMoveSpeed * dt)));
		}

		const POINT mouseDelta = input.GetMouseDelta();
		m_cameraYawRadians += mouseDelta.x * m_cameraMouseSensitivity;
		m_cameraPitchRadians += mouseDelta.y * m_cameraMouseSensitivity;
	}

	void Engine::Impl::UpdateApplyFinalCameraLookAt()
	{
		using namespace DirectX;

		const float pitchLimit = XMConvertToRadians(89.0f);
		m_cameraPitchRadians = std::clamp(m_cameraPitchRadians, -pitchLimit, pitchLimit);

		const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0);
		const XMVECTOR camForward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), camRot);
		const XMVECTOR camPos = XMLoadFloat3(&m_cameraPosition);

		XMFLOAT3 targetPos;
		XMStoreFloat3(&targetPos, XMVectorAdd(camPos, camForward));

		m_camera.SetLookAt(m_cameraPosition, targetPos, XMFLOAT3(0, 1, 0));
	}

	void Engine::Impl::UpdateUI(float /*dt*/)
	{
		m_aliceUIRenderer.Update(m_world, m_inputSystem, m_camera,
			static_cast<float>(m_width), static_cast<float>(m_height), m_unscaledDeltaTime);

		m_prevIsPlaying = m_isPlaying;
		// Step은 한 프레임만 진행되어야 하므로 매 프레임 말미에 소비(리셋)한다.
		m_stepOneFrame = false;
	}

	//=========================================================
	// 물리 시스템
}
