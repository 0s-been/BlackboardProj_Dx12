#pragma once

#include <string>
#include <vector>

struct Color
{
    float r, g, b, a;
};

struct Point2
{
    float x, y;
};

// 마우스 드래그 한 번(누름 ~ 뗌)으로 만들어지는 선 하나.
struct Stroke
{
    Color               color;
    float               width;     // 붓 굵기(px)
    bool                isEraser;  // 지우개 선은 항상 현재 배경색으로 그려진다
    std::vector<Point2> points;
};

enum class BoardBackground : int
{
    Green = 0,
    White,
    Black,
    Count
};

// 칠판의 그림 데이터(선 목록 + 배경색)를 보관하고 파일로 저장/로드한다.
class Blackboard
{
public:
    void BeginStroke(Point2 p, const Color& color, float width, bool isEraser);
    void ContinueStroke(Point2 p);
    void EndStroke();
    bool IsDrawing() const { return m_drawing; }

    void Clear();

    void            SetBackground(BoardBackground bg);
    BoardBackground GetBackground() const { return m_background; }
    Color           GetBackgroundColor() const;
    static Color    GetBackgroundColor(BoardBackground bg);

    const std::vector<Stroke>& GetStrokes() const { return m_strokes; }

    // 데이터가 바뀔 때마다 증가한다. 렌더러가 정점을 다시 만들지 판단할 때 사용.
    unsigned int GetVersion() const { return m_version; }

    bool SaveToFile(const std::wstring& path) const;
    bool LoadFromFile(const std::wstring& path);

private:
    std::vector<Stroke> m_strokes;
    BoardBackground     m_background = BoardBackground::Green;
    bool                m_drawing    = false;
    unsigned int        m_version    = 0;
};
