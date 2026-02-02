#include "Runtime/ECS/World.h"
#include "Runtime/Resources/SceneFile.h"
#include "Runtime/Foundation/Logger.h"
#include "ThirdParty/json/json.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Alice
{
	// BuildSettings.json 파싱 및 시작 씬 로드
	bool LoadStartupSceneFromBuildSettings(World& world, const std::filesystem::path& exeDir)
	{
		// 1. 설정 파일 경로 확보 (Exe위치 -> 프로젝트 루트 순)
		std::filesystem::path cfg = exeDir / "BuildSettings.json";
		if (!std::filesystem::exists(cfg))
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

		// 3. 타겟 씬 결정 및 경로 보정
		if (target.empty() && !scenes.empty()) target = scenes.front();
		if (target.empty()) return false;

		std::filesystem::path finalPath = target;
		if (!finalPath.is_absolute())
		{
			// Exe 기준 존재 여부 확인 후, 없으면 루트 기준 적용
			if (std::filesystem::exists(exeDir / finalPath)) finalPath = exeDir / finalPath;
			else finalPath = exeDir.parent_path().parent_path().parent_path() / finalPath;
		}

		ALICE_LOG_INFO("Loading Startup Scene: %s", finalPath.string().c_str());

		// 4. 로드 실패 검사
		if (!SceneFile::Load(world, finalPath))
		{
			ALICE_LOG_ERRORF("Scene Load Failed: %s", finalPath.string().c_str());
			return false;
		}

		return true;
	}
}
