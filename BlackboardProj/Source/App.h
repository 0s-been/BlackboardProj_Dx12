#pragma once

#include <windows.h>

#include "Core/Dx12Core.h"
#include "Board/Blackboard.h"
#include "Board/StrokeRenderer.h"
#include "UI/UIManager.h"

// 창 생성, 메시지 루프, 각 모듈(DX12 Core / 칠판 / UI)을 연결하는 클래스.
class App
{
public:
    bool Initialize(HINSTANCE instance, int showCommand);
    int  Run();
    void Shutdown();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void Frame();
    void HandleBoardInput();

    HWND           m_hwnd        = nullptr;
    bool           m_initialized = false;
    Dx12Core       m_core;
    Blackboard     m_board;
    StrokeRenderer m_strokeRenderer;
    UIManager      m_ui;
    ToolState      m_tool;
};
