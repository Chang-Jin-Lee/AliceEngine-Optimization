#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <DirectXMath.h>
#include <wrl/client.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11SamplerState;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;
struct ID3D11ShaderResourceView;

namespace Alice
{
    class World;
    class Camera;
    struct ID3D11RenderDevice;

    /// Unity VFX effect.json 중 Mesh/Billboard Renderer 노드를 단순 언릿 렌더링
    /// - Compute 파티클이 아닌 "Mesh/Billboard" 타입을 빠르게 시각화하는 용도
    class UnityVfxMeshRenderSystem
    {
    public:
        explicit UnityVfxMeshRenderSystem(ID3D11RenderDevice& renderDevice);

        bool Initialize();
        void Render(const World& world, const Camera& camera, float dtSec);

    private:
        struct MeshGpu
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer> vb;
            Microsoft::WRL::ComPtr<ID3D11Buffer> ib;
            unsigned int indexCount{ 0 };
        };

        enum class BlendMode
        {
            Additive = 0,
            Alpha = 1,
        };

        struct MaterialGpu
        {
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> noiseTexture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rampTexture;
            DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT2 uvScale{ 1.0f, 1.0f };
            DirectX::XMFLOAT2 uvOffset{ 0.0f, 0.0f };
            DirectX::XMFLOAT2 uvScroll{ 0.0f, 0.0f };
            float dissolve{ 0.0f };
            float noiseStrength{ 0.0f };
            float rampStrength{ 0.0f };
            BlendMode blendMode{ BlendMode::Additive };
        };

        struct MeshNode
        {
            DirectX::XMMATRIX local;
            std::string meshPath;
            std::string materialPath;
            DirectX::XMFLOAT4 colorMul;
            float sizeMul{ 1.0f };
        };

        struct CurveKey
        {
            float time{ 0.0f };
            float value{ 0.0f };
        };

        struct Curve
        {
            std::vector<CurveKey> keys;
        };

        struct MinMaxCurve
        {
            enum class Mode
            {
                Constant,
                TwoConstants,
                Curve,
                TwoCurves,
            };

            Mode mode{ Mode::Constant };
            float multiplier{ 1.0f };
            float constant{ 0.0f };
            float constantMin{ 0.0f };
            float constantMax{ 0.0f };
            Curve curve;
            Curve curveMin;
            Curve curveMax;
        };

        struct GradientKey
        {
            float time{ 0.0f };
            DirectX::XMFLOAT4 color{ 1,1,1,1 };
        };

        struct AlphaKey
        {
            float time{ 0.0f };
            float alpha{ 1.0f };
        };

        struct Gradient
        {
            std::vector<GradientKey> colorKeys;
            std::vector<AlphaKey> alphaKeys;
        };

        struct MinMaxGradient
        {
            enum class Mode
            {
                Color,
                TwoColors,
                Gradient,
                TwoGradients,
            };

            Mode mode{ Mode::Color };
            DirectX::XMFLOAT4 color{ 1,1,1,1 };
            DirectX::XMFLOAT4 colorMin{ 1,1,1,1 };
            DirectX::XMFLOAT4 colorMax{ 1,1,1,1 };
            Gradient gradient;
            Gradient gradientMin;
            Gradient gradientMax;
        };

        struct BurstDef
        {
            float time{ 0.0f };
            int minCount{ 0 };
            int maxCount{ 0 };
            int cycleCount{ 1 };
            float repeatInterval{ 0.0f };
            float probability{ 1.0f };
        };

        enum class ShapeType
        {
            Cone,
            Box,
            Sphere,
        };

        struct ShapeDef
        {
            ShapeType type{ ShapeType::Cone };
            float radius{ 1.0f };
            float angleDeg{ 25.0f };
            float length{ 5.0f };
            DirectX::XMFLOAT3 box{ 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
            bool alignToDirection{ false };
        };

        enum class BillboardMode
        {
            Billboard = 0,
            Horizontal = 1,
            Stretch = 2,
        };

        struct BillboardNode
        {
            DirectX::XMFLOAT3 localPos{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 localScale{ 1.0f, 1.0f, 1.0f };
            std::string materialPath;
            DirectX::XMFLOAT4 colorMul;
            float sizeMul{ 1.0f };
            BillboardMode mode{ BillboardMode::Billboard };
            float stretchScale{ 1.0f };
            bool useTexSheet{ false };
            int tilesX{ 1 };
            int tilesY{ 1 };
            float frameMultiplier{ 1.0f };
        };

        enum class SimulationSpace
        {
            Local,
            World,
        };

        struct TextureSheetDef
        {
            bool enabled{ false };
            int tilesX{ 1 };
            int tilesY{ 1 };
            MinMaxCurve frameOverTime;
            MinMaxCurve startFrame;
            int cycleCount{ 1 };
        };

        struct EmitterDef
        {
            std::string name;
            DirectX::XMFLOAT3 localPos{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 localRot{ 0.0f, 0.0f, 0.0f, 1.0f };
            DirectX::XMFLOAT3 localScale{ 1.0f, 1.0f, 1.0f };
            float duration{ 1.0f };
            bool loop{ true };
            SimulationSpace space{ SimulationSpace::Local };
            MinMaxCurve startLifetime;
            MinMaxCurve startSpeed;
            MinMaxCurve startSize;
            MinMaxGradient startColor;
            MinMaxCurve startRotation; // z-axis
            bool startRotation3D{ false };
            MinMaxCurve startRotationX;
            MinMaxCurve startRotationY;
            MinMaxCurve startRotationZ;
            struct RotationOverLifetimeDef
            {
                bool enabled{ false };
                bool separateAxes{ false };
                MinMaxCurve x;
                MinMaxCurve y;
                MinMaxCurve z;
            } rotationOverLifetime;
            struct VelocityOverLifetimeDef
            {
                bool enabled{ false };
                bool separateAxes{ false };
                SimulationSpace space{ SimulationSpace::Local };
                MinMaxCurve x;
                MinMaxCurve y;
                MinMaxCurve z;
            } velocityOverLifetime;
            bool colorOverLifetimeEnabled{ false };
            MinMaxGradient colorOverLifetime;
            bool sizeOverLifetimeEnabled{ false };
            MinMaxCurve sizeOverLifetime;
            float rateOverTime{ 0.0f };
            std::vector<BurstDef> bursts;
            ShapeDef shape;
            bool shapeEnabled{ true };
            BillboardMode renderMode{ BillboardMode::Billboard };
            bool meshRenderer{ false };
            std::string materialPath;
            std::string meshPath;
            float stretchScale{ 1.0f };
            TextureSheetDef texSheet;
            struct TrailsDef
            {
                bool enabled{ false };
                float lifetime{ 0.5f };
                float minVertexDistance{ 0.05f };
                MinMaxCurve widthOverTrail;
                MinMaxGradient colorOverTrail;
            } trails;
        };

        struct EffectCache
        {
            bool loaded{ false };
            bool valid{ false };
            std::string error;
            std::vector<MeshNode> meshNodes;
            std::vector<BillboardNode> billboardNodes;
            std::vector<EmitterDef> emitters;
        };

        struct CBPerObject
        {
            DirectX::XMMATRIX world;
            DirectX::XMMATRIX viewProj;
            DirectX::XMFLOAT4 color;
            DirectX::XMFLOAT4 uvParams; // xy = scale, zw = offset
            DirectX::XMFLOAT4 customParams; // x=dissolve y=noiseStrength z=rampStrength w=time
            DirectX::XMFLOAT4 uvAnim; // xy=scroll speed
        };

        struct Particle
        {
            DirectX::XMFLOAT3 pos{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 vel{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 baseVel{ 0.0f, 0.0f, 0.0f };
            float age{ 0.0f };
            float lifetime{ 1.0f };
            float size{ 1.0f };
            float baseSize{ 1.0f };
            DirectX::XMFLOAT4 color{ 1,1,1,1 };
            DirectX::XMFLOAT4 baseColor{ 1,1,1,1 };
            DirectX::XMFLOAT3 rotation3{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 baseRotation3{ 0.0f, 0.0f, 0.0f };
            struct TrailPoint
            {
                DirectX::XMFLOAT3 pos;
                float age{ 0.0f };
            };
            std::vector<TrailPoint> trail;
            float trailAccumDist{ 0.0f };
        };

        struct BurstRuntime
        {
            float nextTime{ 0.0f };
            int remaining{ 0 };
            float repeatInterval{ 0.0f };
        };

        struct EmitterRuntime
        {
            float time{ 0.0f };
            float spawnAccum{ 0.0f };
            std::vector<Particle> particles;
            std::vector<BurstRuntime> bursts;
        };

        struct EffectRuntime
        {
            std::string effectPath;
            std::uint32_t playId{ 0 };
            std::vector<EmitterRuntime> emitters;
        };

        bool CreateShadersAndInputLayout();
        bool CreateStates();
        bool CreateDefaultTexture();
        bool CreateQuadMesh();
        bool CreateTrailShaders();
        bool EnsureTrailVertexBuffer(size_t vertexCount);
        bool EnsureBillboardVertexBuffer(size_t vertexCount);
        bool EnsureMeshLoaded(const std::string& path, MeshGpu& outMesh);
        bool EnsureMaterialLoaded(const std::string& effectBaseDir, const std::string& path, MaterialGpu& outMat);
        const EffectCache* GetEffectCache(const std::string& effectPath);

        ID3D11RenderDevice& m_renderDevice;
        Microsoft::WRL::ComPtr<ID3D11Device> m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_billboardVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ps;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_billboardLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbPerObject;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_trailVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_trailPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_trailLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_billboardVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_trailVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbTrailVS;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbTrailPS;
        size_t m_billboardVBSize{ 0 };
        size_t m_trailVBSize{ 0 };
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendAdd;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendAlpha;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterState;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_defaultTexture;
        MeshGpu m_quadMesh;

        std::unordered_map<std::string, MeshGpu> m_meshCache;
        std::unordered_map<std::string, MaterialGpu> m_materialCache;
        std::unordered_map<std::string, EffectCache> m_effectCache;
        std::unordered_map<uint64_t, EffectRuntime> m_runtimeCache;
        float m_timeSec{ 0.0f };
    };
}
