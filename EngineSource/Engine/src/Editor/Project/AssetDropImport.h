#pragma once

#include <filesystem>
#include <vector>

namespace Alice::AssetDropImport
{
    /// 탐색기에서 드롭된 파일들을 확장자 규칙에 따라 프로젝트로 복사한다.
    /// 폴더는 재귀 처리. 복사 후 ResourceManager negative cache를 비운다.
    void Handle(const std::vector<std::filesystem::path>& files,
                const std::filesystem::path& projectRoot);
}
