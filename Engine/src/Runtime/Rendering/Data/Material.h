#pragma once

#include <filesystem>

namespace Alice
{
    struct MaterialComponent;

    /// 간단한 머티리얼 파일 입출력 유틸리티입니다.
    /// - RTTR + JSON 기반 저장/로드입니다.
    class MaterialFile
    {
    public:
        /// .mat 파일에서 머티리얼을 읽어와 MaterialComponent 에 채웁니다.
        /// 파일이 없거나 포맷이 잘못되면 false 를 반환합니다.
        static bool Load(const std::filesystem::path& path, MaterialComponent& outMaterial, class ResourceManager* rm);

        /// MaterialComponent 의 내용을 .mat 파일로 저장합니다.
        /// 상위 디렉터리가 없으면 자동으로 생성합니다.
        static bool Save(const std::filesystem::path& path, const MaterialComponent& material);
    };
}


