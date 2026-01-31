#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <DirectXMath.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct aiScene;
struct aiNodeAnim;

namespace DirectX { struct XMFLOAT4X4; struct XMMATRIX; }

/// 로컬 SRT(스케일/회전/이동) 표현
struct FbxLocalSRT
{
	DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f }; // Quaternion (x,y,z,w)
	DirectX::XMFLOAT3 translation{ 0.0f, 0.0f, 0.0f };
};

// Controls animation state, builds bone palettes and uploads to GPU
class FbxAnimation
{
public:
	enum class AnimType { None = 0, Skinned = 1, Rigid = 2 };

	FbxAnimation();
	~FbxAnimation();

	void Clear();

	void InitMetadata(const aiScene* scene);
	// Bind shared scene/skeleton once per instance to avoid per-frame lookups
	void SetSharedContext(
		const aiScene* scene,
		const std::unordered_map<std::string,int>& nodeIndexOfName,
		const std::vector<std::string>* boneNames,
		const std::vector<DirectX::XMFLOAT4X4>* boneOffsets,
		const DirectX::XMFLOAT4X4* globalInverse);
	void SetType(AnimType t) { m_Type = t; }
	AnimType GetType() const { return m_Type; }

	// Time/clip controls
	const std::vector<std::string>& GetNames() const { return m_Names; }
	int GetCurrentIndex() const { return m_Current; }
	void SetCurrentIndex(int idx);
	void SetPlaying(bool p) { m_Playing = p; }
	bool IsPlaying() const { return m_Playing; }
	double GetTimeSec() const { return m_TimeSec; }
	void SetTimeSec(double t);
	double GetClipDurationSec(int idx) const;

	ID3D11Buffer* GetBoneCB() const { return m_pBoneCB; }
	void EnsureBoneCB(ID3D11Device* device, int maxBones);

	// Evaluate and upload palette
	void UpdateAndUpload(
		ID3D11DeviceContext* ctx,
		double dtSec,
		const aiScene* scene,
		const std::unordered_map<std::string,int>& nodeIndexOfName,
		const std::vector<std::string>& boneNames,
		const std::vector<DirectX::XMFLOAT4X4>& boneOffsets,
		const DirectX::XMFLOAT4X4& globalInverse);

	// Rigid-only palette upload (no offsets)
	void UploadRigid(ID3D11DeviceContext* ctx,
		const aiScene* scene,
		const std::unordered_map<std::string,int>& nodeIndexOfName,
		const std::vector<std::string>& boneNames,
		const DirectX::XMFLOAT4X4& globalInverse);

	void EvaluateGlobals(const aiScene* scene,
		const std::unordered_map<std::string,int>& nodeIndexOfName,
		std::vector<DirectX::XMFLOAT4X4>& outGlobal) const;

	// CPU 팔레트 생성 (ForwardRenderSystem에서 전치해서 업로드하므로 전치 없이 XMFLOAT4X4로 반환)
	// - precomputed clip이 있으면 그걸 사용하고, 없으면 on-the-fly 평가로 fallback 합니다.
	void BuildCurrentPaletteFloat4x4(std::vector<DirectX::XMFLOAT4X4>& outPalette);

	// 임의 클립/시간 팔레트 생성 (FSM/블렌드용)
	void BuildPaletteAt(int clipIndex, double timeSec, std::vector<DirectX::XMFLOAT4X4>& outPalette);
	// 임의 클립/시간 전역 행렬 생성 (소켓용)
	void EvaluateGlobalsAt(int clipIndex, double timeSec, std::vector<DirectX::XMFLOAT4X4>& outGlobal);
	// 모든 노드 전역 행렬 생성 (스킨에 포함되지 않는 소켓 본까지 포함)
	void EvaluateGlobalsAtFull(int clipIndex, double timeSec, std::vector<DirectX::XMFLOAT4X4>& outGlobal);

	// 임의 클립/시간 로컬 SRT 생성 (고급 블렌드/IK용)
	void EvaluateLocalsAt(int clipIndex, double timeSec,
		std::vector<FbxLocalSRT>& outLocals,
		std::vector<std::uint8_t>* outHasChannel = nullptr) const;
private:
	void UploadPalette(ID3D11DeviceContext* ctx, const std::vector<DirectX::XMMATRIX>& pal);

	// Precompute all clips at load-time to avoid per-frame evaluation
	void PrecomputeAll(
		const aiScene* scene,
		const std::unordered_map<std::string,int>& nodeIndexOfName,
		const std::vector<std::string>& boneNames,
		const std::vector<DirectX::XMFLOAT4X4>& boneOffsets,
		const DirectX::XMFLOAT4X4& globalInverse,
		int samplesPerSecond = 30);

private:
	AnimType m_Type = AnimType::None;
	std::vector<std::string> m_Names;
	std::vector<double> m_DurationSec;
	std::vector<double> m_TicksPerSec;
	int m_Current = -1;
	double m_TimeSec = 0.0;
	bool m_Playing = false;
	ID3D11Buffer* m_pBoneCB = nullptr;

	// Shared context (per instance) and caches
	const aiScene* m_Scene = nullptr;
	std::unordered_map<std::string,int> m_NodeIndexOfName;
	const std::vector<std::string>* m_BoneNames = nullptr;
	const std::vector<DirectX::XMFLOAT4X4>* m_BoneOffsets = nullptr;
	const DirectX::XMFLOAT4X4* m_GlobalInverse = nullptr;
	std::vector<const aiNodeAnim*> m_ChannelOfNode; // size=node count
	bool m_ChannelDirty = true;
	mutable std::vector<DirectX::XMFLOAT4X4> m_GlobalScratch;
	mutable std::vector<DirectX::XMMATRIX> m_PaletteScratch;
	// Optimized traversal data
	std::vector<const struct aiNode*> m_NodePtrByIndex;
	std::vector<int> m_ParentIndexByIndex;
	std::vector<int> m_BoneNodeIndices; // same order as boneNames

	// Precomputed palettes per clip
	struct PrecomputedClip
	{
		double durationSec = 0.0;
		double ticksPerSec = 0.0;
		double sampleDt = 0.0;
		bool   valid = false;
		bool   rigid = false; // rigid uses Gi*G, skinned uses Gi*G*Off
		std::vector<double> times; // seconds
		std::vector<std::vector<DirectX::XMMATRIX>> palettes; // [timeIndex][boneIndex]
	};
	std::vector<PrecomputedClip> m_Precomputed; // size = num clips
};


