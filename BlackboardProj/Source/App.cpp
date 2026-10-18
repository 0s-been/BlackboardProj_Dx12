#include "App.h"

#include <cstdio>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    const wchar_t kWindowClass[] = L"BlackboardWindowClass";
    const wchar_t kWindowTitle[] = L"칠판 (DirectX 12)";
    const int     kWindowWidth   = 1280;
    const int     kWindowHeight  = 800;
}

bool App::Initialize(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = instance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // 클라이언트 영역이 1280x800이 되도록 창 크기를 계산한다.
    RECT rect = { 0, 0, kWindowWidth, kWindowHeight };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
    if (!m_hwnd)
        return false;

    if (!m_core.Initialize(m_hwnd, kWindowWidth, kWindowHeight))
    {
        MessageBoxW(m_hwnd, L"DirectX 12 초기화에 실패했습니다.", kWindowTitle, MB_ICONERROR);
        return false;
    }
    if (!m_strokeRenderer.Initialize(m_core.GetDevice(), Dx12Core::kBackBufferFormat))
    {
        MessageBoxW(m_hwnd, L"선 렌더러(셰이더/파이프라인) 초기화에 실패했습니다.", kWindowTitle, MB_ICONERROR);
        return false;
    }
    if (!m_ui.Initialize(m_hwnd, m_core))
    {
        MessageBoxW(m_hwnd, L"ImGui 초기화에 실패했습니다.", kWindowTitle, MB_ICONERROR);
        return false;
    }

    m_initialized = true;
    ShowWindow(m_hwnd, showCommand);
    UpdateWindow(m_hwnd);
    return true;
}

int App::Run()
{
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // 처리할 메시지가 있으면 처리하고, 없으면 한 프레임 그린다.
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }
        Frame();
    }
    return (int)msg.wParam;
}

void App::Shutdown()
{
    if (m_initialized)
    {
        m_core.WaitForGpu();
        m_ui.Shutdown();
    }
    m_core.Shutdown();
}

void App::Frame()
{
    if (!IsWindow(m_hwnd))   // 종료 중 (WM_QUIT 처리 전)
        return;
    if (IsIconic(m_hwnd))   // 최소화 상태에서는 그리지 않는다
    {
        Sleep(10);
        return;
    }

    // 1) 입력 처리 + UI 구성
    m_ui.NewFrame();
    HandleBoardInput();
    m_ui.Build(m_tool, m_board);

    // 2) 그리기: 배경색으로 지우기 -> 선 -> UI
    Color bg = m_board.GetBackgroundColor();
    const float clearColor[4] = { bg.r, bg.g, bg.b, 1.0f };

    ID3D12GraphicsCommandList* commandList = m_core.BeginFrame(clearColor);
    m_strokeRenderer.Draw(commandList, m_board, (float)m_core.GetWidth(), (float)m_core.GetHeight());
    m_ui.Render(commandList, m_core.GetSrvHeap());
    if (!m_core.EndFrame())
    {
        // GPU 장치가 제거됨(드라이버 재시작, GPU 오류 등). 원인은 출력 창에 기록된다.
        MessageBoxW(m_hwnd, L"GPU 장치 오류로 프로그램을 종료합니다.", kWindowTitle, MB_ICONERROR);
        DestroyWindow(m_hwnd);
        return;
    }

    // 3) 스포이드 결과가 나왔으면 사용자 색상으로 설정하고 펜으로 전환
    float rgb[3];
    if (m_core.PopPixelRead(rgb))
    {
        m_tool.customColor = { rgb[0], rgb[1], rgb[2], 1.0f };
        m_tool.color       = m_tool.customColor;
        m_tool.tool        = ToolType::Pen;

        char message[64];
        snprintf(message, sizeof(message), "스포이드: RGB(%d, %d, %d)", (int)(rgb[0] * 255.0f + 0.5f),
                  (int)(rgb[1] * 255.0f + 0.5f), (int)(rgb[2] * 255.0f + 0.5f));
        m_ui.SetStatusMessage(message);
    }
}

void App::HandleBoardInput()
{
    ImGuiIO& io = ImGui::GetIO();
    Point2 mouse = { io.MousePos.x, io.MousePos.y };

    // 그리는 중이면 버튼을 뗄 때까지 계속 점을 추가한다. (UI 위로 지나가도 계속)
    if (m_board.IsDrawing())
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (ImGui::IsMousePosValid())
                m_board.ContinueStroke(mouse);
        }
        else
        {
            m_board.EndStroke();
        }
        return;
    }

    // 마우스가 UI 위에 있으면 칠판은 입력을 받지 않는다.
    if (m_ui.WantsMouse() || !ImGui::IsMousePosValid())
        return;

    // 마우스 휠로 붓 굵기 조절.
    // 한 프레임에 여러 칸이 들어오거나(빠른 휠) 터치패드처럼 1칸보다 작게 들어올 수 있으므로
    // 누적해 두었다가 1칸 단위로 반영한다.
    m_wheelAccum += io.MouseWheel;
    int steps = (int)m_wheelAccum;
    if (steps != 0)
    {
        m_wheelAccum -= (float)steps;
        m_tool.brushSize += steps;
        if (m_tool.brushSize < ToolState::kMinBrushSize) m_tool.brushSize = ToolState::kMinBrushSize;
        if (m_tool.brushSize > ToolState::kMaxBrushSize) m_tool.brushSize = ToolState::kMaxBrushSize;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        switch (m_tool.tool)
        {
        case ToolType::Eyedropper:
            m_core.RequestPixelRead((int)mouse.x, (int)mouse.y);
            break;
        case ToolType::Eraser:
            m_board.BeginStroke(mouse, m_board.GetBackgroundColor(), (float)m_tool.brushSize, true);
            break;
        case ToolType::Pen:
            m_board.BeginStroke(mouse, m_tool.color, (float)m_tool.brushSize, false);
            break;
        }
    }
}

LRESULT CALLBACK App::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // CreateWindowEx에 넘긴 this 포인터를 창에 저장해 두고 멤버 함수로 넘긴다.
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)createStruct->lpCreateParams);
    }

    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app)
        return app->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 마우스/키보드 입력을 ImGui에 먼저 전달한다.
    if (m_initialized && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (m_initialized && wParam != SIZE_MINIMIZED)
            m_core.Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
