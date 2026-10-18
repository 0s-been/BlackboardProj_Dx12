#include "UIManager.h"

#include <commdlg.h>
#include <cstdio>

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include "Core/Dx12Core.h"

namespace
{
    const float kPanelWidth    = 260.0f;
    const float kSlideSpeed    = 6.0f;    // 초당 슬라이드 비율 (약 0.17초에 완료)
    const float kStatusHeight  = 28.0f;

    struct PaletteEntry
    {
        const char* name;
        Color       color;
    };

    // 기본 색상 8종
    const PaletteEntry kPalette[] =
    {
        { "검정", { 0.00f, 0.00f, 0.00f, 1.0f } },
        { "흰색", { 1.00f, 1.00f, 1.00f, 1.0f } },
        { "빨강", { 0.90f, 0.22f, 0.21f, 1.0f } },
        { "주황", { 0.98f, 0.55f, 0.00f, 1.0f } },
        { "노랑", { 1.00f, 0.84f, 0.00f, 1.0f } },
        { "파랑", { 0.12f, 0.53f, 0.90f, 1.0f } },
        { "남색", { 0.12f, 0.23f, 0.54f, 1.0f } },
        { "보라", { 0.56f, 0.14f, 0.67f, 1.0f } },
    };

    const char* kBackgroundNames[] = { "초록", "흰색", "검정" };

    bool SameColor(const Color& a, const Color& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    ImVec4 ToImVec4(const Color& c)
    {
        return ImVec4(c.r, c.g, c.b, c.a);
    }

    // 선택된 버튼은 진한 색으로 표시하는 토글 버튼
    bool ToggleButton(const char* label, bool selected, const ImVec2& size)
    {
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool pressed = ImGui::Button(label, size);
        if (selected)
            ImGui::PopStyleColor();
        return pressed;
    }

    // 색 버튼. 선택된 색은 테두리를 그린다.
    bool ColorSwatch(const char* id, const Color& color, bool selected, const ImVec2& size)
    {
        bool pressed = ImGui::ColorButton(id, ToImVec4(color), ImGuiColorEditFlags_NoTooltip, size);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 border = selected ? IM_COL32(66, 150, 250, 255) : IM_COL32(110, 110, 110, 255);
        drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), border, 0.0f, 0, selected ? 3.0f : 1.0f);
        return pressed;
    }

    bool ShowFileDialog(HWND owner, wchar_t* path, DWORD pathSize, bool save)
    {
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = owner;
        ofn.lpstrFilter = L"칠판 파일 (*.board)\0*.board\0모든 파일 (*.*)\0*.*\0";
        ofn.lpstrFile   = path;
        ofn.nMaxFile    = pathSize;
        ofn.lpstrDefExt = L"board";
        if (save)
        {
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            return GetSaveFileNameW(&ofn) != FALSE;
        }
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        return GetOpenFileNameW(&ofn) != FALSE;
    }
}

bool UIManager::Initialize(HWND hwnd, Dx12Core& core)
{
    m_hwnd = hwnd;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // 창 위치를 imgui.ini로 저장하지 않는다

    ImGui::StyleColorsDark();

    // 한글 표시를 위해 Windows 기본 글꼴(맑은 고딕)을 불러온다. 없으면 ImGui 기본 글꼴.
    ImFont* font = nullptr;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\malgun.ttf") != INVALID_FILE_ATTRIBUTES)
        font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", 17.0f, nullptr,
                                            io.Fonts->GetGlyphRangesKorean());
    if (!font)
        io.Fonts->AddFontDefault();

    if (!ImGui_ImplWin32_Init(hwnd))
        return false;

    ID3D12DescriptorHeap* srvHeap = core.GetSrvHeap();

    ImGui_ImplDX12_InitInfo initInfo;
    initInfo.Device            = core.GetDevice();
    initInfo.CommandQueue      = core.GetCommandQueue();
    initInfo.NumFramesInFlight = Dx12Core::kFrameCount;
    initInfo.RTVFormat         = Dx12Core::kBackBufferFormat;
    initInfo.SrvDescriptorHeap = srvHeap;
    initInfo.LegacySingleSrvCpuDescriptor = srvHeap->GetCPUDescriptorHandleForHeapStart();
    initInfo.LegacySingleSrvGpuDescriptor = srvHeap->GetGPUDescriptorHandleForHeapStart();
    return ImGui_ImplDX12_Init(&initInfo);
}

void UIManager::Shutdown()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void UIManager::NewFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

bool UIManager::WantsMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

void UIManager::Build(ToolState& tool, Blackboard& board)
{
    // 패널 슬라이드 애니메이션: m_slide를 목표값(0 또는 1)으로 조금씩 이동시킨다.
    float target = m_panelOpen ? 0.0f : 1.0f;
    float step   = ImGui::GetIO().DeltaTime * kSlideSpeed;
    if (m_slide < target)      m_slide = (m_slide + step > target) ? target : m_slide + step;
    else if (m_slide > target) m_slide = (m_slide - step < target) ? target : m_slide - step;

    if (m_slide < 1.0f)
        BuildToolPanel(tool, board);
    if (!m_panelOpen && m_slide >= 1.0f)
        BuildExpandButton();

    BuildStatusBar(tool, board);
    DrawCursorPreview(tool);
}

void UIManager::BuildToolPanel(ToolState& tool, Blackboard& board)
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(-kPanelWidth * m_slide, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, display.y - kStatusHeight));
    ImGui::Begin("##ToolPanel", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    const float fullWidth = ImGui::GetContentRegionAvail().x;
    const float spacing   = ImGui::GetStyle().ItemSpacing.x;

    // 제목줄: "도구" + 접기(<) 버튼
    ImGui::AlignTextToFramePadding();
    ImGui::Text("도구");
    ImGui::SameLine(ImGui::GetCursorStartPos().x + fullWidth - 30.0f);
    if (ImGui::Button("<", ImVec2(30.0f, 0.0f)))
        m_panelOpen = false;
    ImGui::Separator();

    // 파일
    ImGui::TextDisabled("파일");
    float third = (fullWidth - spacing * 2.0f) / 3.0f;
    if (ImGui::Button("저장", ImVec2(third, 0.0f)))
        SaveBoard(board);
    ImGui::SameLine();
    if (ImGui::Button("불러오기", ImVec2(third, 0.0f)))
        LoadBoard(board);
    ImGui::SameLine();
    if (ImGui::Button("전체 지우기", ImVec2(third, 0.0f)))
        board.Clear();

    // 도구
    ImGui::Spacing();
    ImGui::TextDisabled("도구");
    if (ToggleButton("펜", tool.tool == ToolType::Pen, ImVec2(third, 0.0f)))
        tool.tool = ToolType::Pen;
    ImGui::SameLine();
    if (ToggleButton("지우개", tool.tool == ToolType::Eraser, ImVec2(third, 0.0f)))
        tool.tool = ToolType::Eraser;
    ImGui::SameLine();
    if (ToggleButton("스포이드", tool.tool == ToolType::Eyedropper, ImVec2(third, 0.0f)))
        tool.tool = ToolType::Eyedropper;

    // 기본 색상 8종 (누르면 펜으로 전환)
    ImGui::Spacing();
    ImGui::TextDisabled("기본 색상");
    const int   paletteCount = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    const float swatch       = (fullWidth - spacing * (paletteCount - 1)) / paletteCount;
    for (int i = 0; i < paletteCount; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        ImGui::PushID(i);
        bool selected = tool.tool == ToolType::Pen && SameColor(tool.color, kPalette[i].color);
        if (ColorSwatch("##palette", kPalette[i].color, selected, ImVec2(swatch, swatch)))
        {
            tool.color = kPalette[i].color;
            tool.tool  = ToolType::Pen;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", kPalette[i].name);
        ImGui::PopID();
    }

    // 사용자 색상: 색 박스 안의 포인트(채도/명도)와 색상 막대를 드래그해서 RGB를 정한다.
    ImGui::Spacing();
    ImGui::TextDisabled("사용자 색상");
    ImGui::SetNextItemWidth(fullWidth);
    if (ImGui::ColorPicker3("##custom", &tool.customColor.r,
                            ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB |
                            ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoAlpha))
    {
        tool.color = tool.customColor;
        tool.tool  = ToolType::Pen;
    }
    bool customSelected = tool.tool == ToolType::Pen && SameColor(tool.color, tool.customColor);
    if (ColorSwatch("##customSwatch", tool.customColor, customSelected, ImVec2(swatch * 2.0f, swatch)))
    {
        tool.color = tool.customColor;
        tool.tool  = ToolType::Pen;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("사용자 색 사용");

    // 붓 굵기
    ImGui::Spacing();
    ImGui::TextDisabled("붓 굵기 (칠판 위에서 마우스 휠)");
    ImGui::SetNextItemWidth(fullWidth);
    ImGui::SliderInt("##brushSize", &tool.brushSize, ToolState::kMinBrushSize, ToolState::kMaxBrushSize, "%d px");

    // 칠판 배경색
    ImGui::Spacing();
    ImGui::TextDisabled("칠판 배경색");
    for (int i = 0; i < (int)BoardBackground::Count; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::RadioButton(kBackgroundNames[i], (int)board.GetBackground() == i))
            board.SetBackground((BoardBackground)i);
    }

    ImGui::End();
}

void UIManager::BuildExpandButton()
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::Begin("##ExpandButton", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::Button(">", ImVec2(24.0f, 44.0f)))
        m_panelOpen = true;
    ImGui::End();
}

void UIManager::BuildStatusBar(const ToolState& tool, const Blackboard& board)
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, display.y - kStatusHeight));
    ImGui::SetNextWindowSize(ImVec2(display.x, kStatusHeight));
    ImGui::Begin("##StatusBar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar);

    const char* toolName = tool.tool == ToolType::Pen    ? "펜"
                         : tool.tool == ToolType::Eraser ? "지우개"
                                                         : "스포이드";
    ImVec2 mouse = ImGui::GetMousePos();
    ImGui::Text("도구: %s    굵기: %dpx    색: (%d, %d, %d)    배경: %s    선 개수: %d    커서: (%d, %d)    %s",
                toolName, tool.brushSize,
                (int)(tool.color.r * 255.0f + 0.5f), (int)(tool.color.g * 255.0f + 0.5f),
                (int)(tool.color.b * 255.0f + 0.5f),
                kBackgroundNames[(int)board.GetBackground()], (int)board.GetStrokes().size(),
                ImGui::IsMousePosValid() ? (int)mouse.x : 0, ImGui::IsMousePosValid() ? (int)mouse.y : 0,
                m_statusMessage.c_str());
    ImGui::End();
}

void UIManager::DrawCursorPreview(const ToolState& tool)
{
    if (WantsMouse() || !ImGui::IsMousePosValid())
        return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec2      mouse    = ImGui::GetMousePos();

    if (tool.tool == ToolType::Eyedropper)
    {
        // 가운데 픽셀은 비워 둔다. (스포이드가 미리보기 링 색을 읽지 않도록)
        drawList->AddCircle(mouse, 9.0f, IM_COL32(0, 0, 0, 255), 0, 3.0f);
        drawList->AddCircle(mouse, 9.0f, IM_COL32(255, 255, 255, 255), 0, 1.5f);
        ImGui::SetTooltip("클릭하여 색 추출");
        return;
    }

    // 붓 굵기만큼의 원 미리보기 (흰색 + 검정 테두리 -> 어떤 배경에서도 보이도록)
    float radius = tool.brushSize * 0.5f;
    drawList->AddCircle(mouse, radius + 1.0f, IM_COL32(0, 0, 0, 255));
    drawList->AddCircle(mouse, radius, IM_COL32(255, 255, 255, 255));
}

void UIManager::SaveBoard(Blackboard& board)
{
    wchar_t path[MAX_PATH] = L"drawing.board";
    if (!ShowFileDialog(m_hwnd, path, MAX_PATH, true))
        return;
    m_statusMessage = board.SaveToFile(path) ? "저장 완료" : "저장 실패";
}

void UIManager::LoadBoard(Blackboard& board)
{
    wchar_t path[MAX_PATH] = L"";
    if (!ShowFileDialog(m_hwnd, path, MAX_PATH, false))
        return;
    m_statusMessage = board.LoadFromFile(path) ? "불러오기 완료" : "불러오기 실패 (올바른 칠판 파일이 아닙니다)";
}

void UIManager::Render(ID3D12GraphicsCommandList* commandList, ID3D12DescriptorHeap* srvHeap)
{
    ImGui::Render();
    commandList->SetDescriptorHeaps(1, &srvHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}
