#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorCommands.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/DeferredRenderSystem.h"
#include "Runtime/Importing/FbxModel.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Physics/Components/Phy_RigidBodyComponent.h"
#include "Runtime/UI/UIRenderer.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "ImGuizmo.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Alice
{
	using DirectX::Keyboard;

	namespace
	{
		// === ImGuizmo 통합을 위한 어댑터 함수들 ===
		inline DirectX::XMMATRIX BuildRotYPR_Rad(const DirectX::XMFLOAT3& rotation)
		{
			return DirectX::XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
		}

		inline DirectX::XMMATRIX BuildLocalMatrix(const TransformComponent& transform)
		{
			DirectX::XMMATRIX S = DirectX::XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z);
			DirectX::XMMATRIX R = BuildRotYPR_Rad(transform.rotation);
			DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(transform.position.x, transform.position.y, transform.position.z);

			return S * R * T;
		}

		inline DirectX::XMFLOAT3 QuaternionToYPR_Rad(DirectX::FXMVECTOR q)
		{
			DirectX::XMFLOAT4 qq;
			DirectX::XMStoreFloat4(&qq, q);

			const float x = qq.x, y = qq.y, z = qq.z, w = qq.w;

			float sinp = 2.0f * (w * x - y * z);
			float pitch = (std::abs(sinp) >= 1.0f)
				? std::copysign(DirectX::XM_PIDIV2, sinp)
				: std::asin(sinp);

			float siny_cosp = 2.0f * (w * y + x * z);
			float cosy_cosp = 1.0f - 2.0f * (x * x + y * y);
			float yaw = std::atan2(siny_cosp, cosy_cosp);

			float sinr_cosp = 2.0f * (w * z + x * y);
			float cosr_cosp = 1.0f - 2.0f * (x * x + z * z);
			float roll = std::atan2(sinr_cosp, cosr_cosp);

			return DirectX::XMFLOAT3(pitch, yaw, roll);
		}

		inline bool DecomposeLocalMatrix(const DirectX::XMMATRIX& localMatrix, DirectX::XMFLOAT3& position, DirectX::XMFLOAT3& rotation, DirectX::XMFLOAT3& scale)
		{
			DirectX::XMVECTOR s, q, t;
			if (!DirectX::XMMatrixDecompose(&s, &q, &t, localMatrix))
				return false;

			DirectX::XMStoreFloat3(&position, t);
			DirectX::XMStoreFloat3(&scale, s);
			rotation = QuaternionToYPR_Rad(q);
			return true;
		}

		// 스냅 모드 enum
		enum class SnapMode {
			None = 0,
			Increment = 1,
			Object = 2
		};

		// 오브젝트 스냅 타입 enum (Blender 스타일)
		enum class ObjectSnapType {
			Center = 0,  // 중심점 (Transform position)
			Vertex = 1,  // 버텍스
			Edge = 2,    // 엣지
			Face = 3     // 면
		};
	}

	void EditorCore::DrawGameViewportWindow(World& world,
		Camera& camera,
		ForwardRenderSystem& forward,
		DeferredRenderSystem& deferred,
		EntityId& selectedEntity,
		ViewportPicker& picker,
		float& cameraMoveSpeed,
		bool& useForwardRendering,
		bool& isPlaying,
		int& shadingMode,
		bool& useFillLight)
	{
		// === Game ===
		if (!ImGui::Begin("Game"))
		{
			ImGui::End();
			return;
		}

		ImGuiIO& io = ImGui::GetIO();
		const bool isTextInputActive = io.WantTextInput || ImGui::IsAnyItemActive();

		// Gizmo 및 스냅 관련 변수 (뷰창과 인스펙터에서 공유)
		static ImGuizmo::OPERATION gizmoOp = ImGuizmo::TRANSLATE;
		static ImGuizmo::MODE gizmoMode = ImGuizmo::WORLD; // 기본값: WORLD 모드
		static SnapMode snapMode = SnapMode::None;
		static ObjectSnapType objectSnapType = ObjectSnapType::Center;
		static bool gizmoSnap = false; // 레거시 호환성 (Increment 모드와 동일)
		static DirectX::XMFLOAT3 snapTranslation = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
		static float snapRotation = 15.0f; // degrees
		static float snapScale = 1.0f;
		static float objectSnapDistance = 0.5f; // 오브젝트 스냅 거리
		static int viewMode = 0; // 0: Scene, 1: Decal DBuffer

		// 키보드 단축키로 Gizmo 모드 변경 (InputSystem 사용)
		// 텍스트 입력 중이 아닐 때만 단축키 작동
		if (m_inputSystem && !isTextInputActive)
		{
			using namespace DirectX;
			if (m_inputSystem->IsKeyPressed(Keyboard::Keys::W)) gizmoOp = ImGuizmo::TRANSLATE;
			if (m_inputSystem->IsKeyPressed(Keyboard::Keys::E)) gizmoOp = ImGuizmo::ROTATE;
			if (m_inputSystem->IsKeyPressed(Keyboard::Keys::R)) gizmoOp = ImGuizmo::SCALE;
			if (m_inputSystem->IsKeyPressed(Keyboard::Keys::X))
			{
				gizmoMode = (gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
			}
		}

		// Gizmo Operation 선택 버튼
		if (ImGui::RadioButton("Translate (W)", gizmoOp == ImGuizmo::TRANSLATE))
			gizmoOp = ImGuizmo::TRANSLATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate (E)", gizmoOp == ImGuizmo::ROTATE))
			gizmoOp = ImGuizmo::ROTATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale (R)", gizmoOp == ImGuizmo::SCALE))
			gizmoOp = ImGuizmo::SCALE;

		// Gizmo Mode 선택 (Scale 모드에서는 World만 지원)
		if (gizmoOp != ImGuizmo::SCALE)
		{
			ImGui::SameLine();
			if (ImGui::RadioButton("Local (X)", gizmoMode == ImGuizmo::LOCAL))
				gizmoMode = ImGuizmo::LOCAL;
			ImGui::SameLine();
			if (ImGui::RadioButton("World (X)", gizmoMode == ImGuizmo::WORLD))
				gizmoMode = ImGuizmo::WORLD;
		}
		else
		{
			gizmoMode = ImGuizmo::LOCAL; // Scale은 항상 Local
		}

		// 게임 상태 표시 (한 줄, 색상 포함)
		ImGui::SameLine();
		ImGui::Text(" | ");
		ImGui::SameLine();
		ImVec4 stateColor = isPlaying ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		ImGui::TextColored(stateColor, "%s", isPlaying ? "Playing" : "Stopped");

		// 스냅 토글 버튼
		ImGui::SameLine();
		ImGui::Text(" | ");
		ImGui::SameLine();
		static bool showSnapSettings = false;
		if (ImGui::SmallButton("Snap"))
		{
			showSnapSettings = !showSnapSettings;
		}

		// View Mode (Scene / DBuffer)
		{
			const char* viewItems[] = { "Scene", "Decal DBuffer" };
			const bool canShowDBuffer = !useForwardRendering;
			if (!canShowDBuffer && viewMode == 1)
			{
				viewMode = 0;
			}

			ImGui::SameLine();
			ImGui::Text(" | ");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			ImGui::BeginDisabled(!canShowDBuffer);
			ImGui::Combo("View", &viewMode, viewItems, IM_ARRAYSIZE(viewItems));
			ImGui::EndDisabled();

			if (!canShowDBuffer)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(Deferred only)");
			}
		}

		// 스냅 설정 UI (토글이 켜져 있을 때만 표시)
		if (showSnapSettings)
		{
			ImGui::Separator();
			ImGui::Text("Snap Settings");

			// Snap 모드 선택
			ImGui::Text("Snap Mode:");
			const char* snapModeItems[] = { "None", "Increment", "Object" };
			int snapModeInt = static_cast<int>(snapMode);
			if (ImGui::Combo("##SnapMode", &snapModeInt, snapModeItems, IM_ARRAYSIZE(snapModeItems)))
			{
				snapMode = static_cast<SnapMode>(snapModeInt);
				gizmoSnap = (snapMode == SnapMode::Increment); // 레거시 호환성
			}

			// Snap 값 설정
			if (snapMode == SnapMode::Increment)
			{
				ImGui::Indent();
				switch (gizmoOp)
				{
				case ImGuizmo::TRANSLATE:
					ImGui::DragFloat3("Snap Translation", &snapTranslation.x, 0.1f, 0.01f, 100.0f);
					break;
				case ImGuizmo::ROTATE:
					ImGui::DragFloat("Snap Rotation (deg)", &snapRotation, 1.0f, 1.0f, 90.0f);
					break;
				case ImGuizmo::SCALE:
					ImGui::DragFloat("Snap Scale", &snapScale, 0.1f, 0.1f, 10.0f);
					break;
				default:
					break;
				}
				ImGui::Unindent();
			}
			else if (snapMode == SnapMode::Object)
			{
				ImGui::Indent();
				ImGui::DragFloat("Snap Distance", &objectSnapDistance, 0.1f, 0.01f, 10.0f);

				// 오브젝트 스냅 타입 선택 (Blender 스타일)
				ImGui::Text("Snap To:");
				const char* snapTypeItems[] = { "Center", "Vertex", "Edge", "Face" };
				int snapTypeInt = static_cast<int>(objectSnapType);
				if (ImGui::Combo("##SnapType", &snapTypeInt, snapTypeItems, IM_ARRAYSIZE(snapTypeItems)))
				{
					objectSnapType = static_cast<ObjectSnapType>(snapTypeInt);
				}

				// 현재 스냅 타입 설명
				switch (objectSnapType)
				{
				case ObjectSnapType::Center:
					ImGui::TextDisabled("Snap to object center");
					break;
				case ObjectSnapType::Vertex:
					ImGui::TextDisabled("Snap to mesh vertices (if available)");
					break;
				case ObjectSnapType::Edge:
					ImGui::TextDisabled("Snap to mesh edges (if available)");
					break;
				case ObjectSnapType::Face:
					ImGui::TextDisabled("Snap to mesh face centers (if available)");
					break;
				}
				ImGui::Unindent();
			}
			ImGui::Separator();
		}

		// 에디터 뷰포트는 톤매핑 완료(LDR) 텍스처를 표시해야 정상 색감이 나옵니다.
		ID3D11ShaderResourceView* sceneSRV = nullptr;
		float sceneWidth = 0.0f;
		float sceneHeight = 0.0f;

		if (useForwardRendering)
		{
			sceneSRV = forward.GetViewportSRV();
			sceneWidth = static_cast<float>(forward.GetSceneWidth());
			sceneHeight = static_cast<float>(forward.GetSceneHeight());
		}
		else
		{
			sceneSRV = deferred.GetViewportSRV();
			sceneWidth = static_cast<float>(deferred.GetSceneWidth());
			sceneHeight = static_cast<float>(deferred.GetSceneHeight());
		}

		if (!useForwardRendering && viewMode == 1)
		{
			if (ID3D11ShaderResourceView* decalSrv = deferred.GetDecalDBufferSRV())
			{
				sceneSRV = decalSrv;
				sceneWidth = static_cast<float>(deferred.GetSceneWidth());
				sceneHeight = static_cast<float>(deferred.GetSceneHeight());
			}
		}

		if (sceneSRV)
		{
			ImVec2 avail = ImGui::GetContentRegionAvail();
			ImVec2 size = avail;

			if (sceneWidth > 0.0f && sceneHeight > 0.0f)
			{
				const float aspectScene = sceneWidth / sceneHeight;
				const float aspectAvail = (avail.y > 0.0f) ? (avail.x / avail.y) : aspectScene;

				if (aspectAvail > aspectScene)
				{
					size.x = avail.y * aspectScene;
					size.y = avail.y;
				}
				else
				{
					size.x = avail.x;
					size.y = avail.x / aspectScene;
				}
			}

			// Image를 그린다
			ImGui::Image(sceneSRV, size);

			// 이미지가 화면에 그려진 사각형(픽셀) - Image 호출 직후에만 유효
			ImVec2 imgMin = ImGui::GetItemRectMin();
			ImVec2 imgMax = ImGui::GetItemRectMax();
			ImVec2 imgSize = ImGui::GetItemRectSize();

			if (m_aliceUIRenderer && m_hwnd && imgSize.x > 0.0f && imgSize.y > 0.0f && sceneWidth > 0.0f && sceneHeight > 0.0f)
			{
				// ViewportsEnable 활성 시 GetItemRectMin()/GetMousePos()는 메인 창 클라이언트 좌표가 아니라
				// 데스크톱 절대좌표다. Game 패널이 메인 창에 도킹돼 있으면 이 창의 뷰포트가 곧 메인 뷰포트라
				// PlatformHandle == m_hwnd라서 문제가 없지만, Game 패널을 별도 OS 창으로 분리하면
				// m_hwnd(캐시된 메인 창) 고정 사용 시 ScreenToClient가 엉뚱한 창 기준으로 변환되어
				// SetScreenInputRect가 두 창의 화면 위치 차이만큼 어긋난다. 현재 그려지고 있는 Game 창의
				// 실제 HWND(GetWindowViewport()->PlatformHandle)를 사용해 도킹/분리 양쪽을 모두 정확히 처리한다.
				HWND targetHwnd = m_hwnd;
				if (ImGuiViewport* gameViewport = ImGui::GetWindowViewport())
				{
					if (HWND viewportHwnd = static_cast<HWND>(gameViewport->PlatformHandle))
						targetHwnd = viewportHwnd;
				}

				POINT p = { static_cast<LONG>(imgMin.x), static_cast<LONG>(imgMin.y) };
				::ScreenToClient(targetHwnd, &p);
				m_aliceUIRenderer->SetScreenInputRect(
					static_cast<float>(p.x),
					static_cast<float>(p.y),
					imgSize.x,
					imgSize.y,
					sceneWidth,
					sceneHeight);

				ImVec2 mousePos = ImGui::GetMousePos();
				const float u = (mousePos.x - imgMin.x) / imgSize.x;
				const float v = (mousePos.y - imgMin.y) / imgSize.y;
				const float mx = u * sceneWidth;
				const float my = v * sceneHeight;
				m_aliceUIRenderer->SetScreenMouseOverride(mx, my);
			}

			// 프리팹 드래그앤드롭: 뷰포트 이미지 위에 드롭 타겟 추가
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
				{
					const char* pathStr = static_cast<const char*>(payload->Data);
					std::filesystem::path droppedPath(pathStr);
					std::string ext = droppedPath.extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

					if (ext == ".prefab")
					{
						// 마우스 위치를 이미지 내 상대 좌표(0~1)로 변환
						ImVec2 mousePos = ImGui::GetMousePos();
						float u = (mousePos.x - imgMin.x) / imgSize.x;
						float v = (mousePos.y - imgMin.y) / imgSize.y;

						// 이미지 영역 내에 있는지 확인
						if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)
						{
							// NDC 좌표로 변환
							const float ndcX = 2.0f * u - 1.0f;
							const float ndcY = 1.0f - 2.0f * v;

							// 카메라에서 레이를 쏴서 월드 좌표 계산
							using namespace DirectX;
							XMMATRIX viewXM = camera.GetViewMatrix();
							XMMATRIX projXM = camera.GetProjectionMatrix();
							XMMATRIX invViewProj = XMMatrixInverse(nullptr, XMMatrixMultiply(viewXM, projXM));

							// 카메라 앞 일정 거리(5미터)에 배치
							XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
							XMVECTOR farPoint = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

							nearPoint = XMVector3TransformCoord(nearPoint, invViewProj);
							farPoint = XMVector3TransformCoord(farPoint, invViewProj);

							XMVECTOR dirWorld = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));
							XMFLOAT3 camPos = camera.GetPosition();
							XMVECTOR originWorld = XMLoadFloat3(&camPos);

							// 카메라 앞 5미터 위치에 배치
							const float distance = 5.0f;
							XMVECTOR spawnPos = XMVectorAdd(originWorld, XMVectorScale(dirWorld, distance));

							XMFLOAT3 spawnPosition;
							XMStoreFloat3(&spawnPosition, spawnPos);

							// 프리팹 인스턴스화
							EntityId e = Alice::Prefab::InstantiateFromFile(world, droppedPath);
							if (e != InvalidEntityId)
							{
								if (auto* transform = world.GetComponent<TransformComponent>(e))
								{
									transform->position = spawnPosition;
									world.MarkTransformDirty(e);
								}
								selectedEntity = e;
								g_SceneDirty = true;
							}
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			// ImGuizmo를 사용하여 선택된 엔티티 조작 (재생 중이 아닐 때만)
			if (!isPlaying && selectedEntity != InvalidEntityId)
			{
				if (TransformComponent* transform = world.GetComponent<TransformComponent>(selectedEntity))
				{
					// Gizmo 조작 시작/종료 감지를 위한 static 변수
					static bool wasUsingGizmo = false;
					static EntityId lastGizmoEntity = InvalidEntityId;
					static TransformCommand::TransformData gizmoStartTransform;

					// View/Proj 행렬 준비 (XMFLOAT4X4로 변환)
					DirectX::XMMATRIX viewXM = camera.GetViewMatrix();
					DirectX::XMMATRIX projXM = camera.GetProjectionMatrix();

					DirectX::XMFLOAT4X4 viewMatrix, projMatrix;
					DirectX::XMStoreFloat4x4(&viewMatrix, viewXM);
					DirectX::XMStoreFloat4x4(&projMatrix, projXM);

					// ComputeWorldMatrix()로 통일 (런타임과 동일한 규약)
					using namespace DirectX;

					// ComputeWorldMatrix()를 사용하여 월드 행렬 계산 (런타임과 동일)
					XMMATRIX worldMatrixXM = world.ComputeWorldMatrix(selectedEntity);

					// XMMATRIX를 float[16] 배열로 변환 (ImGuizmo 형식: row-major)
					XMFLOAT4X4 worldMatrixFloat4x4;
					XMStoreFloat4x4(&worldMatrixFloat4x4, worldMatrixXM);
					float worldMatrix[16];
					memcpy(worldMatrix, &worldMatrixFloat4x4, sizeof(worldMatrix));

					// ImGuizmo에 직접 포인터 전달
					const float* viewMat = reinterpret_cast<const float*>(viewMatrix.m);
					const float* projMat = reinterpret_cast<const float*>(projMatrix.m);
					float* objMat = worldMatrix;

					// ImGuizmo 설정
					ImGuizmo::SetOrthographic(false);
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					ImGuizmo::SetDrawlist(drawList);
					// SetRect는 실제 이미지가 그려진 사각형(픽셀)을 사용
					// sceneWidth/sceneHeight는 GPU 렌더 타겟 해상도이므로 화면 픽셀과 다를 수 있음
					ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);

					// Snap 값 준비
					float* snap = nullptr;
					float snapValue[3] = { 0, 0, 0 }; // Snap 값을 받을 임시 배열
					bool forceSnap = false;
					// 텍스트 입력 중이 아닐 때만 Ctrl 스냅 작동
					if (m_inputSystem && !isTextInputActive)
					{
						using namespace DirectX;
						forceSnap = m_inputSystem->IsKeyDown(Keyboard::Keys::LeftControl) ||
							m_inputSystem->IsKeyDown(Keyboard::Keys::RightControl);
					}

					// Increment 스냅 모드일 때만 ImGuizmo에 snap 전달
					if (snapMode == SnapMode::Increment && (gizmoSnap || forceSnap))
					{
						if (gizmoOp == ImGuizmo::TRANSLATE)
						{
							snapValue[0] = snapTranslation.x;
							snapValue[1] = snapTranslation.y;
							snapValue[2] = snapTranslation.z;
							snap = snapValue;
						}
						else if (gizmoOp == ImGuizmo::ROTATE)
						{
							snapValue[0] = snapRotation;
							snap = snapValue;
						}
						else if (gizmoOp == ImGuizmo::SCALE)
						{
							snapValue[0] = snapScale;
							snap = snapValue;
						}
					}

					// Gizmo 조작 시작 감지: Manipulate 호출 전에 체크 (시작 시점 감지용)
					if (lastGizmoEntity != selectedEntity)
					{
						// 다른 엔티티로 변경: 상태 리셋
						wasUsingGizmo = false;
						lastGizmoEntity = selectedEntity;
					}

					// Gizmo 조작 (worldMatrix 배열을 직접 넘겨주어 수정되게 함)
					bool manipulated = ImGuizmo::Manipulate(viewMat, projMat, gizmoOp, gizmoMode, objMat, nullptr, snap);

					// Gizmo 조작 시작/종료 감지: Manipulate 호출 후에 체크 (정확한 상태 반영)
					bool isUsingGizmo = ImGuizmo::IsUsing();

					// 조작 시작 감지: false → true
					if (!wasUsingGizmo && isUsingGizmo && lastGizmoEntity == selectedEntity)
					{
						// 조작 시작: 현재 Transform을 old state로 저장
						gizmoStartTransform.position = transform->position;
						gizmoStartTransform.rotation = transform->rotation;
						gizmoStartTransform.scale = transform->scale;
						gizmoStartTransform.enabled = transform->enabled;
						gizmoStartTransform.visible = transform->visible;
					}

					if (manipulated)
					{
						// 조작된 월드 행렬을 로컬 Transform으로 변환
						// ImGuizmo가 반환한 worldMatrix는 조작된 월드 행렬이므로,
						// 부모의 월드 행렬을 역으로 곱해서 로컬 Transform을 추출해야 함
						using namespace DirectX;

						// 조작된 월드 행렬을 XMMATRIX로 변환
						XMMATRIX manipulatedWorldMatrix = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(worldMatrix));

						// 부모의 월드 행렬 계산 (부모가 있는 경우)
						XMMATRIX parentWorldMatrix = XMMatrixIdentity();
						if (transform->parent != InvalidEntityId)
						{
							parentWorldMatrix = world.ComputeWorldMatrix(transform->parent);
						}

						// 부모의 월드 행렬을 역으로 곱해서 로컬 행렬 추출 (row-vector 컨벤션)
						XMMATRIX parentWorldMatrixInv = XMMatrixInverse(nullptr, parentWorldMatrix);

						// 오브젝트 스냅 모드: 다른 엔티티에 스냅 (스냅이 있으면 월드 행렬 수정)
						XMMATRIX finalWorldMatrix = manipulatedWorldMatrix;
						if (snapMode == SnapMode::Object && gizmoOp == ImGuizmo::TRANSLATE)
						{
							// 오브젝트 스냅 모드용 위치 (월드 공간)
							XMFLOAT3 worldPosition;
							XMStoreFloat3(&worldPosition, XMVector3TransformCoord(XMVectorZero(), manipulatedWorldMatrix));
							XMVECTOR currentPos = XMLoadFloat3(&worldPosition);

							float minDistance = objectSnapDistance;
							XMFLOAT3 snappedPosition = worldPosition;
							bool foundSnap = false;

							// 모든 엔티티를 순회하며 가장 가까운 위치 찾기
							for (auto&& [eid, otherTransform] : world.GetComponents<TransformComponent>())
							{
								if (eid == selectedEntity) continue; // 자기 자신은 제외

								// 스냅 타입에 따라 타겟 위치 결정
								// 초기값: 월드 위치로 설정 (폴백용)
								XMMATRIX otherWorld = world.ComputeWorldMatrix(eid);
								XMVECTOR bestSnapPos = otherWorld.r[3]; // 월드 위치 (translation 부분)
								float bestSnapDist = objectSnapDistance;
								bool hasMeshSnap = false;

								switch (objectSnapType)
								{
								case ObjectSnapType::Center:
									// 중심점 스냅: 월드 위치 계산 (이미 bestSnapPos에 설정됨)
								{
									XMVECTOR diff = currentPos - bestSnapPos;
									float dist = XMVectorGetX(XMVector3Length(diff));
									if (dist < bestSnapDist)
									{
										bestSnapDist = dist;
										hasMeshSnap = true;
									}
								}
								break;

								case ObjectSnapType::Vertex:
								case ObjectSnapType::Edge:
								case ObjectSnapType::Face:
								{
									// 메시 데이터 접근하여 버텍스/엣지/면 스냅
									if (auto* skinned = world.GetComponent<SkinnedMeshComponent>(eid))
									{
										if (m_skinnedRegistry && !skinned->meshAssetPath.empty())
										{
											auto mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);
											if (mesh && mesh->sourceModel)
											{
												// 어댑터 함수를 사용하여 월드 행렬 계산
												XMMATRIX worldMatrix = world.ComputeWorldMatrix(eid);

												const auto& vertices = mesh->sourceModel->GetCPUVertices();
												const auto& indices = mesh->sourceModel->GetCPUIndices();

												if (!vertices.empty())
												{
													if (objectSnapType == ObjectSnapType::Vertex)
													{
														// 버텍스 스냅: 모든 버텍스를 월드 공간으로 변환
														for (const auto& vert : vertices)
														{
															XMVECTOR localPos = XMLoadFloat3(&vert.pos);
															XMVECTOR worldPos = XMVector3TransformCoord(localPos, worldMatrix);

															XMVECTOR diff = currentPos - worldPos;
															float dist = XMVectorGetX(XMVector3Length(diff));

															if (dist < bestSnapDist)
															{
																bestSnapDist = dist;
																bestSnapPos = worldPos;
																hasMeshSnap = true;
															}
														}
													}
													else if (objectSnapType == ObjectSnapType::Edge && !indices.empty())
													{
														// 엣지 스냅: 인덱스를 사용해 엣지 중점 계산
														// 삼각형 리스트 가정 (3개씩)
														for (size_t i = 0; i < indices.size(); i += 3)
														{
															if (i + 2 >= indices.size()) break;

															uint32_t i0 = indices[i];
															uint32_t i1 = indices[i + 1];
															uint32_t i2 = indices[i + 2];

															if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
																continue;

															// 삼각형의 3개 엣지
															XMVECTOR v0 = XMVector3TransformCoord(XMLoadFloat3(&vertices[i0].pos), worldMatrix);
															XMVECTOR v1 = XMVector3TransformCoord(XMLoadFloat3(&vertices[i1].pos), worldMatrix);
															XMVECTOR v2 = XMVector3TransformCoord(XMLoadFloat3(&vertices[i2].pos), worldMatrix);

															// 각 엣지의 중점
															XMVECTOR edgeMidpoints[3] = {
																(v0 + v1) * 0.5f,
																(v1 + v2) * 0.5f,
																(v2 + v0) * 0.5f
															};

															for (int e = 0; e < 3; ++e)
															{
																XMVECTOR diff = currentPos - edgeMidpoints[e];
																float dist = XMVectorGetX(XMVector3Length(diff));

																if (dist < bestSnapDist)
																{
																	bestSnapDist = dist;
																	bestSnapPos = edgeMidpoints[e];
																	hasMeshSnap = true;
																}
															}
														}
													}
													else if (objectSnapType == ObjectSnapType::Face && !indices.empty())
													{
														// 면 스냅: 삼각형 중심 계산
														for (size_t i = 0; i < indices.size(); i += 3)
														{
															if (i + 2 >= indices.size()) break;

															uint32_t i0 = indices[i];
															uint32_t i1 = indices[i + 1];
															uint32_t i2 = indices[i + 2];

															if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
																continue;

															XMVECTOR v0 = XMVector3TransformCoord(XMLoadFloat3(&vertices[i0].pos), worldMatrix);
															XMVECTOR v1 = XMVector3TransformCoord(XMLoadFloat3(&vertices[i1].pos), worldMatrix);
															XMVECTOR v2 = XMVector3TransformCoord(XMLoadFloat3(&vertices[i2].pos), worldMatrix);

															// 삼각형 중심 (3개 버텍스의 평균)
															XMVECTOR faceCenter = (v0 + v1 + v2) / 3.0f;

															XMVECTOR diff = currentPos - faceCenter;
															float dist = XMVectorGetX(XMVector3Length(diff));

															if (dist < bestSnapDist)
															{
																bestSnapDist = dist;
																bestSnapPos = faceCenter;
																hasMeshSnap = true;
															}
														}
													}
												}
											}
										}
									}

									// 메시가 없으면 중심점으로 폴백 (이미 bestSnapPos에 월드 위치 설정됨)
									if (!hasMeshSnap)
									{
										hasMeshSnap = true;
									}
									break;
								}
								}

								if (hasMeshSnap)
								{
									XMVECTOR diff = currentPos - bestSnapPos;
									float distance = XMVectorGetX(XMVector3Length(diff));

									if (distance < minDistance)
									{
										minDistance = distance;
										XMStoreFloat3(&snappedPosition, bestSnapPos);
										foundSnap = true;
									}
								}
							}

							if (foundSnap)
							{
								// 스냅된 월드 위치를 최종 월드 행렬에 반영
								finalWorldMatrix.r[3] = XMVectorSet(snappedPosition.x, snappedPosition.y, snappedPosition.z, 1.0f);
							}
						}

						// 최종 월드 행렬을 로컬 행렬로 변환 (스냅 적용 여부와 관계없이 한 번만)
						XMMATRIX localMatrix = finalWorldMatrix * parentWorldMatrixInv;

						// 어댑터 함수를 사용하여 로컬 행렬을 TRS로 분해
						XMFLOAT3 newPosition, newRotation, newScale;
						if (DecomposeLocalMatrix(localMatrix, newPosition, newRotation, newScale))
						{
							if (gizmoOp == ImGuizmo::TRANSLATE)
							{
								transform->position = newPosition;
							}
							else if (gizmoOp == ImGuizmo::ROTATE)
							{
								transform->rotation = newRotation;  // (x=pitch, y=yaw, z=roll) 라디안
							}
							else if (gizmoOp == ImGuizmo::SCALE)
							{
								transform->scale = newScale;
							}
						}

						// ImGuizmo로 Transform이 변경되었고 물리 컴포넌트가 있으면 텔레포트 자동 활성화
						if (auto* rigidBody = world.GetComponent<Phy_RigidBodyComponent>(selectedEntity))
						{
							rigidBody->teleport = true;
						}
						if (auto* cct = world.GetComponent<Phy_CCTComponent>(selectedEntity))
						{
							cct->teleport = true;
						}
						world.MarkTransformDirty(selectedEntity);
						g_SceneDirty = true;
					}

					// 조작 종료 감지: true → false
					if (wasUsingGizmo && !isUsingGizmo && lastGizmoEntity == selectedEntity)
					{
						// 조작 종료: TransformCommand push
						TransformCommand::TransformData newTransform;
						newTransform.position = transform->position;
						newTransform.rotation = transform->rotation;
						newTransform.scale = transform->scale;
						newTransform.enabled = transform->enabled;
						newTransform.visible = transform->visible;

						// Transform이 실제로 변경되었는지 확인 (float 비교는 epsilon 사용)
						constexpr float kFloatEpsilon = 1e-6f;
						auto FloatNotEqual = [](float a, float b) { return std::fabs(a - b) > kFloatEpsilon; };

						bool hasChanged =
							FloatNotEqual(gizmoStartTransform.position.x, newTransform.position.x) ||
							FloatNotEqual(gizmoStartTransform.position.y, newTransform.position.y) ||
							FloatNotEqual(gizmoStartTransform.position.z, newTransform.position.z) ||
							FloatNotEqual(gizmoStartTransform.rotation.x, newTransform.rotation.x) ||
							FloatNotEqual(gizmoStartTransform.rotation.y, newTransform.rotation.y) ||
							FloatNotEqual(gizmoStartTransform.rotation.z, newTransform.rotation.z) ||
							FloatNotEqual(gizmoStartTransform.scale.x, newTransform.scale.x) ||
							FloatNotEqual(gizmoStartTransform.scale.y, newTransform.scale.y) ||
							FloatNotEqual(gizmoStartTransform.scale.z, newTransform.scale.z) ||
							(gizmoStartTransform.enabled != newTransform.enabled) ||
							(gizmoStartTransform.visible != newTransform.visible);

						if (hasChanged)
						{
							PushCommand(std::make_unique<TransformCommand>(
								selectedEntity, gizmoStartTransform, newTransform));
						}
					}

					// 상태 업데이트
					wasUsingGizmo = isUsingGizmo;
					lastGizmoEntity = selectedEntity;
				}
			}

			// Delete 키 입력 처리: 뷰포트가 포커스를 가지고 있고, 텍스트 입력 중이 아닐 때
			// 기즈모를 잡고 있어도 삭제 가능하도록 처리
			if (selectedEntity != InvalidEntityId &&
				ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				!isTextInputActive &&
				m_inputSystem &&
				m_inputSystem->IsKeyPressed(Keyboard::Keys::Delete))
			{
				// 선택된 엔티티 삭제
				const std::string entityName = world.GetEntityName(selectedEntity);
				PushCommand(std::make_unique<DestroyEntityCommand>(selectedEntity, entityName, world));
				world.DestroyEntity(selectedEntity);
				selectedEntity = InvalidEntityId;
				g_SceneDirty = true;
			}

			// 엔티티 선택 (Gizmo 위에 있지 않을 때만)
			// 최종 빌드(Release)에서는 뷰포트 피커가 작동하지 않도록 함
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				// Gizmo 위에 있지 않고 사용 중이 아닐 때만 선택 처리
				if (!ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
				{
					const ImVec2 mousePos = ImGui::GetIO().MousePos;

					//피킹은 실제 이미지 사각형(imgMin, imgSize)을 기준으로 계산
					// imagePos나 size를 사용하면 레터박스/패딩 때문에 위치가 어긋남
					const float localX = mousePos.x - imgMin.x;
					const float localY = mousePos.y - imgMin.y;

					if (localX >= 0.0f && localX <= imgSize.x &&
						localY >= 0.0f && localY <= imgSize.y)
					{
						// UV 좌표를 실제 이미지 크기 기준으로 계산
						const float u = (imgSize.x > 0.0f) ? (localX / imgSize.x) : 0.0f;
						const float v = (imgSize.y > 0.0f) ? (localY / imgSize.y) : 0.0f;
						EntityId hit = picker.Pick(world, camera, m_skinnedRegistry, u, v);
						selectedEntity = hit;
					}
				}
			}
		}
		else
		{
			Alice::ImGuiText("씬 텍스처가 아직 준비되지 않았습니다.");
		}
		ImGui::End();
	}
}
