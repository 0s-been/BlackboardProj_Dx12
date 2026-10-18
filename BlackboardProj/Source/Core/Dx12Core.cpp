#include "Dx12Core.h"

bool Dx12Core::Initialize(HWND hwnd, UINT width, UINT height)
{
    m_width  = width;
    m_height = height;

#if defined(_DEBUG)
    // 디버그 빌드에서는 D3D12 디버그 레이어를 켜서 잘못된 API 사용을 출력창에 알려준다.
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        debug->EnableDebugLayer();
#endif

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&m_factory))))
        return false;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
        return false;

    // 커맨드 큐 / 할당자 / 리스트
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
        return false;
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator))))
        return false;
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr,
                                           IID_PPV_ARGS(&m_commandList))))
        return false;
    m_commandList->Close();

    // 스왑체인
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width            = width;
    scDesc.Height           = height;
    scDesc.Format           = kBackBufferFormat;
    scDesc.BufferCount      = kFrameCount;
    scDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &swapChain1)))
        return false;
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    swapChain1.As(&m_swapChain);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // 디스크립터 힙: RTV(백버퍼 개수만큼), SRV(ImGui 폰트 1개, 셰이더에서 보이도록)
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kFrameCount;
    if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
        return false;
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap))))
        return false;

    CreateRenderTargets();

    // CPU-GPU 동기화용 펜스
    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
        return false;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // 스포이드용 리드백 버퍼 (픽셀 1개지만 행 피치 정렬 때문에 256바이트)
    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width            = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    bufferDesc.Height           = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels        = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(m_device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&m_readbackBuffer))))
        return false;

    return true;
}

void Dx12Core::Shutdown()
{
    if (m_device)
        WaitForGpu();
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

void Dx12Core::CreateRenderTargets()
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrameCount; ++i)
    {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }
}

void Dx12Core::ReleaseRenderTargets()
{
    for (UINT i = 0; i < kFrameCount; ++i)
        m_renderTargets[i].Reset();
}

void Dx12Core::Resize(UINT width, UINT height)
{
    if (!m_swapChain || width == 0 || height == 0)
        return;   // 최소화 등

    WaitForGpu();
    ReleaseRenderTargets();
    m_swapChain->ResizeBuffers(kFrameCount, width, height, kBackBufferFormat, 0);
    CreateRenderTargets();

    m_width      = width;
    m_height     = height;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Dx12Core::Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter  = after;
    m_commandList->ResourceBarrier(1, &barrier);
}

ID3D12GraphicsCommandList* Dx12Core::BeginFrame(const float clearColor[4])
{
    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    ID3D12Resource* backBuffer = m_renderTargets[m_frameIndex].Get();
    Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_frameIndex * m_rtvDescriptorSize;
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT     scissor  = { 0, 0, (LONG)m_width, (LONG)m_height };
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    return m_commandList.Get();
}

void Dx12Core::EndFrame()
{
    ID3D12Resource* backBuffer = m_renderTargets[m_frameIndex].Get();
    bool readPixel = m_pixelReadRequested;

    if (readPixel)
    {
        // 화면에 그려진 결과(백버퍼)에서 픽셀 1개를 리드백 버퍼로 복사한다.
        Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource                          = m_readbackBuffer.Get();
        dst.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset             = 0;
        dst.PlacedFootprint.Footprint.Format   = kBackBufferFormat;
        dst.PlacedFootprint.Footprint.Width    = 1;
        dst.PlacedFootprint.Footprint.Height   = 1;
        dst.PlacedFootprint.Footprint.Depth    = 1;
        dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource        = backBuffer;
        src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        D3D12_BOX box = { (UINT)m_pixelX, (UINT)m_pixelY, 0, (UINT)m_pixelX + 1, (UINT)m_pixelY + 1, 1 };
        m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

        Transition(backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    }
    else
    {
        Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    }

    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    m_swapChain->Present(1, 0);   // VSync on

    // 가장 단순한 동기화 방식: 매 프레임 GPU가 끝날 때까지 기다린다.
    WaitForGpu();
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (readPixel)
    {
        D3D12_RANGE readRange = { 0, 4 };
        unsigned char* data = nullptr;
        if (SUCCEEDED(m_readbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&data))))
        {
            m_pixelRgb[0] = data[0] / 255.0f;
            m_pixelRgb[1] = data[1] / 255.0f;
            m_pixelRgb[2] = data[2] / 255.0f;
            D3D12_RANGE writeRange = { 0, 0 };
            m_readbackBuffer->Unmap(0, &writeRange);
            m_pixelReadReady = true;
        }
        m_pixelReadRequested = false;
    }
}

void Dx12Core::WaitForGpu()
{
    ++m_fenceValue;
    m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue)
    {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void Dx12Core::RequestPixelRead(int x, int y)
{
    if (x < 0 || y < 0 || x >= (int)m_width || y >= (int)m_height)
        return;
    m_pixelX = x;
    m_pixelY = y;
    m_pixelReadRequested = true;
}

bool Dx12Core::PopPixelRead(float outRgb[3])
{
    if (!m_pixelReadReady)
        return false;
    outRgb[0] = m_pixelRgb[0];
    outRgb[1] = m_pixelRgb[1];
    outRgb[2] = m_pixelRgb[2];
    m_pixelReadReady = false;
    return true;
}
