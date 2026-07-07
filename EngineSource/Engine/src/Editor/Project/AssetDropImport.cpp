#include "Editor/Project/AssetDropImport.h"

#include "Runtime/Foundation/Logger.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Importing/FbxImporter.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

namespace Alice::AssetDropImport
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string LowerExt(const fs::path& p)
        {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext;
        }

        // 확장자 → 프로젝트 내 대상 폴더 (스펙의 규칙 테이블)
        // 반환이 빈 경로면 미지원 확장자.
        fs::path TargetDirFor(const fs::path& file, const fs::path& projectRoot)
        {
            const std::string ext = LowerExt(file);
            const std::string stem = file.stem().string();

            if (ext == ".fbx")
                return projectRoot / "Resource" / "fbx" / stem;
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga")
                return projectRoot / "Resource" / "Textures" / stem;
            if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")
                return projectRoot / "Resource" / "Sound";
            if (ext == ".ttf" || ext == ".otf")
                return projectRoot / "Resource" / "Fonts";
            return {};
        }

        // 대상에 같은 이름이 있으면 "이름_1", "이름_2"... 로 회피
        fs::path UniqueDestPath(const fs::path& dir, const fs::path& filename)
        {
            fs::path dest = dir / filename;
            std::error_code ec;
            int suffix = 1;
            while (fs::exists(dest, ec) && !ec)
            {
                fs::path renamed = filename.stem();
                renamed += "_" + std::to_string(suffix++);
                renamed += filename.extension();
                dest = dir / renamed;
            }
            return dest;
        }

        // 복사된 .fbx에 대해 기존 Load FBX 파이프라인(FbxImporter::Import)을 그대로 실행해
        // .fbxasset까지 생성한다. device가 없으면(=호출자가 미지원) 복사만으로 종료한다.
        void ImportFbxAsset(const fs::path& dest, ID3D11Device* device, SkinnedMeshRegistry* skinnedRegistry)
        {
            FbxImportOptions opt{};
            FbxImporter importer(ResourceManager::Get(), skinnedRegistry);
            FbxImportResult result = importer.Import(device, dest, opt);
            if (result.meshAssetPath.empty())
            {
                ALICE_LOG_WARN("[DropImport] fbxasset generation failed: \"%s\"", dest.string().c_str());
                return;
            }
            ALICE_LOG_INFO("[DropImport] fbxasset generated: \"%s\"", result.instanceAssetPath.c_str());
        }

        void ImportOneFile(const fs::path& file, const fs::path& projectRoot,
                           ID3D11Device* device, SkinnedMeshRegistry* skinnedRegistry,
                           std::size_t& imported, std::size_t& skipped)
        {
            const fs::path targetDir = TargetDirFor(file, projectRoot);
            if (targetDir.empty())
            {
                ALICE_LOG_WARN("[DropImport] unsupported extension, skipped: \"%s\"",
                               file.string().c_str());
                ++skipped;
                return;
            }

            std::error_code ec;
            fs::create_directories(targetDir, ec);
            ec.clear();

            const fs::path dest = UniqueDestPath(targetDir, file.filename());
            fs::copy_file(file, dest, ec);
            if (ec)
            {
                ALICE_LOG_ERRORF("[DropImport] copy failed: \"%s\" -> \"%s\" (%s)",
                                 file.string().c_str(), dest.string().c_str(), ec.message().c_str());
                ++skipped;
                return;
            }

            ALICE_LOG_INFO("[DropImport] imported: \"%s\" -> \"%s\"",
                           file.string().c_str(), dest.string().c_str());
            ++imported;

            if (device && LowerExt(file) == ".fbx")
                ImportFbxAsset(dest, device, skinnedRegistry);
        }
    }

    void Handle(const std::vector<fs::path>& files, const fs::path& projectRoot,
                ID3D11Device* device, SkinnedMeshRegistry* skinnedRegistry)
    {
        std::size_t imported = 0, skipped = 0;
        std::error_code ec;

        for (const auto& item : files)
        {
            if (fs::is_directory(item, ec) && !ec)
            {
                for (fs::recursive_directory_iterator it(item, ec), end; it != end; it.increment(ec))
                {
                    if (ec) { ec.clear(); continue; }
                    if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
                    ImportOneFile(it->path(), projectRoot, device, skinnedRegistry, imported, skipped);
                }
            }
            else
            {
                ec.clear();
                ImportOneFile(item, projectRoot, device, skinnedRegistry, imported, skipped);
            }
        }

        if (imported > 0)
            ResourceManager::Get().ClearNegativeCache();

        ALICE_LOG_INFO("[DropImport] done. imported=%zu skipped=%zu", imported, skipped);
    }
}
