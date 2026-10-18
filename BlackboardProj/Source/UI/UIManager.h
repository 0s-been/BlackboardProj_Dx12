#pragma once

#include <string>
#include <windows.h>
#include <d3d12.h>

#include "Board/Blackboard.h"

class Dx12Core;

enum class ToolType
{
    Pen,
    Eraser,
    Eyedropper
};

// 현재 선택된 도구 상태. UI에서 바꾸고 App에서 그릴 때 사용한다.
struct ToolState
{
    ToolType tool        = ToolType::Pen;
    Color    color       = { 1.0f, 1.0f, 1.0f, 1.0f };  // 펜 색
    Color    customColor = { 0.8f, 0.2f, 0.2f, 1.0f };  // 사용자 색상(RGB 박스 / 스포이드)
    int      brushSize   = 8;                           // px

    static const int kMinBrushSize = 1;
    static const int kMaxBrushSize = 50;
};

// ImGui 초기화/렌더링과 도구 패널(좌측), 상태 표시줄(하단)을 담당한다.
class UIManager
{
public:
    bool Initialize(HWND hwnd, Dx12Core& core);
    void Shutdown();

    void NewFrame();
    void Build(ToolState& tool, Blackboard& board);
    void Render(ID3D12GraphicsCommandList* commandList, ID3D12DescriptorHeap* srvHeap);

    // 마우스가 UI 위에 있거나 UI를 조작 중이면 true (칠판 입력을 막는다)
    bool WantsMouse() const;

    void SetStatusMessage(const std::string& message) { m_statusMessage = message; }

private:
    void BuildToolPanel(ToolState& tool, Blackboard& board);
    void BuildExpandButton();
    void BuildStatusBar(const ToolState& tool, const Blackboard& board);
    void DrawCursorPreview(const ToolState& tool);

    void SaveBoard(Blackboard& board);
    void LoadBoard(Blackboard& board);

    HWND        m_hwnd = nullptr;
    bool        m_panelOpen = true;
    float       m_slide = 0.0f;   // 0 = 완전히 펼침, 1 = 완전히 접힘
    std::string m_statusMessage;
};
