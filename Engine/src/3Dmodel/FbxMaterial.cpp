#include "FbxMaterial.h"
#include "../Core/Helper.h"
#include "../Core/ResourceManager.h"

#include <directxtk/WICTextureLoader.h>
#include <directxtk/DDSTextureLoader.h>
#include <DirectXTex.h>
#include <wrl/client.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <filesystem>

using Microsoft::WRL::ComPtr;

// 내부 구현: 베이스 컬러 / 메탈릭 / 러프니스 맵을 각각 관리
struct FbxMaterialLoader::Impl
{
	std::vector<ID3D11ShaderResourceView*> baseColorSRVs;
	std::vector<ID3D11ShaderResourceView*> normalSRVs;
	std::vector<ID3D11ShaderResourceView*> metallicSRVs;
	std::vector<ID3D11ShaderResourceView*> roughnessSRVs;
	std::unordered_map<std::wstring, ID3D11ShaderResourceView*> cache;
	ID3D11ShaderResourceView* white = nullptr; // 기본 색상 / roughness 기본값(1)
	ID3D11ShaderResourceView* black = nullptr; // metallic 기본값(0)
	ID3D11ShaderResourceView* flatNormal = nullptr; // normal 기본값(0.5,0.5,1)
};

FbxMaterialLoader::FbxMaterialLoader() : m_(new Impl) {}
FbxMaterialLoader::~FbxMaterialLoader() { Clear(); delete m_; }

void FbxMaterialLoader::Clear()
{
	for (auto* p : m_->baseColorSRVs) SAFE_RELEASE(p);
	for (auto* p : m_->normalSRVs) SAFE_RELEASE(p);
	for (auto* p : m_->metallicSRVs) SAFE_RELEASE(p);
	for (auto* p : m_->roughnessSRVs) SAFE_RELEASE(p);
	m_->baseColorSRVs.clear();
	m_->normalSRVs.clear();
	m_->metallicSRVs.clear();
	m_->roughnessSRVs.clear();

	for (auto& kv : m_->cache) { SAFE_RELEASE(kv.second); }
	m_->cache.clear();

	SAFE_RELEASE(m_->white);
	SAFE_RELEASE(m_->black);
	SAFE_RELEASE(m_->flatNormal);
	m_->white = nullptr;
	m_->black = nullptr;
	m_->flatNormal = nullptr;
}

const std::vector<ID3D11ShaderResourceView*>& FbxMaterialLoader::GetMaterialSRVs() const
{
	// 기존 코드 호환: diffuse/baseColor 맵
	return m_->baseColorSRVs;
}

const std::vector<ID3D11ShaderResourceView*>& FbxMaterialLoader::GetNormalSRVs() const
{
	return m_->normalSRVs;
}

const std::vector<ID3D11ShaderResourceView*>& FbxMaterialLoader::GetMetallicSRVs() const
{
	return m_->metallicSRVs;
}

const std::vector<ID3D11ShaderResourceView*>& FbxMaterialLoader::GetRoughnessSRVs() const
{
	return m_->roughnessSRVs;
}

static ID3D11ShaderResourceView* FindCached(std::unordered_map<std::wstring, ID3D11ShaderResourceView*>& cache, const std::wstring& key)
{
	auto it = cache.find(key); return (it == cache.end()) ? nullptr : it->second;
}

static void AddCache(std::unordered_map<std::wstring, ID3D11ShaderResourceView*>& cache, const std::wstring& key, ID3D11ShaderResourceView* v)
{
	if (v) { cache[key] = v; v->AddRef(); }
}

// 단색(1x1) 텍스처 SRV 생성 헬퍼
static void CreateSolidColorSRV(ID3D11Device* device, UINT rgba, ID3D11ShaderResourceView** outSRV)
{
	if (!device || !outSRV || *outSRV) return;

	D3D11_TEXTURE2D_DESC td{};
	td.Width = 1;
	td.Height = 1;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_IMMUTABLE;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA sd{};
	sd.pSysMem = &rgba;
	sd.SysMemPitch = sizeof(UINT);

	ComPtr<ID3D11Texture2D> tex;
	HR_T(device->CreateTexture2D(&td, &sd, tex.GetAddressOf()));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
	srvd.Format = td.Format;
	srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvd.Texture2D.MipLevels = 1;
	srvd.Texture2D.MostDetailedMip = 0;
	HR_T(device->CreateShaderResourceView(tex.Get(), &srvd, outSRV));
}

// aiTexture(임베디드 텍스처)로부터 SRV 생성
static ID3D11ShaderResourceView* CreateSRVFromEmbedded(
	ID3D11Device* device,
	const aiTexture* at)
{
	if (!device || !at) return nullptr;

	ComPtr<ID3D11Resource> res;
	ID3D11ShaderResourceView* srv = nullptr;

	if (at->mHeight == 0)
	{
		if (SUCCEEDED(DirectX::CreateWICTextureFromMemory(
			device,
			reinterpret_cast<const uint8_t*>(at->pcData),
			at->mWidth,
			res.GetAddressOf(),
			&srv)))
		{
			return srv;
		}
	}
	else
	{
		D3D11_TEXTURE2D_DESC td{};
		td.Width = at->mWidth;
		td.Height = at->mHeight;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA sd{};
		sd.pSysMem = at->pcData;
		sd.SysMemPitch = at->mWidth * sizeof(aiTexel);

		ComPtr<ID3D11Texture2D> tex;
		if (SUCCEEDED(device->CreateTexture2D(&td, &sd, tex.GetAddressOf())))
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
			srvd.Format = td.Format;
			srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvd.Texture2D.MipLevels = 1;
			srvd.Texture2D.MostDetailedMip = 0;
			if (SUCCEEDED(device->CreateShaderResourceView(tex.Get(), &srvd, &srv)))
			{
				return srv;
			}
		}
	}
	return nullptr;
}


static HRESULT CreateTextureFromTgaFile(ID3D11Device* device, const wchar_t* path, ID3D11ShaderResourceView** outSRV)
{
	if (!device || !path || !outSRV) return E_INVALIDARG;
	*outSRV = nullptr;

	using namespace DirectX;

	TexMetadata metadata{};
	ScratchImage image;
	HRESULT hr = LoadFromTGAFile(path, &metadata, image);
	if (FAILED(hr)) return hr;

	hr = CreateShaderResourceView(
		device,
		image.GetImages(),
		image.GetImageCount(),
		metadata,
		outSRV);
	return hr;
}

// WIC + TGA 지원을 한꺼번에 처리하는 래퍼
static HRESULT CreateTextureFromFileWithTga(
	ID3D11Device* device,
	const std::wstring& path,
	ID3D11Resource** outRes,
	ID3D11ShaderResourceView** outSRV)
{
	if (!device || path.empty() || !outSRV) return E_INVALIDARG;

	ComPtr<ID3D11Resource> dummyRes;
	ID3D11Resource** resPtr = outRes ? outRes : dummyRes.GetAddressOf();

	ID3D11ShaderResourceView* srv = nullptr;
	HRESULT hr = DirectX::CreateWICTextureFromFile(device, path.c_str(), resPtr, &srv);
	if (FAILED(hr))
	{
		// 확장자가 .tga 이면 직접 파싱 시도
		if (path.size() >= 4)
		{
			std::wstring ext = path.substr(path.size() - 4);
			if (ext == L".tga" || ext == L".TGA")
			{
				hr = CreateTextureFromTgaFile(device, path.c_str(), &srv);
			}
		}
	}

	if (FAILED(hr))
	{
		if (srv) srv->Release();
		return hr;
	}

	*outSRV = srv;
	return S_OK;
}

// 공통 텍스처 로더: aiMaterial + aiTextureType 기반으로 한 장 로드
// fbx 내부에 저장된 텍스쳐 검색 
// 없으면 임베드된 경로로 탐색
static ID3D11ShaderResourceView* LoadTextureFromMaterial(
	ID3D11Device* device,
	const aiScene* scene,
	aiMaterial* mat,
	aiTextureType texType,
	const std::wstring& baseDirW,
	std::unordered_map<std::wstring, ID3D11ShaderResourceView*>& cache,
	ID3D11ShaderResourceView* fallback)
{
	if (!device || !scene || !mat) return nullptr;

	aiString texPath;
	ID3D11ShaderResourceView* result = nullptr;
	std::wstring fullPathW;

	// 1. 텍스처 경로 획득 및 임베디드 확인
	if (mat->GetTexture(texType, 0, &texPath) == AI_SUCCESS)
	{
		std::string texPathStr = texPath.C_Str();

		// 임베디드 텍스처를 확인하고 처리 (가장 빠른 경로)
		if (!texPathStr.empty())
		{
			const aiTexture* at = scene->GetEmbeddedTexture(texPathStr.c_str());
			if (at)
			{
				// (Embedded 텍스처를 SRV로 변환하는 함수 호출)
				result = CreateSRVFromEmbedded(device, at);
			}
		}

		// 임베디드 처리에 실패했거나(result == nullptr) 외부 파일인 경우, 탐색 시작
		if (!result)
		{
			// 2. 외부 파일 경로 탐색 로직
			std::wstring wtex = WStringFromUtf8(texPathStr);
			std::filesystem::path baseDir(baseDirW);
			std::filesystem::path fileOnly = std::filesystem::path(wtex).filename();

			// A. 기본 경로 탐색 (절대 경로 or baseDir / wtex)
			std::filesystem::path currentPath = wtex;
			if (!currentPath.is_absolute()) {
				currentPath = baseDir / wtex;
			}

			// B. .fbm 폴더 탐색 (경로를 찾을 때까지 시도)
			if (!std::filesystem::exists(currentPath))
			{
				try
				{
					for (const auto& entry : std::filesystem::directory_iterator(baseDir))
					{
						if (entry.is_directory() &&
							(entry.path().extension() == L".fbm" || entry.path().extension() == L".FBM"))
						{
							// baseDir/.fbm_folder/file_name 으로 경로 대체
							currentPath = entry.path() / fileOnly;
							if (std::filesystem::exists(currentPath)) {
								break; // 찾았으면 반복문 탈출
							}
						}
					}
				}
				catch (...) {} // 탐색 실패는 무시
			}

			fullPathW = currentPath.wstring();

			// 3. 캐시 확인 및 로드
			if (std::filesystem::exists(currentPath))
			{
				if (auto* cached = FindCached(cache, fullPathW))
				{
					result = cached;
					result->AddRef();
				}
				else if (SUCCEEDED(CreateTextureFromFileWithTga(device, fullPathW,
					(ID3D11Resource**)nullptr, &result)))
				{
					AddCache(cache, fullPathW, result);
				}
			}
		}
	}

	if (!result && fallback)
	{
		fallback->AddRef();
		result = fallback;
	}

	return result;
}

// ResourceManager 기반 텍스처 경로 생성 헬퍼
static std::filesystem::path MakeLogicalTexturePath([[maybe_unused]] Alice::ResourceManager& rm,
                                                    const std::filesystem::path& fbxLogicalPath,
                                                    const char* assimpTex)
{
	namespace fs = std::filesystem;
	fs::path p = assimpTex;
	if (p.empty())
		return {};

	if (p.is_absolute())
	{
		// 절대 경로를 논리 경로로 정규화
		return Alice::ResourceManager::NormalizeResourcePathAbsoluteToLogical(p);
	}

	// 상대 경로면 fbxLogicalPath 기준
	fs::path base = fbxLogicalPath.parent_path();
	fs::path out = (base / p).lexically_normal();
	return out;
}

// ResourceManager 기반 텍스처 로더
static ID3D11ShaderResourceView* LoadTextureFromMaterialRM(
	ID3D11Device* device,
	const aiScene* scene,
	aiMaterial* mat,
	aiTextureType texType,
	const std::filesystem::path& fbxLogicalPath,
	Alice::ResourceManager& rm,
	std::unordered_map<std::string, ID3D11ShaderResourceView*>& cache,
	ID3D11ShaderResourceView* fallback)
{
	if (!device || !scene || !mat) return nullptr;

	aiString texPath;
	ID3D11ShaderResourceView* result = nullptr;

	// 1. 텍스처 경로 획득 및 임베디드 확인
	if (mat->GetTexture(texType, 0, &texPath) == AI_SUCCESS)
	{
		std::string texPathStr = texPath.C_Str();

		// 임베디드 텍스처를 확인하고 처리 (가장 빠른 경로)
		if (!texPathStr.empty())
		{
			const aiTexture* at = scene->GetEmbeddedTexture(texPathStr.c_str());
			if (at)
			{
				result = CreateSRVFromEmbedded(device, at);
			}
		}

		// 임베디드 처리에 실패했거나 외부 파일인 경우, ResourceManager로 로드
		if (!result)
		{
			std::filesystem::path logicalTex = MakeLogicalTexturePath(rm, fbxLogicalPath, texPathStr.c_str());
			if (!logicalTex.empty())
			{
				// 캐시 키는 논리 경로 문자열
				std::string cacheKey = logicalTex.generic_string();
				auto it = cache.find(cacheKey);
				if (it != cache.end())
				{
					result = it->second;
					if (result) result->AddRef();
				}
				else
				{
					// ResourceManager를 통해 로드
					auto srv = rm.Load<ID3D11ShaderResourceView>(logicalTex, device);
					if (srv)
					{
						result = srv.Get();
						result->AddRef();
						cache[cacheKey] = result;
					}
				}
			}
		}
	}

	if (!result && fallback)
	{
		fallback->AddRef();
		result = fallback;
	}

	return result;
}

bool FbxMaterialLoader::Load(ID3D11Device* device, const aiScene* scene, const std::filesystem::path& fbxLogicalPath, Alice::ResourceManager& rm)
{
	if (!device || !scene) return false;
	Clear();

	// 1x1 화이트/블랙 텍스처 생성 (폴백 및 기본값)
	CreateSolidColorSRV(device, 0xFFFFFFFF, &m_->white);   // RGBA(1,1,1,1)
	CreateSolidColorSRV(device, 0x000000FF, &m_->black);   // RGBA(0,0,0,1)
	// 1x1 flat normal (R,G,B,A)=(0.5,0.5,1,1) => (128,128,255,255)
	CreateSolidColorSRV(device, 0xFFFF8080, &m_->flatNormal);

	const size_t matCount = scene->mNumMaterials;
	m_->baseColorSRVs.assign(matCount, nullptr);
	m_->normalSRVs.assign(matCount, nullptr);
	m_->metallicSRVs.assign(matCount, nullptr);
	m_->roughnessSRVs.assign(matCount, nullptr);

	// 논리 경로 기반 캐시 (문자열 키)
	std::unordered_map<std::string, ID3D11ShaderResourceView*> logicalCache;

	for (unsigned m = 0; m < scene->mNumMaterials; ++m)
	{
		aiMaterial* mat = scene->mMaterials[m];

		// BaseColor / Diffuse
		m_->baseColorSRVs[m] = LoadTextureFromMaterialRM(
			device, scene, mat,
			aiTextureType_DIFFUSE,          // BaseColor
			fbxLogicalPath,
			rm,
			logicalCache,
			m_->white);

		// Normal map
		m_->normalSRVs[m] = LoadTextureFromMaterialRM(
			device, scene, mat,
			aiTextureType_NORMALS,
			fbxLogicalPath,
			rm,
			logicalCache,
			m_->flatNormal);

		// Metallic / Roughness (Assimp PBR 텍스처 타입 사용)
		m_->metallicSRVs[m] = LoadTextureFromMaterialRM(
			device, scene, mat,
			aiTextureType_METALNESS,
			fbxLogicalPath,
			rm,
			logicalCache,
			m_->black);

		m_->roughnessSRVs[m] = LoadTextureFromMaterialRM(
			device, scene, mat,
			aiTextureType_DIFFUSE_ROUGHNESS,
			fbxLogicalPath,
			rm,
			logicalCache,
			m_->white);
	}

	return true;
}

bool FbxMaterialLoader::Load(ID3D11Device* device, const aiScene* scene, const std::wstring& baseDir)
{
	if (!device || !scene) return false;
	Clear();

	// 1x1 화이트/블랙 텍스처 생성 (폴백 및 기본값)
	CreateSolidColorSRV(device, 0xFFFFFFFF, &m_->white);   // RGBA(1,1,1,1)
	CreateSolidColorSRV(device, 0x000000FF, &m_->black);   // RGBA(0,0,0,1)
	// 1x1 flat normal (R,G,B,A)=(0.5,0.5,1,1) => (128,128,255,255)
	// CreateSolidColorSRV는 little-endian에서 0xAABBGGRR 형태로 들어갑니다.
	CreateSolidColorSRV(device, 0xFFFF8080, &m_->flatNormal);

	const size_t matCount = scene->mNumMaterials;
	m_->baseColorSRVs.assign(matCount, nullptr);
	m_->normalSRVs.assign(matCount, nullptr);
	m_->metallicSRVs.assign(matCount, nullptr);
	m_->roughnessSRVs.assign(matCount, nullptr);

	for (unsigned m = 0; m < scene->mNumMaterials; ++m)
	{
		aiMaterial* mat = scene->mMaterials[m];

		// BaseColor / Diffuse
		m_->baseColorSRVs[m] = LoadTextureFromMaterial(
			device, scene, mat,
			aiTextureType_DIFFUSE,          // BaseColor
			baseDir,
			m_->cache,
			m_->white);

		// Normal map
		m_->normalSRVs[m] = LoadTextureFromMaterial(
			device, scene, mat,
			aiTextureType_NORMALS,
			baseDir,
			m_->cache,
			m_->flatNormal);

		// Metallic / Roughness (Assimp PBR 텍스처 타입 사용)
		m_->metallicSRVs[m] = LoadTextureFromMaterial(
			device, scene, mat,
			aiTextureType_METALNESS,
			baseDir,
			m_->cache,
			m_->black);

		m_->roughnessSRVs[m] = LoadTextureFromMaterial(
			device, scene, mat,
			aiTextureType_DIFFUSE_ROUGHNESS,
			baseDir,
			m_->cache,
			m_->white);
	}

	return true;
}