#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/Resources/SceneFile.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/UI/UICurveAsset.h"
#include <DirectXMath.h>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <cstring>
#include <shellapi.h>
#include "ThirdParty/json/json.hpp"

namespace Alice
{
	namespace
	{
		bool WritePreloadJsonSample(const std::filesystem::path& path)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);

			nlohmann::json j;
			j["preload"] = nlohmann::json::array({
				"Resource/Icon/GameBanner.png",
				"Resource/Fonts/NotoSansKR-Regular.ttf",
				"Resource/Fonts/meiryo.ttc"
				});

			std::ofstream ofs(path);
			if (!ofs.is_open())
				return false;
			ofs << j.dump(4);
			return true;
		}
	}

	void EditorCore::DrawDirectoryNode(World& world,
		EntityId& selectedEntity,
		const std::filesystem::path& path)
	{
		namespace fs = std::filesystem;
		if (!fs::exists(path)) return;

		const bool        isDirectory = fs::is_directory(path);
		const std::string label = path.filename().string();

		ImGuiTreeNodeFlags baseFlags = ImGuiTreeNodeFlags_SpanAvailWidth;

		// 파일/폴더 이름 변경 상태를 관리하는 간단한 정적 상태입니다.
		static bool                 s_renaming = false;
		static std::filesystem::path s_renamingPath;
		static char                 s_renameBuffer[260] = {};
		static bool                 s_renameFocus = false;

		// 공통 Rename 상태: 파일/폴더 모두 이 플래그를 사용합니다.
		const bool isRenamingThis = s_renaming && (s_renamingPath == path);

		if (isDirectory)
		{
			bool open = false;

			// 폴더 이름 영역: 일반 텍스트 또는 인라인 입력 박스
			ImGui::PushID(label.c_str());
			if (isRenamingThis)
			{
				ImGui::SetNextItemWidth(-1.0f);
				if (s_renameFocus)
				{
					ImGui::SetKeyboardFocusHere();
					s_renameFocus = false;
				}

				bool enterPressed = ImGui::InputText(
					"##RenameFolder",
					s_renameBuffer,
					sizeof(s_renameBuffer),
					ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);

				bool finished = enterPressed || ImGui::IsItemDeactivatedAfterEdit();
				if (finished)
				{
					if (std::strlen(s_renameBuffer) > 0)
					{
						fs::path newPath = path.parent_path() / s_renameBuffer;
						if (!fs::exists(newPath))
						{
							std::error_code ec;
							fs::rename(path, newPath, ec);
						}
					}
					s_renaming = false;
				}
			}
			else
			{
				open = ImGui::TreeNodeEx(label.c_str(), baseFlags);
			}
			ImGui::PopID();

			// 디렉터리 노드에 대한 우클릭 컨텍스트 메뉴 (폴더/스크립트/프리팹 생성 등)
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Rename Folder..."))
				{
					std::string folderName = path.filename().string();
					std::memset(s_renameBuffer, 0, sizeof(s_renameBuffer));
					strncpy_s(s_renameBuffer,
						sizeof(s_renameBuffer),
						folderName.c_str(),
						_TRUNCATE);
					s_renaming = true;
					s_renamingPath = path;
					s_renameFocus = true;
					ImGui::CloseCurrentPopup();
				}

				// 새 하위 폴더 생성
				if (ImGui::MenuItem("Create Folder"))
				{
					fs::path newPath = path / "NewFolder";
					int index = 1;
					while (fs::exists(newPath))
					{
						newPath = path / ("NewFolder" + std::to_string(index) + "");
						++index;
					}

					std::error_code ec;
					fs::create_directories(newPath, ec);
				}

				// 새 Material 파일 생성
				if (ImGui::MenuItem("Create Material"))
				{
					const std::string baseName = "NewMaterial";
					fs::path matPath = path / (baseName + ".mat");

					int index = 1;
					while (fs::exists(matPath))
					{
						matPath = path / (baseName + std::to_string(index) + ".mat");
						++index;
					}

					// 기본 MaterialComponent 생성 및 저장
					MaterialComponent defaultMat;
					defaultMat.color = DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f);
					defaultMat.roughness = 0.5f;
					defaultMat.metalness = 0.0f;
					defaultMat.shadingMode = -1; // Global

					if (MaterialFile::Save(matPath, defaultMat))
					{
						ALICE_LOG_INFO("[EditorCore] Created new Material file: %s", matPath.string().c_str());
					}
					else
					{
						ALICE_LOG_ERRORF("[EditorCore] Failed to create Material file: %s", matPath.string().c_str());
					}
				}

				// 새 UI Curve Asset 생성
				if (ImGui::MenuItem("Create CurveAsset"))
				{
					const std::string baseName = "NewCurve";
					fs::path curvePath = path / (baseName + ".uicurve");

					int index = 1;
					while (fs::exists(curvePath))
					{
						curvePath = path / (baseName + std::to_string(index) + ".uicurve");
						++index;
					}

					UICurveAsset asset;
					asset.name = curvePath.stem().string();
					asset.keys.push_back({ 0.0f, 0.0f, 0.0f, 0.0f, UICurveInterp::Cubic, UICurveTangentMode::Auto });
					asset.keys.push_back({ 1.0f, 1.0f, 0.0f, 0.0f, UICurveInterp::Cubic, UICurveTangentMode::Auto });
					asset.Sort();
					asset.RecalcAutoTangents();

					if (SaveUICurveAsset(curvePath, asset))
					{
						ALICE_LOG_INFO("[EditorCore] Created new Curve asset: %s", curvePath.string().c_str());
					}
					else
					{
						ALICE_LOG_ERRORF("[EditorCore] Failed to create Curve asset: %s", curvePath.string().c_str());
					}
				}

				// Preload.json 생성
				if (ImGui::MenuItem("Create Preload.json"))
				{
					fs::path preloadPath = path / "Preload.json";
					if (!fs::exists(preloadPath))
					{
						if (!WritePreloadJsonSample(preloadPath))
						{
							ALICE_LOG_ERRORF("[EditorCore] Failed to create Preload.json: %s", preloadPath.string().c_str());
						}
					}
					g_PreloadEditorPath = preloadPath;
					g_PreloadEditorOpen = true;
				}

				// Unity 스타일: C++ 스크립트(.h/.cpp)와 프리팹을 간단하게 생성합니다.
				if (ImGui::MenuItem("Create C++ Script"))
				{
					const std::string baseName = "NewScript";

					fs::path headerPath = path / (baseName + ".h");
					fs::path sourcePath = path / (baseName + ".cpp");

					int index = 1;
					while (fs::exists(headerPath) || fs::exists(sourcePath))
					{
						const std::string numbered = baseName + std::to_string(index);
						headerPath = path / (numbered + ".h");
						sourcePath = path / (numbered + ".cpp");
						++index;
					}

					const std::string className = headerPath.stem().string();

					// 헤더 파일 템플릿 작성
					{
						std::ofstream hfs(headerPath);
						if (hfs.is_open())
						{
							hfs << "#pragma once\n\n";
							hfs << "#include \"Runtime/Scripting/IScript.h\"\n";
							hfs << "#include \"Runtime/Scripting/ScriptReflection.h\"\n\n";
							hfs << "namespace Alice\n";
							hfs << "{\n";
							hfs << "    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.\n";
							hfs << "    class " << className << " : public IScript\n";
							hfs << "    {\n";
							hfs << "        ALICE_BODY(" << className << ");\n\n";
							hfs << "    public:\n";
							hfs << "        void Start() override;\n";
							hfs << "        void Update(float deltaTime) override;\n\n";
							hfs << "        // --- 변수 리플렉션 예시 (에디터에서 수정 가능) ---\n";
							hfs << "        ALICE_PROPERTY(float, m_exampleValue, 1.0f);\n\n";
							hfs << "        // --- 함수 리플렉션 예시 ---\n";
							hfs << "        void ExampleFunction();\n";
							hfs << "        ALICE_FUNC(ExampleFunction);\n";
							hfs << "    };\n";
							hfs << "}\n";
						}
					}

					// cpp 파일 템플릿 작성
					{
						std::ofstream cfs(sourcePath);
						if (cfs.is_open())
						{
							cfs << "#include \"" << headerPath.filename().string() << "\"\n";
							cfs << "#include \"Runtime/Scripting/ScriptFactory.h\"\n";
							cfs << "#include \"Runtime/Foundation/Logger.h\"\n";
							cfs << "#include \"Runtime/ECS/World.h\"\n\n";
							cfs << "namespace Alice\n";
							cfs << "{\n";
							cfs << "    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.\n";
							cfs << "    REGISTER_SCRIPT(" << className << ");\n\n";
							cfs << "    void " << className << "::Start()\n";
							cfs << "    {\n";
							cfs << "        // 초기화 로직을 여기에 작성하세요.\n";
							cfs << "    }\n\n";
							cfs << "    void " << className << "::Update(float deltaTime)\n";
							cfs << "    {\n";
							cfs << "        // 매 프레임 호출되는 로직을 여기에 작성하세요.\n";
							cfs << "    }\n\n";
							cfs << "    void " << className << "::ExampleFunction()\n";
							cfs << "    {\n";
							cfs << "        // 리플렉션으로 등록된 함수 예시입니다.\n";
							cfs << "        // 이 함수는 에디터에서 호출할 수 있습니다.\n";
							cfs << "        \n";
							cfs << "        // 예시: Transform 컴포넌트 가져오기\n";
							cfs << "        if (auto* transform = GetComponent<TransformComponent>())\n";
							cfs << "        {\n";
							cfs << "            // 위치를 (0, 0, 0)으로 리셋하는 예시\n";
							cfs << "            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);\n";
							cfs << "        }\n";
							cfs << "    }\n";
							cfs << "}\n";
						}
					}
				}

				if (ImGui::MenuItem("Create Prefab"))
				{
					// 기본 프리팹(JSON) 생성 (.prefab)
					fs::path newPath = path / "NewPrefab.prefab";
					int index = 1;
					while (fs::exists(newPath))
					{
						newPath = path / ("NewPrefab" + std::to_string(index) + ".prefab");
						++index;
					}

					nlohmann::json j;
					j["version"] = 1;
					j["name"] = "NewPrefab";
					j["Transform"] = {
						{ "position", { { "x", 0.0f }, { "y", 0.0f }, { "z", 0.0f } } },
						{ "rotation", { { "x", 0.0f }, { "y", 0.0f }, { "z", 0.0f } } },
						{ "scale",    { { "x", 1.0f }, { "y", 1.0f }, { "z", 1.0f } } },
						{ "enabled", true },
						{ "visible", true }
					};
					j["Scripts"] = nlohmann::json::array();

					std::ofstream ofs(newPath);
					if (ofs.is_open())
						ofs << j.dump(4);
				}


				if (ImGui::MenuItem("Create Scene"))
				{
					fs::path newPath = path / "NewScene.scene";
					int index = 1;
					while (fs::exists(newPath))
					{
						newPath = path / ("NewScene" + std::to_string(index) + ".scene");
						++index;
					}

					// 기본 씬: 큐브(Transform 1개) + 기본 Material 1개
					// ForwardRenderSystem은 Transform만 있어도 기본 큐브를 그립니다.
					World temp;
					std::string cubeAssetPath = "Assets/Fbx/Cube.fbxasset";
					EntityId e = InstantiateFbxAssetToWorld(temp, cubeAssetPath, "Cube");
					if (e == InvalidEntityId)
					{
						e = temp.CreateEntity();
						temp.AddComponent<TransformComponent>(e);
						temp.AddComponent<MaterialComponent>(e, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
					}

					SceneFile::Save(temp, newPath);
				}

				// 디렉터리 삭제 (Assets 안에서만 사용)
				if (ImGui::MenuItem("Delete Folder"))
				{
					std::error_code ec;
					fs::remove_all(path, ec);
				}

				ImGui::EndPopup();
			}

			if (open)
			{
				// 이 노드가 그 사이에 삭제되었으면 순회를 건너뜁니다.
				if (fs::exists(path) && fs::is_directory(path))
				{
					struct DirCache { std::vector<fs::path> entries; fs::file_time_type stamp; };
					static std::unordered_map<std::string, DirCache> s_dirCache;

					const std::string key = path.string();
					auto mtime = fs::last_write_time(path);
					auto& cached = s_dirCache[key];
					if (cached.entries.empty() || cached.stamp != mtime)
					{
						cached.entries.clear();
						for (const auto& e : fs::directory_iterator(path))
							cached.entries.push_back(e.path());
						cached.stamp = mtime;
					}
					for (const auto& childPath : cached.entries)
						DrawDirectoryNode(world, selectedEntity, childPath);
				}

				ImGui::TreePop();
			}
		}
		else
		{
			const std::string ext = path.extension().string();

			// 파일 이름 렌더링: 일반 텍스트 또는 인라인 입력 박스
			ImGui::PushID(label.c_str());
			if (isRenamingThis)
			{
				ImGui::SetNextItemWidth(-1.0f);
				if (s_renameFocus)
				{
					ImGui::SetKeyboardFocusHere();
					s_renameFocus = false;
				}

				bool enterPressed = ImGui::InputText(
					"##RenameFile",
					s_renameBuffer,
					sizeof(s_renameBuffer),
					ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);

				bool finished = enterPressed || ImGui::IsItemDeactivatedAfterEdit();
				if (finished)
				{
					if (std::strlen(s_renameBuffer) > 0)
					{
						fs::path newPath = path.parent_path() / s_renameBuffer;
						if (!fs::exists(newPath))
						{
							std::error_code ec;
							fs::rename(path, newPath, ec);
						}
					}
					s_renaming = false;
				}
			}
			else
			{
				ImGui::TreeNodeEx(label.c_str(),
					baseFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

				// 파일 드래그 소스: Inspector로 드래그앤드롭 가능
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
				{
					// 파일 경로를 문자열로 전달
					std::string pathStr = path.string();
					ImGui::SetDragDropPayload("ASSET_FILE_PATH", pathStr.c_str(), pathStr.size() + 1);
					ImGui::TextUnformatted(label.c_str());
					ImGui::EndDragDropSource();
				}
			}
			ImGui::PopID();

			// 파일 노드를 더블클릭하면 파일 형식에 따라 동작합니다.
			if (!isRenamingThis &&
				ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cxx")
				{
					//fs::path absPath = fs::absolute(path);
					//std::wstring wpath = absPath.wstring();
					//ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					wchar_t exePathW[MAX_PATH] = {};
					GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
					std::filesystem::path exePath = exePathW;
					std::filesystem::path exeDir = exePath.parent_path();
					std::filesystem::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
					std::filesystem::path scriptsSolutionRoot = projectRoot / "ScriptsBuild" / "build" / "AliceUserScripts.sln";
					ALICE_LOG_INFO("[Editor] Opening script solution: \"%s\"", scriptsSolutionRoot.string().c_str());
					ShellExecuteW(nullptr, L"open", scriptsSolutionRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				else if (ext == ".scene")
				{
					// 씬 파일을 더블클릭하면, 필요한 경우 저장 여부를 물은 뒤 로드합니다.
					g_NextScenePath = path;
					g_RequestSceneLoad = true;
				}
				else if (ext == ".mat")
				{
					// 머티리얼 에셋 전용 편집 창을 엽니다.
					g_MaterialEditorPath = path;
					g_MaterialEditorData = {};
					// 파일에서 값을 불러옵니다. 실패하면 기본 값으로 남겨둡니다.
					MaterialFile::Load(path, g_MaterialEditorData, &ResourceManager::Get());
					g_MaterialEditorData.assetPath = path.string();
					g_MaterialEditorOpen = true;
				}
				else if (ext == ".uicurve")
				{
					g_UICurveEditorPath = path;
					g_UICurveEditorData = {};
					if (!LoadUICurveAsset(path, g_UICurveEditorData))
					{
						g_UICurveEditorData.name = path.stem().string();
						g_UICurveEditorData.keys.push_back({ 0.0f, 0.0f, 0.0f, 0.0f, UICurveInterp::Cubic, UICurveTangentMode::Auto });
						g_UICurveEditorData.keys.push_back({ 1.0f, 1.0f, 0.0f, 0.0f, UICurveInterp::Cubic, UICurveTangentMode::Auto });
					}
					g_UICurveEditorData.Sort();
					g_UICurveEditorData.RecalcAutoTangents();
					g_UICurveEditorSelected = -1;
					g_UICurveEditorOpen = true;
				}
				else if (_stricmp(path.filename().string().c_str(), "Preload.json") == 0)
				{
					g_PreloadEditorPath = path;
					g_PreloadEditorOpen = true;
				}
			}

			// 파일 노드에 대한 우클릭 컨텍스트 메뉴 (열기/이름 바꾸기/삭제/프리팹 Instantiate 등)
			if (ImGui::BeginPopupContextItem())
			{
				// 어떤 확장자든 기본 Open / Rename / Delete 는 제공한다.
				if (ImGui::MenuItem("Open"))
				{
					fs::path absPath = fs::absolute(path);
					std::wstring wpath = absPath.wstring();
					ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}

				if (ImGui::MenuItem("Rename..."))
				{
					std::string fileName = path.filename().string();
					std::memset(s_renameBuffer, 0, sizeof(s_renameBuffer));
					strncpy_s(s_renameBuffer,
						sizeof(s_renameBuffer),
						fileName.c_str(),
						_TRUNCATE);
					s_renaming = true;
					s_renamingPath = path;
					s_renameFocus = true;
					// 바로 인라인 입력 박스를 보여주기 위해 팝업을 닫습니다.
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Delete"))
				{
					std::error_code ec;
					fs::remove(path, ec);

					// .cpp 삭제 시 같은 폴더의 <stem>.meta 도 같이 제거합니다.
					if (ext == ".cpp")
					{
						std::error_code ec2;
						fs::path metaPath = path.parent_path() / (path.stem().string() + ".meta");
						fs::remove(metaPath, ec2);
					}
				}

				// 프리팹 파일에 대한 Instantiate 동작
				if (ext == ".prefab")
				{
					if (ImGui::MenuItem("Instantiate Prefab"))
					{
						EntityId e = Alice::Prefab::InstantiateFromFile(world, path);
						if (e != InvalidEntityId)
						{
							selectedEntity = e;
							g_SceneDirty = true;
						}
					}
				}

				// 머티리얼 파일에 대한 간단한 적용 기능
				if (ext == ".mat")
				{
					if (ImGui::MenuItem("Assign To Selected Entity") &&
						selectedEntity != InvalidEntityId &&
						world.GetComponent<TransformComponent>(selectedEntity))
					{
						MaterialComponent* mat = world.GetComponent<MaterialComponent>(selectedEntity);
						if (!mat)
						{
							DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
							mat = &world.AddComponent<MaterialComponent>(selectedEntity, defaultColor);
						}

						if (mat)
						{
							MaterialFile::Load(path, *mat, &ResourceManager::Get());
							mat->assetPath = path.string();
							g_SceneDirty = true;
						}
					}
				}

				// 씬 파일 저장/로드
				if (ext == ".scene")
				{
					if (ImGui::MenuItem("Load Scene"))
					{
						g_NextScenePath = path;
						g_RequestSceneLoad = true;
					}
					if (ImGui::MenuItem("Save Current Scene"))
					{
						SceneFile::Save(world, path);
						g_CurrentScenePath = path;
						g_HasCurrentScenePath = true;
						g_SceneDirty = false;
					}
				}

				// FBX 인스턴스 에셋(.fbxasset)을 월드에 배치
				if (ext == ".fbxasset")
				{
					if (ImGui::MenuItem("Instantiate FBX"))
					{
						Alice::FbxInstanceAsset asset{};
						if (Alice::LoadFbxInstanceAsset(path, asset) && !asset.meshAssetPath.empty())
						{
							// 디버그 로깅: .fbxasset 로드 결과
							ALICE_LOG_INFO("[Editor] Instantiate FBX: assetPath=\"%s\" sourceFbx=\"%s\" meshKey=\"%s\" mats=%zu\n",
								path.string().c_str(),
								asset.sourceFbx.c_str(),
								asset.meshAssetPath.c_str(),
								asset.materialAssetPaths.size());

							// 레지스트리에 GPU 메시가 없다면, 원본 FBX 를 다시 임포트해서 등록합니다.
							if (m_skinnedRegistry && m_renderDevice)
							{
								if (!m_skinnedRegistry->Find(asset.meshAssetPath))
								{
									FbxImportOptions opt{};
									FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);
									auto* device = m_renderDevice->GetDevice();
									std::filesystem::path srcFbxPath = ResourceManager::Get().Resolve(asset.sourceFbx);
									importer.Import(device, srcFbxPath, opt);

									ALICE_LOG_INFO("[Editor] Instantiate FBX: mesh was not in registry, re-imported FBX");
								}
								else
								{
									ALICE_LOG_INFO("[Editor] Instantiate FBX: mesh already in registry");
								}
							}

							EntityId e = world.CreateEntity();
							TransformComponent& t = world.AddComponent<TransformComponent>(e);
							t.position = { 0.0f, 0.0f, 0.0f };
							t.scale = { 1.0f, 1.0f, 1.0f };
							t.rotation = { 0.0f, 0.0f, 0.0f };

							SkinnedMeshComponent& skinned = world.AddComponent<SkinnedMeshComponent>(e, asset.meshAssetPath);
							skinned.instanceAssetPath = path.string();
							static DirectX::XMFLOAT4X4 s_identityBone =
								DirectX::XMFLOAT4X4(1, 0, 0, 0,
									0, 1, 0, 0,
									0, 0, 1, 0,
									0, 0, 0, 1);
							skinned.boneMatrices = &s_identityBone;
							skinned.boneCount = 1;

							ALICE_LOG_INFO("[Editor] Instantiate FBX: created entity=%u, boneCount=%u\n",
								static_cast<unsigned>(e),
								skinned.boneCount);

							if (!asset.materialAssetPaths.empty())
							{
								DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
								MaterialComponent& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
								mat.assetPath = asset.materialAssetPaths.front();
								MaterialFile::Load(mat.assetPath, mat, &ResourceManager::Get());
							}

							selectedEntity = e;
							g_SceneDirty = true;
						}
					}
				}

				ImGui::EndPopup();
			}
		}
	}

}

