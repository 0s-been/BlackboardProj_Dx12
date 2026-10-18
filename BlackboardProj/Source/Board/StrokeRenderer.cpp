#include "StrokeRenderer.h"

#include <cmath>
#include <cstring>
#include <d3dcompiler.h>

namespace
{
    // 정점 좌표는 픽셀 단위로 넘기고, 셰이더에서 화면 크기로 나눠 NDC(-1~1)로 바꾼다.
    const char kShaderSource[] = R"(
cbuffer ScreenConstants : register(b0)
{
    float2 gScreenSize;
    float2 gPadding;     // 상수 버퍼는 16바이트 단위라서 루트 상수 4개와 크기를 맞춘다
};

struct VSInput
{
    float2 position : POSITION;
    float4 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    float2 ndc = input.position / gScreenSize * 2.0f - 1.0f;
    ndc.y = -ndc.y;   // 화면 좌표는 아래로 갈수록 y가 커진다
    output.position = float4(ndc, 0.0f, 1.0f);
    output.color    = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
)";

    const float kPi = 3.14159265f;

    ComPtr<ID3DBlob> CompileShader(const char* entryPoint, const char* target)
    {
        UINT flags = 0;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ComPtr<ID3DBlob> code;
        ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "StrokeShader", nullptr, nullptr,
                                entryPoint, target, flags, 0, &code, &errors);
        if (FAILED(hr))
        {
            if (errors)
                OutputDebugStringA((const char*)errors->GetBufferPointer());
            return nullptr;
        }
        return code;
    }
}

bool StrokeRenderer::Initialize(ID3D12Device* device, DXGI_FORMAT renderTargetFormat)
{
    m_device = device;
    return CreatePipeline(renderTargetFormat) && EnsureVertexBuffer(64 * 1024);
}

bool StrokeRenderer::CreatePipeline(DXGI_FORMAT renderTargetFormat)
{
    // 루트 시그니처: 32비트 상수 4개(화면 너비, 높이, 패딩 2개)만 사용한다.
    // 셰이더의 cbuffer는 16바이트(상수 4개) 단위로 잡히므로 2개만 선언하면 크기가 맞지 않는다.
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParam.Constants.ShaderRegister = 0;
    rootParam.Constants.RegisterSpace  = 0;
    rootParam.Constants.Num32BitValues = 4;
    rootParam.ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters   = &rootParam;
    rootDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)))
        return false;
    if (FAILED(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                             IID_PPV_ARGS(&m_rootSignature))))
        return false;

    ComPtr<ID3DBlob> vs = CompileShader("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = CompileShader("PSMain", "ps_5_0");
    if (!vs || !ps)
        return false;

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout    = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };

    psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;   // 삼각형 방향과 상관없이 그린다
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.BlendState.RenderTarget[0].BlendEnable           = FALSE;  // 불투명 선
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.DepthStencilState.DepthEnable   = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = renderTargetFormat;
    psoDesc.SampleDesc.Count      = 1;

    return SUCCEEDED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
}

bool StrokeRenderer::EnsureVertexBuffer(size_t vertexCount)
{
    if (vertexCount <= m_vertexCapacity)
        return true;

    // 모자라면 2배씩 키워서 새로 만든다.
    // (Dx12Core가 매 프레임 GPU 완료를 기다리므로 이전 버퍼는 이미 사용이 끝난 상태다)
    size_t newCapacity = m_vertexCapacity == 0 ? vertexCount : m_vertexCapacity;
    while (newCapacity < vertexCount)
        newCapacity *= 2;

    if (m_vertexBuffer)
        m_vertexBuffer->Unmap(0, nullptr);
    m_vertexBuffer.Reset();
    m_mappedVertices = nullptr;
    m_vertexCapacity = 0;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = newCapacity * sizeof(Vertex);
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&m_vertexBuffer))))
        return false;

    D3D12_RANGE noRead = { 0, 0 };
    m_vertexBuffer->Map(0, &noRead, reinterpret_cast<void**>(&m_mappedVertices));
    m_vertexCapacity = newCapacity;
    return true;
}

void StrokeRenderer::AddVertex(float x, float y, const Color& color)
{
    m_vertices.push_back({ x, y, color.r, color.g, color.b, color.a });
}

void StrokeRenderer::AddCircle(Point2 c, float radius, const Color& color)
{
    // 반지름이 클수록 더 많은 조각으로 나눠 매끄럽게 만든다.
    int segments = (int)(radius * 1.5f);
    if (segments < 8)  segments = 8;
    if (segments > 32) segments = 32;

    for (int i = 0; i < segments; ++i)
    {
        float a0 = 2.0f * kPi * i / segments;
        float a1 = 2.0f * kPi * (i + 1) / segments;
        AddVertex(c.x, c.y, color);
        AddVertex(c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius, color);
        AddVertex(c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius, color);
    }
}

void StrokeRenderer::AddSegment(Point2 p0, Point2 p1, float radius, const Color& color)
{
    float dx = p1.x - p0.x;
    float dy = p1.y - p0.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.0001f)
        return;

    // 선분에 수직인 방향(법선)으로 반지름만큼 벌려서 사각형을 만든다.
    float nx = -dy / length * radius;
    float ny =  dx / length * radius;

    AddVertex(p0.x + nx, p0.y + ny, color);
    AddVertex(p1.x + nx, p1.y + ny, color);
    AddVertex(p1.x - nx, p1.y - ny, color);

    AddVertex(p0.x + nx, p0.y + ny, color);
    AddVertex(p1.x - nx, p1.y - ny, color);
    AddVertex(p0.x - nx, p0.y - ny, color);
}

void StrokeRenderer::AddStroke(const Stroke& stroke, const Color& background)
{
    const Color& color  = stroke.isEraser ? background : stroke.color;
    float        radius = stroke.width * 0.5f;

    for (size_t i = 0; i < stroke.points.size(); ++i)
    {
        AddCircle(stroke.points[i], radius, color);
        if (i > 0)
            AddSegment(stroke.points[i - 1], stroke.points[i], radius, color);
    }
}

size_t StrokeRenderer::UpdateVertices(const Blackboard& board)
{
    const std::vector<Stroke>& strokes = board.GetStrokes();

    // 그리는 중에는 마지막 선만 길어지거나 새 선이 뒤에 붙는다.
    // 그런 경우엔 이전 선들의 정점은 그대로 두고 마지막 선부터 다시 만든다.
    // (전체 지우기/불러오기/배경 변경이면 처음부터 다시 만든다)
    size_t firstStroke = 0;
    if (board.GetResetVersion() == m_builtResetVersion && !m_strokeStarts.empty() &&
        strokes.size() >= m_strokeStarts.size())
        firstStroke = m_strokeStarts.size() - 1;

    m_vertices.resize(firstStroke < m_strokeStarts.size() ? m_strokeStarts[firstStroke] : 0);
    m_strokeStarts.resize(firstStroke);

    // 그린 순서대로 쌓이므로 나중 선(지우개 포함)이 앞의 선을 덮는다.
    Color background = board.GetBackgroundColor();
    for (size_t i = firstStroke; i < strokes.size(); ++i)
    {
        m_strokeStarts.push_back(m_vertices.size());
        AddStroke(strokes[i], background);
    }

    m_builtVersion      = board.GetVersion();
    m_builtResetVersion = board.GetResetVersion();
    return firstStroke < m_strokeStarts.size() ? m_strokeStarts[firstStroke] : m_vertices.size();
}

void StrokeRenderer::Draw(ID3D12GraphicsCommandList* commandList, const Blackboard& board,
                          float screenWidth, float screenHeight)
{
    // 칠판 데이터가 바뀌었을 때만 정점을 다시 만들고, 바뀐 부분만 GPU 버퍼로 복사한다.
    if (board.GetVersion() != m_builtVersion)
    {
        size_t firstChanged = UpdateVertices(board);
        if (m_vertices.size() > m_vertexCapacity)
        {
            if (!EnsureVertexBuffer(m_vertices.size()))
            {
                m_builtResetVersion = 0xFFFFFFFF;   // 다음 프레임에 처음부터 다시 시도
                m_builtVersion      = 0xFFFFFFFF;
                return;
            }
            firstChanged = 0;   // 새 버퍼에는 전부 복사
        }
        if (m_vertices.size() > firstChanged)
            std::memcpy(m_mappedVertices + firstChanged, m_vertices.data() + firstChanged,
                        (m_vertices.size() - firstChanged) * sizeof(Vertex));
    }

    if (m_vertices.empty() || m_vertices.size() > m_vertexCapacity)
        return;

    float screenConstants[4] = { screenWidth, screenHeight, 0.0f, 0.0f };

    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->SetPipelineState(m_pipelineState.Get());
    commandList->SetGraphicsRoot32BitConstants(0, 4, screenConstants, 0);

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    vbView.SizeInBytes    = (UINT)(m_vertices.size() * sizeof(Vertex));
    vbView.StrideInBytes  = sizeof(Vertex);

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vbView);
    commandList->DrawInstanced((UINT)m_vertices.size(), 1, 0, 0);
}
