#pragma once

#include <vector>
#include <string>

// - UINT / D3D11 타입을 사용하기 위해 <d3d11.h> 가 필요합니다.
#include <d3d11.h>

#include "Runtime/Rendering/Data/Vertex.h"
#include "FbxTypes.h"

struct ID3D11Device;
struct ID3D11Buffer;
struct aiScene;

// Builds GPU buffers (VB/IB) and subset ranges from Assimp scene
class FbxGeometryBuilder
{
public:
	FbxGeometryBuilder();
	~FbxGeometryBuilder();

	bool Build(ID3D11Device* device, const aiScene* scene);
	void Clear();

	ID3D11Buffer* GetVB() const;
	ID3D11Buffer* GetIB() const;
	int GetIndexCount() const;
	UINT GetVertexStride() const;
	const std::vector<FbxSubset>& GetSubsets() const;

	// For rigid animation helper
	const std::vector<std::string>& GetVertexOwningNodeNames() const;
	std::vector<VertexSkinnedTBN>& GetCPUVertices();
	const std::vector<VertexSkinnedTBN>& GetCPUVertices() const;
	const std::vector<uint32_t>& GetCPUIndices() const;

	// Recreate VB from CPU-side vertices (after skin weights applied)
	bool RebuildVBFromCPU(ID3D11Device* device);

private:
	struct Impl; Impl* m_;
};


