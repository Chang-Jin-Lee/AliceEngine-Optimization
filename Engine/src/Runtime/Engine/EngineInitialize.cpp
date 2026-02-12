#include "Runtime/Engine/EngineImpl.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UIEffectComponent.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"
#include "Runtime/Importing/FbxAnimation.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace Alice
{
	namespace
	{

		// PVD 설정 저장/로드 함수
		std::filesystem::path GetEngineSettingsPath(const std::filesystem::path& exeDir)
		{
			namespace fs = std::filesystem;
			// 에디터 모드: 프로젝트 루트 / EngineSettings.json
			// 게임 모드: 실행 파일 위치 / EngineSettings.json
			fs::path cfg = exeDir / "EngineSettings.json";
			if (!fs::exists(cfg))
			{
				// 빌드 경로에도 확인
				cfg = exeDir.parent_path().parent_path().parent_path() / "EngineSettings.json";
			}
			return cfg;
		}

		void LoadPvdSettings(const std::filesystem::path& exeDir, bool& enabled, std::string& host, int& port)
		{
			namespace fs = std::filesystem;
			fs::path cfg = GetEngineSettingsPath(exeDir);

			if (!fs::exists(cfg))
			{
				// 파일이 없으면 기본값 유지
				return;
			}

			std::ifstream ifs(cfg);
			if (!ifs.is_open()) return;

			nlohmann::json j;
			try
			{
				ifs >> j;
			}
			catch (...)
			{
				ALICE_LOG_WARN("EngineSettings.json parse error. Using defaults.");
				return;
			}

			if (j.contains("pvd"))
			{
				const auto& pvd = j["pvd"];
				if (pvd.contains("enabled") && pvd["enabled"].is_boolean())
					enabled = pvd["enabled"].get<bool>();
				if (pvd.contains("host") && pvd["host"].is_string())
					host = pvd["host"].get<std::string>();
				if (pvd.contains("port") && pvd["port"].is_number_integer())
					port = pvd["port"].get<int>();
			}
		}

		void SavePvdSettingsFile(const std::filesystem::path& exeDir, bool enabled, const std::string& host, int port)
		{
			namespace fs = std::filesystem;
			fs::path cfg = GetEngineSettingsPath(exeDir);

			// 디렉토리 생성 (없으면)
			fs::create_directories(cfg.parent_path());

			nlohmann::json j;

			// 기존 파일이 있으면 읽어서 병합
			if (fs::exists(cfg))
			{
				std::ifstream ifs(cfg);
				if (ifs.is_open())
				{
					try
					{
						ifs >> j;
					}
					catch (...)
					{
						// 파싱 실패해도 계속 진행 (새 파일로 덮어쓰기)
					}
				}
			}

			// PVD 설정 업데이트
			j["pvd"] = nlohmann::json::object();
			j["pvd"]["enabled"] = enabled;
			j["pvd"]["host"] = host;
			j["pvd"]["port"] = port;

			// 저장
			std::ofstream ofs(cfg);
			if (!ofs.is_open())
			{
				ALICE_LOG_ERRORF("Failed to save EngineSettings.json");
				return;
			}

		ofs << j.dump(4); // 들여쓰기 4칸으로 포맷
		ALICE_LOG_INFO("PVD settings saved to EngineSettings.json");
	}

	std::string TrimAsciiRuntime(const std::string& s)
	{
		size_t start = 0;
		while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
			++start;
		size_t end = s.size();
		while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
			--end;
		return s.substr(start, end - start);
	}

	bool HasParentTraversalRuntime(const std::string& s);
	bool IsAllowedLogicalPathRuntime(const std::string& s);

	std::string NormalizeLogicalPathRuntime(const std::string& s)
	{
		std::string temp = s;
		std::replace(temp.begin(), temp.end(), '\\', '/');
		if (temp.rfind("./", 0) == 0)
			temp = temp.substr(2);
		std::filesystem::path p(temp);
		std::string normalized = p.lexically_normal().generic_string();
		if (normalized.rfind("./", 0) == 0)
			normalized = normalized.substr(2);
		return normalized;
	}

	std::string NormalizeDerivedLogicalPathRuntime(const std::string& raw)
	{
		const std::string trimmed = TrimAsciiRuntime(raw);
		if (trimmed.empty())
			return {};

		const std::string normalized = NormalizeLogicalPathRuntime(trimmed);
		if (normalized.empty() || HasParentTraversalRuntime(normalized))
			return {};
		if (IsAllowedLogicalPathRuntime(normalized))
			return normalized;

		const std::filesystem::path p(normalized);
		if (!p.is_absolute())
			return {};

		const std::string abs = p.generic_string();
		std::string lower = abs;
		for (char& c : lower)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

		auto MakeLogical = [&](const std::string& needle, const std::string& prefix) -> std::string
		{
			const auto pos = lower.find(needle);
			if (pos == std::string::npos)
				return {};
			const std::size_t relOff = pos + needle.size();
			if (relOff >= abs.size())
				return std::string(prefix);
			return NormalizeLogicalPathRuntime(std::string(prefix) + abs.substr(relOff));
		};

		const std::string assets = MakeLogical("/assets/", "Assets/");
		if (!assets.empty() && IsAllowedLogicalPathRuntime(assets) && !HasParentTraversalRuntime(assets))
			return assets;

		const std::string resource = MakeLogical("/resource/", "Resource/");
		if (!resource.empty() && IsAllowedLogicalPathRuntime(resource) && !HasParentTraversalRuntime(resource))
			return resource;

		const std::string cooked = MakeLogical("/cooked/", "Cooked/");
		if (!cooked.empty() && IsAllowedLogicalPathRuntime(cooked) && !HasParentTraversalRuntime(cooked))
			return cooked;

		return {};
	}

	bool HasParentTraversalRuntime(const std::string& s)
	{
		std::filesystem::path p(s);
		for (const auto& part : p)
		{
			if (part == "..")
				return true;
		}
		return false;
	}

	bool IsAllowedLogicalPathRuntime(const std::string& s)
	{
		std::string lower = s;
		for (char& c : lower)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return lower.rfind("assets/", 0) == 0 ||
			lower.rfind("resource/", 0) == 0 ||
			lower.rfind("cooked/", 0) == 0;
	}

	std::string ToLowerAsciiRuntime(std::string s)
	{
		for (char& c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	bool EndsWithAsciiRuntime(const std::string& s, const std::string& suffix)
	{
		if (s.size() < suffix.size()) return false;
		return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
	}

	std::string GetLowerExtensionRuntime(const std::string& path)
	{
		const std::string ext = std::filesystem::path(path).extension().string();
		return ToLowerAsciiRuntime(ext);
	}

	std::string MakeFbxHashedKeyRuntime(const std::string& logicalPath)
	{
		std::string normalized = NormalizeLogicalPathRuntime(logicalPath);
		if (normalized.empty())
			return {};
		std::string lower = ToLowerAsciiRuntime(normalized);
		const std::uint64_t h = ResourceManager::HashString64(lower);
		char hex[17] = {};
		std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
		std::string baseName = std::filesystem::path(logicalPath).stem().string();
		if (baseName.empty())
			baseName = "fbx";
		return baseName + "_" + std::string(hex, hex + 8);
	}

	bool IsAudioExtensionRuntime(const std::string& extLower)
	{
		return extLower == ".wav" || extLower == ".ogg" || extLower == ".mp3" ||
			extLower == ".flac" || extLower == ".m4a" || extLower == ".aac";
	}

	void AppendPreloadArray(const nlohmann::json& arr, std::vector<std::string>& out)
	{
		for (const auto& v : arr)
		{
			if (!v.is_string())
				continue;
			std::string s = TrimAsciiRuntime(v.get<std::string>());
			if (s.empty())
				continue;
			out.push_back(NormalizeLogicalPathRuntime(s));
		}
	}

	bool ParsePreloadJsonText(const std::string& text, std::vector<std::string>& out)
	{
		out.clear();
		if (text.empty())
			return false;

		nlohmann::json j;
		try
		{
			j = nlohmann::json::parse(text);
		}
		catch (...)
		{
			return false;
		}

		if (j.is_array())
		{
			AppendPreloadArray(j, out);
			return true;
		}

		if (j.contains("preload") && j["preload"].is_array())
		{
			AppendPreloadArray(j["preload"], out);
			return true;
		}

		if (j.contains("startup") && j["startup"].is_array())
		{
			AppendPreloadArray(j["startup"], out);
			return true;
		}

		return true;
	}

	struct PreloadContext
	{
		ResourceManager& resources;
		SkinnedMeshRegistry& skinnedRegistry;
		std::unordered_map<std::string, std::shared_ptr<const std::vector<std::uint8_t>>>& blobs;
		ForwardRenderSystem* forward = nullptr;
		DeferredRenderSystem* deferred = nullptr;
		UnityVfxMeshRenderSystem* vfx = nullptr;
		ComputeEffectSystem* compute = nullptr;
		ID3D11RenderDevice* renderDevice = nullptr;
		bool useForward = false;
		std::unordered_map<std::string, std::uint32_t> warmedComputePresetCounts;

		enum class KeyMatch
		{
			Unknown,
			Match,
			Mismatch
		};

		KeyMatch CheckMeshKeyForSource(const std::string& meshKey, const std::string& fbxLogicalPath)
		{
			if (meshKey.empty())
				return KeyMatch::Unknown;

			FbxInstanceAsset asset{};
			const std::filesystem::path assetPath = std::filesystem::path("Assets/Fbx") / (meshKey + ".fbxasset");
			if (!LoadFbxInstanceAssetAuto(resources, assetPath, asset))
				return KeyMatch::Unknown;

			if (asset.sourceFbx.empty())
				return KeyMatch::Unknown;

			std::string a = ToLowerAsciiRuntime(NormalizeLogicalPathRuntime(asset.sourceFbx));
			std::string b = ToLowerAsciiRuntime(NormalizeLogicalPathRuntime(fbxLogicalPath));
			if (a.empty() || b.empty())
				return KeyMatch::Unknown;

			return (a == b) ? KeyMatch::Match : KeyMatch::Mismatch;
		}

		bool PreloadBlob(const std::string& path)
		{
			auto sp = resources.LoadSharedBinaryAuto(path);
			if (!sp)
				return false;
			blobs[path] = sp;
			return true;
		}

		bool PreloadTexture(const std::string& path)
		{
			bool ok = false;
			if (useForward && forward)
				ok = forward->PreloadTexture(path) || ok;
			if (!useForward && deferred)
				ok = deferred->PreloadTexture(path) || ok;
			if (!ok && forward)
				ok = forward->PreloadTexture(path) || ok;
			if (!ok && deferred)
				ok = deferred->PreloadTexture(path) || ok;
			return ok;
		}

		struct PrefabDependencies
		{
			std::vector<std::string> effectPaths;
			std::vector<std::string> computePresets;
		};

		bool TryCollectPrefabDependencies(const std::string& prefabPath, PrefabDependencies& outDeps) const
		{
			outDeps.effectPaths.clear();
			outDeps.computePresets.clear();

			Prefab::DependencyInfo rawDeps;
			if (!Prefab::CollectDependenciesAuto(prefabPath, rawDeps))
			{
				ALICE_LOG_WARN("Preload: prefab dependency scan failed: %s", prefabPath.c_str());
				return false;
			}

			std::unordered_set<std::string> seenEffects;
			for (const std::string& rawPath : rawDeps.unityEffectPaths)
			{
				const std::string logical = NormalizeDerivedLogicalPathRuntime(rawPath);
				if (logical.empty())
				{
					ALICE_LOG_WARN("Preload: invalid UnityVfx.effectPath in prefab \"%s\": %s",
						prefabPath.c_str(), rawPath.c_str());
					continue;
				}

				const std::string lower = ToLowerAsciiRuntime(logical);
				if (!EndsWithAsciiRuntime(lower, "effect.json"))
				{
					ALICE_LOG_WARN("Preload: UnityVfx.effectPath is not effect.json in prefab \"%s\": %s",
						prefabPath.c_str(), logical.c_str());
					continue;
				}

				if (seenEffects.insert(lower).second)
					outDeps.effectPaths.push_back(logical);
			}

			for (const std::string& rawPreset : rawDeps.computeShaderNames)
			{
				std::string preset = TrimAsciiRuntime(rawPreset);
				if (preset.empty())
					preset = "Particle";
				outDeps.computePresets.push_back(preset);
			}

			return true;
		}

		bool WarmComputePreset(const std::string& presetName)
		{
			if (!compute || presetName.empty())
				return false;

			std::string preset = TrimAsciiRuntime(presetName);
			if (preset.empty())
				preset = "Particle";

			const std::string key = ToLowerAsciiRuntime(preset);
			if (key.empty())
				return false;

			auto emplaceResult = warmedComputePresetCounts.emplace(key, 0u);
			auto it = emplaceResult.first;
			if (it->second < (std::numeric_limits<std::uint32_t>::max)())
				++it->second;
			const std::uint32_t expectedEmitterCount = (std::max)(1u, it->second);

			if (!compute->PrewarmPreset(preset, expectedEmitterCount))
			{
				ALICE_LOG_WARN("Preload: compute preset warmup failed: %s (expectedEmitters=%u)",
					preset.c_str(), expectedEmitterCount);
				return false;
			}
			return true;
		}

		bool PrecomputeAnimationForMesh(const std::string& meshKey, const std::shared_ptr<SkinnedMeshGPU>& mesh)
		{
			if (meshKey.empty())
				return false;
			if (!mesh || !mesh->sourceModel)
				return false;
			if (skinnedRegistry.GetPrecomputedAnimation(meshKey))
				return true;
			if (!mesh->sourceModel->HasAnimations())
				return true;
			if (mesh->sourceModel->GetBoneNames().empty())
				return true;

			auto anim = std::make_shared<::FbxAnimation>();
			anim->InitMetadata(mesh->sourceModel->GetScenePtr());
			anim->SetSharedContext(
				mesh->sourceModel->GetScenePtr(),
				mesh->sourceModel->GetNodeIndexOfName(),
				&mesh->sourceModel->GetBoneNames(),
				&mesh->sourceModel->GetBoneOffsets(),
				&mesh->sourceModel->GetGlobalInverse());

			const auto t = mesh->sourceModel->GetCurrentAnimationType();
			if (t == FbxModel::AnimationType::Rigid) anim->SetType(::FbxAnimation::AnimType::Rigid);
			else if (t == FbxModel::AnimationType::Skinned) anim->SetType(::FbxAnimation::AnimType::Skinned);
			else anim->SetType(::FbxAnimation::AnimType::None);

			skinnedRegistry.SetPrecomputedAnimation(meshKey, anim);
			return true;
		}

		bool PreloadByType(const std::string& path, bool allowGpu)
		{
			const std::string lower = ToLowerAsciiRuntime(path);
			const std::string ext = GetLowerExtensionRuntime(lower);
			const bool hasDevice = allowGpu && renderDevice && renderDevice->GetDevice();
			ID3D11Device* device = hasDevice ? renderDevice->GetDevice() : nullptr;

			if (EndsWithAsciiRuntime(lower, "effect.json"))
			{
				bool ok = false;
				if (allowGpu && vfx)
				{
					ok = vfx->PreloadEffect(path) || ok;
				}
				if (allowGpu && compute)
				{
					ok = compute->PrewarmUnityEffect(path, 1) || ok;
				}
				ok = PreloadBlob(path) || ok;
				return ok;
			}

			if (ext == ".prefab")
			{
				bool ok = PreloadBlob(path);
				PrefabDependencies deps;
				if (!TryCollectPrefabDependencies(path, deps))
					return ok;

				for (const auto& effectPath : deps.effectPaths)
					ok = PreloadByType(effectPath, allowGpu) || ok;
				if (allowGpu && compute)
				{
					for (const auto& presetName : deps.computePresets)
						ok = WarmComputePreset(presetName) || ok;
				}
				return ok;
			}

			if (ext == ".fbxasset")
			{
				if (allowGpu && device)
				{
					FbxInstanceAsset asset{};
					if (LoadFbxInstanceAssetAuto(resources, std::filesystem::path(path), asset))
					{
						if (asset.meshAssetPath.empty())
							return PreloadBlob(path);
						if (!skinnedRegistry.Has(asset.meshAssetPath))
						{
							FbxImporter importer(resources, &skinnedRegistry);
							skinnedRegistry.LoadFromFbxAsset(asset.meshAssetPath, path, resources, importer, device);
						}
						if (auto mesh = skinnedRegistry.Find(asset.meshAssetPath))
							PrecomputeAnimationForMesh(asset.meshAssetPath, mesh);
						return true;
					}
				}
				return PreloadBlob(path);
			}

			if (ext == ".fbx")
			{
				if (allowGpu && device)
				{
					const std::string meshKey = std::filesystem::path(path).stem().string();
					const std::string hashedKey = MakeFbxHashedKeyRuntime(path);
					const auto baseMatch = CheckMeshKeyForSource(meshKey, path);
					const auto hashMatch = CheckMeshKeyForSource(hashedKey, path);
					auto TryPrecompute = [&](const std::string& key) -> bool
						{
							if (key.empty())
								return false;
							if (auto mesh = skinnedRegistry.Find(key))
							{
								PrecomputeAnimationForMesh(key, mesh);
								return true;
							}
							return false;
						};

					if (hashMatch == KeyMatch::Match && !hashedKey.empty() && skinnedRegistry.Has(hashedKey))
					{
						TryPrecompute(hashedKey);
						return true;
					}
					if (baseMatch == KeyMatch::Match && !meshKey.empty() && skinnedRegistry.Has(meshKey))
					{
						TryPrecompute(meshKey);
						return true;
					}
					if (hashMatch != KeyMatch::Mismatch && !hashedKey.empty() && skinnedRegistry.Has(hashedKey))
					{
						TryPrecompute(hashedKey);
						return true;
					}
					if (baseMatch != KeyMatch::Mismatch && !meshKey.empty() && skinnedRegistry.Has(meshKey))
					{
						TryPrecompute(meshKey);
						return true;
					}

					FbxImporter importer(resources, &skinnedRegistry);
					FbxImportResult res = importer.Import(device, std::filesystem::path(path), FbxImportOptions{});
					if (!res.meshAssetPath.empty())
					{
						if (auto mesh = skinnedRegistry.Find(res.meshAssetPath))
							PrecomputeAnimationForMesh(res.meshAssetPath, mesh);
						return true;
					}
				}
				return PreloadBlob(path);
			}

			if (IsAudioExtensionRuntime(ext))
			{
				const std::wstring key = WStringFromUtf8(path);
				if (!key.empty())
				{
					if (Sound::LoadAuto(resources, key, std::filesystem::path(path), Sound::Type::SFX))
						return true;
				}
				return PreloadBlob(path);
			}

			if (ResourceManager::IsImageLogicalPath(std::filesystem::path(path)))
			{
				if (ext != ".tga")
				{
					if (allowGpu && PreloadTexture(path))
						return true;
				}
				return PreloadBlob(path);
			}

			return PreloadBlob(path);
		}
	};

		void LoadLightingSettings(const std::filesystem::path& exeDir,
			int& shadingMode,
			bool& useFillLight,
			LightingParameters& lighting,
			int& skyboxChoice,
			std::string& skyboxCustomDir,
			std::string& skyboxCustomPrefix,
			int& skyboxResolution)
		{
			namespace fs = std::filesystem;
			fs::path cfg = GetEngineSettingsPath(exeDir);

			if (!fs::exists(cfg))
				return;

			std::ifstream ifs(cfg);
			if (!ifs.is_open())
				return;

			nlohmann::json j;
			try
			{
				ifs >> j;
			}
			catch (...)
			{
				ALICE_LOG_WARN("EngineSettings.json parse error (lighting). Using defaults.");
				return;
			}

			auto ReadVec3 = [](const nlohmann::json& v, DirectX::XMFLOAT3& out)
				{
					if (v.is_array() && v.size() >= 3)
					{
						out.x = v[0].get<float>();
						out.y = v[1].get<float>();
						out.z = v[2].get<float>();
						return;
					}
					if (v.is_object())
					{
						if (v.contains("x")) out.x = v["x"].get<float>();
						if (v.contains("y")) out.y = v["y"].get<float>();
						if (v.contains("z")) out.z = v["z"].get<float>();
					}
				};

			if (j.contains("lighting"))
			{
				const auto& l = j["lighting"];
				if (l.contains("shadingMode") && l["shadingMode"].is_number_integer())
					shadingMode = l["shadingMode"].get<int>();
				if (l.contains("useFillLight") && l["useFillLight"].is_boolean())
					useFillLight = l["useFillLight"].get<bool>();

				if (l.contains("params"))
				{
					const auto& p = l["params"];
					if (p.contains("diffuseColor")) ReadVec3(p["diffuseColor"], lighting.diffuseColor);
					if (p.contains("specularColor")) ReadVec3(p["specularColor"], lighting.specularColor);
					if (p.contains("shininess") && p["shininess"].is_number())
						lighting.shininess = p["shininess"].get<float>();

					if (p.contains("baseColor")) ReadVec3(p["baseColor"], lighting.baseColor);
					if (p.contains("metalness") && p["metalness"].is_number())
						lighting.metalness = p["metalness"].get<float>();
					if (p.contains("roughness") && p["roughness"].is_number())
						lighting.roughness = p["roughness"].get<float>();
					if (p.contains("ambientOcclusion") && p["ambientOcclusion"].is_number())
						lighting.ambientOcclusion = p["ambientOcclusion"].get<float>();
					if (p.contains("globalIBLIntensity") && p["globalIBLIntensity"].is_number())
						lighting.globalIBLIntensity = p["globalIBLIntensity"].get<float>();
					if (p.contains("skyboxRotation") && p["skyboxRotation"].is_number())
						lighting.skyboxRotation = p["skyboxRotation"].get<float>();
					if (p.contains("skyboxRotationPitch") && p["skyboxRotationPitch"].is_number())
						lighting.skyboxRotationPitch = p["skyboxRotationPitch"].get<float>();
					if (p.contains("shadowStrength") && p["shadowStrength"].is_number())
						lighting.shadowStrength = p["shadowStrength"].get<float>();
					if (p.contains("toonShadowStrength") && p["toonShadowStrength"].is_number())
						lighting.toonShadowStrength = p["toonShadowStrength"].get<float>();
					lighting.globalIBLIntensity = std::clamp(lighting.globalIBLIntensity, 0.0f, 1.0f);
					lighting.skyboxRotation = std::fmod(lighting.skyboxRotation, 360.0f);
					if (lighting.skyboxRotation < 0.0f)
						lighting.skyboxRotation += 360.0f;
					lighting.skyboxRotationPitch = std::clamp(lighting.skyboxRotationPitch, -89.9f, 89.9f);
					lighting.shadowStrength = std::clamp(lighting.shadowStrength, 0.0f, 1.0f);
					lighting.toonShadowStrength = std::clamp(lighting.toonShadowStrength, 0.0f, 1.0f);

					if (p.contains("keyIntensity") && p["keyIntensity"].is_number())
						lighting.keyIntensity = p["keyIntensity"].get<float>();
					if (p.contains("fillIntensity") && p["fillIntensity"].is_number())
						lighting.fillIntensity = p["fillIntensity"].get<float>();
					if (p.contains("keyDirection")) ReadVec3(p["keyDirection"], lighting.keyDirection);
					if (p.contains("fillDirection")) ReadVec3(p["fillDirection"], lighting.fillDirection);
				}
			}

			if (j.contains("skybox"))
			{
				const auto& s = j["skybox"];
				if (s.contains("choice") && s["choice"].is_number_integer())
					skyboxChoice = s["choice"].get<int>();
				if (s.contains("customDir") && s["customDir"].is_string())
					skyboxCustomDir = s["customDir"].get<std::string>();
				if (s.contains("customPrefix") && s["customPrefix"].is_string())
					skyboxCustomPrefix = s["customPrefix"].get<std::string>();
				if (s.contains("resolution") && s["resolution"].is_number_integer())
					skyboxResolution = s["resolution"].get<int>();
			}
		}

		void SaveLightingSettingsFile(const std::filesystem::path& exeDir,
			int shadingMode,
			bool useFillLight,
			const LightingParameters& lighting,
			int skyboxChoice,
			const std::string& skyboxCustomDir,
			const std::string& skyboxCustomPrefix,
			int skyboxResolution)
		{
			namespace fs = std::filesystem;
			fs::path cfg = GetEngineSettingsPath(exeDir);
			fs::create_directories(cfg.parent_path());

			nlohmann::json j;
			if (fs::exists(cfg))
			{
				std::ifstream ifs(cfg);
				if (ifs.is_open())
				{
					try { ifs >> j; }
					catch (...) {}
				}
			}

			auto Vec3ToJson = [](const DirectX::XMFLOAT3& v)
				{
					return nlohmann::json::array({ v.x, v.y, v.z });
				};

			j["lighting"] = nlohmann::json::object();
			j["lighting"]["shadingMode"] = shadingMode;
			j["lighting"]["useFillLight"] = useFillLight;
			j["lighting"]["params"] = nlohmann::json::object();
			auto& p = j["lighting"]["params"];
			p["diffuseColor"] = Vec3ToJson(lighting.diffuseColor);
			p["specularColor"] = Vec3ToJson(lighting.specularColor);
			p["shininess"] = lighting.shininess;
			p["baseColor"] = Vec3ToJson(lighting.baseColor);
			p["metalness"] = lighting.metalness;
			p["roughness"] = lighting.roughness;
			p["ambientOcclusion"] = lighting.ambientOcclusion;
			p["globalIBLIntensity"] = lighting.globalIBLIntensity;
			p["skyboxRotation"] = lighting.skyboxRotation;
			p["skyboxRotationPitch"] = lighting.skyboxRotationPitch;
			p["shadowStrength"] = lighting.shadowStrength;
			p["toonShadowStrength"] = lighting.toonShadowStrength;
			p["keyIntensity"] = lighting.keyIntensity;
			p["fillIntensity"] = lighting.fillIntensity;
			p["keyDirection"] = Vec3ToJson(lighting.keyDirection);
			p["fillDirection"] = Vec3ToJson(lighting.fillDirection);

			j["skybox"] = nlohmann::json::object();
			j["skybox"]["choice"] = skyboxChoice;
			j["skybox"]["customDir"] = skyboxCustomDir;
			j["skybox"]["customPrefix"] = skyboxCustomPrefix;
			j["skybox"]["resolution"] = skyboxResolution;

			std::ofstream ofs(cfg);
			if (!ofs.is_open())
			{
				ALICE_LOG_ERRORF("Failed to save EngineSettings.json (lighting)");
				return;
			}

			ofs << j.dump(4);
			ALICE_LOG_INFO("Lighting settings saved to EngineSettings.json");
		}

		// BuildSettings.txt 에서 시작 씬(.scene 파일)을 읽어와 World 에 로드합니다.
		// - scenes 섹션은 "index: path" 형식으로 저장되어 있다고 가정합니다.
		bool LoadStartupSceneFromBuildSettings(World& world, const ResourceManager& resources, const std::filesystem::path& exeDir)
		{
			namespace fs = std::filesystem;

			// 경로 설정 (상수 없이 바로 대입)
			fs::path cfg = exeDir / "BuildSettings.json";
			if (!fs::exists(cfg)) // 빌드 경로 없으면 프로젝트 루트 확인
				cfg = exeDir.parent_path().parent_path().parent_path() / "Build/BuildSettings.json";

			std::ifstream ifs(cfg);
			if (!ifs.is_open()) return false;

			nlohmann::json j;
			try { ifs >> j; }
			catch (...) { return false; }

			std::string target = j.value("default", std::string{});
			std::vector<std::string> scenes;
			if (j.contains("scenes") && j["scenes"].is_array())
			{
				for (const auto& v : j["scenes"])
					if (v.is_string()) scenes.push_back(v.get<std::string>());
			}

			// 씬 결정 및 경로 보정
			if (target.empty() && !scenes.empty()) target = scenes[0];
			if (target.empty()) return false;

			const fs::path logicalScene = fs::path(target);
			ALICE_LOG_INFO("Loading Startup Scene: %s", logicalScene.string().c_str());

			// gameMode에서는 Assets/... 가 Metas/Chunks 로 패킹되어 있으므로 LoadAuto를 사용합니다.
			if (!SceneFile::LoadAuto(world, resources, logicalScene))
			{
				ALICE_LOG_ERRORF("Scene Load Failed: %s", logicalScene.string().c_str());
				return false;
			}

			ALICE_LOG_INFO("Startup Scene loaded successfully: %s", logicalScene.string().c_str());
			return true;
		}
	}

	bool Engine::Impl::InitializeAll(Engine& owner, HINSTANCE hInstance, int nCmdShow)
	{
		m_hInstance = hInstance;

		InitializeMainThreadAndRegistry();

		const std::filesystem::path exeDir = InitializeResolveExeDir();
		ApplyEditorModeFromExeName(exeDir);
		InitializeDllSearchPath(exeDir);

		if (!InitializeConfigureResourceManagers(exeDir)) return false;
		if (!InitializeValidateGameDataIfNeeded()) return false;

		InitializeLoadPvdSettings(exeDir);
		InitializeLoadLightingSettings(exeDir);
		if (!InitializePhysicsContext()) return false;

		if (!InitializeWindowAndInput(owner, nCmdShow)) return false;
		if (!InitializeRenderDevice()) return false;

		// 방금 생성한 윈도우가 흰색으로 보이는 현상을 막기 위해
		// 가능한 가장 이른 시점에 한 프레임이라도 그려둡니다.
		if (m_renderDevice)
		{
			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			m_renderDevice->BeginFrame(clearColor);
			m_renderDevice->EndFrame();
		}

		if (!InitializeEditorCoreIfNeeded()) return false;

		if (m_editorMode)
		{
			InitializeAudio();
			if (!InitializeRenderSystems()) return false;
			if (!InitializeUI()) return false;
			if (!InitializeComputeEffectSystem()) return false;
		}
		else
		{
			ALICE_LOG_INFO("[Loading] Initializing UI renderer...");
			if (!InitializeUI())
			{
				ALICE_LOG_ERRORF("[Loading] InitializeUI failed.");
				return false;
			}
			ALICE_LOG_INFO("[Loading] UI renderer initialized.");
			if (!InitializePreloadAndLoadingScreen(exeDir))
			{
				if (m_initCanceled)
				{
					ALICE_LOG_INFO("[Loading] Initialization canceled by user.");
					return true;
				}
				ALICE_LOG_ERRORF("[Loading] Loading screen stage failed.");
				return false;
			}
		}

		InitializeCameraAndScriptHotReload();

		if (!InitializeScene(exeDir)) return false;
		if (!InitializePhysicsSystemAndWorldCallbacks()) return false;

		InitializePostLoadBindings(owner);
		ALICE_LOG_INFO("Engine::Initialize: Success (Entities: %zu)",
			m_world.GetComponents<TransformComponent>().size());

		if (m_editorMode)
			m_editorCore.RequestEngineLogoDismiss();

		return true;
	}

	void Engine::Impl::InitializeMainThreadAndRegistry()
	{
		ThreadSafety::SetMainThreadId(std::this_thread::get_id());
		LinkComponentRegistry();
		ALICE_LOG_INFO("Engine::Initialize: Begin (EditorMode=%d)", m_editorMode);
	}

	void Engine::Impl::InitializeDllSearchPath(const std::filesystem::path& exeDir)
	{
#if defined(_WIN32)
		const std::filesystem::path dllDir = exeDir / "dll";
		if (!std::filesystem::exists(dllDir))
			return;

		const std::wstring dllDirW = dllDir.wstring();
		// exe/dll 만 검색하도록 설정 (보안 + 배포 일관성)
		SetDllDirectoryW(dllDirW.c_str());
#endif
	}

	std::filesystem::path Engine::Impl::InitializeResolveExeDir()
	{
		wchar_t pathBuf[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);
		return std::filesystem::path(pathBuf).parent_path();
	}

	void Engine::Impl::ApplyEditorModeFromExeName(const std::filesystem::path& exeDir)
	{
		// 실행 파일 이름에 따라 에디터/게임 모드를 강제합니다.
		// - Launch.exe / AliceRenderer.exe  : 에디터 모드
		// - AlicePlayer.exe : 게임 모드
		wchar_t exePathBuf[MAX_PATH] = {};
		std::filesystem::path exePath = exeDir;
		if (GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH) > 0)
			exePath = std::filesystem::path(exePathBuf);

		const std::wstring exeName = exePath.filename().wstring();
		auto IsExe = [&](const wchar_t* name)
		{
			return _wcsicmp(exeName.c_str(), name) == 0;
		};

		bool forced = false;
		if (IsExe(L"Launch.exe") || IsExe(L"AliceRenderer.exe"))
		{
			if (!m_editorMode)
			{
				m_editorMode = true;
				forced = true;
			}
		}
		else if (IsExe(L"AlicePlayer.exe"))
		{
			if (m_editorMode)
			{
				m_editorMode = false;
				forced = true;
			}
		}

		if (forced)
		{
			m_scriptSystem.SetEditorMode(m_editorMode);
			ALICE_LOG_INFO("Engine::Initialize: editorMode forced by exe name (%ls) -> %d", exeName.c_str(), m_editorMode ? 1 : 0);
		}
	}

	bool Engine::Impl::InitializeConfigureResourceManagers(const std::filesystem::path& exeDir)
	{
		m_resourceManager.Configure(!m_editorMode, exeDir);

		if (m_editorMode)
			ResourceManager::Get().Configure(false, exeDir);

		Prefab::SetDefaultWorld(&m_world);
		Prefab::SetDefaultResources(&m_resourceManager);
		ScriptHotReload_SetServices(&m_world, &m_resourceManager);
		return true;
	}

	bool Engine::Impl::InitializeValidateGameDataIfNeeded()
	{
		// 게임 모드 무결성 검증은 로딩 UI 단계(InitializePreloadAndLoadingScreen)에서 수행합니다.
		// 이유: 사용자에게 로딩바/상태 텍스트로 실패 원인을 명확히 보여주기 위함.
		return true;
	}

	void Engine::Impl::InitializeLoadPvdSettings(const std::filesystem::path& exeDir)
	{
		LoadPvdSettings(exeDir, m_pvdEnabled, m_pvdHost, m_pvdPort);
		if (m_pvdEnabled)
		{
			ALICE_LOG_INFO("PVD settings loaded from EngineSettings.json: %s:%d",
				m_pvdHost.c_str(), m_pvdPort);
		}
	}

	void Engine::Impl::InitializeLoadLightingSettings(const std::filesystem::path& exeDir)
	{
		int shadingMode = static_cast<int>(m_shadingMode);
		LoadLightingSettings(
			exeDir,
			shadingMode,
			m_useFillLight,
			m_savedLightingParameters,
			m_skyboxChoice,
			m_skyboxCustomDir,
			m_skyboxCustomPrefix,
			m_skyboxResolution);
		m_shadingMode = static_cast<Engine::Impl::ShadingMode>(shadingMode);
	}

	void Engine::Impl::SaveLightingSettings(const std::filesystem::path& exeDir)
	{
		SaveLightingSettingsFile(
			exeDir,
			static_cast<int>(m_shadingMode),
			m_useFillLight,
			m_savedLightingParameters,
			m_skyboxChoice,
			m_skyboxCustomDir,
			m_skyboxCustomPrefix,
			m_skyboxResolution);
	}

	bool Engine::Impl::InitializePhysicsContext()
	{
		PhysicsModule::ContextInitDesc ctx{};
		ctx.enablePvd = m_pvdEnabled;
		ctx.pvdHost = m_pvdHost.c_str();
		ctx.pvdPort = m_pvdPort;
		ctx.pvdTimeoutMs = 1000;

		if (!m_physics.InitializeContext(ctx))
		{
			const std::string& error = m_physics.GetLastError();
			ALICE_LOG_ERRORF("PhysicsModule::InitializeContext failed: %s", error.c_str());
			return false;
		}

		if (m_pvdEnabled)
		{
			ALICE_LOG_INFO("PVD enabled: %s:%d (connection may fail silently if PVD server is not running)",
				m_pvdHost.c_str(), m_pvdPort);
		}

		return true;
	}

	bool Engine::Impl::InitializeWindowAndInput(Engine& owner, int nCmdShow)
	{
		if (!CreateMainWindow(owner, nCmdShow)) return false;
		m_inputSystem.Initialize(m_hWnd);
		return true;
	}

	bool Engine::Impl::InitializeRenderDevice()
	{
		m_renderDevice = std::make_unique<D3D11RenderDevice>();
		if (!m_renderDevice->Initialize(m_hWnd, m_width, m_height))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: RenderDevice failed.");
			return false;
		}
		return true;
	}

	bool Engine::Impl::InitializeEditorCoreIfNeeded()
	{
		if (!m_editorMode) return true;

		m_editorCore.SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);
		m_editorCore.SetInputSystem(&m_inputSystem);

		if (!m_editorCore.Initialize(m_hWnd, *m_renderDevice))
			return false;

		m_editorCore.SetEngineLogoHoldUntilRelease(true);
		m_editorCore.StartEngineLogoOverlay(m_resourceManager, "Resource/Icon/EngineBanner.png");
		if (!RenderStartupLogoFrames(0.7f))
			return false;

		return true;
	}

	bool Engine::Impl::RenderStartupLogoFrames(float seconds)
	{
		if (!m_editorMode || !m_renderDevice || seconds <= 0.0f)
			return true;

		using namespace std::chrono;
		const auto endTime = steady_clock::now() + duration<float>(seconds);
		MSG msg{};

		while (steady_clock::now() < endTime)
		{
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
					return false;
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			m_renderDevice->BeginFrame(clearColor);

			m_editorCore.BeginFrame();
			m_editorCore.DrawEngineLogoOnly();
			m_editorCore.RenderDrawData();

			m_renderDevice->EndFrame();
			std::this_thread::sleep_for(16ms);
		}

		return true;
	}

	void Engine::Impl::InitializeAudio()
	{
		m_audioSystem.SetResourceManager(&m_resourceManager);
		Sound::Initialize();
	}

	bool Engine::Impl::InitializeRenderSystems()
	{
		m_forwardRenderSystem = std::make_unique<ForwardRenderSystem>(*m_renderDevice);
		m_forwardRenderSystem->SetResourceManager(&m_resourceManager);
		m_forwardRenderSystem->SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);

		m_attackDriverSystem.SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);

		if (!m_forwardRenderSystem->Initialize(m_width, m_height))
		{
			ALICE_LOG_ERRORF("m_forwardRenderSystem->Initialize: fail...");
			return false;
		}

		m_deferredRenderSystem = std::make_unique<DeferredRenderSystem>(*m_renderDevice);
		m_deferredRenderSystem->SetResourceManager(&m_resourceManager);
		m_deferredRenderSystem->SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);

		if (!m_deferredRenderSystem->Initialize(m_width, m_height))
		{
			ALICE_LOG_ERRORF("m_deferredRenderSystem->Initialize: fail...");
			return false;
		}

		// Apply persisted lighting parameters (forward/deferred 동일하게 유지)
		m_forwardRenderSystem->GetLightingParameters() = m_savedLightingParameters;
		m_deferredRenderSystem->GetLightingParameters() = m_savedLightingParameters;

		const std::string iblSuffix = (m_skyboxResolution == 1) ? "MDR" : "HDR";
		auto ApplySkyboxChoice = [&](auto& renderer)
			{
				if (m_skyboxChoice == 0)
				{
					renderer.SetSkyboxEnabled(false);
					return;
				}

				renderer.SetSkyboxEnabled(true);
				switch (m_skyboxChoice)
				{
				case 1: renderer.SetIblSet("Bridge", "bridge", iblSuffix); break;
				case 2: renderer.SetIblSet("Indoor", "indoor", iblSuffix); break;
				case 3: renderer.SetIblSet("Sample", "BakerSample", iblSuffix); break;
				case 4: renderer.SetIblSet("darkenv", "darkenvDiffuseHDR", iblSuffix); break;
				case 5:
					if (!m_skyboxCustomDir.empty() && !m_skyboxCustomPrefix.empty())
						renderer.SetIblSet(m_skyboxCustomDir, m_skyboxCustomPrefix, iblSuffix);
					else
						renderer.SetSkyboxEnabled(false);
					break;
				default:
					break;
				}
			};

		ApplySkyboxChoice(*m_forwardRenderSystem);
		ApplySkyboxChoice(*m_deferredRenderSystem);

		m_debugDrawSystem = std::make_unique<DebugDrawSystem>(*m_renderDevice);
		if (!m_debugDrawSystem->Initialize())
		{
			ALICE_LOG_ERRORF("m_debugDrawSystem->Initialize(): fail...");
			return false;
		}

		m_gizmoDrawSystem = std::make_unique<DebugDrawSystem>(*m_renderDevice);
		if (!m_gizmoDrawSystem->Initialize())
		{
			ALICE_LOG_ERRORF("m_gizmoDrawSystem->Initialize(): fail...");
			return false;
		}

		m_effectSystem = std::make_unique<EffectSystem>(*m_renderDevice);
		if (!m_effectSystem->Initialize())
		{
			ALICE_LOG_ERRORF("m_effectSystem->Initialize(): fail...");
			return false;
		}

		m_trailRenderSystem = std::make_unique<TrailEffectRenderSystem>(*m_renderDevice);
		m_trailRenderSystem->SetResourceManager(&m_resourceManager);
		if (!m_trailRenderSystem->Initialize()) return false;

		m_unityVfxMeshRenderSystem = std::make_unique<UnityVfxMeshRenderSystem>(*m_renderDevice);
		if (!m_unityVfxMeshRenderSystem->Initialize())
		{
			ALICE_LOG_ERRORF("m_unityVfxMeshRenderSystem->Initialize(): fail...");
			return false;
		}

		if (m_deferredRenderSystem && m_trailRenderSystem)
			m_deferredRenderSystem->SetSwordRenderSystem(m_trailRenderSystem.get());

		if (m_forwardRenderSystem)  m_forwardRenderSystem->SetUIRenderer(&m_aliceUIRenderer);
		if (m_deferredRenderSystem) m_deferredRenderSystem->SetUIRenderer(&m_aliceUIRenderer);

		return true;
	}

	bool Engine::Impl::InitializeUI()
	{
		auto* device = m_renderDevice->GetDevice();
		auto* context = m_renderDevice->GetImmediateContext();
		if (!device || !context)
		{
			ALICE_LOG_ERRORF("[Debug] Device or Context is NULL inside UI Block!");
			return false;
		}

		if (!m_aliceUIRenderer.Initialize(device, context, &m_resourceManager))
			ALICE_LOG_ERRORF("[AliceUI] UIRenderer Initialize failed.");

		if (m_forwardRenderSystem)  m_forwardRenderSystem->SetUIRenderer(&m_aliceUIRenderer);
		if (m_deferredRenderSystem) m_deferredRenderSystem->SetUIRenderer(&m_aliceUIRenderer);
		if (m_editorMode)          m_editorCore.SetAliceUIRenderer(&m_aliceUIRenderer);

		return true;
	}

	bool Engine::Impl::InitializeComputeEffectSystem()
	{
		m_computeEffectSystem = std::make_unique<ComputeEffectSystem>(*m_renderDevice);
		if (!m_computeEffectSystem->Initialize(m_width, m_height))
		{
			ALICE_LOG_ERRORF("[Debug] ComputeEffectSystem Init Failed!");
			return false;
		}
		return true;
	}

	bool Engine::Impl::InitializePreloadAndLoadingScreen(const std::filesystem::path& /*exeDir*/)
	{
		if (m_editorMode)
			return true;

		ALICE_LOG_INFO("[Loading] InitializePreloadAndLoadingScreen begin.");

		// 로딩바를 가능한 한 빠르게 먼저 보여줘서 "초기 멈춤 느낌"을 줄입니다.
		World earlyLoadingWorld;
		EntityId earlyStatusId = InvalidEntityId;
		EntityId earlyGaugeId = InvalidEntityId;
		auto RenderEarlyLoading = [&](float progress, const char* status) -> bool
		{
			if (!m_renderDevice || !m_renderDevice->GetBackBufferRTV())
				return true;
			if (earlyStatusId == InvalidEntityId || earlyGaugeId == InvalidEntityId)
				return true;

			MSG msg{};
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					m_initCanceled = true;
					return false;
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			if (auto* statusText = earlyLoadingWorld.GetComponent<UITextComponent>(earlyStatusId))
				statusText->text = status ? status : "쉐이더 컴파일중.";
			if (auto* gauge = earlyLoadingWorld.GetComponent<UIGaugeComponent>(earlyGaugeId))
				gauge->value = std::clamp(progress, 0.0f, 1.0f);

			m_inputSystem.Update(0.0f);
			m_aliceUIRenderer.Update(earlyLoadingWorld, m_inputSystem, m_camera,
				static_cast<float>(m_width), static_cast<float>(m_height), 0.0f);

			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			m_renderDevice->BeginFrame(clearColor);
			m_aliceUIRenderer.RenderScreen(earlyLoadingWorld, m_camera,
				m_renderDevice->GetBackBufferRTV(),
				static_cast<float>(m_width), static_cast<float>(m_height));
			m_renderDevice->EndFrame();
			return true;
		};

		if (m_renderDevice && m_renderDevice->GetBackBufferRTV())
		{
			auto CreateEarlyScreenWidget = [&](const char* name, float anchorX, float anchorY,
				const DirectX::XMFLOAT2& size, int sortOrder)
			{
				EntityId e = earlyLoadingWorld.CreateEntity();
				earlyLoadingWorld.SetEntityName(e, name);

				auto& widget = earlyLoadingWorld.AddComponent<UIWidgetComponent>(e);
				widget.widgetName = name;
				widget.space = AliceUI::UISpace::Screen;

				auto& t = earlyLoadingWorld.AddComponent<UITransformComponent>(e);
				t.anchorMin = DirectX::XMFLOAT2(anchorX, anchorY);
				t.anchorMax = DirectX::XMFLOAT2(anchorX, anchorY);
				t.position = DirectX::XMFLOAT2(0.0f, 0.0f);
				t.size = size;
				t.useAlignment = true;
				t.alignH = AliceUI::UIAlignH::Center;
				t.alignV = AliceUI::UIAlignV::Center;
				t.sortOrder = sortOrder;

				earlyLoadingWorld.AddComponent<TransformComponent>(e);
				return e;
			};

			earlyStatusId = CreateEarlyScreenWidget("Early_Loading_Status", 0.5f, 0.75f,
				DirectX::XMFLOAT2(640.0f, 40.0f), 1);
			{
				auto& statusText = earlyLoadingWorld.AddComponent<UITextComponent>(earlyStatusId);
				statusText.fontPath = "Resource/Fonts/NotoSansKR-Regular.ttf";
				statusText.fontSize = 24.0f;
				statusText.alignH = AliceUI::UIAlignH::Center;
				statusText.alignV = AliceUI::UIAlignV::Center;
				statusText.text = "쉐이더 컴파일중.";
			}

			earlyGaugeId = CreateEarlyScreenWidget("Early_Loading_Bar", 0.5f, 0.80f,
				DirectX::XMFLOAT2(560.0f, 20.0f), 1);
			{
				auto& gauge = earlyLoadingWorld.AddComponent<UIGaugeComponent>(earlyGaugeId);
				gauge.normalized = true;
				gauge.value = 0.0f;
				gauge.displayedValue = 0.0f;
				gauge.smoothing = 0.15f;
				gauge.fillColor = DirectX::XMFLOAT4(0.2f, 0.9f, 0.2f, 1.0f);
				gauge.backgroundColor = DirectX::XMFLOAT4(0.1f, 0.1f, 0.1f, 0.85f);
			}

			if (!RenderEarlyLoading(0.0f, "쉐이더 컴파일중."))
				return false;
		}

		// 1) Preload.json 로드
		std::string preloadText;
		bool hasPreload = m_resourceManager.LoadText("Assets/Startup/Preload.json", preloadText);
		if (!hasPreload)
			hasPreload = m_resourceManager.LoadText("Assets/Preload.json", preloadText);

		std::vector<std::string> rawList;
		if (hasPreload)
		{
			if (!ParsePreloadJsonText(preloadText, rawList))
			{
				ALICE_LOG_WARN("Preload.json parse failed. Preload list will be empty.");
				rawList.clear();
			}
		}
		else
		{
			ALICE_LOG_WARN("Preload.json not found. Loading screen will use empty list.");
		}

		// 2) 경로 검증 + 중복 제거
		std::vector<std::string> filtered;
		filtered.reserve(rawList.size());
		std::unordered_set<std::string> seen;
		std::size_t filteredProcessed = 0;
		for (const auto& entry : rawList)
		{
			std::string normalized = NormalizeLogicalPathRuntime(entry);
			if (normalized.empty())
			{
				++filteredProcessed;
				continue;
			}
			if (HasParentTraversalRuntime(normalized))
			{
				ALICE_LOG_WARN("Preload: skipped path with '..' : %s", normalized.c_str());
				++filteredProcessed;
				continue;
			}
			if (!IsAllowedLogicalPathRuntime(normalized))
			{
				ALICE_LOG_WARN("Preload: invalid path (must be Assets/Resource/Cooked): %s", normalized.c_str());
				++filteredProcessed;
				continue;
			}

			std::string key = normalized;
			for (char& c : key)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (seen.insert(key).second)
				filtered.push_back(normalized);

			++filteredProcessed;
			if (earlyGaugeId != InvalidEntityId && ((filteredProcessed % 16) == 0 || filteredProcessed == rawList.size()))
			{
				const float ratio = rawList.empty() ? 1.0f : static_cast<float>(filteredProcessed) / static_cast<float>(rawList.size());
				if (!RenderEarlyLoading(0.02f + ratio * 0.10f, "로딩 데이터 준비중."))
					return false;
			}
		}

		// 3) 존재 확인
		std::vector<std::string> preloadList;
		preloadList.reserve(filtered.size());
		std::size_t existsProcessed = 0;
		for (const auto& path : filtered)
		{
			const std::filesystem::path resolved = m_resourceManager.Resolve(path);
			std::error_code ec;
			if (std::filesystem::exists(resolved, ec))
			{
				preloadList.push_back(path);
			}
			else
			{
				ALICE_LOG_WARN("Preload: missing file (skipped): %s (resolved: %s)",
					path.c_str(), resolved.string().c_str());
			}

			++existsProcessed;
			if (earlyGaugeId != InvalidEntityId && ((existsProcessed % 16) == 0 || existsProcessed == filtered.size()))
			{
				const float ratio = filtered.empty() ? 1.0f : static_cast<float>(existsProcessed) / static_cast<float>(filtered.size());
				if (!RenderEarlyLoading(0.12f + ratio * 0.10f, "로딩 파일 확인중."))
					return false;
			}
		}

		ALICE_LOG_INFO("[Loading] Preload list ready. raw=%zu filtered=%zu valid=%zu",
			rawList.size(), filtered.size(), preloadList.size());
		PreloadContext preloadCtx{
			m_resourceManager,
			m_skinnedMeshRegistry,
			m_preloadedBlobs,
			m_forwardRenderSystem.get(),
			m_deferredRenderSystem.get(),
			m_unityVfxMeshRenderSystem.get(),
			m_computeEffectSystem.get(),
			m_renderDevice.get(),
			m_useForwardRendering
		};

		// 4) 렌더 장치가 없으면 UI 없이 프리로드만 수행
		if (!m_renderDevice || !m_renderDevice->GetBackBufferRTV())
		{
			ALICE_LOG_WARN("[Loading] Render device not ready. Running preload without UI.");
			if (!m_resourceManager.ValidateGameData())
			{
				ALICE_LOG_ERRORF("[Loading] Chunk integrity validation failed before preload (no loading UI path).");
				return false;
			}
			InitializeAudio();
			if (!InitializeRenderSystems()) return false;
			if (!InitializeComputeEffectSystem()) return false;
			preloadCtx.forward = m_forwardRenderSystem.get();
			preloadCtx.deferred = m_deferredRenderSystem.get();
			preloadCtx.vfx = m_unityVfxMeshRenderSystem.get();
			preloadCtx.compute = m_computeEffectSystem.get();
			preloadCtx.renderDevice = m_renderDevice.get();
			preloadCtx.useForward = m_useForwardRendering;
			if (!m_editorMode && m_computeEffectSystem)
			{
				const std::size_t warmed = m_computeEffectSystem->PrewarmAllRegisteredPresets(1);
				ALICE_LOG_INFO("[Loading] Build-mode compute preset warmup finished (count=%zu).", warmed);
			}

			for (const auto& path : preloadList)
				preloadCtx.PreloadByType(path, false);
			return true;
		}

		// 5) 로딩 UI 월드 구성
		World loadingWorld;
		auto CreateScreenWidget = [&](const char* name, float anchorX, float anchorY,
			const DirectX::XMFLOAT2& size, int sortOrder)
		{
			EntityId e = loadingWorld.CreateEntity();
			loadingWorld.SetEntityName(e, name);
			auto& widget = loadingWorld.AddComponent<UIWidgetComponent>(e);
			widget.widgetName = name;
			widget.space = AliceUI::UISpace::Screen;

			auto& t = loadingWorld.AddComponent<UITransformComponent>(e);
			t.anchorMin = DirectX::XMFLOAT2(anchorX, anchorY);
			t.anchorMax = DirectX::XMFLOAT2(anchorX, anchorY);
			t.position = DirectX::XMFLOAT2(0.0f, 0.0f);
			t.size = size;
			t.useAlignment = true;
			t.alignH = AliceUI::UIAlignH::Center;
			t.alignV = AliceUI::UIAlignV::Center;
			t.sortOrder = sortOrder;

			loadingWorld.AddComponent<TransformComponent>(e);
			return e;
		};

		// Banner: 텍스처 원본 픽셀 크기를 그대로 사용합니다.
		DirectX::XMFLOAT2 bannerSize(0.0f, 0.0f);
		{
			auto bannerSrv = m_resourceManager.Load<ID3D11ShaderResourceView>(
				"Resource/Icon/GameBanner.png", m_renderDevice->GetDevice());
			if (bannerSrv.Get())
			{
				Microsoft::WRL::ComPtr<ID3D11Resource> resource;
				bannerSrv->GetResource(resource.GetAddressOf());
				Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2D;
				if (resource.Get() && SUCCEEDED(resource.As(&texture2D)) && texture2D.Get())
				{
					D3D11_TEXTURE2D_DESC texDesc{};
					texture2D->GetDesc(&texDesc);
					if (texDesc.Width > 0 && texDesc.Height > 0)
					{
						bannerSize = DirectX::XMFLOAT2(
							static_cast<float>(texDesc.Width),
							static_cast<float>(texDesc.Height));
					}
				}
			}

			// 크기 조회에 실패하면 최소 유효 크기로 처리합니다.
			if (bannerSize.x <= 0.0f || bannerSize.y <= 0.0f)
			{
				ALICE_LOG_WARN("[Loading] GameBanner size query failed. Using minimal fallback size.");
				bannerSize = DirectX::XMFLOAT2(1.0f, 1.0f);
			}
		}

		const float bannerAnchorX = 0.5f;
		const float bannerAnchorY = 0.42f;
		const EntityId bannerId = CreateScreenWidget("Loading_Banner", bannerAnchorX, bannerAnchorY,
			bannerSize, 0);
		auto& bannerImg = loadingWorld.AddComponent<UIImageComponent>(bannerId);
		bannerImg.texturePath = "Resource/Icon/GameBanner.png";
		bannerImg.preserveAspect = true;
		if (auto* bannerTransform = loadingWorld.GetComponent<UITransformComponent>(bannerId))
		{
			// 소형 텍스처에서도 흐림을 줄이기 위해 배너 쿼드를 픽셀 그리드에 맞춥니다.
			const float left = bannerAnchorX * static_cast<float>(m_width) - bannerSize.x * 0.5f;
			const float top = bannerAnchorY * static_cast<float>(m_height) - bannerSize.y * 0.5f;
			const float snapX = std::round(left) - left;
			const float snapY = std::round(top) - top;
			bannerTransform->position.x = snapX;
			bannerTransform->position.y = -snapY;
		}

		// Status text
		const EntityId statusId = CreateScreenWidget("Loading_Status", 0.5f, 0.75f,
			DirectX::XMFLOAT2(640.0f, 40.0f), 1);
		{
			auto& statusText = loadingWorld.AddComponent<UITextComponent>(statusId);
			statusText.fontPath = "Resource/Fonts/NotoSansKR-Regular.ttf";
			statusText.fontSize = 24.0f;
			statusText.alignH = AliceUI::UIAlignH::Center;
			statusText.alignV = AliceUI::UIAlignV::Center;
			statusText.text = "쉐이더 컴파일중.";
		}

		// Progress gauge
		const EntityId gaugeId = CreateScreenWidget("Loading_Bar", 0.5f, 0.80f,
			DirectX::XMFLOAT2(560.0f, 20.0f), 1);
		{
			auto& gauge = loadingWorld.AddComponent<UIGaugeComponent>(gaugeId);
			gauge.normalized = true;
			gauge.value = 0.0f;
			gauge.displayedValue = 0.0f;
			gauge.smoothing = 0.15f;
			gauge.fillColor = DirectX::XMFLOAT4(0.2f, 0.9f, 0.2f, 1.0f);
			gauge.backgroundColor = DirectX::XMFLOAT4(0.1f, 0.1f, 0.1f, 0.85f);
		}

		{
			auto& gaugeFx = loadingWorld.AddComponent<UIEffectComponent>(gaugeId);
			gaugeFx.glowEnabled = false;
			gaugeFx.glowColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			gaugeFx.glowStrength = 0.25f;
			gaugeFx.glowWidth = 0.25f;
			gaugeFx.glowSpeed = 0.75f;
		}

		// Hint text
		const EntityId hintId = CreateScreenWidget("Loading_Hint", 0.5f, 0.86f,
			DirectX::XMFLOAT2(640.0f, 32.0f), 1);
		{
			auto& hintText = loadingWorld.AddComponent<UITextComponent>(hintId);
			hintText.fontPath = "Resource/Fonts/NotoSansKR-Regular.ttf";
			hintText.fontSize = 20.0f;
			hintText.alignH = AliceUI::UIAlignH::Center;
			hintText.alignV = AliceUI::UIAlignV::Center;
			hintText.text.clear();
		}

		// 6) 로딩 화면 루프
		using clock = std::chrono::steady_clock;
		auto last = clock::now();
		float dotTimer = 0.0f;
		int dotIndex = 0;
		const char* dotSeq[] = { ".", "..", "...", ".." };
		float displayedProgress = 0.0f;
		float targetProgress = 0.0f;

		const float kAudioWeight = 0.05f;
		const float kRenderWeight = 0.45f;
		const float kComputeWeight = 0.15f;
		float progressBase = 0.0f;

		auto PumpMessages = [&]() -> bool
		{
			MSG msg{};
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					m_initCanceled = true;
					ALICE_LOG_INFO("[Loading] WM_QUIT received. Canceling initialization.");
					return false;
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			return true;
		};

		auto CalcDelta = [&]() -> float
		{
			const auto now = clock::now();
			float dt = std::chrono::duration<float>(now - last).count();
			last = now;
			if (dt < 0.0f) dt = 0.0f;
			if (dt > 0.1f) dt = 0.1f;
			return dt;
		};

		auto UpdateLoadingUI = [&](float dt)
		{
			dotTimer += dt;
			if (dotTimer >= 0.35f)
			{
				dotTimer = 0.0f;
				dotIndex = (dotIndex + 1) % 4;
			}
			if (auto* statusText = loadingWorld.GetComponent<UITextComponent>(statusId))
				statusText->text = std::string("쉐이더 컴파일중") + dotSeq[dotIndex];

			const float speed = 3.5f;
			const float t = std::clamp(dt * speed, 0.0f, 1.0f);
			displayedProgress = displayedProgress + (targetProgress - displayedProgress) * t;
			if (auto* gauge = loadingWorld.GetComponent<UIGaugeComponent>(gaugeId))
				gauge->value = displayedProgress;
		};

		auto RenderFrame = [&](float dt, bool animate) -> bool
		{
			static int s_debugFrames = 2;
			const bool debug = (s_debugFrames > 0);
			if (debug)
				ALICE_LOG_INFO("[Loading] RenderFrame begin (dt=%.4f, animate=%d)", dt, animate ? 1 : 0);

			if (!PumpMessages())
			{
				if (debug)
					ALICE_LOG_WARN("[Loading] PumpMessages failed.");
				return false;
			}
			if (debug)
				ALICE_LOG_INFO("[Loading] PumpMessages ok");

			if (animate)
				UpdateLoadingUI(dt);
			if (debug)
				ALICE_LOG_INFO("[Loading] UpdateLoadingUI ok");

			m_inputSystem.Update(dt);
			if (debug)
				ALICE_LOG_INFO("[Loading] InputSystem.Update ok");

			m_aliceUIRenderer.Update(loadingWorld, m_inputSystem, m_camera,
				static_cast<float>(m_width), static_cast<float>(m_height), dt);
			if (debug)
				ALICE_LOG_INFO("[Loading] UIRenderer.Update ok");

			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			m_renderDevice->BeginFrame(clearColor);
			if (debug)
				ALICE_LOG_INFO("[Loading] BeginFrame ok");

			m_aliceUIRenderer.RenderScreen(loadingWorld, m_camera,
				m_renderDevice->GetBackBufferRTV(),
				static_cast<float>(m_width), static_cast<float>(m_height));
			if (debug)
				ALICE_LOG_INFO("[Loading] RenderScreen ok");

			m_renderDevice->EndFrame();
			if (debug)
				ALICE_LOG_INFO("[Loading] EndFrame ok");

			if (debug)
				--s_debugFrames;
			return true;
		};

		auto AdvanceTo = [&](float nextTarget, float minSeconds) -> bool
		{
			targetProgress = std::clamp(nextTarget, 0.0f, 1.0f);
			const auto endTime = clock::now() + std::chrono::duration<float>(std::max(0.0f, minSeconds));
			while (clock::now() < endTime || displayedProgress + 0.001f < targetProgress)
			{
				float dt = CalcDelta();
				if (!RenderFrame(dt, true))
				{
					ALICE_LOG_WARN("[Loading] RenderFrame failed during progress update.");
					return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(8));
			}
			return true;
		};

		// 초기 프레임
		if (!RenderFrame(CalcDelta(), true))
		{
			ALICE_LOG_WARN("[Loading] Initial frame render failed.");
			return false;
		}

		// 게임 데이터 무결성(청크) 검증: 프리로드 시작 직전에 가장 먼저 확인합니다.
		if (auto* statusText = loadingWorld.GetComponent<UITextComponent>(statusId))
			statusText->text = "청크 검증중.";

		std::atomic<bool> integrityDone{ false };
		std::atomic<bool> integrityOk{ false };
		std::thread integrityThread([&]()
		{
			integrityOk.store(m_resourceManager.ValidateGameData(), std::memory_order_relaxed);
			integrityDone.store(true, std::memory_order_release);
		});

		while (!integrityDone.load(std::memory_order_acquire))
		{
			targetProgress = (std::max)(targetProgress, 0.02f);
			if (!RenderFrame(CalcDelta(), true))
			{
				if (integrityThread.joinable())
					integrityThread.join();
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
		}
		if (integrityThread.joinable())
			integrityThread.join();

		if (!integrityOk.load(std::memory_order_relaxed))
		{
			if (auto* gauge = loadingWorld.GetComponent<UIGaugeComponent>(gaugeId))
			{
				gauge->fillColor = DirectX::XMFLOAT4(0.92f, 0.18f, 0.18f, 1.0f);
				gauge->backgroundColor = DirectX::XMFLOAT4(0.20f, 0.08f, 0.08f, 0.95f);
				gauge->value = 1.0f;
			}
			if (auto* statusText = loadingWorld.GetComponent<UITextComponent>(statusId))
				statusText->text = "쉐이더 컴파일중...";
			if (auto* hintText = loadingWorld.GetComponent<UITextComponent>(hintId))
				hintText->text = "Chunk폴더를 임의로 바꾸셨습니다. 게임을 실행할 수 없습니다.";
			if (auto* gaugeFx = loadingWorld.GetComponent<UIEffectComponent>(gaugeId))
				gaugeFx->glowEnabled = false;

			// 에러 문구와 빨간 로딩바를 확실히 표시합니다.
			if (!RenderFrame(CalcDelta(), false))
				return false;

			while (true)
			{
				if (!RenderFrame(CalcDelta(), false))
					return false;

				if (m_inputSystem.IsMouseButtonPressed(0))
					break;

				std::this_thread::sleep_for(std::chrono::milliseconds(8));
			}

			ALICE_LOG_ERRORF("[Loading] Chunk integrity validation failed.");
			return false;
		}
		if (!AdvanceTo(0.02f, 0.15f))
			return false;

		// 시스템 초기화 단계
		ALICE_LOG_INFO("[Loading] Initializing audio...");
		InitializeAudio();
		ALICE_LOG_INFO("[Loading] Audio initialized.");
		progressBase += kAudioWeight;
		if (!AdvanceTo(progressBase, 0.12f))
			return false;

		ALICE_LOG_INFO("[Loading] Initializing render systems...");
		if (!InitializeRenderSystems())
		{
			ALICE_LOG_ERRORF("[Loading] Render systems initialization failed.");
			return false;
		}
		ALICE_LOG_INFO("[Loading] Render systems initialized.");
		progressBase += kRenderWeight;
		if (!AdvanceTo(progressBase, 0.20f))
			return false;

		ALICE_LOG_INFO("[Loading] Initializing compute effect system...");
		if (!InitializeComputeEffectSystem())
		{
			ALICE_LOG_ERRORF("[Loading] Compute effect system initialization failed.");
			return false;
		}
		preloadCtx.forward = m_forwardRenderSystem.get();
		preloadCtx.deferred = m_deferredRenderSystem.get();
		preloadCtx.vfx = m_unityVfxMeshRenderSystem.get();
		preloadCtx.compute = m_computeEffectSystem.get();
		preloadCtx.renderDevice = m_renderDevice.get();
		preloadCtx.useForward = m_useForwardRendering;
		ALICE_LOG_INFO("[Loading] Compute effect system initialized.");
		progressBase += kComputeWeight;
		if (!AdvanceTo(progressBase, 0.15f))
			return false;

		// 프리로드 단계
		const size_t total = preloadList.size();
		size_t loaded = 0;
		const float preloadBase = progressBase;
		const float preloadWeight = std::max(0.0f, 1.0f - preloadBase);

		if (total == 0)
		{
			ALICE_LOG_INFO("[Loading] Preload list empty.");
			progressBase = preloadBase + preloadWeight;
			if (!AdvanceTo(progressBase, 0.15f))
				return false;
		}
		else
		{
			ALICE_LOG_INFO("[Loading] Preloading %zu items...", total);
			float stepTime = 0.015f;
			if (total <= 6) stepTime = 0.08f;
			else if (total <= 24) stepTime = 0.03f;

			for (const auto& path : preloadList)
			{
				if (!preloadCtx.PreloadByType(path, true))
				{
					ALICE_LOG_WARN("Preload failed: %s", path.c_str());
				}

				++loaded;
				const float frac = static_cast<float>(loaded) / static_cast<float>(total);
				const float nextTarget = preloadBase + preloadWeight * frac;
				if (!AdvanceTo(nextTarget, stepTime))
					return false;
			}
			ALICE_LOG_INFO("[Loading] Preload finished.");
		}

		if (!m_editorMode && m_computeEffectSystem)
		{
			if (auto* statusText = loadingWorld.GetComponent<UITextComponent>(statusId))
				statusText->text = "컴퓨트 이펙트 워밍업.";
			if (!RenderFrame(CalcDelta(), true))
				return false;

			const auto warmBegin = clock::now();
			const std::size_t warmedTotal = m_computeEffectSystem->PrewarmAllRegisteredPresets(1);
			const double warmMs = std::chrono::duration<double, std::milli>(clock::now() - warmBegin).count();
			ALICE_LOG_INFO("[Loading] Build-mode compute preset warmup finished (from preload=%zu, total=%zu, %.2fms).",
				preloadCtx.warmedComputePresetCounts.size(), warmedTotal, warmMs);

			progressBase = (std::min)(1.0f, progressBase + 0.04f);
			if (!AdvanceTo(progressBase, 0.06f))
				return false;
		}

		if (!AdvanceTo(1.0f, 0.10f))
			return false;

		// 완료 상태
		displayedProgress = 1.0f;
		targetProgress = 1.0f;
		if (auto* gauge = loadingWorld.GetComponent<UIGaugeComponent>(gaugeId))
			gauge->value = 1.0f;
		if (auto* statusText = loadingWorld.GetComponent<UITextComponent>(statusId))
			statusText->text = "쉐이더 컴파일 완료";
		if (auto* hintText = loadingWorld.GetComponent<UITextComponent>(hintId))
			hintText->text = "마우스를 클릭하여 시작";
		if (auto* gaugeFx = loadingWorld.GetComponent<UIEffectComponent>(gaugeId))
			gaugeFx->glowEnabled = true;

		float glowTime = 0.0f;
		while (true)
		{
			float dt = CalcDelta();
			glowTime += dt;
			if (auto* gaugeFx = loadingWorld.GetComponent<UIEffectComponent>(gaugeId))
				gaugeFx->glowStrength = 0.2f + 0.15f * (0.5f + 0.5f * std::sin(glowTime * 3.0f));

			if (!RenderFrame(dt, false))
			{
				ALICE_LOG_WARN("[Loading] RenderFrame failed during completion wait.");
				return false;
			}

			if (m_inputSystem.IsMouseButtonPressed(0))
				break;

			std::this_thread::sleep_for(std::chrono::milliseconds(8));
		}

		return true;
	}

	void Engine::Impl::InitializeCameraAndScriptHotReload()
	{
		m_cameraPosition = { 0.0f, 2.0f, -5.0f };
		m_camera.SetLookAt(m_cameraPosition, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
		m_camera.SetPerspective(DirectX::XM_PIDIV4,
			static_cast<float>(m_width) / m_height, 0.1f, 5000.0f);

		ScriptHotReload_Load();
	}

	bool Engine::Impl::InitializeScene(const std::filesystem::path& exeDir)
	{
		m_resourceManager.Clear();
		m_sceneManager = std::make_unique<SceneManager>(m_world, m_resourceManager);

		bool isSceneLoaded = false;

		if (!m_editorMode)
		{
			isSceneLoaded = LoadStartupSceneFromBuildSettings(
				m_world, m_resourceManager, exeDir);
		}

		if (!isSceneLoaded)
		{
			m_sceneManager->SwitchToImmediate("SampleScene");
			ALICE_LOG_INFO("Engine::Initialize: Loaded SampleScene (Fallback or Editor).");
		}

		if (!m_editorMode)
		{
			using clock = std::chrono::steady_clock;
			const auto warmBegin = clock::now();

			std::size_t warmedUnityEffects = 0;
			std::size_t warmedComputePresets = 0;
			std::unordered_set<std::string> seenEffectKeys;
			std::unordered_map<std::string, std::pair<std::string, std::uint32_t>> presetEmitterCounts;

			for (auto&& [entityId, unityVfx] : m_world.GetComponents<UnityVfxComponent>())
			{
				(void)entityId;
				if (unityVfx.effectPath.empty())
					continue;

				const std::string effectPath = NormalizeDerivedLogicalPathRuntime(unityVfx.effectPath);
				if (effectPath.empty())
					continue;

				const std::string effectKey = ToLowerAsciiRuntime(effectPath);
				if (!seenEffectKeys.insert(effectKey).second)
					continue;

				bool warmedThisPath = false;
				if (m_unityVfxMeshRenderSystem && m_unityVfxMeshRenderSystem->PreloadEffect(effectPath))
					warmedThisPath = true;
				if (m_computeEffectSystem && m_computeEffectSystem->PrewarmUnityEffect(effectPath, 1))
					warmedThisPath = true;

				if (warmedThisPath)
					++warmedUnityEffects;
			}

			for (auto&& [entityId, effect] : m_world.GetComponents<ComputeEffectComponent>())
			{
				(void)entityId;
				std::string presetName = TrimAsciiRuntime(effect.shaderName);
				if (presetName.empty())
					presetName = "Particle";

				const std::string presetKey = ToLowerAsciiRuntime(presetName);
				auto [it, inserted] = presetEmitterCounts.emplace(
					presetKey,
					std::make_pair(presetName, 0u));
				if (inserted)
					it->second.first = presetName;
				if (it->second.second < (std::numeric_limits<std::uint32_t>::max)())
					++it->second.second;
			}

			if (m_computeEffectSystem)
			{
				for (const auto& [presetKey, entry] : presetEmitterCounts)
				{
					(void)presetKey;
					const std::string& presetName = entry.first;
					const std::uint32_t emitterCount = (std::max)(1u, entry.second);
					if (m_computeEffectSystem->PrewarmPreset(presetName, emitterCount))
						++warmedComputePresets;
				}
			}

			const double warmMs = std::chrono::duration<double, std::milli>(clock::now() - warmBegin).count();
			ALICE_LOG_INFO("[Loading] Scene VFX warmup finished (unityEffects=%zu, computePresets=%zu, %.2fms).",
				warmedUnityEffects, warmedComputePresets, warmMs);
		}

		return true;
	}

	bool Engine::Impl::InitializePhysicsSystemAndWorldCallbacks()
	{
		m_physicsSystem = std::make_unique<PhysicsSystem>(m_world);
		m_physicsSystem->SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);

		m_world.SetOnBeforeClearCallback([this]() {
			if (m_physicsSystem)
			{
				if (auto pwShared = m_world.GetPhysicsWorldShared())
					pwShared->Flush();

				m_physicsSystem->SetPhysicsWorld(nullptr);
			}

			m_physAccum = 0.0f;
			m_physicsEventQueue.clear();
		});

		RefreshPhysicsForCurrentWorld();
		return true;
	}

	void Engine::Impl::InitializePostLoadBindings(Engine& owner)
	{
		EnsureSkinnedMeshesRegisteredForWorld();

		m_scriptSystem.SetServices(&m_inputSystem, m_sceneManager.get(),
			&m_resourceManager, &m_skinnedMeshRegistry);

		m_scriptSystem.onAfterSceneLoaded.BindObject(&owner, &Engine::EnsureSkinnedMeshesRegisteredForWorld);
		m_scriptSystem.onTrimVideoMemory.BindObject(&owner, &Engine::TrimVideoMemory);
		m_scriptSystem.onAfterSceneLoaded.BindObject(&owner, &Engine::RefreshPhysicsForCurrentWorld);
		m_scriptSystem.onSetDeltaTimeStopped.BindObject(&owner, &Engine::StopDeltaTime);
		m_scriptSystem.onGetDeltaTimeStopped.BindObject(&owner, &Engine::IsDeltaTimeStopped);
		m_scriptSystem.onGetGameDeltaTime.BindObject(&owner, &Engine::GetGameDeltaTime);
		m_scriptSystem.onGetUnscaledDeltaTime.BindObject(&owner, &Engine::GetUnscaledDeltaTime);
	}

	void Engine::Impl::SavePvdSettings(const std::filesystem::path& exeDir)
	{
		SavePvdSettingsFile(exeDir, m_pvdEnabled, m_pvdHost, m_pvdPort);
	}
}
