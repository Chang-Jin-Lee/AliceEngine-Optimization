#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct aiScene;

namespace Alice { class ResourceManager; }

// Loads material textures (embedded, indexed, or file-based) and maintains a cache
class FbxMaterialLoader
{
public:
	FbxMaterialLoader();
	~FbxMaterialLoader();

	/// ResourceManager를 통해 텍스처를 로드합니다.
	/// - fbxLogicalPath: FBX 파일의 논리 경로 (예: "Resource/fbx/char/char.fbx")
	/// - baseDir는 더 이상 사용하지 않습니다. ResourceManager가 경로를 처리합니다.
	bool Load(ID3D11Device* device, const aiScene* scene, const std::filesystem::path& fbxLogicalPath, Alice::ResourceManager& rm);
	
	/// 레거시 호환용 (baseDir 기반, 에디터 모드에서만 사용)
	bool Load(ID3D11Device* device, const aiScene* scene, const std::wstring& baseDir);
	
	void Clear();

	// Legacy diffuse/baseColor map list (index == aiMaterial index)
	const std::vector<ID3D11ShaderResourceView*>& GetMaterialSRVs() const;
	// Normal map list (index == aiMaterial index)
	const std::vector<ID3D11ShaderResourceView*>& GetNormalSRVs() const;
	// PBR 확장을 위한 metallic / roughness 텍스처 슬롯 (index == aiMaterial index)
	const std::vector<ID3D11ShaderResourceView*>& GetMetallicSRVs() const;
	const std::vector<ID3D11ShaderResourceView*>& GetRoughnessSRVs() const;

private:
	struct Impl; Impl* m_;
};


