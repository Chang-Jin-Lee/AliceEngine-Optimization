#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <filesystem>

// DirectX 수학 타입(XMFLOAT4X4 등)을 사용
#include <DirectXMath.h>

#include "FbxTypes.h"
#include "Runtime/Rendering/Data/Vertex.h"
#include <wtypes.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
namespace Alice { class ResourceManager; }

// High-level FBX model loader composed of sub-systems (materials, geometry, skeleton, animation)
// API intentionally mirrors existing FbxManager to ease migration
class FbxModel
{
public:
	enum class AnimationType { None = 0, Skinned = 1, Rigid = 2 };

	FbxModel();
	~FbxModel();

	bool Load(ID3D11Device* device, const std::wstring& pathW);
	
	/// ResourceManager 기반 로드 (권장)
	/// - fbxLogicalPath: FBX 파일의 논리 경로 (예: "Resource/fbx/char/char.fbx")
	bool Load(ID3D11Device* device, Alice::ResourceManager& rm, const std::filesystem::path& fbxLogicalPath);
	
	// Cooked/Chunks 에서 복호화된 FBX 바이트를 임시파일 없이 바로 로드합니다.
	// - virtualNameUtf8: 확장자 힌트(예: "Rapi.fbx") 용
	// - baseDirW: 외부 텍스처 상대 경로 해석용(없으면 L"")
	bool LoadFromMemory(ID3D11Device* device,
	                    const void* data,
	                    size_t size,
	                    const std::string& virtualNameUtf8,
	                    const std::wstring& baseDirW);
	
	/// ResourceManager 기반 메모리 로드 (권장)
	/// - fbxLogicalPath: FBX 파일의 논리 경로 (예: "Resource/fbx/char/char.fbx")
	bool LoadFromMemory(ID3D11Device* device,
						Alice::ResourceManager& rm,
	                    const std::filesystem::path& fbxLogicalPath,
	                    const void* data,
	                    size_t size,
	                    const std::string& virtualNameUtf8);
	void Release();

	// Mesh
	bool HasMesh() const;
	ID3D11Buffer* GetVertexBuffer() const;
	ID3D11Buffer* GetIndexBuffer() const;
	int GetIndexCount() const;
	UINT GetVertexStride() const;
	UINT GetVertexOffset() const { return 0; }
	const std::vector<FbxSubset>& GetSubsets() const;
	const std::vector<ID3D11ShaderResourceView*>& GetMaterialSRVs() const;      // BaseColor / Diffuse
	const std::vector<ID3D11ShaderResourceView*>& GetNormalSRVs() const;        // Normal map
	const std::vector<ID3D11ShaderResourceView*>& GetMetallicSRVs() const;      // PBR Metallic
	const std::vector<ID3D11ShaderResourceView*>& GetRoughnessSRVs() const;     // PBR Roughness
	const std::vector<VertexSkinnedTBN>& GetCPUVertices() const;
	const std::vector<uint32_t>& GetCPUIndices() const;

	// Skeleton
	bool HasSkeleton() const;
	bool HasAnimations() const;
	const std::vector<FbxSkeletonNode>& GetSkeleton() const;
	int GetSkeletonRoot() const;
	ID3D11Buffer* GetBoneConstantBuffer() const;
	UINT GetBoneCount() const;

	// Animation controls
	const std::vector<std::string>& GetAnimationNames() const;
	int GetCurrentAnimationIndex() const;
	void SetCurrentAnimation(int idx);
	void SetAnimationPlaying(bool playing);
	bool IsAnimationPlaying() const;
	double GetAnimationTimeSeconds() const;
	void SetAnimationTimeSeconds(double t);
	void UpdateAnimation(ID3D11DeviceContext* ctx, double dtSec);
	double GetClipDurationSec(int idx) const;

	AnimationType GetCurrentAnimationType() const;

	// Shared data accessors for per-instance animators
	const struct aiScene* GetScenePtr() const;
	const std::unordered_map<std::string,int>& GetNodeIndexOfName() const;
	const std::vector<std::string>& GetBoneNames() const;
	const std::vector<DirectX::XMFLOAT4X4>& GetBoneOffsets() const;
	const DirectX::XMFLOAT4X4& GetGlobalInverse() const;

	// Bounds (local space)
	// - 로드 시점에 CPU-side bindVertices(pos) 기준으로 계산된 로컬 AABB입니다.
	// - World Transform(TransformComponent)이 적용되기 전의 순수 모델 공간 바운딩입니다.
	bool GetLocalBounds(DirectX::XMFLOAT3& outMin, DirectX::XMFLOAT3& outMax) const;

private:
	struct Impl; std::unique_ptr<Impl> m_;
};


