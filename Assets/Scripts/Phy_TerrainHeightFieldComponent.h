#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // Terrain HeightField 컴포넌트를 추가하고 설정하는 스크립트
    // - Awake에서 Phy_TerrainHeightFieldComponent를 추가하고 높이맵 데이터를 생성합니다.
    // - 에디터에서 파라미터를 조정할 수 있습니다.
    class TerrainHeightFieldTest : public IScript
    {
        ALICE_BODY(TerrainHeightFieldTest);

    public:
        void Awake() override;
        void Start() override;
        void Update(float deltaTime) override;

        // --- 높이맵 생성 파라미터 (에디터에서 수정 가능) ---
        ALICE_PROPERTY(uint32_t, m_numRows, 64);           // 그리드 행 수 (>= 2)
        ALICE_PROPERTY(uint32_t, m_numCols, 64);           // 그리드 열 수 (>= 2)
        ALICE_PROPERTY(float, m_rowScale, 1.0f);           // 행 방향 스케일 (월드 단위)
        ALICE_PROPERTY(float, m_colScale, 1.0f);           // 열 방향 스케일 (월드 단위)
        ALICE_PROPERTY(float, m_heightScale, 0.01f);       // 높이 양자화 스케일 (1 step = 1cm)
        ALICE_PROPERTY(float, m_maxHeight, 10.0f);        // 최대 높이 (월드 단위)
        ALICE_PROPERTY(bool, m_centerPivot, true);         // 피벗을 지형 중앙에 둘지 여부

        // --- 높이맵 생성 함수 (에디터에서 호출 가능) ---
        void GenerateHeightField();
        ALICE_FUNC(GenerateHeightField);

        // --- 재생성 함수 (런타임에서 높이맵을 다시 생성) ---
        void RegenerateHeightField();
        ALICE_FUNC(RegenerateHeightField);

    private:
        // 간단한 노이즈 기반 높이맵 생성 (예제용)
        void GenerateNoiseHeightMap(std::vector<float>& heights, uint32_t rows, uint32_t cols, float maxHeight);
    };
}
