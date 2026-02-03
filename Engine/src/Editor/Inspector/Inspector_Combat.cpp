#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/HurtboxComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Importing/FbxModel.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Alice
{
	void EditorCore::DrawInspectorAttackDriver(World& world, const EntityId& _selectedEntity)
	{
		if (auto* driver = world.GetComponent<AttackDriverComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Attack Driver", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##AttackDriverRemove"))
				{
					world.RemoveComponent<AttackDriverComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				std::uint64_t traceGuid = driver->traceGuid;
				if (ImGui::InputScalar("Trace GUID", ImGuiDataType_U64, &traceGuid))
				{
					driver->traceGuid = traceGuid;
					driver->traceCached = InvalidEntityId;
					changed = true;
				}

				// Trace entity picker (WeaponTrace 보유 엔티티 위주)
				{
					EntityId resolved = (driver->traceGuid != 0) ? world.FindEntityByGuid(driver->traceGuid) : InvalidEntityId;
					std::string preview = "(self)";
					if (driver->traceGuid != 0)
					{
						if (resolved != InvalidEntityId)
						{
							std::string name = world.GetEntityName(resolved);
							if (name.empty()) name = "Entity " + std::to_string(resolved);
							preview = name + " (" + std::to_string(driver->traceGuid) + ")";
						}
						else
						{
							preview = std::to_string(driver->traceGuid);
						}
					}

					if (ImGui::BeginCombo("Trace (pick entity)", preview.c_str()))
					{
						const bool selSelf = (driver->traceGuid == 0);
						if (ImGui::Selectable("(self)", selSelf))
						{
							driver->traceGuid = 0;
							driver->traceCached = InvalidEntityId;
							changed = true;
						}
						if (selSelf)
							ImGui::SetItemDefaultFocus();

						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							if (!world.GetComponent<WeaponTraceComponent>(eid))
								continue;

							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == driver->traceGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								driver->traceGuid = idc.guid;
								driver->traceCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				// Clip picker (SkinnedMesh animation list)
				{
					std::vector<std::string> clipNames;
					const auto* animComp = world.GetComponent<AdvancedAnimationComponent>(_selectedEntity);
					if (m_skinnedRegistry)
					{
						if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(_selectedEntity))
						{
							if (!skinned->meshAssetPath.empty())
							{
								std::shared_ptr<SkinnedMeshGPU> mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);
								if (mesh && mesh->sourceModel)
									clipNames = mesh->sourceModel->GetAnimationNames();
							}
						}
					}

					ImGui::Separator();
					ImGui::Text("Clip Timings");
					if (ImGui::Button("+ Add Clip"))
					{
						AttackDriverClip newClip{};
						newClip.type = AttackDriverNotifyType::Attack;
						newClip.source = AttackDriverClipSource::Explicit;
						driver->clips.emplace_back(std::move(newClip));
						changed = true;
					}

					auto ResolveClipNameForUI = [&](const AttackDriverClip& clip) -> std::string {
						if (!animComp)
							return clip.clipName;

						switch (clip.source)
						{
						case AttackDriverClipSource::BaseA: return animComp->base.clipA;
						case AttackDriverClipSource::BaseB: return animComp->base.clipB;
						case AttackDriverClipSource::UpperA: return animComp->upper.clipA;
						case AttackDriverClipSource::UpperB: return animComp->upper.clipB;
						case AttackDriverClipSource::Additive: return animComp->additive.clip;
						case AttackDriverClipSource::Explicit:
						default: return clip.clipName;
						}
						};

					for (size_t i = 0; i < driver->clips.size(); ++i)
					{
						AttackDriverClip& clip = driver->clips[i];
						ImGui::PushID(static_cast<int>(i));

						const std::string resolvedName = ResolveClipNameForUI(clip);
						const char* clipPreview = resolvedName.empty() ? "(none)" : resolvedName.c_str();
						bool open = ImGui::TreeNode("Clip", "%s [%.2f - %.2f]", clipPreview, clip.startTimeSec, clip.endTimeSec);

						ImGui::SameLine();
						bool moveUp = ImGui::SmallButton("^");
						ImGui::SameLine();
						bool moveDown = ImGui::SmallButton("v");
						ImGui::SameLine();
						bool duplicate = ImGui::SmallButton("Dup");
						ImGui::SameLine();
						bool remove = ImGui::SmallButton("Remove");

						if (moveUp && i > 0)
						{
							std::swap(driver->clips[i - 1], driver->clips[i]);
							changed = true;
							ImGui::PopID();
							if (open) ImGui::TreePop();
							continue;
						}
						if (moveDown && (i + 1) < driver->clips.size())
						{
							std::swap(driver->clips[i + 1], driver->clips[i]);
							changed = true;
							ImGui::PopID();
							if (open) ImGui::TreePop();
							continue;
						}
						if (duplicate)
						{
							driver->clips.insert(driver->clips.begin() + static_cast<ptrdiff_t>(i + 1), clip);
							changed = true;
							ImGui::PopID();
							if (open) ImGui::TreePop();
							continue;
						}
						if (remove)
						{
							driver->clips.erase(driver->clips.begin() + static_cast<ptrdiff_t>(i));
							changed = true;
							ImGui::PopID();
							if (open) ImGui::TreePop();
							continue;
						}

						if (open)
						{
							changed |= ImGui::Checkbox("Enabled", &clip.enabled);

							const char* typeLabels[] = { "Attack", "Dodge", "Guard" };
							int typeIndex = static_cast<int>(clip.type);
							if (ImGui::Combo("Type", &typeIndex, typeLabels, IM_ARRAYSIZE(typeLabels)))
							{
								clip.type = static_cast<AttackDriverNotifyType>(typeIndex);
								changed = true;
							}

							const char* sourceLabels[] = { "Explicit", "Base A", "Base B", "Upper A", "Upper B", "Additive" };
							int sourceIndex = static_cast<int>(clip.source);
							if (ImGui::Combo("Source", &sourceIndex, sourceLabels, IM_ARRAYSIZE(sourceLabels)))
							{
								clip.source = static_cast<AttackDriverClipSource>(sourceIndex);
								changed = true;
							}

							if (clip.source == AttackDriverClipSource::Explicit)
							{
								if (!clipNames.empty())
								{
									if (ImGui::BeginCombo("Clip", clip.clipName.empty() ? "(none)" : clip.clipName.c_str()))
									{
										const bool selNone = clip.clipName.empty();
										if (ImGui::Selectable("(none)", selNone))
										{
											clip.clipName.clear();
											changed = true;
										}
										if (selNone)
											ImGui::SetItemDefaultFocus();

										for (const auto& name : clipNames)
										{
											const bool sel = (clip.clipName == name);
											if (ImGui::Selectable(name.c_str(), sel))
											{
												clip.clipName = name;
												changed = true;
											}
											if (sel)
												ImGui::SetItemDefaultFocus();
										}
										ImGui::EndCombo();
									}
								}
								else
								{
									ImGui::TextDisabled("No animation clips available (SkinnedMesh/FBX not ready).");
								}
							}
							else
							{
								ImGui::Text("Clip: %s", resolvedName.empty() ? "(none)" : resolvedName.c_str());
							}

							changed |= ImGui::DragFloat("Start Time (sec)", &clip.startTimeSec, 0.01f, 0.0f, 60.0f);
							changed |= ImGui::DragFloat("End Time (sec)", &clip.endTimeSec, 0.01f, 0.0f, 60.0f);

							if (clip.endTimeSec < clip.startTimeSec)
							{
								clip.endTimeSec = clip.startTimeSec;
								changed = true;
								ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Warning: End < Start");
							}

							ImGui::TreePop();
						}

						ImGui::PopID();
					}
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}


	void EditorCore::DrawInspectorHurtbox(World& world, const EntityId& _selectedEntity)
	{
		if (auto* hb = world.GetComponent<HurtboxComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Hurtbox", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##HurtboxRemove"))
				{
					world.RemoveComponent<HurtboxComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				std::uint64_t ownerGuid = hb->ownerGuid;
				if (ImGui::InputScalar("Owner GUID", ImGuiDataType_U64, &ownerGuid))
				{
					hb->ownerGuid = ownerGuid;
					hb->ownerCached = InvalidEntityId;

					EntityId resolved = (ownerGuid != 0) ? world.FindEntityByGuid(ownerGuid) : InvalidEntityId;
					if (resolved != InvalidEntityId)
						hb->ownerNameDebug = world.GetEntityName(resolved);
					changed = true;
				}

				if (hb->ownerGuid == 0)
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Owner GUID is 0 -> hurtbox will not resolve");

				// Owner picker
				{
					std::string preview = hb->ownerNameDebug.empty()
						? (hb->ownerGuid != 0 ? std::to_string(hb->ownerGuid) : "(none)")
						: hb->ownerNameDebug;
					if (ImGui::BeginCombo("Owner (pick entity)", preview.c_str()))
					{
						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == hb->ownerGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								hb->ownerGuid = idc.guid;
								hb->ownerNameDebug = world.GetEntityName(eid);
								hb->ownerCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				ImGui::Text("Owner Name: %s", hb->ownerNameDebug.empty() ? "(none)" : hb->ownerNameDebug.c_str());

				uint32_t teamId = hb->teamId;
				if (ImGui::InputScalar("Team Id", ImGuiDataType_U32, &teamId))
				{
					hb->teamId = teamId;
					changed = true;
				}

				uint32_t part = hb->part;
				if (ImGui::InputScalar("Part", ImGuiDataType_U32, &part))
				{
					hb->part = part;
					changed = true;
				}

				changed |= ImGui::DragFloat("Damage Scale", &hb->damageScale, 0.01f, 0.0f, 100.0f);

				if (changed) g_SceneDirty = true;
			}
		}
	}


	void EditorCore::DrawInspectorWeaponTrace(World& world, const EntityId& _selectedEntity)
	{
		if (auto* trace = world.GetComponent<WeaponTraceComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Weapon Trace", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##WeaponTraceRemove"))
				{
					world.RemoveComponent<WeaponTraceComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				std::uint64_t ownerGuid = trace->ownerGuid;
				if (ImGui::InputScalar("Owner GUID", ImGuiDataType_U64, &ownerGuid))
				{
					trace->ownerGuid = ownerGuid;
					trace->ownerCached = InvalidEntityId;
					EntityId resolved = (ownerGuid != 0) ? world.FindEntityByGuid(ownerGuid) : InvalidEntityId;
					if (resolved != InvalidEntityId)
						trace->ownerNameDebug = world.GetEntityName(resolved);
					changed = true;
				}

				if (trace->ownerGuid == 0)
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Owner GUID is 0 -> trace will not run");

				if (ImGui::Button("Pick from selected entity"))
				{
					if (const auto* idc = world.GetComponent<IDComponent>(_selectedEntity))
					{
						trace->ownerGuid = idc->guid;
						trace->ownerCached = _selectedEntity;
						trace->ownerNameDebug = world.GetEntityName(_selectedEntity);
						changed = true;
					}
				}

				// Owner picker
				{
					std::string preview = trace->ownerNameDebug.empty()
						? (trace->ownerGuid != 0 ? std::to_string(trace->ownerGuid) : "(none)")
						: trace->ownerNameDebug;
					if (ImGui::BeginCombo("Owner (pick entity)", preview.c_str()))
					{
						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == trace->ownerGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								trace->ownerGuid = idc.guid;
								trace->ownerNameDebug = world.GetEntityName(eid);
								trace->ownerCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				ImGui::Text("Owner Name: %s", trace->ownerNameDebug.empty() ? "(none)" : trace->ownerNameDebug.c_str());

				std::uint64_t basisGuid = trace->traceBasisGuid;
				if (ImGui::InputScalar("Trace Basis GUID", ImGuiDataType_U64, &basisGuid))
				{
					trace->traceBasisGuid = basisGuid;
					trace->traceBasisCached = InvalidEntityId;
					changed = true;
				}

				if (trace->traceBasisGuid == 0)
					ImGui::TextDisabled("Trace Basis GUID is 0 -> uses self");

				// Trace basis picker
				{
					std::string preview;
					if (trace->traceBasisGuid == 0)
					{
						preview = "(self)";
					}
					else
					{
						EntityId resolved = world.FindEntityByGuid(trace->traceBasisGuid);
						preview = (resolved != InvalidEntityId)
							? world.GetEntityName(resolved)
							: std::to_string(trace->traceBasisGuid);
						if (preview.empty())
							preview = std::to_string(trace->traceBasisGuid);
					}

					if (ImGui::BeginCombo("Trace Basis (pick entity)", preview.c_str()))
					{
						const bool selfSel = (trace->traceBasisGuid == 0);
						if (ImGui::Selectable("(self)", selfSel))
						{
							trace->traceBasisGuid = 0;
							trace->traceBasisCached = InvalidEntityId;
							changed = true;
						}
						if (selfSel)
							ImGui::SetItemDefaultFocus();

						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == trace->traceBasisGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								trace->traceBasisGuid = idc.guid;
								trace->traceBasisCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				changed |= ImGui::Checkbox("Active", &trace->active);
				changed |= ImGui::Checkbox("Debug Draw", &trace->debugDraw);
				changed |= ImGui::DragFloat("Base Damage", &trace->baseDamage, 0.1f, 0.0f, 100000.0f);

				uint32_t teamId = trace->teamId;
				if (ImGui::InputScalar("Team Id", ImGuiDataType_U32, &teamId))
				{
					trace->teamId = teamId;
					changed = true;
				}

				uint32_t attackId = trace->attackInstanceId;
				if (ImGui::InputScalar("Attack Instance Id", ImGuiDataType_U32, &attackId))
				{
					trace->attackInstanceId = attackId;
					changed = true;
				}

				uint32_t targetBits = trace->targetLayerBits;
				if (ImGui::InputScalar("Target Layer Bits", ImGuiDataType_U32, &targetBits))
				{
					trace->targetLayerBits = targetBits;
					changed = true;
				}

				uint32_t queryBits = trace->queryLayerBits;
				if (ImGui::InputScalar("Query Layer Bits", ImGuiDataType_U32, &queryBits))
				{
					trace->queryLayerBits = queryBits;
					changed = true;
				}

				uint32_t subSteps = trace->subSteps;
				if (ImGui::InputScalar("Sub Steps", ImGuiDataType_U32, &subSteps))
				{
					trace->subSteps = std::max(1u, subSteps);
					changed = true;
				}

				ImGui::Separator();
				ImGui::Text("Trace Shapes");
				if (ImGui::Button("Add Shape"))
				{
					trace->shapes.emplace_back();
					changed = true;
				}

				for (size_t i = 0; i < trace->shapes.size(); ++i)
				{
					WeaponTraceShape& shape = trace->shapes[i];
					ImGui::PushID(static_cast<int>(i));

					const char* typeName = (shape.type == WeaponTraceShapeType::Sphere)
						? "Sphere"
						: (shape.type == WeaponTraceShapeType::Capsule ? "Capsule" : "Box");
					const char* namePreview = shape.name.empty() ? "(unnamed)" : shape.name.c_str();
					bool open = ImGui::TreeNode("Shape", "%s [%s]", namePreview, typeName);

					ImGui::SameLine();
					bool moveUp = ImGui::SmallButton("^");
					ImGui::SameLine();
					bool moveDown = ImGui::SmallButton("v");
					ImGui::SameLine();
					bool duplicate = ImGui::SmallButton("Dup");
					ImGui::SameLine();
					bool remove = ImGui::SmallButton("Remove");

					if (moveUp && i > 0)
					{
						std::swap(trace->shapes[i - 1], trace->shapes[i]);
						changed = true;
						ImGui::PopID();
						if (open) ImGui::TreePop();
						continue;
					}
					if (moveDown && (i + 1) < trace->shapes.size())
					{
						std::swap(trace->shapes[i + 1], trace->shapes[i]);
						changed = true;
						ImGui::PopID();
						if (open) ImGui::TreePop();
						continue;
					}
					if (duplicate)
					{
						trace->shapes.insert(trace->shapes.begin() + static_cast<ptrdiff_t>(i + 1), shape);
						changed = true;
						ImGui::PopID();
						if (open) ImGui::TreePop();
						continue;
					}
					if (remove)
					{
						trace->shapes.erase(trace->shapes.begin() + static_cast<ptrdiff_t>(i));
						changed = true;
						ImGui::PopID();
						if (open) ImGui::TreePop();
						continue;
					}

					if (open)
					{
						char nameBuf[256];
						std::snprintf(nameBuf, sizeof(nameBuf), "%.255s", shape.name.c_str());
						if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
						{
							shape.name = nameBuf;
							changed = true;
						}

						changed |= ImGui::Checkbox("Enabled", &shape.enabled);

						const char* typeItems[] = { "Sphere", "Capsule", "Box" };
						int typeIdx = static_cast<int>(shape.type);
						if (ImGui::Combo("Type", &typeIdx, typeItems, IM_ARRAYSIZE(typeItems)))
						{
							shape.type = static_cast<WeaponTraceShapeType>(typeIdx);
							changed = true;
						}

						changed |= ImGui::DragFloat3("Local Pos", &shape.localPos.x, 0.01f);
						changed |= ImGui::DragFloat3("Local Rot (deg)", &shape.localRotDeg.x, 0.5f);

						if (shape.type == WeaponTraceShapeType::Sphere)
						{
							changed |= ImGui::DragFloat("Radius", &shape.radius, 0.01f, 0.0f, 100.0f);
						}
						else if (shape.type == WeaponTraceShapeType::Capsule)
						{
							changed |= ImGui::DragFloat("Radius", &shape.radius, 0.01f, 0.0f, 100.0f);
							changed |= ImGui::DragFloat("Half Height", &shape.capsuleHalfHeight, 0.01f, 0.0f, 100.0f);
						}
						else if (shape.type == WeaponTraceShapeType::Box)
						{
							changed |= ImGui::DragFloat3("Half Extents", &shape.boxHalfExtents.x, 0.01f, 0.0f, 100.0f);
						}

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}
}
