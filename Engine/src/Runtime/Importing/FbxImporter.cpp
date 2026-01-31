#include "Runtime/Importing/FbxImporter.h"

#include <algorithm>
#include <system_error>
#include <cstring>
#include <fstream>
#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <DirectXMath.h>

#include <assimp/scene.h>
#include <assimp/material.h>

#include "Runtime/ECS/World.h"          // MaterialComponent 정의
#include "Runtime/Rendering/Data/Material.h"       // MaterialFile::Save
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Importing/FbxModel.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"   // SkinnedMeshGPU / SkinnedMeshRegistry
#include <Runtime/Foundation/Helper.h>
#include "Runtime/Importing/FbxAsset.h"

namespace Alice
{
    namespace
    {
        inline char ToLowerChar(unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }

        inline void ToLowerInPlace(std::string& s)
        {
            for (char& c : s) c = ToLowerChar(static_cast<unsigned char>(c));
        }

        // 간단한 이미지 확장자 체크 함수입니다.
        inline bool IsImageFile(const std::filesystem::path& path)
        {
            const std::string ext = path.extension().string();
            if (ext.empty()) return false;

            std::string lower = ext;
            ToLowerInPlace(lower);

            return lower == ".png"  || lower == ".jpg"  || lower == ".jpeg" ||
                   lower == ".tga"  || lower == ".bmp"  || lower == ".dds";
        }

        inline bool EndsWith(std::string_view s, std::string_view suffix)
        {
            return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
        }

        // "...\Resource\<rel>" absolute 경로를 "Resource/<rel>" 로 최대한 정규화합니다.
        inline std::string NormalizeToResourceLogical(const std::filesystem::path& p)
        {
            if (!p.is_absolute())
                return p.generic_string();

            const std::string s = p.generic_string();
            std::string lower = s;
            ToLowerInPlace(lower);
            const std::string needle = "/resource/";
            const auto pos = lower.find(needle);
            if (pos == std::string::npos)
                return s;

            const std::string rel = s.substr(pos + needle.size());
            return (std::filesystem::path("Resource") / std::filesystem::path(rel)).generic_string();
        }

        static void AppendSkeletonText(const std::vector<FbxSkeletonNode>& nodes,
                                       int idx,
                                       int depth,
                                       std::string& text)
        {
            if (idx < 0)
                return;
            if (idx >= static_cast<int>(nodes.size()))
                return;

            const FbxSkeletonNode& n = nodes[static_cast<std::size_t>(idx)];
            text.append(static_cast<std::size_t>(depth * 2), ' ');
            text += n.name;
            text += "\n";

            for (int child : n.children)
                AppendSkeletonText(nodes, child, depth + 1, text);
        }

        // 텍스처를 Resource/Textures/<ModelName>/ 에 저장하고 논리 경로를 반환
        // 쿠킹은 하지 않음 (전역 빌드 프로세스가 청크로 변환)
        static std::string ProcessTexture(ResourceManager& rm,
                                         const aiScene* scene,
                                         const aiMaterial* mat,
                                         aiTextureType type,
                                         const std::filesystem::path& fbxPath,
                                         const std::string& modelName,
                                         const std::string& typeSuffix,
                                         int& embeddedCounter)
        {
            namespace fs = std::filesystem;

            if (!scene || !mat)
                return {};

            aiString texPath;
            if (mat->GetTexture(type, 0, &texPath) != AI_SUCCESS)
                return {};

            const std::string rawPath = texPath.C_Str();
            if (rawPath.empty())
                return {};

            // 목적지 디렉터리: Resource/Textures/<ModelName>
            fs::path destDir = fs::path("Resource") / "Textures" / modelName;
            std::error_code ec;
            fs::path destDirAbs = rm.Resolve(destDir);
            fs::create_directories(destDirAbs, ec);

            // 목적지 파일명 결정
            std::string destFilename;
            const aiTexture* embedded = scene->GetEmbeddedTexture(rawPath.c_str());

            if (embedded)
            {
                // 임베디드: <Model>_<Suffix>_<Index>.<Ext> 형식
                std::string ext = embedded->achFormatHint;
                if (ext.empty())
                    ext = (embedded->mHeight == 0) ? "png" : "dds";
                if (ext.empty())
                    ext = "png"; // 폴백
                destFilename = modelName + "_" + typeSuffix + "_" + std::to_string(embeddedCounter++) + "." + ext;
            }
            else
            {
                // 외부 파일: 원본 파일명 유지
                destFilename = fs::path(rawPath).filename().string();
            }

            fs::path finalAbsPath = destDirAbs / destFilename;

            // 저장 또는 복사
            if (embedded)
            {
                // 임베디드 데이터를 Resource/Textures/... 에 직접 저장
                std::ofstream ofs(finalAbsPath, std::ios::binary);
                if (ofs.is_open())
                {
                    size_t size = (embedded->mHeight == 0) 
                        ? embedded->mWidth 
                        : (static_cast<size_t>(embedded->mWidth) * static_cast<size_t>(embedded->mHeight) * sizeof(aiTexel));
                    if (size > 0)
                    {
                        ofs.write(reinterpret_cast<const char*>(embedded->pcData), static_cast<std::streamsize>(size));
                    }
                }
            }
            else
            {
                // 외부 파일 복사 (존재하지 않을 때만)
                fs::path srcAbsPath = rawPath;
                if (!srcAbsPath.is_absolute())
                {
                    srcAbsPath = fbxPath.parent_path() / rawPath;
                    srcAbsPath = srcAbsPath.lexically_normal();
                }

                if (fs::exists(srcAbsPath) && !fs::exists(finalAbsPath))
                {
                    fs::copy_file(srcAbsPath, finalAbsPath, fs::copy_options::overwrite_existing, ec);
                    if (ec)
                    {
                        ALICE_LOG_WARN("[FbxImporter] ProcessTexture: failed to copy \"%s\" -> \"%s\": %s",
                            srcAbsPath.string().c_str(), finalAbsPath.string().c_str(), ec.message().c_str());
                        return {};
                    }
                }
            }

            // 논리 경로 반환 (엔진에서 사용, 청크는 이 경로로 매핑됨)
            fs::path logical = fs::path("Resource/Textures") / modelName / destFilename;
            return logical.generic_string();
        }
    }

    FbxImporter::FbxImporter(ResourceManager& resources,
                             SkinnedMeshRegistry* meshRegistry)
        : m_resources(resources)
        , m_meshRegistry(meshRegistry)
    {
    }

    FbxImportResult FbxImporter::Import(ID3D11Device* device,
                                        const std::filesystem::path& fbxPath,
                                        const FbxImportOptions& /*options*/)
    {
        auto model = std::make_shared<FbxModel>();
        FbxImportResult result;

        ALICE_LOG_INFO("[FbxImporter] Import start: path=\"%s\"", fbxPath.string().c_str());

        // 절대 "decrypted 임시파일"을 만들지 않습니다.
        // - 파일이 있으면 그대로 파일 로드
        // - 없으면(Resource/Cooked/Chunks) 메모리에서 복호화된 바이트를 받아 Assimp ReadFileFromMemory 로 로드
        namespace fs = std::filesystem;

        //const bool fileExists = !fbxPath.empty() && fs::exists(fbxPath);
		const fs::path resolved = m_resources.Resolve(fbxPath);
        const bool isChunk = (resolved.extension() == ".alice");   // Cooked/Chunks/.../*.alice
		const bool fileExists = !isChunk && fs::exists(resolved);

        // 키/생성물 이름은 항상 원래 요청된 fbxPath 기준(stem)으로 고정함
        // C:/Models/Robot/robot_01.fbx -> 	robot_01
        std::string baseName = std::filesystem::path(fbxPath).stem().string();

        // 논리 경로 정규화 (ResourceManager가 처리할 수 있도록)
        std::filesystem::path resolvedLogical = fbxPath;
        if (resolvedLogical.is_absolute())
        {
            // 절대 경로를 논리 경로로 변환
            resolvedLogical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(resolvedLogical);
        }

        if (fileExists)
        {
            // 파일이 존재하는 경우: 새로운 ResourceManager 기반 Load 사용
            if (!model->Load(device, m_resources, resolvedLogical))
            {
                ALICE_LOG_ERRORF("[FbxImporter] FbxModel::Load FAILED for \"%s\"\n", resolved.string().c_str());
                return result;
            }
        }
        else
        {
            // 청크/메모리 로드: ResourceManager 기반 LoadFromMemory 사용
            auto sp = m_resources.LoadSharedBinaryAuto(resolvedLogical);
            if (!sp || sp->empty())
            {
                ALICE_LOG_ERRORF("[FbxImporter] Import FAILED: cooked load failed \"%s\"\n",
                    resolvedLogical.string().c_str());
                return result;
            }

            if (!model->LoadFromMemory(device, m_resources, resolvedLogical, sp->data(), sp->size(), baseName + ".fbx"))
            {
                ALICE_LOG_ERRORF("[FbxImporter] FbxModel::LoadFromMemory FAILED for \"%s\" (bytes=%zu)\n", resolvedLogical.string().c_str(), sp->size());
                return result;
            }
        }

        const aiScene* scene = model->GetScenePtr();
        if (!scene)
        {
            ALICE_LOG_ERRORF("[FbxImporter] model.GetScenePtr() returned null for \"%s\"\n",
                resolvedLogical.string().c_str());
            return result;
        }

        // 0-1) 스키닝 메시 GPU 를 레지스트리에 등록
        //     - FBX 모델이 유효하고 레지스트리가 주입된 경우에만 수행합니다.
        if (m_meshRegistry && model->HasMesh())
        {
            auto gpu = std::make_shared<SkinnedMeshGPU>();
            gpu->vertexBuffer = model->GetVertexBuffer(); // AddRef 발생
            gpu->indexBuffer  = model->GetIndexBuffer();
            gpu->stride       = model->GetVertexStride();
            gpu->indexCount   = static_cast<UINT>(model->GetIndexCount());
            gpu->startIndex   = 0;
            gpu->baseVertex   = 0;

            // 서브셋 / 머티리얼 SRV 복사
            gpu->subsets = model->GetSubsets();
            const auto& matSrvs = model->GetMaterialSRVs();
            const auto& nrmSrvs = model->GetNormalSRVs();
            gpu->materialSRVs.resize(matSrvs.size());
            gpu->normalSRVs.resize(nrmSrvs.size());
            gpu->materialOverridePaths.resize(matSrvs.size());
            for (std::size_t i = 0; i < matSrvs.size(); ++i)
            {
                gpu->materialSRVs[i] = matSrvs[i]; // ComPtr 으로 AddRef
                gpu->materialOverridePaths[i].clear();
            }
            for (std::size_t i = 0; i < nrmSrvs.size(); ++i)
            {
                gpu->normalSRVs[i] = nrmSrvs[i];
            }

            // 스켈레톤 정보 복사
            if (model->HasSkeleton())
            {
                gpu->skeleton     = model->GetSkeleton();
                gpu->skeletonRoot = model->GetSkeletonRoot();

                // 간단한 본 트리 텍스트 생성 (App.cpp 의 boneDisplayText 와 유사)
                const auto& nodes = gpu->skeleton;
                int root = gpu->skeletonRoot;

                std::string text;
                AppendSkeletonText(nodes, root, 0, text);
                gpu->skeletonText = text;
            }

            // 애니메이션 재생/클립 목록을 위해 원본 컨텍스트를 유지합니다.
            gpu->sourceModel = model;

            const std::string meshKey = baseName;
            m_meshRegistry->Register(meshKey, gpu);

            ALICE_LOG_INFO("[FbxImporter] Registered mesh key=\"%s\" stride=%u indexCount=%u subsets=%zu mats=%zu\n",
                meshKey.c_str(),
                gpu->stride,
                gpu->indexCount,
                gpu->subsets.size(),
                gpu->materialSRVs.size());
        }

        // 3) 텍스처 처리: Resource/Textures/<ModelName>/ 에 저장
        //    쿠킹은 하지 않음 (전역 빌드 프로세스가 Resource 폴더를 청크로 변환)
        //    텍스처는 FBX 파일의 실제 위치를 기준으로 찾아야 하므로 resolved 경로 사용
        int embeddedCounter = 0;
        std::vector<std::string> textureLogicalPaths;

        // 각 머티리얼에서 텍스처 추출 및 저장
        for (unsigned mi = 0; mi < scene->mNumMaterials; ++mi)
        {
            aiMaterial* mat = scene->mMaterials[mi];
            
            // Diffuse/BaseColor 텍스처 (우선순위: DIFFUSE > BASE_COLOR)
            // fbxPath는 원본 경로(절대 또는 논리), resolved는 실제 파일 위치
            std::string albedoPath = ProcessTexture(m_resources, scene, mat, aiTextureType_DIFFUSE, resolved, baseName, "D", embeddedCounter);
            if (albedoPath.empty())
                albedoPath = ProcessTexture(m_resources, scene, mat, aiTextureType_BASE_COLOR, resolved, baseName, "D", embeddedCounter);
            
            if (!albedoPath.empty())
                textureLogicalPaths.push_back(albedoPath);
        }

        // 4) .mat 파일 생성 (서브셋 개수만큼)
        const auto& subsets = model->GetSubsets();
        const size_t matCount = subsets.empty() ? 1 : subsets.size();
        
        fs::path matDir = m_resources.Resolve("Assets/Materials");
        std::error_code ec;
        fs::create_directories(matDir, ec);

        for (std::size_t i = 0; i < matCount; ++i)
        {
            fs::path matPath = matDir / (baseName + "_" + std::to_string(i) + ".mat");

            MaterialComponent matComp;
            matComp.color = DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f);
            matComp.roughness = 0.5f;
            matComp.metalness = 0.0f;
            matComp.assetPath = matPath.string();
            
            // 첫 번째 텍스처를 albedo로 사용 (서브셋별로 매핑 가능하지만 현재는 단순화)
            if (i < textureLogicalPaths.size())
            {
                matComp.albedoTexturePath = textureLogicalPaths[i];
            }

            MaterialFile::Save(matPath, matComp);

            // 상대 경로로 변환하여 저장
            fs::path matPathRelative = fs::path("Assets/Materials") / (baseName + "_" + std::to_string(i) + ".mat");
            result.materialAssetPaths.push_back(matPathRelative.generic_string());
        }

        // 5) 메시 자산의 논리 경로는 FBX 이름을 그대로 사용합니다.
        result.meshAssetPath = baseName;

        // 6) 에디터/World 에서 사용할 인스턴스 에셋(.fbxasset)을 생성합니다.
        //    - 언리얼의 SkeletalMesh 에셋 비슷한 개념으로, FBX 원본과 머티리얼을 묶어 둡니다.
        {
            fs::path fbxAssetDir = m_resources.Resolve("Assets/Fbx");
            std::error_code ec;
            fs::create_directories(fbxAssetDir, ec);

            fs::path fbxAssetPath = fbxAssetDir / (baseName + ".fbxasset");

            FbxInstanceAsset asset;
            asset.sourceFbx = NormalizeToResourceLogical(resolvedLogical);
            asset.meshAssetPath = result.meshAssetPath;
            asset.materialAssetPaths = result.materialAssetPaths;

            if (!SaveFbxInstanceAsset(fbxAssetPath, asset)) return result;

            // 상대 경로로 변환하여 저장 D:\\Github\\AliceRenderer\\Assets\\Materials -> (Assets/Fbx/... 형식)
            fs::path fbxAssetPathRelative = fs::path("Assets/Fbx") / (baseName + ".fbxasset");
            result.instanceAssetPath = fbxAssetPathRelative.generic_string();
        }

        // 디버그 로깅: Import 완료
        {

			ALICE_LOG_INFO("[FbxImporter] Import done: meshAssetPath=\"%s\", materials=%zu\n",
				result.meshAssetPath.c_str(),
				result.materialAssetPaths.size());
        }

        return result;
    }
}


