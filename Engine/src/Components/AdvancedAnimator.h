#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>
#include <assimp/scene.h>

namespace Alice
{
    // High-level animator: blending, layers, additive, IK, sockets
    class AdvancedAnimator
    {
    public:
        struct LayerBlendDesc
        {
            bool enabled = false;
            const aiAnimation* animA = nullptr;
            float timeA = 0.0f;
            const aiAnimation* animB = nullptr;
            float timeB = 0.0f;
            float blend01 = 0.0f;
            float layerAlpha = 1.0f;
        };

        struct AdditiveDesc
        {
            bool enabled = false;
            const aiAnimation* anim = nullptr;
            float time = 0.0f;
            const aiAnimation* ref = nullptr;
            float alpha = 1.0f;
        };

        struct ProceduralDesc
        {
            float strength = 0.0f;
            std::uint32_t seed = 0u;
            float timeSec = 0.0f;
        };

        struct IKDesc
        {
            bool enabled = false;
            const char* tipBone = nullptr;
            int chainLen = 0;
            DirectX::XMVECTOR targetMS = DirectX::XMVectorZero();
            float weight = 0.0f;
        };

        struct AimDesc
        {
            bool enabled = false;
            float yawRad = 0.0f;
            float weight = 1.0f;
        };

        struct UpdateDesc
        {
            float dt = 0.0f;
            LayerBlendDesc base;
            LayerBlendDesc upper;
            AdditiveDesc additive;
            ProceduralDesc procedural;
            
            // 단일 IK -> 다중 IK 리스트
            std::vector<IKDesc> ikChains;
            
            // 기존 코드를 위해 단일 IK 접근 유지
            IKDesc ik;
            
            AimDesc aim;
        };

        void Initialize(const aiScene* scene,
                        const std::unordered_map<std::string, int>& nodeMap,
                        const DirectX::XMFLOAT4X4& globalInv,
                        const std::vector<std::string>& boneNames,
                        const std::vector<DirectX::XMFLOAT4X4>& boneOffsets)
        {
            using namespace DirectX;

            m_Scene = scene;
            m_NodeIndexMap = &nodeMap;
            m_GlobalInverse = globalInv;
            m_BoneNames = boneNames;

            const size_t boneCount = boneNames.size();
            m_BoneOffsets.assign(boneCount, XMMatrixIdentity());
            m_BoneNodeIndices.assign(boneCount, -1);
            finalTransforms.assign(boneCount, XMMatrixIdentity());

            for (size_t i = 0; i < boneCount; ++i)
            {
                m_BoneOffsets[i] = XMLoadFloat4x4(&boneOffsets[i]);
                auto it = nodeMap.find(boneNames[i]);
                if (it != nodeMap.end())
                    m_BoneNodeIndices[i] = it->second;
            }

            const size_t nodeCount = nodeMap.size();
            m_NodePtrs.assign(nodeCount, nullptr);
            m_NodeParents.assign(nodeCount, -1);
            m_NodeNames.assign(nodeCount, std::string{});
            m_GlobalMatrices.assign(nodeCount, XMMatrixIdentity());

            if (m_Scene && m_Scene->mRootNode)
                BuildNodeHierarchy(m_Scene->mRootNode, -1);
        }

        void Update(const UpdateDesc& d)
        {
            using namespace DirectX;

            if (m_NodePtrs.empty())
                return;

            // -----------------------------------------------------------------
            // Fast path: single animation with no blending/layers/additive/IK
            // -----------------------------------------------------------------
            const bool isSimpleSingleAnim =
                (d.base.enabled) &&
                (d.base.animA == d.base.animB) &&
                (d.base.blend01 == 0.0f) &&
                (!d.upper.enabled) &&
                (!d.additive.enabled) &&
                (d.procedural.strength == 0.0f) &&
                (!d.ik.enabled) &&
                (!d.aim.enabled);
            if (isSimpleSingleAnim)
            {
                EvaluateLikeFbxAnimation(d.base.animA, d.base.timeA);

                // Final palette: GlobalInv * Global * Offset
                XMMATRIX mGlobalInv = XMLoadFloat4x4(&m_GlobalInverse);
                for (size_t bi = 0; bi < m_BoneNodeIndices.size(); ++bi)
                {
                    const int nodeIdx = m_BoneNodeIndices[bi];
                    if (nodeIdx >= 0 && nodeIdx < (int)m_GlobalMatrices.size())
                        finalTransforms[bi] = mGlobalInv * m_GlobalMatrices[(size_t)nodeIdx] * m_BoneOffsets[bi];
                    else
                        finalTransforms[bi] = XMMatrixIdentity();
                }

                // Socket update
                for (auto& s : m_Sockets)
                {
                    if (s.parentNodeIndex >= 0 && s.parentNodeIndex < (int)m_GlobalMatrices.size())
                        s.finalWorldMatrix = s.offsetMatrix * m_GlobalMatrices[(size_t)s.parentNodeIndex];
                    else
                        s.finalWorldMatrix = s.offsetMatrix;
                }
                return;
            }

            // -----------------------------------------------------------------
            // Full path: blending, layers, additive, IK, aim
            // -----------------------------------------------------------------
            const size_t nodeCount = m_NodePtrs.size();
            auto Clamp01 = [](float x) { return std::clamp(x, 0.0f, 1.0f); };
            const float baseBlend = Clamp01(d.base.blend01);

            // Build channel map (nodeIdx -> aiNodeAnim*)
            auto BuildChannelMap = [&](const aiAnimation* anim,
                                       std::vector<const aiNodeAnim*>& outChOfNode)
            {
                outChOfNode.assign(nodeCount, nullptr);
                if (!anim || !m_NodeIndexMap)
                    return;
                for (unsigned ci = 0; ci < anim->mNumChannels; ++ci)
                {
                    const aiNodeAnim* ch = anim->mChannels[ci];
                    if (!ch)
                        continue;
                    auto it = m_NodeIndexMap->find(ch->mNodeName.C_Str());
                    if (it == m_NodeIndexMap->end())
                        continue;
                    const int idx = it->second;
                    if (idx >= 0 && (size_t)idx < outChOfNode.size())
                        outChOfNode[(size_t)idx] = ch;
                }
            };

            auto WrapToTicks = [&](const aiAnimation* anim, float timeSec) -> double
            {
                const double tps = (anim && anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0;
                const double dur = (anim && anim->mDuration > 0.0) ? anim->mDuration : 0.0;
                double tTicks = (double)timeSec * tps;
                if (dur > 0.0)
                {
                    tTicks = std::fmod(tTicks, dur);
                    if (tTicks < 0.0)
                        tTicks += dur;
                }
                return tTicks;
            };

            // Evaluate local matrices (TRS). Missing channels -> bind pose
            auto EvalLocals = [&](const aiAnimation* anim,
                                  float timeSec,
                                  std::vector<XMMATRIX>& outLocals,
                                  std::vector<std::uint8_t>& outHasChannel)
            {
                outLocals.assign(nodeCount, XMMatrixIdentity());
                outHasChannel.assign(nodeCount, 0);

                std::vector<const aiNodeAnim*> chOfNode;
                BuildChannelMap(anim, chOfNode);
                const double tTicks = WrapToTicks(anim, timeSec);

                for (size_t i = 0; i < nodeCount; ++i)
                {
                    const aiNode* node = m_NodePtrs[i];
                    if (!node)
                    {
                        outLocals[i] = XMMatrixIdentity();
                        outHasChannel[i] = 0;
                        continue;
                    }

                    const aiNodeAnim* ch = (i < chOfNode.size()) ? chOfNode[i] : nullptr;
                    if (anim && ch)
                    {
                        const aiVector3D S = InterpVecFbx(ch->mScalingKeys, ch->mNumScalingKeys, tTicks, aiVector3D(1, 1, 1));
                        const aiVector3D T = InterpVecFbx(ch->mPositionKeys, ch->mNumPositionKeys, tTicks, aiVector3D(0, 0, 0));
                        const aiQuaternion R = InterpQuatFbx(ch->mRotationKeys, ch->mNumRotationKeys, tTicks, aiQuaternion());

                        aiMatrix4x4 mS; mS.Scaling(S, mS);
                        aiMatrix4x4 mR = aiMatrix4x4(R.GetMatrix());
                        aiMatrix4x4 mT; mT.Translation(T, mT);
                        const aiMatrix4x4 mLocal = mT * mR * mS; // TRS

                        outLocals[i] = AiToXM(mLocal);
                        outHasChannel[i] = 1;
                    }
                    else
                    {
                        outLocals[i] = AiToXM(node->mTransformation);
                        outHasChannel[i] = 0;
                    }
                }
            };

            // Column-vector matrix decomposition
            auto DecomposeSRT_Col = [&](const XMMATRIX& mCol,
                                        XMVECTOR& outS,
                                        XMVECTOR& outR,
                                        XMVECTOR& outT) -> bool
            {
                const XMMATRIX mRow = XMMatrixTranspose(mCol);
                return XMMatrixDecompose(&outS, &outR, &outT, mRow) != 0;
            };

            auto ComposeSRT_Col = [&](XMVECTOR S, XMVECTOR R, XMVECTOR T) -> XMMATRIX
            {
                const XMMATRIX mRow =
                    XMMatrixScalingFromVector(S) *
                    XMMatrixRotationQuaternion(R) *
                    XMMatrixTranslationFromVector(T);
                return XMMatrixTranspose(mRow);
            };

            auto GetTranslation_Col = [&](const XMMATRIX& mCol) -> XMVECTOR
            {
                return XMMatrixTranspose(mCol).r[3];
            };

            auto BlendMatricesSRT = [&](const XMMATRIX& aCol,
                                        const XMMATRIX& bCol,
                                        float alpha) -> XMMATRIX
            {
                alpha = Clamp01(alpha);
                XMVECTOR Sa, Ra, Ta;
                XMVECTOR Sb, Rb, Tb;
                if (!DecomposeSRT_Col(aCol, Sa, Ra, Ta))
                    return (alpha < 0.5f) ? aCol : bCol;
                if (!DecomposeSRT_Col(bCol, Sb, Rb, Tb))
                    return (alpha < 0.5f) ? aCol : bCol;
                XMVECTOR S = XMVectorLerp(Sa, Sb, alpha);
                XMVECTOR T = XMVectorLerp(Ta, Tb, alpha);
                XMVECTOR R = XMQuaternionSlerp(Ra, Rb, alpha);
                R = XMQuaternionNormalize(R);
                return ComposeSRT_Col(S, R, T);
            };

            auto ApplyAdditiveSRT = [&](const XMMATRIX& baseCol,
                                        const XMMATRIX& addCol,
                                        const XMMATRIX& refCol,
                                        float alpha) -> XMMATRIX
            {
                alpha = Clamp01(alpha);
                if (alpha <= 0.0001f)
                    return baseCol;

                XMVECTOR Sb, Rb, Tb;
                XMVECTOR Sa, Ra, Ta;
                XMVECTOR Sr, Rr, Tr;
                if (!DecomposeSRT_Col(baseCol, Sb, Rb, Tb))
                    return baseCol;
                if (!DecomposeSRT_Col(addCol, Sa, Ra, Ta))
                    return baseCol;
                if (!DecomposeSRT_Col(refCol, Sr, Rr, Tr))
                    return baseCol;

                XMVECTOR deltaT = XMVectorSubtract(Ta, Tr);
                XMVECTOR outT = XMVectorAdd(Tb, XMVectorScale(deltaT, alpha));

                XMVECTOR invRefR = XMQuaternionInverse(Rr);
                XMVECTOR deltaR = XMQuaternionMultiply(Ra, invRefR);
                deltaR = XMQuaternionNormalize(deltaR);

                XMVECTOR deltaApplied = XMQuaternionSlerp(XMQuaternionIdentity(), deltaR, alpha);
                XMVECTOR outR = XMQuaternionMultiply(deltaApplied, Rb);
                outR = XMQuaternionNormalize(outR);

                XMVECTOR outS = Sb;
                return ComposeSRT_Col(outS, outR, outT);
            };

            auto ApplyProceduralNoiseToMatrix = [&](const XMMATRIX& inCol,
                                                    int idx,
                                                    float time,
                                                    float strength,
                                                    std::uint32_t seedV) -> XMMATRIX
            {
                if (strength <= 0.0f)
                    return inCol;
                XMVECTOR S, R, T;
                if (!DecomposeSRT_Col(inCol, S, R, T))
                    return inCol;

                auto Hash = [](std::uint32_t x) { x ^= x << 13; x ^= x >> 17; return (float)(x & 0xFFFF) / 65536.0f; };
                std::uint32_t h = (std::uint32_t)idx * 12345u + seedV;
                float rx = std::sinf(time * 10.0f + Hash(h) * 6.28f) * strength * 0.05f;
                float ry = std::sinf(time * 7.0f + Hash(h + 1) * 6.28f) * strength * 0.05f;
                float rz = std::sinf(time * 13.0f + Hash(h + 2) * 6.28f) * strength * 0.05f;

                XMVECTOR deltaR = XMQuaternionRotationRollPitchYaw(rx, ry, rz);
                R = XMQuaternionMultiply(deltaR, R);
                R = XMQuaternionNormalize(R);
                return ComposeSRT_Col(S, R, T);
            };

            auto ComputeGlobalsFromLocals = [&](const std::vector<XMMATRIX>& locals,
                                                std::vector<XMMATRIX>& outGlobals)
            {
                outGlobals.assign(nodeCount, XMMatrixIdentity());
                std::vector<std::uint8_t> done(nodeCount, 0);
                auto computeNode = [&](auto&& self, int idx) -> void
                {
                    if (idx < 0 || (size_t)idx >= nodeCount) return;
                    if (done[(size_t)idx]) return;
                    const int pi = m_NodeParents[(size_t)idx];
                    if (pi >= 0) self(self, pi);
                    const XMMATRIX parent = (pi >= 0 && (size_t)pi < nodeCount) ? outGlobals[(size_t)pi] : XMMatrixIdentity();
                    outGlobals[(size_t)idx] = parent * locals[(size_t)idx];
                    done[(size_t)idx] = 1;
                };
                for (int i = 0; i < (int)nodeCount; ++i)
                    computeNode(computeNode, i);
            };

            // ---------------------------------------------------------
            // 1) Base layer (A/B blend)
            // ---------------------------------------------------------
            std::vector<XMMATRIX> localsA, localsB;
            std::vector<std::uint8_t> hasA, hasB;
            if (d.base.enabled)
            {
                EvalLocals(d.base.animA, d.base.timeA, localsA, hasA);
                EvalLocals(d.base.animB, d.base.timeB, localsB, hasB);
            }
            else
            {
                localsA.assign(nodeCount, XMMatrixIdentity());
                localsB.assign(nodeCount, XMMatrixIdentity());
                hasA.assign(nodeCount, 0);
                hasB.assign(nodeCount, 0);
            }

            std::vector<XMMATRIX> localsFinal(nodeCount, XMMatrixIdentity());
            for (size_t i = 0; i < nodeCount; ++i)
            {
                if (!hasA[i] && !hasB[i])
                    localsFinal[i] = localsA[i];
                else if (baseBlend <= 0.0f)
                    localsFinal[i] = localsA[i];
                else if (baseBlend >= 1.0f)
                    localsFinal[i] = localsB[i];
                else
                    localsFinal[i] = BlendMatricesSRT(localsA[i], localsB[i], baseBlend);
            }

            // ---------------------------------------------------------
            // 2) Upper layer (masked to upper body)
            // ---------------------------------------------------------
            if (d.upper.enabled && (d.upper.animA || d.upper.animB) && d.upper.layerAlpha > 0.0001f)
            {
                std::vector<XMMATRIX> localsUA;
                std::vector<std::uint8_t> hasUA;
                if (d.upper.animA)
                    EvalLocals(d.upper.animA, d.upper.timeA, localsUA, hasUA);
                else
                {
                    localsUA.assign(nodeCount, XMMatrixIdentity());
                    hasUA.assign(nodeCount, 0);
                }

                std::vector<XMMATRIX> localsUB;
                std::vector<std::uint8_t> hasUB;
                const bool upperHasB = (d.upper.animB != nullptr) && (d.upper.blend01 > 0.0001f);
                if (upperHasB)
                    EvalLocals(d.upper.animB, d.upper.timeB, localsUB, hasUB);
                else
                {
                    localsUB.assign(nodeCount, XMMatrixIdentity());
                    hasUB.assign(nodeCount, 0);
                }

                const float upperBlend = Clamp01(d.upper.blend01);
                const float upperAlpha = Clamp01(d.upper.layerAlpha);

                for (size_t i = 0; i < nodeCount; ++i)
                {
                    if (!IsUpperBody(m_NodeNames[i]))
                        continue;

                    const bool aHas = (i < hasUA.size()) ? (hasUA[i] != 0) : false;
                    const bool bHas = upperHasB ? ((i < hasUB.size()) ? (hasUB[i] != 0) : false) : false;
                    if (!aHas && !bHas)
                        continue;

                    XMMATRIX upperLocal = localsUA[i];
                    if (upperHasB)
                    {
                        if (!aHas) upperLocal = localsUB[i];
                        else if (!bHas) upperLocal = localsUA[i];
                        else upperLocal = BlendMatricesSRT(localsUA[i], localsUB[i], upperBlend);
                    }

                    localsFinal[i] = BlendMatricesSRT(localsFinal[i], upperLocal, upperAlpha);
                }
            }

            // ---------------------------------------------------------
            // 3) Additive layer (upper body only by default)
            // ---------------------------------------------------------
            if (d.additive.enabled && d.additive.anim && d.additive.ref)
            {
                std::vector<XMMATRIX> localsAdd, localsRef;
                std::vector<std::uint8_t> hasAdd, hasRef;
                EvalLocals(d.additive.anim, d.additive.time, localsAdd, hasAdd);
                EvalLocals(d.additive.ref, 0.0f, localsRef, hasRef);

                const float addAlpha = Clamp01(d.additive.alpha);
                for (size_t i = 0; i < nodeCount; ++i)
                {
                    if (!IsUpperBody(m_NodeNames[i]))
                        continue;
                    if (!hasAdd[i])
                        continue;
                    localsFinal[i] = ApplyAdditiveSRT(localsFinal[i], localsAdd[i], localsRef[i], addAlpha);
                }
            }

            // ---------------------------------------------------------
            // 4) Procedural noise (optional)
            // ---------------------------------------------------------
            if (d.procedural.strength > 0.0f)
            {
                for (size_t i = 0; i < nodeCount; ++i)
                {
                    if (!IsUpperBody(m_NodeNames[i]))
                        continue;
                    localsFinal[i] = ApplyProceduralNoiseToMatrix(localsFinal[i], (int)i, d.procedural.timeSec, d.procedural.strength, d.procedural.seed);
                }
            }

            // ---------------------------------------------------------
            // 4.5) Aim Yaw (spine chain)
            // ---------------------------------------------------------
            auto ApplyYawToMatrix = [&](const XMMATRIX& inCol, float yawRad) -> XMMATRIX
            {
                XMVECTOR S, R, T;
                if (!DecomposeSRT_Col(inCol, S, R, T))
                    return inCol;
                XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yawRad);
                R = XMQuaternionMultiply(qYaw, R);
                R = XMQuaternionNormalize(R);
                return ComposeSRT_Col(S, R, T);
            };

            if (d.aim.enabled && std::fabs(d.aim.yawRad) > 1e-6f && d.aim.weight > 0.0001f)
            {
                const float w = std::clamp(d.aim.weight, 0.0f, 1.0f);
                std::vector<size_t> aimNodes;
                aimNodes.reserve(8);
                for (size_t i = 0; i < nodeCount; ++i)
                {
                    if (IsAimSpineBone(m_NodeNames[i]))
                        aimNodes.push_back(i);
                }

                if (!aimNodes.empty())
                {
                    const float per = (d.aim.yawRad * w) / (float)aimNodes.size();
                    for (size_t idx : aimNodes)
                        localsFinal[idx] = ApplyYawToMatrix(localsFinal[idx], per);
                }
            }

            // ---------------------------------------------------------
            // 5) IK (CCD) - 다중 IK 지원
            // ---------------------------------------------------------
            // 다중 IK 체인 처리 (발 IK 등)
            for (const auto& ikDesc : d.ikChains)
            {
                if (!ikDesc.enabled || !ikDesc.tipBone || !m_NodeIndexMap || !m_NodeIndexMap->count(ikDesc.tipBone))
                    continue;
                    
                const int tipIdx = m_NodeIndexMap->at(ikDesc.tipBone);
                if (tipIdx < 0 || (size_t)tipIdx >= nodeCount || ikDesc.chainLen <= 0 || ikDesc.weight <= 0.0f)
                    continue;

                std::vector<int> chain;
                int curr = tipIdx;
                for (int i = 0; i <= ikDesc.chainLen && curr != -1; ++i)
                {
                    chain.push_back(curr);
                    curr = m_NodeParents[(size_t)curr];
                }

                auto SolveIKOnce = [&]()
                {
                    std::vector<XMMATRIX> globals;
                    ComputeGlobalsFromLocals(localsFinal, globals);

                    XMVECTOR effectorPos = GetTranslation_Col(globals[(size_t)tipIdx]);
                    for (size_t ci = 1; ci < chain.size(); ++ci)
                    {
                        const int jointIdx = chain[ci];
                        const int pIdx = (jointIdx >= 0) ? m_NodeParents[(size_t)jointIdx] : -1;

                        XMVECTOR jointPos = GetTranslation_Col(globals[(size_t)jointIdx]);
                        XMVECTOR toEff = XMVector3Normalize(XMVectorSubtract(effectorPos, jointPos));
                        XMVECTOR toTar = XMVector3Normalize(XMVectorSubtract(ikDesc.targetMS, jointPos));

                        float dot = XMVectorGetX(XMVector3Dot(toEff, toTar));
                        if (dot > 0.999f)
                            continue;
                        dot = std::clamp(dot, -1.0f, 1.0f);

                        XMVECTOR axisWS = XMVector3Cross(toEff, toTar);
                        axisWS = XMVector3Normalize(axisWS);
                        float angle = std::acosf(dot) * ikDesc.weight;

                        XMVECTOR axisLS = axisWS;
                        if (pIdx >= 0 && (size_t)pIdx < nodeCount)
                        {
                            XMMATRIX invP = XMMatrixInverse(nullptr, globals[(size_t)pIdx]);
                            axisLS = XMVector3TransformNormal(axisWS, invP);
                            axisLS = XMVector3Normalize(axisLS);
                        }

                        XMVECTOR S, R, T;
                        if (!DecomposeSRT_Col(localsFinal[(size_t)jointIdx], S, R, T))
                            continue;
                        XMVECTOR qDelta = XMQuaternionRotationAxis(axisLS, angle);
                        R = XMQuaternionMultiply(qDelta, R);
                        R = XMQuaternionNormalize(R);
                        localsFinal[(size_t)jointIdx] = ComposeSRT_Col(S, R, T);

                        ComputeGlobalsFromLocals(localsFinal, globals);
                        effectorPos = GetTranslation_Col(globals[(size_t)tipIdx]);
                    }
                };

                for (int iter = 0; iter < 5; ++iter)
                    SolveIKOnce();
            }
            
            // 기존 단일 IK 처리 (ikChains가 비어있을 때만)
            if (d.ikChains.empty() && d.ik.enabled && d.ik.tipBone && m_NodeIndexMap && m_NodeIndexMap->count(d.ik.tipBone))
            {
                const int tipIdx = m_NodeIndexMap->at(d.ik.tipBone);
                if (tipIdx >= 0 && (size_t)tipIdx < nodeCount && d.ik.chainLen > 0 && d.ik.weight > 0.0f)
                {
                    std::vector<int> chain;
                    int curr = tipIdx;
                    for (int i = 0; i <= d.ik.chainLen && curr != -1; ++i)
                    {
                        chain.push_back(curr);
                        curr = m_NodeParents[(size_t)curr];
                    }

                    auto SolveIKOnce = [&]()
                    {
                        std::vector<XMMATRIX> globals;
                        ComputeGlobalsFromLocals(localsFinal, globals);

                        XMVECTOR effectorPos = GetTranslation_Col(globals[(size_t)tipIdx]);
                        for (size_t ci = 1; ci < chain.size(); ++ci)
                        {
                            const int jointIdx = chain[ci];
                            const int pIdx = (jointIdx >= 0) ? m_NodeParents[(size_t)jointIdx] : -1;

                            XMVECTOR jointPos = GetTranslation_Col(globals[(size_t)jointIdx]);
                            XMVECTOR toEff = XMVector3Normalize(XMVectorSubtract(effectorPos, jointPos));
                            XMVECTOR toTar = XMVector3Normalize(XMVectorSubtract(d.ik.targetMS, jointPos));

                            float dot = XMVectorGetX(XMVector3Dot(toEff, toTar));
                            if (dot > 0.999f)
                                continue;
                            dot = std::clamp(dot, -1.0f, 1.0f);

                            XMVECTOR axisWS = XMVector3Cross(toEff, toTar);
                            axisWS = XMVector3Normalize(axisWS);
                            float angle = std::acosf(dot) * d.ik.weight;

                            XMVECTOR axisLS = axisWS;
                            if (pIdx >= 0 && (size_t)pIdx < nodeCount)
                            {
                                XMMATRIX invP = XMMatrixInverse(nullptr, globals[(size_t)pIdx]);
                                axisLS = XMVector3TransformNormal(axisWS, invP);
                                axisLS = XMVector3Normalize(axisLS);
                            }

                            XMVECTOR S, R, T;
                            if (!DecomposeSRT_Col(localsFinal[(size_t)jointIdx], S, R, T))
                                continue;
                            XMVECTOR qDelta = XMQuaternionRotationAxis(axisLS, angle);
                            R = XMQuaternionMultiply(qDelta, R);
                            R = XMQuaternionNormalize(R);
                            localsFinal[(size_t)jointIdx] = ComposeSRT_Col(S, R, T);

                            ComputeGlobalsFromLocals(localsFinal, globals);
                            effectorPos = GetTranslation_Col(globals[(size_t)tipIdx]);
                        }
                    };

                    for (int iter = 0; iter < 5; ++iter)
                        SolveIKOnce();
                }
            }

            // ---------------------------------------------------------
            // 6) Globals + palette + sockets
            // ---------------------------------------------------------
            ComputeGlobalsFromLocals(localsFinal, m_GlobalMatrices);

            XMMATRIX mGlobalInv = XMLoadFloat4x4(&m_GlobalInverse);
            for (size_t bi = 0; bi < m_BoneNames.size(); ++bi)
            {
                const int nodeIdx = m_BoneNodeIndices[bi];
                if (nodeIdx >= 0 && nodeIdx < (int)m_GlobalMatrices.size())
                    finalTransforms[bi] = mGlobalInv * m_GlobalMatrices[(size_t)nodeIdx] * m_BoneOffsets[bi];
                else
                    finalTransforms[bi] = XMMatrixIdentity();
            }

            for (auto& s : m_Sockets)
            {
                if (s.parentNodeIndex >= 0 && s.parentNodeIndex < (int)m_GlobalMatrices.size())
                    s.finalWorldMatrix = s.offsetMatrix * m_GlobalMatrices[(size_t)s.parentNodeIndex];
                else
                    s.finalWorldMatrix = s.offsetMatrix;
            }
        }

        void SetSocketSRT(const std::string& name,
                          const std::string& parentBone,
                          DirectX::XMFLOAT3 pos,
                          DirectX::XMFLOAT3 rotDeg,
                          DirectX::XMFLOAT3 scale)
        {
            using namespace DirectX;

            auto it = std::find_if(m_Sockets.begin(), m_Sockets.end(),
                                   [&](const Socket& s) { return s.name == name; });
            Socket* s = (it != m_Sockets.end()) ? &(*it) : &m_Sockets.emplace_back();

            s->name = name;
            s->parentBoneName = parentBone;
            s->offsetPos = pos;
            s->offsetRot = rotDeg;
            s->offsetScale = scale;
            s->UpdateOffset();

            if (m_NodeIndexMap && m_NodeIndexMap->count(parentBone))
                s->parentNodeIndex = m_NodeIndexMap->at(parentBone);
        }

        /// 소켓 월드 행렬을 엔진(로우 컨벤션)으로 반환. charWorldRow는 캐릭터 루트 월드(로우).
        DirectX::XMMATRIX GetSocketWorldMatrix(const std::string& name,
                                               DirectX::CXMMATRIX charWorldRow) const
        {
            using namespace DirectX;
            for (const auto& s : m_Sockets)
            {
                if (s.name == name)
                {
                    const XMMATRIX socketRow = XMMatrixTranspose(s.finalWorldMatrix);
                    return socketRow * charWorldRow;
                }
            }
            return charWorldRow;
        }

        /// 본 이름으로 캐릭터 로컬 공간의 본 글로벌 행렬을 엔진(로우 컨벤션)으로 반환. (SocketComponent 갱신용)
        bool GetBoneGlobalMatrix(const std::string& boneName, DirectX::XMMATRIX& outRow) const
        {
            if (!m_NodeIndexMap) return false;
            auto it = m_NodeIndexMap->find(boneName);
            if (it == m_NodeIndexMap->end()) return false;
            const int nodeIdx = it->second;
            if (nodeIdx < 0 || (size_t)nodeIdx >= m_GlobalMatrices.size()) return false;
            outRow = DirectX::XMMatrixTranspose(m_GlobalMatrices[(size_t)nodeIdx]);
            return true;
        }

        const std::vector<DirectX::XMMATRIX>& GetFinalTransforms() const { return finalTransforms; }

    private:
        struct Socket
        {
            std::string name;
            std::string parentBoneName;
            int parentNodeIndex = -1;

            DirectX::XMFLOAT3 offsetPos = { 0, 0, 0 };
            DirectX::XMFLOAT3 offsetRot = { 0, 0, 0 };
            DirectX::XMFLOAT3 offsetScale = { 1, 1, 1 };

            DirectX::XMMATRIX offsetMatrix = DirectX::XMMatrixIdentity();
            DirectX::XMMATRIX finalWorldMatrix = DirectX::XMMatrixIdentity();

            void UpdateOffset()
            {
                using namespace DirectX;
                // Store as column-major to match animator internal convention.
                XMMATRIX mRow =
                    XMMatrixScaling(offsetScale.x, offsetScale.y, offsetScale.z) *
                    XMMatrixRotationRollPitchYaw(
                        XMConvertToRadians(offsetRot.x),
                        XMConvertToRadians(offsetRot.y),
                        XMConvertToRadians(offsetRot.z)) *
                    XMMatrixTranslation(offsetPos.x, offsetPos.y, offsetPos.z);
                offsetMatrix = XMMatrixTranspose(mRow);
            }
        };

        static DirectX::XMMATRIX AiToXM(const aiMatrix4x4& m)
        {
            using namespace DirectX;
            XMFLOAT4X4 fm;
            fm._11 = (float)m.a1; fm._12 = (float)m.a2; fm._13 = (float)m.a3; fm._14 = (float)m.a4;
            fm._21 = (float)m.b1; fm._22 = (float)m.b2; fm._23 = (float)m.b3; fm._24 = (float)m.b4;
            fm._31 = (float)m.c1; fm._32 = (float)m.c2; fm._33 = (float)m.c3; fm._34 = (float)m.c4;
            fm._41 = (float)m.d1; fm._42 = (float)m.d2; fm._43 = (float)m.d3; fm._44 = (float)m.d4;
            return XMLoadFloat4x4(&fm);
        }

        static aiVector3D InterpVecFbx(const aiVectorKey* keys,
                                       unsigned count,
                                       double tTicks,
                                       const aiVector3D& fallback)
        {
            if (!keys || count == 0)
                return fallback;
            if (count == 1)
                return keys[0].mValue;
            unsigned i = 0;
            while (i + 1 < count && tTicks >= keys[i + 1].mTime)
                ++i;
            unsigned j = (i + 1 < count) ? (i + 1) : i;
            const double dt = keys[j].mTime - keys[i].mTime;
            const double a = (dt > 0.0) ? (tTicks - keys[i].mTime) / dt : 0.0;
            const aiVector3D v0 = keys[i].mValue;
            const aiVector3D v1 = keys[j].mValue;
            return v0 + (float)a * (v1 - v0);
        }

        static aiQuaternion InterpQuatFbx(const aiQuatKey* keys,
                                          unsigned count,
                                          double tTicks,
                                          const aiQuaternion& fallback)
        {
            if (!keys || count == 0)
                return fallback;
            if (count == 1)
                return keys[0].mValue;
            unsigned i = 0;
            while (i + 1 < count && tTicks >= keys[i + 1].mTime)
                ++i;
            unsigned j = (i + 1 < count) ? (i + 1) : i;
            const double dt = keys[j].mTime - keys[i].mTime;
            const double a = (dt > 0.0) ? (tTicks - keys[i].mTime) / dt : 0.0;
            aiQuaternion q;
            aiQuaternion::Interpolate(q, keys[i].mValue, keys[j].mValue, (float)a);
            q.Normalize();
            return q;
        }

        void EvaluateLikeFbxAnimation(const aiAnimation* anim, float timeSec)
        {
            using namespace DirectX;

            const size_t nodeCount = m_NodePtrs.size();
            if (nodeCount == 0 || !m_NodeIndexMap)
                return;

            std::vector<const aiNodeAnim*> chOfNode(nodeCount, nullptr);
            if (anim)
            {
                for (unsigned ci = 0; ci < anim->mNumChannels; ++ci)
                {
                    const aiNodeAnim* ch = anim->mChannels[ci];
                    if (!ch) continue;
                    auto it = m_NodeIndexMap->find(ch->mNodeName.C_Str());
                    if (it == m_NodeIndexMap->end()) continue;
                    const int idx = it->second;
                    if (idx >= 0 && (size_t)idx < chOfNode.size())
                        chOfNode[(size_t)idx] = ch;
                }
            }

            std::vector<XMMATRIX> locals(nodeCount, XMMatrixIdentity());
            const double tps = (anim && anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0;
            const double dur = (anim && anim->mDuration > 0.0) ? anim->mDuration : 0.0;
            double tTicks = (double)timeSec * tps;
            if (dur > 0.0)
            {
                tTicks = std::fmod(tTicks, dur);
                if (tTicks < 0.0)
                    tTicks += dur;
            }

            for (size_t i = 0; i < nodeCount; ++i)
            {
                const aiNode* node = m_NodePtrs[i];
                if (!node)
                {
                    locals[i] = XMMatrixIdentity();
                    continue;
                }
                const aiNodeAnim* ch = (i < chOfNode.size()) ? chOfNode[i] : nullptr;
                if (anim && ch)
                {
                    const aiVector3D S = InterpVecFbx(ch->mScalingKeys, ch->mNumScalingKeys, tTicks, aiVector3D(1, 1, 1));
                    const aiVector3D T = InterpVecFbx(ch->mPositionKeys, ch->mNumPositionKeys, tTicks, aiVector3D(0, 0, 0));
                    const aiQuaternion R = InterpQuatFbx(ch->mRotationKeys, ch->mNumRotationKeys, tTicks, aiQuaternion());

                    aiMatrix4x4 mS; mS.Scaling(S, mS);
                    aiMatrix4x4 mR = aiMatrix4x4(R.GetMatrix());
                    aiMatrix4x4 mT; mT.Translation(T, mT);
                    const aiMatrix4x4 mLocal = mT * mR * mS;
                    locals[i] = AiToXM(mLocal);
                }
                else
                {
                    locals[i] = AiToXM(node->mTransformation);
                }
            }

            // 기존에 재귀돌던 부분 반복문으로 교체 (스택 오버플로우 방지)
            // Assimp는 부모 노드가 항상 자식보다 인덱스가 작거나 같도록 저장되므로
            // 순차적으로 계산하면 부모의 행렬이 이미 계산되어 있음이 보장됩니다.
            m_GlobalMatrices.assign(nodeCount, XMMatrixIdentity());
            
            for (size_t i = 0; i < nodeCount; ++i)
            {
                int parentIdx = m_NodeParents[i];
                
                XMMATRIX parentGlobal = XMMatrixIdentity();
                
                // 부모가 유효한 범위 내에 있고, 이미 계산된 경우
                if (parentIdx >= 0 && (size_t)parentIdx < i)
                {
                    // 부모가 이미 계산되었으므로 가져옴
                    parentGlobal = m_GlobalMatrices[(size_t)parentIdx];
                }
                else if (parentIdx >= (int)i && parentIdx < (int)nodeCount)
                {
                    // 데이터 오류: 부모 인덱스가 자식보다 크거나 같으면 순환/비정렬 가능성
                    // Identity로 처리하여 크래시 방지
                    // (일반적으로는 발생하지 않지만 안전장치)
                }
                // parentIdx < 0이면 루트 노드이므로 Identity 유지
                
                // 글로벌 행렬 계산: 부모 * 로컬
                m_GlobalMatrices[i] = parentGlobal * locals[i];
            }
        }

        void BuildNodeHierarchy(const aiNode* node, int parentIdx)
        {
            if (!node || !m_NodeIndexMap)
                return;

            int idx = -1;
            auto it = m_NodeIndexMap->find(node->mName.C_Str());
            if (it != m_NodeIndexMap->end())
            {
                idx = it->second;
                if (idx >= 0 && idx < (int)m_NodePtrs.size())
                {
                    m_NodePtrs[(size_t)idx] = node;
                    m_NodeParents[(size_t)idx] = parentIdx;
                    m_NodeNames[(size_t)idx] = node->mName.C_Str();
                }
            }

            const int parentForChildren = (idx >= 0) ? idx : parentIdx;
            for (unsigned i = 0; i < node->mNumChildren; ++i)
                BuildNodeHierarchy(node->mChildren[i], parentForChildren);
        }

        static std::string ToLower(std::string s)
        {
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        static bool IContains(const std::string& hay, const char* needle)
        {
            return ToLower(hay).find(ToLower(needle)) != std::string::npos;
        }

        bool IsUpperBody(const std::string& name) const
        {
            const char* keys[] = { "spine", "neck", "head", "arm", "hand", "weapon", "upper", "chest" };
            for (auto k : keys)
                if (IContains(name, k))
                    return true;
            return false;
        }

        bool IsAimSpineBone(const std::string& name) const
        {
            const char* keys[] = { "spine", "chest", "upperchest", "torso" };
            for (auto k : keys)
                if (IContains(name, k))
                    return true;
            return false;
        }

    private:
        const aiScene* m_Scene = nullptr;
        const std::unordered_map<std::string, int>* m_NodeIndexMap = nullptr;
        DirectX::XMFLOAT4X4 m_GlobalInverse{ 1,0,0,0,
                                             0,1,0,0,
                                             0,0,1,0,
                                             0,0,0,1 };

        std::vector<std::string> m_BoneNames;
        std::vector<DirectX::XMMATRIX> m_BoneOffsets;
        std::vector<int> m_BoneNodeIndices;

        std::vector<const aiNode*> m_NodePtrs;
        std::vector<int> m_NodeParents;
        std::vector<std::string> m_NodeNames;
        std::vector<DirectX::XMMATRIX> m_GlobalMatrices;

        std::vector<Socket> m_Sockets;

    public:
        std::vector<DirectX::XMMATRIX> finalTransforms;
    };
}

