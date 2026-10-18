#pragma once

#include <vector>
#include <d3d12.h>
#include <wrl/client.h>

#include "Blackboard.h"

using Microsoft::WRL::ComPtr;

// 칠판의 선(Stroke)들을 삼각형으로 바꿔서 DirectX 12로 그리는 클래스.
//  - 선분 하나 = 굵기만큼 폭을 가진 사각형(삼각형 2개)
//  - 점 하나   = 굵기 지름의 원(삼각형 부채꼴) -> 꺾이는 부분과 끝을 둥글게 만든다
class StrokeRenderer
{
public:
    bool Initialize(ID3D12Device* device, DXGI_FORMAT renderTargetFormat);
    void Draw(ID3D12GraphicsCommandList* commandList, const Blackboard& board, float screenWidth, float screenHeight);

private:
    struct Vertex
    {
        float x, y;         // 화면 픽셀 좌표
        float r, g, b, a;
    };

    bool CreatePipeline(DXGI_FORMAT renderTargetFormat);
    bool EnsureVertexBuffer(size_t vertexCount);
    void BuildVertices(const Blackboard& board);
    void AddCircle(Point2 center, float radius, const Color& color);
    void AddSegment(Point2 p0, Point2 p1, float radius, const Color& color);
    void AddVertex(float x, float y, const Color& color);

    ID3D12Device*               m_device = nullptr;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // CPU에서 만든 정점을 GPU로 보내는 업로드 버퍼 (항상 Map 상태로 유지)
    ComPtr<ID3D12Resource> m_vertexBuffer;
    Vertex*                m_mappedVertices = nullptr;
    size_t                 m_vertexCapacity = 0;

    std::vector<Vertex> m_vertices;
    unsigned int        m_builtVersion = 0xFFFFFFFF;
};
