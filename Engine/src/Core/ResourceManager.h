#pragma once

#include <filesystem>
#include <string_view>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <utility>
#include <cassert>
#include <wrl/client.h>
#include "Core/Singleton.h"

// D3D11 타입 전방 선언 (헤더에 d3d11.h 포함 방지)
struct ID3D11Device;
struct ID3D11ShaderResourceView;

// JSON 전방 선언 (json은 typedef이므로 json_fwd.hpp 사용)
#include "json/json_fwd.hpp"

namespace Alice
{
    // [템플릿 확장을 위한 로더 구조체 선언]
    // 이 구조체를 특수화하여 타입별 로딩 전략을 정의합니다.
    template <typename T>
    struct ResourceLoader;

    /// - 이후 텍스처/메시/셰이더 등을 캐싱/스트리밍하는 쪽으로 확장할 수 있습니다.
    /// - 현재는 "암호화/복호화된 바이너리 파일 입출력" 만 담당합니다.
    class ResourceManager : public Singleton<ResourceManager>
    {
    public:
        ResourceManager()
        {
            assert(s_instance == nullptr && "Double creation of ResourceManager!");
            s_instance = this;
        }
        ~ResourceManager() = default;

        /// 게임 모드 여부 확인
        bool IsGameMode() const { return m_gameMode; }

        /// GameMode(배포용 실행)인지 여부에 따라, Assets/Resource/Cooked 루트 해석 기준을 설정합니다.
        /// - editorMode(false): 프로젝트 루트(= exeDir 기준 3단계 상위)를 기준으로 Assets/Resource/Cooked 를 찾습니다.
        /// - gameMode(true)   : exeDir(= 실행 파일 폴더) 기준으로 Assets/Resource/Cooked 를 찾습니다.
        ///
        /// 사용 예:
        ///   resources.Configure(/*gameMode=*/!m_editorMode, exeDir);
        void Configure(bool gameMode, const std::filesystem::path& exeDir);

        /// 논리 경로("Assets/..", "Resource/..", "Cooked/..")를 실제 경로로 변환합니다.
        /// - "../Assets/..." 같은 레거시 경로도 자동으로 "Assets/..." 로 정규화해서 처리합니다.
        std::filesystem::path Resolve(const std::filesystem::path& logicalOrRelative) const;

        /// 루트 디렉터리들(디버그/로그/툴에서 사용)
        const std::filesystem::path& RootDir()   const { return m_rootDir; }
        std::filesystem::path        AssetsDir() const { return m_rootDir / "Assets"; }
        std::filesystem::path        MetasDir() const { return m_rootDir / "Metas"; }
        std::filesystem::path        ResourceDir() const { return m_rootDir / "Resource"; }
        std::filesystem::path        CookedDir() const { return m_rootDir / "Cooked"; }

        /// 보관 중인 리소스를 모두 정리합니다.
        void Clear();

        /// 바이너리 파일을 읽어옵니다.
        /// - encrypted 가 true 이면, 간단한 XOR 기반 복호화를 수행합니다.
        bool LoadBinary(const std::filesystem::path& path,
                        std::vector<std::uint8_t>& outData,
                        bool encrypted) const;

        /// "논리 경로"를 받아서 자동으로 로드합니다.
        /// - gameMode 에서는 Cooked 쪽(암호화)을 우선 사용합니다.
        /// - editorMode 에서는 원본(Resource/Assets) 파일을 우선 사용합니다.
        bool LoadBinaryAuto(const std::filesystem::path& logicalPath,
                            std::vector<std::uint8_t>& outData) const;

        /// LoadBinaryAuto의 shared_ptr 버전 (내부 캐시 사용)
        std::shared_ptr<const std::vector<std::uint8_t>> LoadSharedBinaryAuto(const std::filesystem::path& logicalPath) const;

        /// 원본 파일을 읽어 간단히 암호화해서 대상 경로에 저장합니다.
        /// - "쿠킹(cooking)" 용도로 사용합니다.
        bool CookAndSave(const std::filesystem::path& srcPath,
                         const std::filesystem::path& cookedPath) const;

        /// 메모리(평문) 바이트를 바로 암호화해서 저장합니다. (임시 평문 파일 생성 금지용)
        bool CookAndSaveBytes(const std::vector<std::uint8_t>& plainBytes,
                              const std::filesystem::path& cookedPath) const;

        /// 디렉터리 전체를 암호화 Cooked 로 내보냅니다.
        /// - srcDir 하위의 파일들을 dstDir 하위에 동일한 상대 경로로 저장합니다.
        bool CookDirectoryRecursive(const std::filesystem::path& srcDir,
                                    const std::filesystem::path& dstDir) const;

        /// Resource 폴더를 "폴더구조를 숨긴 청크 파일들"로 Cooked/Chunks 아래에 패킹합니다.
        /// - 입력: Resource/<rel>
        /// - 출력: Cooked/Chunks/<hash>/c0000.alice, c0001.alice...
        bool CookResourceToChunkStore(const std::filesystem::path& resourceDirAbs,
                                      const std::filesystem::path& cookedDirAbs,
                                      std::size_t chunkBytes = 256 * 1024) const;

        /// 게임 실행 시 필수 데이터(청크)가 모두 존재하는지 검증합니다.
        /// Manifest.alice 파일을 읽어 실제 파일 존재 여부를 확인합니다.
        /// - 게임 모드에서만 호출해야 합니다.
        /// - 하나라도 파일이 없으면 false를 반환합니다.
        bool ValidateGameData() const;

        /// -----------------------------------------------------------------------
        /// [템플릿 로드 함수]
        /// 사용법: auto srv = ResourceManager::Get().Load<ID3D11ShaderResourceView>("Path", device);
        /// -----------------------------------------------------------------------
        template <typename T, typename... Args>
        auto Load(const std::filesystem::path& logicalPath, Args&&... args) const
        {
            // 컴파일러는 ResourceLoader<T>의 선언을 보고 반환 타입을 추론합니다.
            // 구현은 cpp에 있어도 링킹 시점에 해결됩니다.
            return ResourceLoader<T>::Load(*this, logicalPath, std::forward<Args>(args)...);
        }

        /// LoadData는 Load의 별칭 (하위 호환성)
        template <typename T, typename... Args>
        auto LoadData(const std::filesystem::path& logicalPath, Args&&... args) const
        {
            return Load<T>(logicalPath, std::forward<Args>(args)...);
        }

        /// 텍스트 파일 로드 (JSON, .mat, .fbxasset 등)
        bool LoadText(const std::filesystem::path& logicalPath, std::string& outText) const;

        /// 이미지 파일 경로인지 확인 (확장자 기반)
        static bool IsImageLogicalPath(const std::filesystem::path& p);

        /// 절대 경로를 논리 경로로 정규화 (public 유틸)
        static std::filesystem::path NormalizeResourcePathAbsoluteToLogical(const std::filesystem::path& p);

    private:
        /// 매우 단순한 XOR 기반 스트림 암·복호화
        void XorCrypt(std::vector<std::uint8_t>& data) const;

        static bool StartsWith(std::string_view s, std::string_view prefix);
        static std::filesystem::path NormalizeLegacyDotDot(const std::filesystem::path& p);
        static std::filesystem::path ToAlicePath(std::filesystem::path p);
        static std::uint64_t Fnv1a64Bytes(const std::uint8_t* data, std::size_t size);
        static std::uint64_t HashString64(std::string_view s);
        static std::uint64_t ComputeBufferHashSampled(const std::vector<std::uint8_t>& data);

        std::shared_ptr<const std::vector<std::uint8_t>> LoadResourceChunksByRel(std::string_view resourceRel) const;
        std::filesystem::path Chunk0PathForResourceRel(std::string_view resourceRel) const;

        std::shared_ptr<const std::vector<std::uint8_t>> LoadMetasChunksByRel(std::string_view assetsRel) const;
        std::filesystem::path Chunk0PathForMetasRel(std::string_view assetsRel) const;

        // 필요하면 나중에 키를 외부에서 주입받을 수 있게 바꿀 수 있습니다.
        const std::string m_key = "AliceRendererSimpleKey";

        bool m_gameMode = false;
        std::filesystem::path m_rootDir; // editorMode: projectRoot, gameMode: exeDir

        // 내부 캐시 (AssetManager 방식: 해시 기반 + weak_ptr)
        mutable std::mutex m_cacheMutex;
        mutable std::unordered_map<std::uint64_t, std::weak_ptr<const std::vector<std::uint8_t>>> m_blobCache; // key: contentHash
        mutable std::unordered_map<std::string, std::uint64_t> m_pathToHash; // logicalPath -> contentHash
    };

    // -----------------------------------------------------------------------
    // [특수화 선언] 
    // 헤더에는 "이런 타입의 로더가 있다"는 것만 알리고, 구현({ ... })은 하지 않습니다.
    // -----------------------------------------------------------------------

    // ID3D11ShaderResourceView (Texture) 특수화
    template <>
    struct ResourceLoader<ID3D11ShaderResourceView>
    {
        // 리턴 타입: ComPtr
        using ReturnType = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;

        // Load 함수 선언 (구현은 CPP 파일에서)
        static ReturnType Load(const ResourceManager& rm, 
                               const std::filesystem::path& path, 
                               ID3D11Device* device); 
    };

    // std::string 텍스트 파일 특수화
    template <>
    struct ResourceLoader<std::string>
    {
        using ReturnType = std::shared_ptr<std::string>;
        static ReturnType Load(const ResourceManager& rm, 
                               const std::filesystem::path& path);
    };

    // nlohmann::json JSON 파일 특수화
    template <>
    struct ResourceLoader<nlohmann::json>
    {
        using ReturnType = std::shared_ptr<nlohmann::json>;
        static ReturnType Load(const ResourceManager& rm, 
                               const std::filesystem::path& path);
    };
}

