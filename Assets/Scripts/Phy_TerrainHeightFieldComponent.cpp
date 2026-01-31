#include "Phy_TerrainHeightFieldComponent.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Physics/Components/Phy_TerrainHeightFieldComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include <cmath>
#include <algorithm>
#include <random>

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(TerrainHeightFieldTest);

    void TerrainHeightFieldTest::Awake()
    {
        ALICE_LOG_INFO("[TerrainHeightFieldTest] Awake called. Generating terrain...");
        
        auto go = gameObject();
        if (!go.IsValid())
        {
            ALICE_LOG_ERRORF("[TerrainHeightFieldTest] GameObject is invalid!");
            return;
        }

        // TransformComponent 확인 및 추가
        auto* transform = go.GetComponent<TransformComponent>();
        if (!transform)
        {
            transform = &go.AddComponent<TransformComponent>();
            ALICE_LOG_INFO("[TerrainHeightFieldTest] TransformComponent added.");
        }

        // Phy_TerrainHeightFieldComponent 확인
        auto* terrain = go.GetComponent<Phy_TerrainHeightFieldComponent>();
        if (!terrain)
        {
            // 컴포넌트가 없으면 추가
            terrain = &go.AddComponent<Phy_TerrainHeightFieldComponent>();
            ALICE_LOG_INFO("[TerrainHeightFieldTest] Phy_TerrainHeightFieldComponent added.");
        }

        // 높이맵 생성
        GenerateHeightField();
    }

    void TerrainHeightFieldTest::Start()
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* terrain = go.GetComponent<Phy_TerrainHeightFieldComponent>();
        if (!terrain) return;

        // 씬 파일에서 로드된 경우 heightSamples가 비어있을 수 있음
        // numRows와 numCols가 설정되어 있으면 플랫 지형 자동 생성
        if (terrain->numRows >= 2 && terrain->numCols >= 2 && terrain->heightSamples.empty())
        {
            const size_t expectedSamples = static_cast<size_t>(terrain->numRows) * static_cast<size_t>(terrain->numCols);
            terrain->heightSamples.resize(expectedSamples, 0.0f);
            ALICE_LOG_INFO("[TerrainHeightFieldTest] Auto-generated flat terrain: %u x %u", 
                terrain->numRows, terrain->numCols);
        }
    }

    void TerrainHeightFieldTest::Update(float deltaTime)
    {
        // 매 프레임 업데이트 로직 (필요시 추가)
        (void)deltaTime;
    }

    void TerrainHeightFieldTest::GenerateHeightField()
    {
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* terrain = go.GetComponent<Phy_TerrainHeightFieldComponent>();
        if (!terrain)
        {
            ALICE_LOG_ERRORF("[TerrainHeightFieldTest] Phy_TerrainHeightFieldComponent not found!");
            return;
        }

        // 파라미터 검증
        uint32_t rows = std::max(2u, Get_m_numRows());
        uint32_t cols = std::max(2u, Get_m_numCols());
        
        if (rows < 2 || cols < 2)
        {
            ALICE_LOG_ERRORF("[TerrainHeightFieldTest] Invalid dimensions: %u x %u (must be >= 2)", rows, cols);
            return;
        }

        // 컴포넌트 설정
        terrain->numRows = rows;
        terrain->numCols = cols;
        terrain->rowScale = Get_m_rowScale();
        terrain->colScale = Get_m_colScale();
        terrain->heightScale = Get_m_heightScale();
        terrain->centerPivot = Get_m_centerPivot();

        // 높이맵 데이터 생성
        terrain->heightSamples.resize(rows * cols);
        GenerateNoiseHeightMap(terrain->heightSamples, rows, cols, Get_m_maxHeight());

        ALICE_LOG_INFO("[TerrainHeightFieldTest] HeightField generated: %u x %u, maxHeight=%.2f", 
            rows, cols, Get_m_maxHeight());
    }

    void TerrainHeightFieldTest::RegenerateHeightField()
    {
        ALICE_LOG_INFO("[TerrainHeightFieldTest] Regenerating height field...");
        GenerateHeightField();
    }

    void TerrainHeightFieldTest::GenerateNoiseHeightMap(std::vector<float>& heights, uint32_t rows, uint32_t cols, float maxHeight)
    {
        if (heights.size() != rows * cols)
        {
            heights.resize(rows * cols);
        }

        // 간단한 노이즈 생성 (Perlin-like 간단 버전)
        // 실제로는 더 정교한 노이즈 함수를 사용할 수 있습니다.
        std::mt19937 rng(12345); // 시드 고정 (재현 가능)
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        // 간단한 그라디언트 + 노이즈 조합
        for (uint32_t i = 0; i < rows; ++i)
        {
            for (uint32_t j = 0; j < cols; ++j)
            {
                float x = static_cast<float>(j) / static_cast<float>(cols - 1);
                float z = static_cast<float>(i) / static_cast<float>(rows - 1);

                // 1. 기본 그라디언트 (중앙이 높음)
                float centerDist = std::sqrt((x - 0.5f) * (x - 0.5f) + (z - 0.5f) * (z - 0.5f));
                float gradient = 1.0f - std::min(1.0f, centerDist * 2.0f);

                // 2. 간단한 노이즈 (여러 옥타브)
                float noise = 0.0f;
                float amplitude = 1.0f;
                float frequency = 4.0f;
                
                for (int octave = 0; octave < 3; ++octave)
                {
                    float nx = x * frequency;
                    float nz = z * frequency;
                    
                    // 간단한 해시 기반 노이즈
                    int ix = static_cast<int>(nx) % 256;
                    int iz = static_cast<int>(nz) % 256;
                    float fx = nx - std::floor(nx);
                    float fz = nz - std::floor(nz);
                    
                    // 보간
                    float n00 = dist(rng) * 2.0f - 1.0f;
                    float n10 = dist(rng) * 2.0f - 1.0f;
                    float n01 = dist(rng) * 2.0f - 1.0f;
                    float n11 = dist(rng) * 2.0f - 1.0f;
                    
                    float nx0 = n00 * (1.0f - fx) + n10 * fx;
                    float nx1 = n01 * (1.0f - fx) + n11 * fx;
                    float n = nx0 * (1.0f - fz) + nx1 * fz;
                    
                    noise += n * amplitude;
                    amplitude *= 0.5f;
                    frequency *= 2.0f;
                }

                // 3. 그라디언트와 노이즈 결합
                float height = (gradient * 0.3f + noise * 0.7f) * maxHeight;
                height = std::max(0.0f, height); // 최소 높이는 0

                heights[i * cols + j] = height;
            }
        }

        ALICE_LOG_INFO("[TerrainHeightFieldTest] Noise height map generated: %zu samples", heights.size());
    }
}
