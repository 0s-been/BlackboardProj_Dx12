#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// DirectX 12 기본 객체(디바이스, 스왑체인, 커맨드 리스트, 펜스)를 관리하는 클래스.
// 프레임마다 BeginFrame() -> (그리기) -> EndFrame() 순서로 사용한다.
class Dx12Core
{
public:
    static constexpr UINT        kFrameCount       = 2;                          // 더블 버퍼링
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    bool Initialize(HWND hwnd, UINT width, UINT height);
    void Shutdown();
    void Resize(UINT width, UINT height);

    // 현재 백버퍼를 clearColor로 지우고 그리기용 커맨드 리스트를 돌려준다.
    ID3D12GraphicsCommandList* BeginFrame(const float clearColor[4]);
    // 커맨드 리스트를 실행하고 화면에 표시(Present)한 뒤 GPU 작업 완료까지 기다린다.
    // GPU 장치가 제거되는 등 계속할 수 없는 오류면 false.
    bool EndFrame();
    void WaitForGpu();

    // 스포이드: 다음 EndFrame에서 (x, y) 픽셀의 색을 백버퍼에서 읽어온다.
    void RequestPixelRead(int x, int y);
    // 읽기가 끝났으면 rgb(0~1)를 채우고 true를 반환한다. (한 번만 true)
    bool PopPixelRead(float outRgb[3]);

    ID3D12Device*         GetDevice() const        { return m_device.Get(); }
    ID3D12CommandQueue*   GetCommandQueue() const  { return m_commandQueue.Get(); }
    ID3D12DescriptorHeap* GetSrvHeap() const       { return m_srvHeap.Get(); }
    UINT                  GetWidth() const         { return m_width; }
    UINT                  GetHeight() const        { return m_height; }

private:
    void CreateRenderTargets();
    void ReleaseRenderTargets();
    void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    UINT m_width  = 0;
    UINT m_height = 0;

    ComPtr<IDXGIFactory4>             m_factory;
    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_commandQueue;
    ComPtr<IDXGISwapChain3>           m_swapChain;
    ComPtr<ID3D12CommandAllocator>    m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;   // 백버퍼용 RTV 힙
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;   // ImGui 폰트 텍스처용 SRV 힙
    UINT                         m_rtvDescriptorSize = 0;
    ComPtr<ID3D12Resource>       m_renderTargets[kFrameCount];
    UINT                         m_frameIndex = 0;

    ComPtr<ID3D12Fence> m_fence;
    UINT64              m_fenceValue = 0;
    HANDLE              m_fenceEvent = nullptr;

    // 스포이드용 픽셀 읽기
    ComPtr<ID3D12Resource> m_readbackBuffer;
    bool  m_pixelReadRequested = false;
    bool  m_pixelReadReady     = false;
    int   m_pixelX = 0;
    int   m_pixelY = 0;
    float m_pixelRgb[3] = {};
};
