#include "FbxAnimation.h"
#include "Runtime/Foundation/Helper.h"

#include <assimp/scene.h>
#include <d3d11.h>
#include <wrl/client.h>

using namespace DirectX;

// 안전한 행렬 보간 헬퍼 (XMMatrixLerp가 없는 환경을 위해 직접 구현)
static XMMATRIX LerpMatrix(const XMMATRIX& A, const XMMATRIX& B, float t)
{
	return A + (B - A) * t;
}

FbxAnimation::FbxAnimation() {}
FbxAnimation::~FbxAnimation() { Clear(); }

void FbxAnimation::Clear()
{
	SAFE_RELEASE(m_pBoneCB);
	m_pBoneCB = nullptr;
	m_Names.clear(); m_DurationSec.clear(); m_TicksPerSec.clear();
	m_Current = -1; m_TimeSec = 0.0; m_Playing = false; m_Type = AnimType::None;
    m_Scene = nullptr; m_NodeIndexOfName.clear(); m_BoneNames = nullptr; m_BoneOffsets = nullptr; m_GlobalInverse = nullptr;
    m_ChannelOfNode.clear(); m_ChannelDirty = true; m_GlobalScratch.clear(); m_PaletteScratch.clear();
    // Optimized traversal caches
    m_NodePtrByIndex.clear();
    m_ParentIndexByIndex.clear();
    m_BoneNodeIndices.clear();
    m_Precomputed.clear();
}

void FbxAnimation::InitMetadata(const aiScene* scene)
{
	m_Names.clear(); m_DurationSec.clear(); m_TicksPerSec.clear();
	if (!scene) return;
	if (scene->mNumAnimations == 0) return;
	m_Names.reserve(scene->mNumAnimations);
	m_DurationSec.reserve(scene->mNumAnimations);
	m_TicksPerSec.reserve(scene->mNumAnimations);
	for (unsigned i = 0; i < scene->mNumAnimations; ++i)
	{
		const aiAnimation* a = scene->mAnimations[i];
		std::string nm = a->mName.length > 0 ? std::string(a->mName.C_Str()) : (std::string("Anim") + std::to_string(i));
		double tps = (a->mTicksPerSecond != 0.0) ? a->mTicksPerSecond : 25.0;
		double durSec = (tps != 0.0) ? (a->mDuration / tps) : 0.0;
		m_Names.push_back(nm);
		m_TicksPerSec.push_back(tps);
		m_DurationSec.push_back(durSec);
	}
	m_Current = 0; m_TimeSec = 0.0; m_Playing = false;
}

void FbxAnimation::SetCurrentIndex(int idx)
{
	if (idx < 0 || idx >= (int)m_Names.size()) return;
	m_Current = idx; m_TimeSec = 0.0;
    m_ChannelDirty = true;
}
void FbxAnimation::SetSharedContext(
    const aiScene* scene,
    const std::unordered_map<std::string,int>& nodeIndexOfName,
    const std::vector<std::string>* boneNames,
    const std::vector<DirectX::XMFLOAT4X4>* boneOffsets,
    const DirectX::XMFLOAT4X4* globalInverse)
{
    m_Scene = scene;
    m_NodeIndexOfName = nodeIndexOfName;
    m_BoneNames = boneNames;
    m_BoneOffsets = boneOffsets;
    m_GlobalInverse = globalInverse;
    m_ChannelOfNode.assign(m_NodeIndexOfName.size(), nullptr);
    m_ChannelDirty = true;

    // Build fast traversal caches aligned with node indices
    m_NodePtrByIndex.clear();
    m_ParentIndexByIndex.clear();
    m_BoneNodeIndices.clear();
    if (m_Scene && !m_NodeIndexOfName.empty())
    {
        m_NodePtrByIndex.resize(m_NodeIndexOfName.size(), nullptr);
        m_ParentIndexByIndex.resize(m_NodeIndexOfName.size(), -1);

        std::function<void(const aiNode*, int)> build = [&](const aiNode* node, int parentIdx)
        {
            auto it = m_NodeIndexOfName.find(node->mName.C_Str());
            int idx = (it != m_NodeIndexOfName.end()) ? it->second : -1;
            if (idx >= 0 && (size_t)idx < m_NodePtrByIndex.size())
            {
                m_NodePtrByIndex[(size_t)idx] = node;
                m_ParentIndexByIndex[(size_t)idx] = parentIdx;
            }
            for (unsigned ci = 0; ci < node->mNumChildren; ++ci)
            {
                build(node->mChildren[ci], idx);
            }
        };
        if (m_Scene->mRootNode)
        {
            auto itRoot = m_NodeIndexOfName.find(m_Scene->mRootNode->mName.C_Str());
            int rootIdx = (itRoot != m_NodeIndexOfName.end()) ? itRoot->second : -1;
            build(m_Scene->mRootNode, -1);
            // Ensure root parent index is -1 if valid
            if (rootIdx >= 0 && (size_t)rootIdx < m_ParentIndexByIndex.size()) m_ParentIndexByIndex[(size_t)rootIdx] = -1;
        }

        // Map bone names to node indices (for optimized evaluation path)
        if (m_BoneNames && !m_BoneNames->empty())
        {
            m_BoneNodeIndices.reserve(m_BoneNames->size());
            for (const auto& bn : *m_BoneNames)
            {
                auto itB = m_NodeIndexOfName.find(bn);
                m_BoneNodeIndices.push_back((itB != m_NodeIndexOfName.end()) ? itB->second : -1);
            }
        }
    }

    // Precompute all clips to avoid per-frame evaluation
    // Default to 30 samples per second to balance memory/quality
    if (m_Scene && m_BoneNames && m_BoneOffsets && m_GlobalInverse)
    {
        PrecomputeAll(m_Scene, m_NodeIndexOfName, *m_BoneNames, *m_BoneOffsets, *m_GlobalInverse, 30);
    }
}

static void RebuildChannelMapIfNeeded(const aiScene* scene, int currentClip, const std::unordered_map<std::string,int>& nodeIndexOfName, std::vector<const aiNodeAnim*>& out)
{
    if (!scene || currentClip < 0 || (size_t)currentClip >= scene->mNumAnimations) return;
    std::fill(out.begin(), out.end(), nullptr);
    const aiAnimation* anim = scene->mAnimations[currentClip];
    for (unsigned i = 0; i < anim->mNumChannels; ++i)
    {
        const aiNodeAnim* ch = anim->mChannels[i];
        auto it = nodeIndexOfName.find(ch->mNodeName.C_Str());
        if (it != nodeIndexOfName.end())
        {
            int idx = it->second;
            if (idx >= 0 && (size_t)idx < out.size()) out[(size_t)idx] = ch;
        }
    }
}


void FbxAnimation::SetTimeSec(double t)
{
	if (m_Current < 0 || m_Current >= (int)m_Names.size()) { m_TimeSec = 0.0; return; }
	double dur = m_DurationSec[m_Current];
	if (dur <= 0.0) { m_TimeSec = 0.0; return; }
	while (t < 0.0) t += dur; while (t >= dur) t -= dur; m_TimeSec = t;
}

double FbxAnimation::GetClipDurationSec(int idx) const
{
	if (idx < 0 || idx >= (int)m_DurationSec.size()) return 0.0; return m_DurationSec[idx];
}

void FbxAnimation::EnsureBoneCB(ID3D11Device* device, int maxBones)
{
	if (m_pBoneCB || !device) return;
	D3D11_BUFFER_DESC bd{}; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.Usage = D3D11_USAGE_DYNAMIC; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * (UINT)maxBones + sizeof(unsigned int) + sizeof(float) * 3;
	HR_T(device->CreateBuffer(&bd, nullptr, &m_pBoneCB));
}

// ★ 핵심: fallback 값을 받도록 수정 (키가 없을 때 바인드 포즈 사용)
static aiVector3D InterpVec(const aiVectorKey* keys, unsigned count, double t, const aiVector3D& fallback = aiVector3D(0, 0, 0))
{
	if (count == 0) return fallback;  // 키가 없으면 fallback 사용 (바인드 포즈)
	if (count == 1) return keys[0].mValue;
	unsigned i = 0; while (i + 1 < count && t >= keys[i + 1].mTime) ++i; unsigned j = (i + 1 < count) ? i + 1 : i;
	double dt = keys[j].mTime - keys[i].mTime; double a = (dt > 0.0) ? (t - keys[i].mTime) / dt : 0.0;
	aiVector3D v0 = keys[i].mValue, v1 = keys[j].mValue; return v0 + (float)a * (v1 - v0);
}

static aiQuaternion InterpQuat(const aiQuatKey* keys, unsigned count, double t)
{
	if (count == 0) return aiQuaternion();
	if (count == 1) return keys[0].mValue;
	unsigned i = 0; while (i + 1 < count && t >= keys[i + 1].mTime) ++i; unsigned j = (i + 1 < count) ? i + 1 : i;
	double dt = keys[j].mTime - keys[i].mTime; double a = (dt > 0.0) ? (t - keys[i].mTime) / dt : 0.0;
	aiQuaternion q; aiQuaternion::Interpolate(q, keys[i].mValue, keys[j].mValue, (float)a); q.Normalize(); return q;
}

static void DecomposeAiMatrix(const aiMatrix4x4& m, FbxLocalSRT& out)
{
	// XMMatrixDecompose 대신 Assimp의 Decompose 사용
	// FBX 노드 변환(프리/포스트 회전, 피벗 베이크, 축 변환 포함)에서 정확함
	// 이렇게 하면 EvaluateLocalsAt()에서 채널이 있는데 position key가 없는 본도
	// bind translation이 정상으로 들어감
	aiVector3D s, t;
	aiQuaternion r;
	m.Decompose(s, r, t);

	out.scale = { (float)s.x, (float)s.y, (float)s.z };
	out.translation = { (float)t.x, (float)t.y, (float)t.z };
	out.rotation = { (float)r.x, (float)r.y, (float)r.z, (float)r.w }; // (x,y,z,w)
}

void FbxAnimation::EvaluateGlobals(
    const aiScene* scene,
    const std::unordered_map<std::string,int>& nodeIndexOfName,
    std::vector<XMFLOAT4X4>& outGlobal) const
{
	outGlobal.clear(); if (!scene) return;
	outGlobal.resize(nodeIndexOfName.size());

	// Optimized path: compute only nodes needed by bones using parent indices
	if (!m_ParentIndexByIndex.empty() && !m_BoneNodeIndices.empty() && m_NodePtrByIndex.size() == nodeIndexOfName.size())
	{
		std::vector<uint8_t> done; done.assign(nodeIndexOfName.size(), 0);
		auto computeNode = [&](auto&& self, int idx) -> void {
			if (idx < 0 || (size_t)idx >= m_NodePtrByIndex.size()) return;
			if (done[(size_t)idx]) return;
			int pi = (idx < (int)m_ParentIndexByIndex.size()) ? m_ParentIndexByIndex[(size_t)idx] : -1;
			if (pi >= 0) self(self, pi);
			const aiNode* node = m_NodePtrByIndex[(size_t)idx];
			aiMatrix4x4 mLocal = node ? node->mTransformation : aiMatrix4x4();
			if ((size_t)idx < m_ChannelOfNode.size())
			{
				const aiNodeAnim* ch = m_ChannelOfNode[(size_t)idx];
				if (ch)
				{
					FbxLocalSRT bindSrt{};
					DecomposeAiMatrix(node->mTransformation, bindSrt);

					double tTicks = m_TimeSec * ((m_Current >= 0 && (size_t)m_Current < m_TicksPerSec.size()) ? m_TicksPerSec[m_Current] : 25.0);
					// ★ 핵심: Scale 기본값 보장
					aiVector3D bindScale = aiVector3D(bindSrt.scale.x, bindSrt.scale.y, bindSrt.scale.z);
					if (bindScale.x < 0.001f) bindScale.x = 1.0f;
					if (bindScale.y < 0.001f) bindScale.y = 1.0f;
					if (bindScale.z < 0.001f) bindScale.z = 1.0f;
					
					// ★ 핵심: InterpVec에 fallback 값 전달 (키가 없을 때 바인드 포즈 사용)
					aiVector3D S = (ch->mNumScalingKeys   > 0) ? InterpVec(ch->mScalingKeys,   ch->mNumScalingKeys,   tTicks, bindScale)
						: bindScale;
					// ★ 핵심: Translation 키가 없을 때 바인드 포즈 Translation 사용 (본이 뭉치는 현상 방지)
					aiVector3D bindT = aiVector3D(bindSrt.translation.x, bindSrt.translation.y, bindSrt.translation.z);
					aiVector3D T = (ch->mNumPositionKeys  > 0) ? InterpVec(ch->mPositionKeys,  ch->mNumPositionKeys,  tTicks, bindT)
						: bindT;
					aiQuaternion R = (ch->mNumRotationKeys  > 0) ? InterpQuat(ch->mRotationKeys, ch->mNumRotationKeys,  tTicks)
						: aiQuaternion(bindSrt.rotation.w, bindSrt.rotation.x, bindSrt.rotation.y, bindSrt.rotation.z);
					aiMatrix4x4 mS; mS.Scaling(S, mS); aiMatrix4x4 mR = aiMatrix4x4(R.GetMatrix()); aiMatrix4x4 mT; mT.Translation(T, mT);
					mLocal = mT * mR * mS;
				}
			}
			XMFLOAT4X4 lm; lm._11 = (float)mLocal.a1; lm._12 = (float)mLocal.a2; lm._13 = (float)mLocal.a3; lm._14 = (float)mLocal.a4;
			lm._21 = (float)mLocal.b1; lm._22 = (float)mLocal.b2; lm._23 = (float)mLocal.b3; lm._24 = (float)mLocal.b4;
			lm._31 = (float)mLocal.c1; lm._32 = (float)mLocal.c2; lm._33 = (float)mLocal.c3; lm._34 = (float)mLocal.c4;
			lm._41 = (float)mLocal.d1; lm._42 = (float)mLocal.d2; lm._43 = (float)mLocal.d3; lm._44 = (float)mLocal.d4;
			XMMATRIX L = XMLoadFloat4x4(&lm);
			XMMATRIX parent = XMMatrixIdentity();
			if (pi >= 0) parent = XMLoadFloat4x4(&outGlobal[(size_t)pi]);
			XMMATRIX G = XMMatrixMultiply(parent, L);
			XMStoreFloat4x4(&outGlobal[(size_t)idx], G);
			done[(size_t)idx] = 1;
		};
		for (int bn : m_BoneNodeIndices) if (bn >= 0) computeNode(computeNode, bn);
		return;
	}
	std::function<void(const aiNode*, int, const XMMATRIX&)> eval = [&](const aiNode* node, int idx, const XMMATRIX& parent){
		aiVector3D S(1,1,1), T(0,0,0); aiQuaternion R; aiMatrix4x4 mLocal = node->mTransformation;
        auto itIndex = nodeIndexOfName.find(node->mName.C_Str());
        if (itIndex != nodeIndexOfName.end())
		{
            int nodeIdx = itIndex->second;
            if (nodeIdx >= 0 && (size_t)nodeIdx < m_ChannelOfNode.size())
            {
                const aiNodeAnim* ch = m_ChannelOfNode[(size_t)nodeIdx];
                if (ch)
                {
                    // ★ 핵심: 바인드 포즈 SRT 추출 (키가 없을 때 사용)
                    FbxLocalSRT bindSrt{};
                    DecomposeAiMatrix(node->mTransformation, bindSrt);
                    
                    // Scale 기본값 보장
                    aiVector3D bindScale = aiVector3D(bindSrt.scale.x, bindSrt.scale.y, bindSrt.scale.z);
                    if (bindScale.x < 0.001f) bindScale.x = 1.0f;
                    if (bindScale.y < 0.001f) bindScale.y = 1.0f;
                    if (bindScale.z < 0.001f) bindScale.z = 1.0f;
                    
                    double tTicks = m_TimeSec * ((m_Current >= 0 && (size_t)m_Current < m_TicksPerSec.size()) ? m_TicksPerSec[m_Current] : 25.0);
                    // ★ 핵심: 키가 없으면 바인드 포즈 값 사용 (Translation이 (0,0,0)이 되지 않도록)
                    S = (ch->mNumScalingKeys   > 0) ? InterpVec(ch->mScalingKeys,   ch->mNumScalingKeys,   tTicks, bindScale) : bindScale;
                    T = (ch->mNumPositionKeys  > 0) ? InterpVec(ch->mPositionKeys,  ch->mNumPositionKeys,  tTicks, aiVector3D(bindSrt.translation.x, bindSrt.translation.y, bindSrt.translation.z)) 
                        : aiVector3D(bindSrt.translation.x, bindSrt.translation.y, bindSrt.translation.z);
                    R = (ch->mNumRotationKeys  > 0) ? InterpQuat(ch->mRotationKeys, ch->mNumRotationKeys,  tTicks) 
                        : aiQuaternion(bindSrt.rotation.w, bindSrt.rotation.x, bindSrt.rotation.y, bindSrt.rotation.z);
                    aiMatrix4x4 mS; mS.Scaling(S, mS); aiMatrix4x4 mR = aiMatrix4x4(R.GetMatrix()); aiMatrix4x4 mT; mT.Translation(T, mT);
                    mLocal = mT * mR * mS;
                }
            }
		}
		XMFLOAT4X4 lm; lm._11 = (float)mLocal.a1; lm._12 = (float)mLocal.a2; lm._13 = (float)mLocal.a3; lm._14 = (float)mLocal.a4;
		lm._21 = (float)mLocal.b1; lm._22 = (float)mLocal.b2; lm._23 = (float)mLocal.b3; lm._24 = (float)mLocal.b4;
		lm._31 = (float)mLocal.c1; lm._32 = (float)mLocal.c2; lm._33 = (float)mLocal.c3; lm._34 = (float)mLocal.c4;
		lm._41 = (float)mLocal.d1; lm._42 = (float)mLocal.d2; lm._43 = (float)mLocal.d3; lm._44 = (float)mLocal.d4;
		XMMATRIX L = XMLoadFloat4x4(&lm);
		XMMATRIX G = XMMatrixMultiply(parent, L);
		if ((size_t)idx < outGlobal.size()) XMStoreFloat4x4(&outGlobal[(size_t)idx], G);
		for (unsigned ci = 0; ci < node->mNumChildren; ++ci)
		{
			auto it = nodeIndexOfName.find(node->mChildren[ci]->mName.C_Str());
			int childIdx = (it != nodeIndexOfName.end()) ? it->second : -1;
			if (childIdx >= 0) eval(node->mChildren[ci], childIdx, G);
		}
	};
	int rootIdx = -1; // find root by nodeIndexOfName of root node
	if (scene->mRootNode) { auto it = nodeIndexOfName.find(scene->mRootNode->mName.C_Str()); if (it != nodeIndexOfName.end()) rootIdx = it->second; }
	if (rootIdx >= 0) eval(scene->mRootNode, rootIdx, XMMatrixIdentity());
}

void FbxAnimation::PrecomputeAll(
	const aiScene* scene,
	const std::unordered_map<std::string,int>& nodeIndexOfName,
	const std::vector<std::string>& boneNames,
	const std::vector<XMFLOAT4X4>& boneOffsets,
	const XMFLOAT4X4& globalInverse,
	int samplesPerSecond)
{
	if (!scene || boneNames.empty() || samplesPerSecond <= 0) { m_Precomputed.clear(); return; }
	m_Precomputed.clear();
	m_Precomputed.resize(m_Names.size());

	// Preserve current playback state while precomputing
	int oldClip = m_Current; double oldTime = m_TimeSec; bool oldPlaying = m_Playing;
	m_Playing = false;

	for (size_t clipIdx = 0; clipIdx < m_Names.size(); ++clipIdx)
	{
		PrecomputedClip pc{};
		pc.ticksPerSec = (clipIdx < m_TicksPerSec.size()) ? m_TicksPerSec[clipIdx] : 25.0;
		pc.durationSec = (clipIdx < m_DurationSec.size()) ? m_DurationSec[clipIdx] : 0.0;
		pc.sampleDt = (samplesPerSecond > 0) ? (1.0 / (double)samplesPerSecond) : 0.0;
		pc.rigid = (m_Type == AnimType::Rigid);
		if (pc.durationSec <= 0.0 || pc.sampleDt <= 0.0) { m_Precomputed[clipIdx] = pc; continue; }

		// Build channel map for this clip
		m_Current = (int)clipIdx;
		m_ChannelOfNode.assign(nodeIndexOfName.size(), nullptr);
		RebuildChannelMapIfNeeded(scene, m_Current, nodeIndexOfName, m_ChannelOfNode);

		int numSamples = (int)std::ceil(pc.durationSec * samplesPerSecond);
		if (numSamples < 1) numSamples = 1;
		pc.times.resize((size_t)numSamples);
		pc.palettes.resize((size_t)numSamples);
		XMMATRIX Gi = XMLoadFloat4x4(&globalInverse);

		for (int si = 0; si < numSamples; ++si)
		{
			double tSec = si * pc.sampleDt; if (tSec > pc.durationSec) tSec = pc.durationSec;
			pc.times[(size_t)si] = tSec;
			// Evaluate globals at tSec (using internal EvaluateGlobals which reads m_TimeSec/m_Current/m_ChannelOfNode)
			m_TimeSec = tSec;
			std::vector<XMFLOAT4X4> global;
			EvaluateGlobals(scene, nodeIndexOfName, global);
			pc.palettes[(size_t)si].resize(boneNames.size(), XMMatrixIdentity());
			for (size_t bi = 0; bi < boneNames.size(); ++bi)
			{
				auto itN = nodeIndexOfName.find(boneNames[bi]); if (itN == nodeIndexOfName.end()) continue;
				int nodeIdx = itN->second; if (nodeIdx < 0 || nodeIdx >= (int)global.size()) continue;
				XMMATRIX G = XMLoadFloat4x4(&global[(size_t)nodeIdx]);
				if (pc.rigid)
				{
					pc.palettes[(size_t)si][bi] = XMMatrixMultiply(Gi, G);
				}
				else
				{
					XMMATRIX Off = XMLoadFloat4x4(&boneOffsets[bi]);
					pc.palettes[(size_t)si][bi] = XMMatrixMultiply(XMMatrixMultiply(Gi, G), Off);
				}
			}
		}
		pc.valid = true;
		m_Precomputed[clipIdx] = std::move(pc);
	}

	// Restore playback state
	m_Current = oldClip; m_TimeSec = oldTime; m_Playing = oldPlaying;
}

void FbxAnimation::UploadPalette(ID3D11DeviceContext* ctx, const std::vector<XMMATRIX>& pal)
{
	if (!ctx) return;
	Microsoft::WRL::ComPtr<ID3D11Device> dev; ctx->GetDevice(dev.GetAddressOf());
	EnsureBoneCB(dev.Get(), (int)pal.size() + 1);
	if (!m_pBoneCB) return;
	struct BoneCB { XMFLOAT4X4 m[1023]; unsigned int boneCount; float pad[3]; };
	BoneCB cb{}; size_t n = std::min(pal.size(), (size_t)1023);
	XMMATRIX I = XMMatrixIdentity();
	for (size_t i = 0; i < 1023; ++i) XMStoreFloat4x4(&cb.m[i], XMMatrixTranspose(I));
	for (size_t i = 0; i < n; ++i) XMStoreFloat4x4(&cb.m[i], XMMatrixTranspose(pal[i]));
	cb.boneCount = (unsigned int)n;
	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (SUCCEEDED(ctx->Map(m_pBoneCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { memcpy(mapped.pData, &cb, sizeof(BoneCB)); ctx->Unmap(m_pBoneCB, 0); }
}

void FbxAnimation::UpdateAndUpload(
	ID3D11DeviceContext* ctx,
	double dtSec,
	const aiScene* scene,
	const std::unordered_map<std::string,int>& nodeIndexOfName,
	const std::vector<std::string>& boneNames,
	const std::vector<XMFLOAT4X4>& boneOffsets,
	const XMFLOAT4X4& globalInverse)
{
	if (m_Playing) SetTimeSec(m_TimeSec + dtSec);
	const aiScene* sc = m_Scene ? m_Scene : scene;
	const auto& nodeMap = !m_NodeIndexOfName.empty() ? m_NodeIndexOfName : nodeIndexOfName;
	const auto* bones = m_BoneNames ? m_BoneNames : &boneNames;
	const auto* offsets = m_BoneOffsets ? m_BoneOffsets : &boneOffsets;
	const XMFLOAT4X4* giPtr = m_GlobalInverse ? m_GlobalInverse : &globalInverse;

	// Fast path: if precomputed exists for current clip, just upload (with time interpolation for smooth motion)
	if (m_Current >= 0 && (size_t)m_Current < m_Precomputed.size())
	{
		const auto& pc = m_Precomputed[(size_t)m_Current];
		if (pc.valid && !pc.times.empty())
		{
			double dur = pc.durationSec;
			double t = m_TimeSec;
			if (dur > 0.0)
			{
				while (t < 0.0) t += dur;
				while (t >= dur) t -= dur;
			}

			// 샘플 간 선형 보간으로 매끄러운 애니메이션 구현
			if (pc.sampleDt > 0.0 && pc.palettes.size() >= 2)
			{
				double f = t / pc.sampleDt;
				double fFloor = std::floor(f);
				int idx0 = (int)fFloor;
				int idx1 = idx0 + 1;
				if (idx0 < 0) idx0 = 0;
				if (idx1 >= (int)pc.palettes.size()) idx1 = (int)pc.palettes.size() - 1;

				float a = (float)(f - fFloor);
				if (idx0 == idx1 || a <= 0.0f)
				{
					UploadPalette(ctx, pc.palettes[(size_t)idx0]);
				}
				else
				{
					const auto& pal0 = pc.palettes[(size_t)idx0];
					const auto& pal1 = pc.palettes[(size_t)idx1];
					size_t nb = pal0.size();
					if (pal1.size() < nb) nb = pal1.size();
					m_PaletteScratch.resize(nb, XMMatrixIdentity());
					for (size_t i = 0; i < nb; ++i)
					{
						m_PaletteScratch[i] = LerpMatrix(pal0[i], pal1[i], a);
					}
					UploadPalette(ctx, m_PaletteScratch);
				}
				return;
			}
			else
			{
				// 샘플 간격 정보가 없으면 가장 가까운 팔레트만 사용
				int idx = 0;
				if (!pc.palettes.empty())
				{
					idx = (int)(pc.palettes.size() * (dur > 0.0 ? (t / dur) : 0.0));
					if (idx >= (int)pc.palettes.size()) idx = (int)pc.palettes.size() - 1;
					if (idx < 0) idx = 0;
				}
				UploadPalette(ctx, pc.palettes[(size_t)idx]);
				return;
			}
		}
	}

	// Fallback: compute on the fly (if not precomputed)
	if (m_Type == AnimType::Rigid) { UploadRigid(ctx, sc, nodeMap, *bones, *giPtr); return; }
	if (!sc || m_Current < 0) return;
	if (m_ChannelDirty && !m_ChannelOfNode.empty()) { RebuildChannelMapIfNeeded(sc, m_Current, nodeMap, m_ChannelOfNode); m_ChannelDirty = false; }
	EvaluateGlobals(sc, nodeMap, m_GlobalScratch);
	m_PaletteScratch.resize(bones->size(), XMMatrixIdentity());
	XMMATRIX Gi = XMLoadFloat4x4(giPtr);
	for (size_t bi = 0; bi < bones->size(); ++bi)
	{
		auto itN = nodeMap.find((*bones)[bi]); if (itN == nodeMap.end()) continue;
		int nodeIdx = itN->second; if (nodeIdx < 0 || nodeIdx >= (int)m_GlobalScratch.size()) continue;
		XMMATRIX G = XMLoadFloat4x4(&m_GlobalScratch[(size_t)nodeIdx]);
		XMMATRIX Off = XMLoadFloat4x4(&(*offsets)[bi]);
		m_PaletteScratch[bi] = XMMatrixMultiply(XMMatrixMultiply(Gi, G), Off);
	}
	UploadPalette(ctx, m_PaletteScratch);
}

void FbxAnimation::BuildCurrentPaletteFloat4x4(std::vector<DirectX::XMFLOAT4X4>& outPalette)
{
	outPalette.clear();
	if (m_Current < 0 || (size_t)m_Current >= m_Names.size())
		return;

	// Fast path: precomputed 팔레트 사용
	// 1. 유효성 검사
	if ((size_t)m_Current >= m_Precomputed.size()) return;
	const auto& pc = m_Precomputed[(size_t)m_Current];
	if (!pc.valid || pc.times.empty() || pc.palettes.empty()) return;

	// 2. 시간 루핑 처리 (std::fmod 사용)
	double t = m_TimeSec;
	if (pc.durationSec > 0.0)
	{
		t = std::fmod(t, pc.durationSec);
		if (t < 0.0) t += pc.durationSec;
	}

	// 3. 인덱스 및 보간 계수(Alpha) 계산
	size_t idx0 = 0, idx1 = 0;
	float alpha = 0.0f;

	// 보간 경로
	if (pc.sampleDt > 0.0 && pc.palettes.size() >= 2)
	{
		double frame = t / pc.sampleDt;
		idx0 = std::clamp<size_t>((size_t)frame, 0, pc.palettes.size() - 1);
		idx1 = std::min(idx0 + 1, pc.palettes.size() - 1);
		alpha = (float)(frame - idx0);
	}
	// 비보간 경로 (단일 프레임이거나 혹은 샘플 정보 없을때)
	else
	{
		double ratio = (pc.durationSec > 0.0) ? (t / pc.durationSec) : 0.0;
		idx0 = std::clamp<size_t>((size_t)(ratio * pc.palettes.size()), 0, pc.palettes.size() - 1);
		idx1 = idx0; // 보간하지 않음
	}

	// 4. 최종 행렬 계산 (보간 또는 복사)
	const auto& p0 = pc.palettes[idx0];
	const auto& p1 = pc.palettes[idx1];
	size_t count = std::min(p0.size(), p1.size());

	outPalette.resize(count);

	// 불필요한 조건문을 줄이고 삼항 연산자로 깔끔하게 처리
	bool doLerp = (idx0 != idx1 && alpha > 0.0001f); // 미세한 오차 무시

	for (size_t i = 0; i < count; ++i)
	{
		DirectX::XMMATRIX m = doLerp ? LerpMatrix(p0[i], p1[i], alpha) : p0[i];
		XMStoreFloat4x4(&outPalette[i], m);
	}

	// ★ precomputed 경로로 채웠으면 여기서 반환 (fallback 실행 방지)
	return;

	// Fallback: on-the-fly 평가
	// 1. 기본 유효성 검사
	if (!m_Scene || !m_BoneNames || !m_GlobalInverse) return;

	// 2. 데이터 갱신 및 전역 행렬 계산
	bool isRigid = (m_Type == AnimType::Rigid);

	if (!isRigid && m_ChannelDirty && !m_ChannelOfNode.empty())
	{
		RebuildChannelMapIfNeeded(m_Scene, m_Current, m_NodeIndexOfName, m_ChannelOfNode);
		m_ChannelDirty = false;
	}

	// m_GlobalScratch를 공용으로 사용하여 메모리 할당 방지
	EvaluateGlobals(m_Scene, m_NodeIndexOfName, m_GlobalScratch);
	if (m_GlobalScratch.empty()) return;
	if (!isRigid && !m_BoneOffsets) return; // 스킨드 애니메이션은 오프셋 필수

	// 3. 팔레트 초기화
	static const XMFLOAT4X4 I = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
	outPalette.assign(m_BoneNames->size(), I); // resize + fill 통합

	// 4. 통합 계산 루프
	XMMATRIX Gi = XMLoadFloat4x4(m_GlobalInverse);

	for (size_t i = 0; i < m_BoneNames->size(); ++i)
	{
		// if init 구문으로 map 검색 간소화
		if (auto it = m_NodeIndexOfName.find((*m_BoneNames)[i]); it != m_NodeIndexOfName.end())
		{
			int idx = it->second;
			if (idx >= 0 && idx < (int)m_GlobalScratch.size())
			{
				XMMATRIX G = XMLoadFloat4x4(&m_GlobalScratch[idx]);
				XMMATRIX FinalM = XMMatrixMultiply(Gi, G);

				// Rigid가 아니면 Bone Offset 추가 적용
				if (!isRigid)
				{
					XMMATRIX Off = XMLoadFloat4x4(&(*m_BoneOffsets)[i]);
					FinalM = XMMatrixMultiply(FinalM, Off);
				}

				XMStoreFloat4x4(&outPalette[i], FinalM);
			}
		}
	}
}

void FbxAnimation::BuildPaletteAt(int clipIndex, double timeSec, std::vector<DirectX::XMFLOAT4X4>& outPalette)
{
	int oldClip = m_Current;
	double oldTime = m_TimeSec;
	bool oldPlaying = m_Playing;
	bool oldDirty = m_ChannelDirty;

	m_Current = clipIndex;
	SetTimeSec(timeSec);
	m_Playing = false;
	m_ChannelDirty = true;

	BuildCurrentPaletteFloat4x4(outPalette);

	m_Current = oldClip;
	m_TimeSec = oldTime;
	m_Playing = oldPlaying;
	m_ChannelDirty = oldDirty;
}

void FbxAnimation::EvaluateGlobalsAt(int clipIndex, double timeSec, std::vector<DirectX::XMFLOAT4X4>& outGlobal)
{
	if (!m_Scene)
	{
		outGlobal.clear();
		return;
	}

	int oldClip = m_Current;
	double oldTime = m_TimeSec;
	bool oldPlaying = m_Playing;
	bool oldDirty = m_ChannelDirty;

	m_Current = clipIndex;
	SetTimeSec(timeSec);
	m_Playing = false;
	m_ChannelDirty = true;
	if (m_ChannelDirty && !m_ChannelOfNode.empty())
	{
		RebuildChannelMapIfNeeded(m_Scene, m_Current, m_NodeIndexOfName, m_ChannelOfNode);
		m_ChannelDirty = false;
	}

	EvaluateGlobals(m_Scene, m_NodeIndexOfName, outGlobal);

	m_Current = oldClip;
	m_TimeSec = oldTime;
	m_Playing = oldPlaying;
	m_ChannelDirty = oldDirty;
}

void FbxAnimation::EvaluateGlobalsAtFull(int clipIndex, double timeSec, std::vector<DirectX::XMFLOAT4X4>& outGlobal)
{
	if (!m_Scene)
	{
		outGlobal.clear();
		return;
	}

	int oldClip = m_Current;
	double oldTime = m_TimeSec;
	bool oldPlaying = m_Playing;
	bool oldDirty = m_ChannelDirty;

	m_Current = clipIndex;
	SetTimeSec(timeSec);
	m_Playing = false;
	m_ChannelDirty = true;
	if (m_ChannelDirty && !m_ChannelOfNode.empty())
	{
		RebuildChannelMapIfNeeded(m_Scene, m_Current, m_NodeIndexOfName, m_ChannelOfNode);
		m_ChannelDirty = false;
	}

	outGlobal.clear();
	outGlobal.resize(m_NodeIndexOfName.size());

	std::function<void(const aiNode*, int, const XMMATRIX&)> eval = [&](const aiNode* node, int idx, const XMMATRIX& parent){
		aiVector3D S(1,1,1), T(0,0,0);
		aiQuaternion R;
		aiMatrix4x4 mLocal = node->mTransformation;
		auto itIndex = m_NodeIndexOfName.find(node->mName.C_Str());
		if (itIndex != m_NodeIndexOfName.end())
		{
			int nodeIdx = itIndex->second;
			if (nodeIdx >= 0 && (size_t)nodeIdx < m_ChannelOfNode.size())
			{
				const aiNodeAnim* ch = m_ChannelOfNode[(size_t)nodeIdx];
				if (ch)
				{
					FbxLocalSRT bindSrt{};
					DecomposeAiMatrix(node->mTransformation, bindSrt);

					aiVector3D bindScale = aiVector3D(bindSrt.scale.x, bindSrt.scale.y, bindSrt.scale.z);
					if (bindScale.x < 0.001f) bindScale.x = 1.0f;
					if (bindScale.y < 0.001f) bindScale.y = 1.0f;
					if (bindScale.z < 0.001f) bindScale.z = 1.0f;

					double tTicks = m_TimeSec * ((m_Current >= 0 && (size_t)m_Current < m_TicksPerSec.size()) ? m_TicksPerSec[m_Current] : 25.0);
					S = (ch->mNumScalingKeys   > 0) ? InterpVec(ch->mScalingKeys,   ch->mNumScalingKeys,   tTicks, bindScale) : bindScale;
					T = (ch->mNumPositionKeys  > 0) ? InterpVec(ch->mPositionKeys,  ch->mNumPositionKeys,  tTicks, aiVector3D(bindSrt.translation.x, bindSrt.translation.y, bindSrt.translation.z)) 
						: aiVector3D(bindSrt.translation.x, bindSrt.translation.y, bindSrt.translation.z);
					R = (ch->mNumRotationKeys  > 0) ? InterpQuat(ch->mRotationKeys, ch->mNumRotationKeys,  tTicks) 
						: aiQuaternion(bindSrt.rotation.w, bindSrt.rotation.x, bindSrt.rotation.y, bindSrt.rotation.z);
					aiMatrix4x4 mS; mS.Scaling(S, mS); aiMatrix4x4 mR = aiMatrix4x4(R.GetMatrix()); aiMatrix4x4 mT; mT.Translation(T, mT);
					mLocal = mT * mR * mS;
				}
			}
		}
		XMFLOAT4X4 lm; lm._11 = (float)mLocal.a1; lm._12 = (float)mLocal.a2; lm._13 = (float)mLocal.a3; lm._14 = (float)mLocal.a4;
		lm._21 = (float)mLocal.b1; lm._22 = (float)mLocal.b2; lm._23 = (float)mLocal.b3; lm._24 = (float)mLocal.b4;
		lm._31 = (float)mLocal.c1; lm._32 = (float)mLocal.c2; lm._33 = (float)mLocal.c3; lm._34 = (float)mLocal.c4;
		lm._41 = (float)mLocal.d1; lm._42 = (float)mLocal.d2; lm._43 = (float)mLocal.d3; lm._44 = (float)mLocal.d4;
		XMMATRIX L = XMLoadFloat4x4(&lm);
		XMMATRIX G = XMMatrixMultiply(parent, L);
		if ((size_t)idx < outGlobal.size()) XMStoreFloat4x4(&outGlobal[(size_t)idx], G);
		for (unsigned ci = 0; ci < node->mNumChildren; ++ci)
		{
			auto it = m_NodeIndexOfName.find(node->mChildren[ci]->mName.C_Str());
			int childIdx = (it != m_NodeIndexOfName.end()) ? it->second : -1;
			if (childIdx >= 0) eval(node->mChildren[ci], childIdx, G);
		}
	};

	int rootIdx = -1;
	if (m_Scene->mRootNode)
	{
		auto it = m_NodeIndexOfName.find(m_Scene->mRootNode->mName.C_Str());
		if (it != m_NodeIndexOfName.end()) rootIdx = it->second;
	}
	if (rootIdx >= 0)
		eval(m_Scene->mRootNode, rootIdx, XMMatrixIdentity());

	m_Current = oldClip;
	m_TimeSec = oldTime;
	m_Playing = oldPlaying;
	m_ChannelDirty = oldDirty;
}

void FbxAnimation::EvaluateLocalsAt(int clipIndex, double timeSec,
	std::vector<FbxLocalSRT>& outLocals,
	std::vector<std::uint8_t>* outHasChannel) const
{
	outLocals.clear();
	if (outHasChannel) outHasChannel->clear();

	if (!m_Scene || clipIndex < 0 || (size_t)clipIndex >= m_Scene->mNumAnimations)
		return;

	const size_t nodeCount = m_NodeIndexOfName.size();
	if (nodeCount == 0 || m_NodePtrByIndex.size() != nodeCount)
		return;

	outLocals.resize(nodeCount);
	if (outHasChannel) outHasChannel->assign(nodeCount, 0);

	const aiAnimation* anim = m_Scene->mAnimations[clipIndex];
	const double tps = (anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0;
	const double tTicks = timeSec * tps;

	std::vector<const aiNodeAnim*> channelOfNode;
	channelOfNode.assign(nodeCount, nullptr);
	for (unsigned i = 0; i < anim->mNumChannels; ++i)
	{
		const aiNodeAnim* ch = anim->mChannels[i];
		auto it = m_NodeIndexOfName.find(ch->mNodeName.C_Str());
		if (it != m_NodeIndexOfName.end())
		{
			int idx = it->second;
			if (idx >= 0 && (size_t)idx < channelOfNode.size())
				channelOfNode[(size_t)idx] = ch;
		}
	}

	for (size_t i = 0; i < nodeCount; ++i)
	{
		const aiNode* node = m_NodePtrByIndex[i];
		if (!node)
			continue;

		const aiNodeAnim* ch = channelOfNode[i];
		if (ch)
		{
			if (outHasChannel) (*outHasChannel)[i] = 1;

			FbxLocalSRT bindSrt{};
			DecomposeAiMatrix(node->mTransformation, bindSrt);

			// ★ 핵심: Scale 기본값을 (1, 1, 1)로 보장 (0이면 본이 사라짐)
			// bindSrt.scale이 0이거나 매우 작으면 (1, 1, 1)로 강제
			aiVector3D bindScale = aiVector3D(bindSrt.scale.x, bindSrt.scale.y, bindSrt.scale.z);
			if (bindScale.x < 0.001f) bindScale.x = 1.0f;
			if (bindScale.y < 0.001f) bindScale.y = 1.0f;
			if (bindScale.z < 0.001f) bindScale.z = 1.0f;

			// ★ 핵심: InterpVec에 fallback 값 전달 (키가 없을 때 바인드 포즈 사용)
			aiVector3D S = (ch->mNumScalingKeys > 0) ? InterpVec(ch->mScalingKeys, ch->mNumScalingKeys, tTicks, bindScale)
				: bindScale;
			// Scale이 0이 되지 않도록 최종 보장
			if (S.x < 0.001f) S.x = 1.0f;
			if (S.y < 0.001f) S.y = 1.0f;
			if (S.z < 0.001f) S.z = 1.0f;

			// ★ 핵심: Translation 키가 없을 때 바인드 포즈 Translation 사용 (본이 뭉치는 현상 방지)
			aiVector3D bindT = aiVector3D(bindSrt.translation.x, bindSrt.translation.y, bindSrt.translation.z);
			aiVector3D T = (ch->mNumPositionKeys > 0) ? InterpVec(ch->mPositionKeys, ch->mNumPositionKeys, tTicks, bindT)
				: bindT;
			aiQuaternion R = (ch->mNumRotationKeys > 0) ? InterpQuat(ch->mRotationKeys, ch->mNumRotationKeys, tTicks)
				: aiQuaternion(bindSrt.rotation.w, bindSrt.rotation.x, bindSrt.rotation.y, bindSrt.rotation.z);

			outLocals[i].scale = { (float)S.x, (float)S.y, (float)S.z };
			outLocals[i].translation = { (float)T.x, (float)T.y, (float)T.z };
			outLocals[i].rotation = { (float)R.x, (float)R.y, (float)R.z, (float)R.w };
		}
		else
		{
			DecomposeAiMatrix(node->mTransformation, outLocals[i]);
			// ★ 채널이 없는 노드도 Scale 기본값 보장
			if (outLocals[i].scale.x < 0.001f) outLocals[i].scale.x = 1.0f;
			if (outLocals[i].scale.y < 0.001f) outLocals[i].scale.y = 1.0f;
			if (outLocals[i].scale.z < 0.001f) outLocals[i].scale.z = 1.0f;
		}
	}
}

void FbxAnimation::UploadRigid(
	ID3D11DeviceContext* ctx,
	const aiScene* scene,
	const std::unordered_map<std::string,int>& nodeIndexOfName,
	const std::vector<std::string>& boneNames,
	const XMFLOAT4X4& globalInverse)
{
    std::vector<XMFLOAT4X4> global; EvaluateGlobals(scene, nodeIndexOfName, global);
	if (global.empty()) return;
	std::vector<XMMATRIX> pal; pal.resize(boneNames.size(), XMMatrixIdentity());
	XMMATRIX Gi = XMLoadFloat4x4(&globalInverse);
	for (size_t bi = 0; bi < boneNames.size(); ++bi)
	{
		auto itN = nodeIndexOfName.find(boneNames[bi]); if (itN == nodeIndexOfName.end()) continue;
		int nodeIdx = itN->second; if (nodeIdx < 0 || nodeIdx >= (int)global.size()) continue;
		XMMATRIX G = XMLoadFloat4x4(&global[(size_t)nodeIdx]);
		pal[bi] = XMMatrixMultiply(Gi, G);
	}
	UploadPalette(ctx, pal);
}



