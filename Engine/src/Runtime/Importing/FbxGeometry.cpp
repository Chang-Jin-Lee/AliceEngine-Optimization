#include "FbxGeometry.h"
#include "Runtime/Foundation/Helper.h"

#include <assimp/scene.h>
#include <d3d11.h>

#include <queue>
#include <algorithm>
#include <numeric>
#include <map>
#include <cmath>
#if __has_include(<execution>)
#include <execution>
#define FBX_HAS_EXECUTION 1
#else
#define FBX_HAS_EXECUTION 0
#endif

struct FbxGeometryBuilder::Impl
{
	ID3D11Buffer* vb = nullptr;
	ID3D11Buffer* ib = nullptr;
	int indexCount = 0;
	UINT vertexStride = sizeof(VertexSkinnedTBN);
	std::vector<FbxSubset> subsets;
	std::vector<VertexSkinnedTBN> bindVertices;
	std::vector<uint32_t> indices;
	std::vector<std::string> owningNode; // per-vertex owner node name
};

FbxGeometryBuilder::FbxGeometryBuilder() : m_(new Impl) {}
FbxGeometryBuilder::~FbxGeometryBuilder() { Clear(); delete m_; }

void FbxGeometryBuilder::Clear()
{
	SAFE_RELEASE(m_->vb);
    m_->vb = nullptr;
	SAFE_RELEASE(m_->ib);
    m_->ib = nullptr;
	m_->indexCount = 0;
	m_->subsets.clear();
	m_->bindVertices.clear();
	m_->indices.clear();
	m_->owningNode.clear();
}

// bool FbxGeometryBuilder::Build(ID3D11Device* device, const aiScene* scene)
// {
// 	if (!device || !scene || !scene->HasMeshes()) return false;
// 	Clear();

// 	// 부모 자식 위상 정렬로 모든 메쉬를 나열하고, 레벨 단위 병렬 처리
// 	struct MeshEntry
// 	{
// 		const aiNode* node;
// 		const aiMesh* mesh;
// 		uint32_t materialIndex;
// 		uint32_t vertexCount;
// 		uint32_t indexCount; // 삼각형만 집계(3의 배수)
// 		size_t vertexOffset;
// 		size_t indexOffset;
// 		size_t entryIndex;
// 	};

// 	std::vector<MeshEntry> entries;
// 	std::vector<std::pair<size_t,size_t>> levelRanges; // {start, count}
// 	std::queue<const aiNode*> q;
// 	q.push(scene->mRootNode);
// 	size_t totalVertices = 0;
// 	size_t totalIndices = 0;
// 	while (!q.empty())
// 	{
// 		size_t levelSize = q.size();
// 		size_t start = entries.size();
// 		for (size_t li = 0; li < levelSize; ++li)
// 		{
// 			const aiNode* node = q.front(); q.pop();
// 			for (unsigned mi = 0; mi < node->mNumMeshes; ++mi)
// 			{
// 				const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
// 				uint32_t vtx = mesh->mNumVertices;
// 				uint32_t idx = mesh->mNumFaces * 3; // 단순 곱셈으로 진행

// 				size_t entryIndex = entries.size();
// 				entries.push_back({ node, mesh, mesh->mMaterialIndex, vtx, idx, 0, 0, entryIndex });
// 				totalVertices += vtx;
// 				totalIndices += idx;
// 			}
// 			for (unsigned ci = 0; ci < node->mNumChildren; ++ci) q.push(node->mChildren[ci]);
// 		}
// 		size_t count = entries.size() - start;
// 		levelRanges.push_back({ start, count });
// 	}

// 	// 오프셋 확정(프리픽스 합)
// 	size_t vOff = 0, iOff = 0;
// 	for (size_t i = 0; i < entries.size(); ++i)
// 	{
// 		entries[i].vertexOffset = vOff;
// 		entries[i].indexOffset = iOff;
// 		entries[i].entryIndex = i;
// 		vOff += entries[i].vertexCount;
// 		iOff += entries[i].indexCount;
// 	}

// 	// 공유 버퍼 사전 할당 후, 각 엔트리가 자기 구간을 병렬로 채움
// 	m_->bindVertices.clear();
// 	m_->indices.clear();
// 	m_->owningNode.clear();
// 	m_->subsets.clear();
// 	m_->bindVertices.resize(totalVertices);
// 	m_->owningNode.resize(totalVertices);
// 	m_->indices.resize(totalIndices);
// 	m_->subsets.resize(entries.size());

// 	auto processEntry = [&](const MeshEntry& e)
// 	{
// 		const aiMesh* mesh = e.mesh;
// 		size_t vBase = e.vertexOffset;
// 		for (unsigned i = 0; i < mesh->mNumVertices; ++i)
// 		{
// 			aiVector3D p = mesh->mVertices[i];
// 			aiVector3D n = mesh->HasNormals() ? mesh->mNormals[i] : aiVector3D(0,1,0);
// 			aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0,0,0);
// 			aiVector3D tg = mesh->HasTangentsAndBitangents() ? mesh->mTangents[i]   : aiVector3D(1,0,0);
// 			aiVector3D bt = mesh->HasTangentsAndBitangents() ? mesh->mBitangents[i] : aiVector3D(0,1,0);
// 			VertexSkinnedTBN v{};
// 			v.pos = {p.x,p.y,p.z}; v.n = {n.x,n.y,n.z}; v.t = {tg.x,tg.y,tg.z}; v.b = {bt.x,bt.y,bt.z};
// 			v.color = {1,1,1,1}; v.uv = {uv.x,uv.y};
// 			v.boneIdx[0]=v.boneIdx[1]=v.boneIdx[2]=v.boneIdx[3]=0; v.boneWeight = {0,0,0,0};
// 			m_->bindVertices[vBase + i] = v;
// 			m_->owningNode[vBase + i] = e.node->mName.C_Str();
// 		}
// 		size_t iBase = e.indexOffset;
// 		for (unsigned f = 0; f < mesh->mNumFaces; ++f)
// 		{
// 			const aiFace& face = mesh->mFaces[f];
// 			// Face는 항상 3개의 인덱스를 가집니다. 인덱스 버퍼에 바로 기록
// 			m_->indices[iBase + (f * 3) + 0] = (uint32_t)(vBase + face.mIndices[0]);
// 			m_->indices[iBase + (f * 3) + 1] = (uint32_t)(vBase + face.mIndices[1]);
// 			m_->indices[iBase + (f * 3) + 2] = (uint32_t)(vBase + face.mIndices[2]);
// 		}
// 		m_->subsets[e.entryIndex] = { (uint32_t)e.indexOffset, (uint32_t)e.indexCount, e.materialIndex };
// 	};

// 	for (const auto& range : levelRanges)
// 	{
// #if FBX_HAS_EXECUTION
// 		std::for_each(std::execution::par, entries.begin(), entries.end(), processEntry);
// #else
// 		std::for_each(entries.begin(), entries.end(), processEntry);
// #endif
// 	}

// 	if (m_->bindVertices.empty() || m_->indices.empty()) return false;

// 	D3D11_BUFFER_DESC vb{}; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER; vb.Usage = D3D11_USAGE_DEFAULT;
// 	vb.ByteWidth = (UINT)(m_->bindVertices.size() * sizeof(VertexSkinnedTBN));
// 	D3D11_SUBRESOURCE_DATA vbd{}; vbd.pSysMem = m_->bindVertices.data();
// 	HR_T(device->CreateBuffer(&vb, &vbd, &m_->vb));

// 	m_->indexCount = (int)m_->indices.size();
// 	D3D11_BUFFER_DESC ib{}; ib.BindFlags = D3D11_BIND_INDEX_BUFFER; ib.Usage = D3D11_USAGE_DEFAULT; ib.ByteWidth = (UINT)(m_->indices.size() * sizeof(uint32_t));
// 	D3D11_SUBRESOURCE_DATA ibd{}; ibd.pSysMem = m_->indices.data();
// 	HR_T(device->CreateBuffer(&ib, &ibd, &m_->ib));
// 	return true;
// }
// 멀티스레드 써서 더 빠르게 한 코드. 오류나면 위 코드로 변경하셈
bool FbxGeometryBuilder::Build(ID3D11Device* device, const aiScene* scene)
{
    if (!device || !scene || !scene->HasMeshes()) return false;
    Clear();

    struct Entry { const aiNode* n; const aiMesh* m; size_t vOff, iOff, id; };
    std::vector<Entry> tasks;
    // scene 그래프가 클 경우를 대비해 적당량 예약 (선택사항)
    tasks.reserve(scene->mNumMeshes * 2); 

    size_t totalV = 0, totalI = 0;

    // [1] 트리 순회 (Flattening) & 오프셋 계산: 재귀 람다로 코드 압축
    auto Traverse = [&](auto&& self, const aiNode* node) -> void {
        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            tasks.push_back({ node, mesh, totalV, totalI, tasks.size() });
            totalV += mesh->mNumVertices;
            totalI += mesh->mNumFaces * 3;
        }
        for (unsigned i = 0; i < node->mNumChildren; ++i) self(self, node->mChildren[i]);
    };
    Traverse(Traverse, scene->mRootNode);

    if (tasks.empty()) return false;

    // [2] 버퍼 일괄 할당
    m_->bindVertices.resize(totalV);
    m_->owningNode.resize(totalV);
    m_->indices.resize(totalI);
    m_->subsets.resize(tasks.size());

    // [3] 병렬 처리 (SIMD랑 Multi-threading 써서)
    std::for_each(std::execution::par_unseq, tasks.begin(), tasks.end(), [&](const Entry& e) {
        const aiMesh* mesh = e.m;
        
        // Subset 정보 등록
        m_->subsets[e.id] = { (uint32_t)e.iOff, (uint32_t)(mesh->mNumFaces * 3), mesh->mMaterialIndex };

        // Vertex 복사
        for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
            auto& d = m_->bindVertices[e.vOff + i];
            const auto& p = mesh->mVertices[i];
            const auto& n = mesh->HasNormals() ? mesh->mNormals[i] : aiVector3D(0,1,0);
            const auto& t = mesh->HasTangentsAndBitangents() ? mesh->mTangents[i] : aiVector3D(1,0,0);
            const auto& b = mesh->HasTangentsAndBitangents() ? mesh->mBitangents[i] : aiVector3D(0,1,0);
            const auto& uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0,0,0);

            d.pos = {p.x, p.y, p.z}; d.n = {n.x, n.y, n.z}; d.t = {t.x, t.y, t.z}; d.b = {b.x, b.y, b.z};
            d.uv = {uv.x, uv.y}; d.color = {1,1,1,1};
            d.boneWeight = {0,0,0,0}; // memset(d.boneIdx, 0, sizeof(d.boneIdx)); 로 대체 가능
            
            // 주의: Vertex마다 string 복사는 매우 무거운 작업이나 요청에 의해 유지
            m_->owningNode[e.vOff + i] = e.n->mName.C_Str();
        }

        // Index 복사
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const auto& face = mesh->mFaces[f];
            uint32_t offset = (uint32_t)e.vOff;
            m_->indices[e.iOff + f * 3 + 0] = offset + face.mIndices[0];
            m_->indices[e.iOff + f * 3 + 1] = offset + face.mIndices[1];
            m_->indices[e.iOff + f * 3 + 2] = offset + face.mIndices[2];
        }
    });

    // [3.5] 스무스 노멀 계산 (아웃라인 끊김 방지)
    // 위치가 같은 버텍스들의 노멀을 평균내어 스무스 노멀 생성
    {
        // 위치 비교를 위한 간단한 헬퍼 구조체 (Map 키용)
        struct Vec3Key {
            DirectX::XMFLOAT3 v;
            bool operator<(const Vec3Key& o) const {
                const float epsilon = 1e-5f;
                if (abs(v.x - o.v.x) > epsilon) return v.x < o.v.x;
                if (abs(v.y - o.v.y) > epsilon) return v.y < o.v.y;
                return v.z < o.v.z - epsilon;
            }
        };

        // 위치별로 노멀을 누적할 맵 생성
        std::map<Vec3Key, DirectX::XMFLOAT3> normalAccumulator;
        std::map<Vec3Key, int> normalCount; // 평균 계산을 위한 카운트

        // 모든 버텍스를 순회하며 위치가 같은 녀석들의 노멀을 더함
        for (const auto& v : m_->bindVertices)
        {
            Vec3Key key{ v.pos };
            normalAccumulator[key].x += v.n.x;
            normalAccumulator[key].y += v.n.y;
            normalAccumulator[key].z += v.n.z;
            normalCount[key]++;
        }

        // 누적된 노멀을 정규화(Normalize)하여 평균 방향(스무스 노멀) 계산 후 적용
        for (auto& v : m_->bindVertices)
        {
            Vec3Key key{ v.pos };
            auto it = normalAccumulator.find(key);
            if (it != normalAccumulator.end() && normalCount[key] > 0)
            {
                DirectX::XMFLOAT3 smoothAvg = it->second;
                // 평균 계산 (누적된 값을 개수로 나눔)
                int count = normalCount[key];
                smoothAvg.x /= count;
                smoothAvg.y /= count;
                smoothAvg.z /= count;
                
                // 벡터 정규화 (길이를 1로)
                DirectX::XMVECTOR smoothVec = DirectX::XMVector3Normalize(
                    DirectX::XMLoadFloat3(&smoothAvg));
                DirectX::XMStoreFloat3(&v.smoothNormal, smoothVec);
            }
            else
            {
                // 평균을 구할 수 없으면 원본 노멀 사용
                v.smoothNormal = v.n;
            }
        }
    }

    // [4] GPU 버퍼 생성
    auto CreateBuf = [&](const void* data, UINT size, UINT bind, ID3D11Buffer** out) {
        D3D11_BUFFER_DESC bd{ size, D3D11_USAGE_DEFAULT, bind, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd{ data, 0, 0 };
        return device->CreateBuffer(&bd, &sd, out);
    };

    m_->indexCount = (int)m_->indices.size();
    if (FAILED(CreateBuf(m_->bindVertices.data(), (UINT)(totalV * sizeof(VertexSkinnedTBN)), D3D11_BIND_VERTEX_BUFFER, &m_->vb))) return false;
    if (FAILED(CreateBuf(m_->indices.data(), (UINT)(totalI * sizeof(uint32_t)), D3D11_BIND_INDEX_BUFFER, &m_->ib))) return false;

    return true;
}

ID3D11Buffer* FbxGeometryBuilder::GetVB() const { return m_->vb; }
ID3D11Buffer* FbxGeometryBuilder::GetIB() const { return m_->ib; }
int FbxGeometryBuilder::GetIndexCount() const { return m_->indexCount; }
UINT FbxGeometryBuilder::GetVertexStride() const { return m_->vertexStride; }
const std::vector<FbxSubset>& FbxGeometryBuilder::GetSubsets() const { return m_->subsets; }
const std::vector<std::string>& FbxGeometryBuilder::GetVertexOwningNodeNames() const { return m_->owningNode; }
std::vector<VertexSkinnedTBN>& FbxGeometryBuilder::GetCPUVertices() { return m_->bindVertices; }
const std::vector<VertexSkinnedTBN>& FbxGeometryBuilder::GetCPUVertices() const { return m_->bindVertices; }
const std::vector<uint32_t>& FbxGeometryBuilder::GetCPUIndices() const { return m_->indices; }

bool FbxGeometryBuilder::RebuildVBFromCPU(ID3D11Device* device)
{
	if (!device) return false;
	SAFE_RELEASE(m_->vb);
	m_->vb = nullptr;
	D3D11_BUFFER_DESC vb{}; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER; vb.Usage = D3D11_USAGE_DEFAULT;
	vb.ByteWidth = (UINT)(m_->bindVertices.size() * sizeof(VertexSkinnedTBN));
	D3D11_SUBRESOURCE_DATA vbd{}; vbd.pSysMem = m_->bindVertices.data();
	HR_T(device->CreateBuffer(&vb, &vbd, &m_->vb));
	return m_->vb != nullptr;
}


