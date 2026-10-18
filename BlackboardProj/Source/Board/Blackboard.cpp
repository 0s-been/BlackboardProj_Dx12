#include "Blackboard.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace
{
    // 파일 형식 (.board, 리틀 엔디안 바이너리)
    //   char[4]  "BBRD"
    //   uint32   버전(1)
    //   int32    배경색
    //   uint32   선 개수
    //   선마다: float rgba[4], float 굵기, uint8 지우개 여부, uint32 점 개수, float2 점[점 개수]
    const char     kMagic[4]     = { 'B', 'B', 'R', 'D' };
    const uint32_t kFileVersion  = 1;
    const float    kMaxWidth     = 1000.0f;   // 손상된 파일 방어용 (굵기 상한)

    // 선 하나가 차지하는 최소 바이트: 색(16) + 굵기(4) + 지우개(1) + 점 개수(4) + 점 1개(8)
    const uint64_t kMinStrokeBytes = 33;

    // 이전 점과 이 거리(px)보다 가까우면 점을 추가하지 않는다.
    const float kMinPointDistance = 1.5f;

    template <typename T>
    void Write(std::ofstream& out, const T& value)
    {
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <typename T>
    bool Read(std::ifstream& in, T& value)
    {
        in.read(reinterpret_cast<char*>(&value), sizeof(T));
        return (bool)in;
    }

    // 파일에서 아직 읽지 않은 바이트 수
    uint64_t Remaining(std::ifstream& in, uint64_t fileSize)
    {
        return fileSize - (uint64_t)in.tellg();
    }

    bool IsValidStroke(const Stroke& s)
    {
        const float c[4] = { s.color.r, s.color.g, s.color.b, s.color.a };
        for (float v : c)
            if (!std::isfinite(v) || v < 0.0f || v > 1.0f)
                return false;
        if (!std::isfinite(s.width) || s.width <= 0.0f || s.width > kMaxWidth)
            return false;
        for (const Point2& p : s.points)
            if (!std::isfinite(p.x) || !std::isfinite(p.y))
                return false;
        return true;
    }
}

void Blackboard::BeginStroke(Point2 p, const Color& color, float width, bool isEraser)
{
    Stroke stroke;
    stroke.color    = color;
    stroke.width    = width;
    stroke.isEraser = isEraser;
    stroke.points.push_back(p);
    m_strokes.push_back(stroke);

    m_drawing = true;
    ++m_version;
}

void Blackboard::ContinueStroke(Point2 p)
{
    if (!m_drawing || m_strokes.empty())
        return;

    std::vector<Point2>& points = m_strokes.back().points;
    const Point2& last = points.back();
    float dx = p.x - last.x;
    float dy = p.y - last.y;
    if (dx * dx + dy * dy < kMinPointDistance * kMinPointDistance)
        return;

    points.push_back(p);
    ++m_version;
}

void Blackboard::EndStroke()
{
    m_drawing = false;
}

void Blackboard::Clear()
{
    m_strokes.clear();
    m_drawing = false;
    ++m_version;
    ++m_resetVersion;
}

void Blackboard::SetBackground(BoardBackground bg)
{
    m_background = bg;
    ++m_version;        // 지우개 선 색도 바뀌므로 다시 그려야 한다
    ++m_resetVersion;
}

Color Blackboard::GetBackgroundColor() const
{
    return GetBackgroundColor(m_background);
}

Color Blackboard::GetBackgroundColor(BoardBackground bg)
{
    switch (bg)
    {
    case BoardBackground::White: return { 0.96f, 0.96f, 0.94f, 1.0f };
    case BoardBackground::Black: return { 0.10f, 0.10f, 0.10f, 1.0f };
    default:                     return { 0.18f, 0.36f, 0.23f, 1.0f };   // 칠판 초록
    }
}

bool Blackboard::SaveToFile(const std::wstring& path) const
{
    std::ofstream out(std::filesystem::path(path), std::ios::binary);
    if (!out)
        return false;

    out.write(kMagic, sizeof(kMagic));
    Write(out, kFileVersion);
    Write(out, (int32_t)m_background);
    Write(out, (uint32_t)m_strokes.size());

    for (const Stroke& stroke : m_strokes)
    {
        Write(out, stroke.color);
        Write(out, stroke.width);
        Write(out, (uint8_t)(stroke.isEraser ? 1 : 0));
        Write(out, (uint32_t)stroke.points.size());
        out.write(reinterpret_cast<const char*>(stroke.points.data()), stroke.points.size() * sizeof(Point2));
    }
    return (bool)out;
}

bool Blackboard::LoadFromFile(const std::wstring& path)
{
    std::ifstream in(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!in)
        return false;
    const uint64_t fileSize = (uint64_t)in.tellg();
    in.seekg(0);

    char     magic[4];
    uint32_t version     = 0;
    int32_t  background  = 0;
    uint32_t strokeCount = 0;

    in.read(magic, sizeof(magic));
    if (!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
        return false;
    if (!Read(in, version) || version != kFileVersion)
        return false;
    if (!Read(in, background) || background < 0 || background >= (int32_t)BoardBackground::Count)
        return false;
    // 개수는 실제 남은 파일 크기로 검사한다. (손상된 개수로 큰 메모리를 잡지 않도록)
    if (!Read(in, strokeCount) || strokeCount * kMinStrokeBytes > Remaining(in, fileSize))
        return false;

    // 다 읽은 뒤에 교체한다. 중간에 실패하면 기존 그림은 그대로 둔다.
    std::vector<Stroke> strokes;
    strokes.reserve(strokeCount);
    for (uint32_t i = 0; i < strokeCount; ++i)
    {
        Stroke   stroke;
        uint8_t  isEraser   = 0;
        uint32_t pointCount = 0;
        if (!Read(in, stroke.color) || !Read(in, stroke.width) || !Read(in, isEraser) || !Read(in, pointCount))
            return false;
        if (pointCount == 0 || (uint64_t)pointCount * sizeof(Point2) > Remaining(in, fileSize))
            return false;

        stroke.isEraser = (isEraser != 0);
        stroke.points.resize(pointCount);
        in.read(reinterpret_cast<char*>(stroke.points.data()), pointCount * sizeof(Point2));
        if (!in || !IsValidStroke(stroke))
            return false;
        strokes.push_back(std::move(stroke));
    }

    m_strokes    = std::move(strokes);
    m_background = (BoardBackground)background;
    m_drawing    = false;
    ++m_version;
    ++m_resetVersion;
    return true;
}
