#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorCommands.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Rendering/DeferredRenderSystem.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Importing/FbxModel.h"

#include "imgui.h"

#include <algorithm>
#include <DirectXMath.h>

using namespace DirectX;

namespace Alice
{
	void EditorCore::DrawCameraWindow(World& world,
		Camera& camera,
		ForwardRenderSystem& forward,
		DeferredRenderSystem& deferred,
		float& cameraMoveSpeed,
		EntityId& selectedEntity,
		bool& useForwardRendering)
	{
		(void)useForwardRendering;

		// === Camera / Animation (같은 영역, 탭) ===
		if (ImGui::Begin("Camera"))
		{
			if (ImGui::BeginTabBar("##CameraTabs"))
			{
				if (ImGui::BeginTabItem("Camera"))
				{
					if (ImGui::BeginTable("CameraSplit", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
					{
						// 왼쪽 카메라 정보 및 설정
						ImGui::TableNextColumn();

						Alice::ImGuiText(L"카메라 정보");
						ImGui::Separator();

						auto pos = camera.GetPosition();
						ImGui::Text("Position : (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);

						ImGui::Separator();
						Alice::ImGuiText(L"카메라 설정");
						float fov = XMConvertToDegrees(camera.GetFovYRadians());
						float nearP = camera.GetNearPlane();
						float farP = camera.GetFarPlane();
						bool changed = false;

						changed |= ImGui::SliderFloat("FOV (deg)", &fov, 20.0f, 120.0f);
						changed |= ImGui::DragFloat("Near Plane", &nearP, 0.01f, 0.01f, 10.0f, "%.3f");
						changed |= ImGui::DragFloat("Far Plane", &farP, 1.0f, 10.0f, 5000.0f, "%.1f");
						ImGui::SliderFloat("Move Speed", &cameraMoveSpeed, 0.1f, 50.0f, "%.2f");

						if (changed)
						{
							nearP = (std::max)(nearP, 0.01f);
							farP = (std::max)(farP, nearP + 0.1f);
							camera.SetPerspective(XMConvertToRadians(fov), camera.GetAspectRatio(), nearP, farP);
						}

						// 오른쪽 버튼 및 액션
						ImGui::TableNextColumn();
						Alice::ImGuiText(L"기능");
						ImGui::Separator();

						if (ImGui::Button("Place Camera", { -FLT_MIN, 0.0f }))
						{
							EntityId e = world.CreateCamera();
							auto* tc = world.GetComponent<TransformComponent>(e);
							if (!tc) tc = &world.AddComponent<TransformComponent>(e);

							tc->SetPosition(camera.GetPosition());
							tc->SetRotation(camera.GetRotationQuat());
							tc->SetScale(camera.GetScale());

							if (auto* cc = world.GetComponent<CameraComponent>(e))
							{
								cc->SetFov(XMConvertToDegrees(camera.GetFovYRadians()));
								cc->SetNear(camera.GetNearPlane());
								cc->SetFar(camera.GetFarPlane());
							}

							// 커맨드 시스템에 등록하여 실행 취소(Ctrl+Z)가 가능하게 함
							PushCommand(std::make_unique<CreateEntityCommand>(e, "Placed Camera"));

							selectedEntity = e;
							g_SceneDirty = true;
						}

						ImGui::Separator();
						Alice::ImGuiText(L"미리보기");

						const bool hasSelectedCamera =
							(selectedEntity != InvalidEntityId) &&
							(world.GetComponent<CameraComponent>(selectedEntity) != nullptr);

						ID3D11ShaderResourceView* previewSRV = deferred.GetCameraPreviewSRV();
						float previewAspect = deferred.GetCameraPreviewAspect();
						if (!previewSRV)
						{
							previewSRV = forward.GetCameraPreviewSRV();
							previewAspect = forward.GetCameraPreviewAspect();
						}

						ImVec2 avail = ImGui::GetContentRegionAvail();
						const float previewMaxHeight = 140.0f;
						float previewWidth = previewMaxHeight * (previewAspect > 0.0f ? previewAspect : 1.0f);
						float previewHeight = previewMaxHeight;
						if (previewWidth > avail.x && previewWidth > 1.0f)
						{
							const float scale = avail.x / previewWidth;
							previewWidth *= scale;
							previewHeight *= scale;
						}
						ImVec2 previewSize((std::max)(1.0f, previewWidth), (std::max)(1.0f, previewHeight));

						if (hasSelectedCamera && previewSRV)
						{
							ImGui::Image(previewSRV, previewSize);
						}
						else
						{
							ImGui::TextUnformatted(hasSelectedCamera ? "Preview unavailable." : "Select a camera to preview.");
						}

						ImGui::EndTable();
					}
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Animation"))
				{
					if (selectedEntity == InvalidEntityId)
					{
						ImGui::TextUnformatted("No entity selected.");
					}
					else
					{
						SkinnedMeshComponent* skinned = world.GetComponent<SkinnedMeshComponent>(selectedEntity);
						if (!skinned || skinned->meshAssetPath.empty())
						{
							ImGui::TextUnformatted("Selected entity has no SkinnedMesh.");
						}
						else
						{
							std::shared_ptr<SkinnedMeshGPU> mesh;
							if (m_skinnedRegistry)
								mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);

							ImGui::Text("Entity: %u", (unsigned)selectedEntity);
							ImGui::Text("Mesh : %s", skinned->meshAssetPath.c_str());

							if (!mesh || !mesh->sourceModel)
							{
								ImGui::Separator();
								ImGui::TextUnformatted("Animation data is not ready (re-import FBX once).");
								if (ImGui::Button("Re-import Skinned Meshes"))
									EnsureSkinnedMeshesRegistered(world);
							}
							else
							{
								const auto& names = mesh->sourceModel->GetAnimationNames();
								if (names.empty())
								{
									ImGui::TextUnformatted("This mesh has no animations.");
								}
								else
								{
									auto* anim = world.GetComponent<SkinnedAnimationComponent>(selectedEntity);
									if (!anim) anim = &world.AddComponent<SkinnedAnimationComponent>(selectedEntity);

									ImGui::Separator();
									ImGui::Checkbox("Playing", &anim->playing);
									ImGui::SliderFloat("Speed", &anim->speed, 0.0f, 3.0f, "%.2f");

									int clip = anim->clipIndex;
									if (clip < 0) clip = 0;
									if (clip >= (int)names.size()) clip = (int)names.size() - 1;

									if (ImGui::BeginCombo("Clip", names[(size_t)clip].c_str()))
									{
										for (int i = 0; i < (int)names.size(); ++i)
										{
											const bool sel = (i == clip);
											if (ImGui::Selectable(names[(size_t)i].c_str(), sel))
											{
												clip = i;
												anim->clipIndex = i;
												anim->timeSec = 0.0;
											}
											if (sel) ImGui::SetItemDefaultFocus();
										}
										ImGui::EndCombo();
									}
									anim->clipIndex = clip;

									const double dur = mesh->sourceModel->GetClipDurationSec(anim->clipIndex);
									float timeSec = (float)anim->timeSec;
									float durF = (dur > 0.0) ? (float)dur : 0.0f;

									ImGui::BeginDisabled(durF <= 0.0f);
									if (ImGui::SliderFloat("Time (sec)", &timeSec, 0.0f, durF, "%.3f"))
										anim->timeSec = (double)timeSec;
									ImGui::EndDisabled();

									if (ImGui::Button("Stop"))
									{
										anim->playing = false;
										anim->timeSec = 0.0;
									}
									ImGui::SameLine();
									if (ImGui::Button("<<"))
									{
										anim->playing = false;
										anim->timeSec = (std::max)(0.0, anim->timeSec - 0.1);
									}
									ImGui::SameLine();
									if (ImGui::Button(">>"))
									{
										anim->playing = false;
										anim->timeSec = anim->timeSec + 0.1;
									}
								}
							}
						}
					}

					ImGui::EndTabItem();
				}

				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}
}
