// 칠판 데이터(Blackboard)와 선 정점 생성(StrokeRenderer)에 대한 단위 테스트.
// GPU 없이 CPU 로직만 검사한다. 실패한 항목이 있으면 종료 코드 1.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <windows.h>

#include "Board/Blackboard.h"
#include "Board/StrokeRenderer.h"

struct StrokeRendererTestAccess
{
    // 처음부터 전부 다시 만든다
    static size_t Build(StrokeRenderer& r, const Blackboard& b)
    {
        r.m_builtResetVersion = 0xFFFFFFFF;
        r.UpdateVertices(b);
        return r.m_vertices.size();
    }
    // Draw()와 같은 방식(바뀐 부분만)으로 갱신한다
    static size_t Update(StrokeRenderer& r, const Blackboard& b)
    {
        return r.UpdateVertices(b);
    }
    static bool SameVertices(const StrokeRenderer& a, const StrokeRenderer& b)
    {
        return a.m_vertices.size() == b.m_vertices.size() &&
               std::memcmp(a.m_vertices.data(), b.m_vertices.data(),
                           a.m_vertices.size() * sizeof(StrokeRenderer::Vertex)) == 0;
    }
    static Color VertexColor(const StrokeRenderer& r, size_t i)
    {
        const auto& v = r.m_vertices[i];
        return { v.r, v.g, v.b, v.a };
    }
    static Point2 VertexPos(const StrokeRenderer& r, size_t i)
    {
        return { r.m_vertices[i].x, r.m_vertices[i].y };
    }
};

namespace
{
    int g_failed = 0;
    int g_passed = 0;

    void Check(bool ok, const char* id, const char* name)
    {
        std::printf("[%s] %-6s %s\n", ok ? "PASS" : "FAIL", id, name);
        ok ? ++g_passed : ++g_failed;
    }

    std::wstring TempPath(const wchar_t* name)
    {
        return (std::filesystem::temp_directory_path() / name).wstring();
    }

    const Color kRed   = { 1, 0, 0, 1 };
    const Color kWhite = { 1, 1, 1, 1 };

    bool SameColor(const Color& a, const Color& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    // 파일 헤더를 직접 써서 손상된 파일을 만든다.
    void WriteRaw(const std::wstring& path, const std::vector<unsigned char>& bytes)
    {
        std::ofstream out(std::filesystem::path(path), std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    template <typename T>
    void Append(std::vector<unsigned char>& bytes, T value)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }

    std::vector<unsigned char> Header(uint32_t version, int32_t bg, uint32_t strokeCount)
    {
        std::vector<unsigned char> bytes = { 'B', 'B', 'R', 'D' };
        Append(bytes, version);
        Append(bytes, bg);
        Append(bytes, strokeCount);
        return bytes;
    }

    Blackboard MakeSampleBoard()
    {
        Blackboard b;
        b.BeginStroke({ 10, 10 }, kRed, 8, false);
        b.ContinueStroke({ 50, 60 });
        b.ContinueStroke({ 90, 20 });
        b.EndStroke();
        b.BeginStroke({ 30, 30 }, kWhite, 20, true);
        b.ContinueStroke({ 40, 30 });
        b.EndStroke();
        return b;
    }
}

// ---------------------------------------------------------------- Blackboard
void TestStrokeEditing()
{
    Blackboard b;
    unsigned v0 = b.GetVersion();
    b.BeginStroke({ 1, 2 }, kRed, 5, false);
    Check(b.IsDrawing() && b.GetStrokes().size() == 1 && b.GetStrokes()[0].points.size() == 1 && b.GetVersion() != v0,
          "U01", "BeginStroke: 선 1개, 점 1개, 그리는 중, 버전 증가");

    b.ContinueStroke({ 2, 2 });      // 1px 이동 -> 무시
    b.ContinueStroke({ 1.5f, 3 });   // 약 1.1px -> 무시
    Check(b.GetStrokes()[0].points.size() == 1, "U02", "ContinueStroke: 1.5px 미만 이동은 점을 추가하지 않음");

    unsigned v1 = b.GetVersion();
    b.ContinueStroke({ 10, 2 });
    Check(b.GetStrokes()[0].points.size() == 2 && b.GetVersion() != v1, "U03", "ContinueStroke: 충분히 이동하면 점 추가, 버전 증가");

    b.EndStroke();
    b.ContinueStroke({ 100, 100 });
    Check(!b.IsDrawing() && b.GetStrokes()[0].points.size() == 2, "U04", "EndStroke 이후 ContinueStroke는 무시");

    Blackboard empty;
    empty.ContinueStroke({ 5, 5 });
    Check(empty.GetStrokes().empty(), "U05", "BeginStroke 없이 ContinueStroke 호출해도 안전");

    unsigned v2 = b.GetVersion();
    b.Clear();
    Check(b.GetStrokes().empty() && !b.IsDrawing() && b.GetVersion() != v2, "U06", "Clear: 선 모두 삭제, 버전 증가");
}

void TestBackground()
{
    Blackboard b;
    Check(b.GetBackground() == BoardBackground::Green, "U07", "기본 배경은 초록");

    unsigned v = b.GetVersion();
    b.SetBackground(BoardBackground::Black);
    Color c = b.GetBackgroundColor();
    Check(b.GetBackground() == BoardBackground::Black && c.r < 0.2f && b.GetVersion() != v, "U08",
          "SetBackground: 배경 변경 + 버전 증가(지우개 선 다시 그리기)");

    Color g = Blackboard::GetBackgroundColor(BoardBackground::Green);
    Color w = Blackboard::GetBackgroundColor(BoardBackground::White);
    Check(g.g > g.r && g.g > g.b && w.r > 0.9f && w.g > 0.9f, "U09", "배경색 값: 초록/흰색");
}

void TestSaveLoad()
{
    Blackboard src = MakeSampleBoard();
    src.SetBackground(BoardBackground::White);
    std::wstring path = TempPath(L"ut_roundtrip.board");
    Check(src.SaveToFile(path), "U10", "SaveToFile 성공");

    Blackboard dst;
    bool loaded = dst.LoadFromFile(path);
    bool same   = loaded && dst.GetBackground() == BoardBackground::White &&
                dst.GetStrokes().size() == src.GetStrokes().size();
    for (size_t i = 0; same && i < src.GetStrokes().size(); ++i)
    {
        const Stroke& a = src.GetStrokes()[i];
        const Stroke& b = dst.GetStrokes()[i];
        same = SameColor(a.color, b.color) && a.width == b.width && a.isEraser == b.isEraser &&
               a.points.size() == b.points.size() &&
               std::memcmp(a.points.data(), b.points.data(), a.points.size() * sizeof(Point2)) == 0;
    }
    Check(same, "U11", "저장 -> 불러오기 왕복: 배경/색/굵기/지우개/점 모두 동일");

    Blackboard emptyBoard;
    std::wstring emptyPath = TempPath(L"ut_empty.board");
    Blackboard loadedEmpty = MakeSampleBoard();
    Check(emptyBoard.SaveToFile(emptyPath) && loadedEmpty.LoadFromFile(emptyPath) && loadedEmpty.GetStrokes().empty(),
          "U12", "빈 칠판 저장/불러오기 -> 선 0개");

    Blackboard drawing;
    drawing.BeginStroke({ 0, 0 }, kRed, 3, false);
    Check(drawing.LoadFromFile(path) && !drawing.IsDrawing(), "U13", "그리는 도중 불러오기 -> 그리기 상태 해제");

    std::wstring koreanPath = TempPath(L"한글 이름 테스트.board");
    Blackboard k;
    Check(src.SaveToFile(koreanPath) && k.LoadFromFile(koreanPath) && k.GetStrokes().size() == 2, "U14",
          "한글/공백이 들어간 경로 저장·불러오기");

    Check(!src.SaveToFile(TempPath(L"no_such_dir\\sub\\x.board")) &&
              !src.SaveToFile(TempPath(L"no_such_dir/sub/x.board")),
          "U15", "존재하지 않는 폴더에 저장 -> 실패 반환");
}

void TestCorruptFiles()
{
    Blackboard board = MakeSampleBoard();
    auto unchanged = [&]() { return board.GetStrokes().size() == 2 && board.GetBackground() == BoardBackground::Green; };

    Check(!board.LoadFromFile(TempPath(L"does_not_exist.board")) && unchanged(), "U16", "없는 파일 -> 실패, 기존 그림 유지");

    std::wstring p = TempPath(L"ut_bad.board");
    WriteRaw(p, { 'X', 'Y', 'Z', 'W', 1, 0, 0, 0 });
    Check(!board.LoadFromFile(p) && unchanged(), "U17", "매직 넘버 불일치 -> 실패, 기존 그림 유지");

    WriteRaw(p, Header(2, 0, 0));
    Check(!board.LoadFromFile(p) && unchanged(), "U18", "지원하지 않는 버전 -> 실패");

    WriteRaw(p, Header(1, 7, 0));
    Check(!board.LoadFromFile(p) && unchanged(), "U19", "잘못된 배경 값 -> 실패");

    WriteRaw(p, {});
    Check(!board.LoadFromFile(p) && unchanged(), "U20", "0바이트 파일 -> 실패");

    // 정상 파일을 중간에서 자른 경우
    std::wstring good = TempPath(L"ut_good.board");
    MakeSampleBoard().SaveToFile(good);
    {
        std::ifstream in(std::filesystem::path(good), std::ios::binary);
        std::vector<unsigned char> all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        all.resize(all.size() - 5);
        WriteRaw(p, all);
    }
    Check(!board.LoadFromFile(p) && unchanged(), "U21", "중간이 잘린 파일 -> 실패, 기존 그림 유지");

    // 선 개수만 크게 적힌 작은 파일: 메모리를 미리 크게 잡지 않고 빨리 실패해야 한다.
    WriteRaw(p, Header(1, 0, 9000000));
    auto t0 = std::chrono::steady_clock::now();
    bool ok = board.LoadFromFile(p);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("         (선 900만 개라고 적힌 16바이트 파일 처리 시간: %.1f ms)\n", ms);
    Check(!ok && unchanged() && ms < 50.0, "U22", "선 개수가 부풀려진 손상 파일 -> 큰 메모리 할당 없이 즉시 실패");

    // 점 개수만 크게 적힌 파일
    std::vector<unsigned char> bytes = Header(1, 0, 1);
    Append(bytes, 1.0f); Append(bytes, 1.0f); Append(bytes, 1.0f); Append(bytes, 1.0f);
    Append(bytes, 8.0f); Append(bytes, (uint8_t)0); Append(bytes, (uint32_t)9000000);
    WriteRaw(p, bytes);
    t0 = std::chrono::steady_clock::now();
    ok = board.LoadFromFile(p);
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("         (점 900만 개라고 적힌 작은 파일 처리 시간: %.1f ms)\n", ms);
    Check(!ok && unchanged() && ms < 50.0, "U23", "점 개수가 부풀려진 손상 파일 -> 큰 메모리 할당 없이 즉시 실패");

    // 굵기가 NaN / 음수 / 비정상적으로 큰 값
    const float badWidths[] = { std::numeric_limits<float>::quiet_NaN(), -5.0f, 1.0e9f };
    bool allRejected = true;
    for (float w : badWidths)
    {
        std::vector<unsigned char> b2 = Header(1, 0, 1);
        Append(b2, 1.0f); Append(b2, 1.0f); Append(b2, 1.0f); Append(b2, 1.0f);
        Append(b2, w); Append(b2, (uint8_t)0); Append(b2, (uint32_t)1);
        Append(b2, 10.0f); Append(b2, 10.0f);
        WriteRaw(p, b2);
        allRejected = allRejected && !board.LoadFromFile(p) && unchanged();
    }
    Check(allRejected, "U24", "굵기가 NaN/음수/비정상 값인 파일 -> 실패");

    // 좌표가 NaN
    std::vector<unsigned char> b3 = Header(1, 0, 1);
    Append(b3, 1.0f); Append(b3, 1.0f); Append(b3, 1.0f); Append(b3, 1.0f);
    Append(b3, 8.0f); Append(b3, (uint8_t)0); Append(b3, (uint32_t)1);
    Append(b3, std::numeric_limits<float>::quiet_NaN()); Append(b3, 10.0f);
    WriteRaw(p, b3);
    Check(!board.LoadFromFile(p) && unchanged(), "U25", "좌표/색에 NaN이 있는 파일 -> 실패");
}

// ---------------------------------------------------------------- StrokeRenderer
void TestGeometry()
{
    StrokeRenderer r;

    Blackboard one;
    one.BeginStroke({ 100, 100 }, kRed, 2, false);   // 반지름 1 -> 최소 8조각
    Check(StrokeRendererTestAccess::Build(r, one) == 8 * 3, "R01", "점 1개(굵기 2): 원 8조각 = 정점 24개");

    Blackboard big;
    big.BeginStroke({ 100, 100 }, kRed, 50, false);  // 반지름 25 -> 최대 32조각
    Check(StrokeRendererTestAccess::Build(r, big) == 32 * 3, "R02", "점 1개(굵기 50): 원 32조각 = 정점 96개");

    Blackboard two;
    two.BeginStroke({ 0, 0 }, kRed, 10, false);       // 반지름 5 -> 8조각
    two.ContinueStroke({ 100, 0 });
    Check(StrokeRendererTestAccess::Build(r, two) == 8 * 3 * 2 + 6, "R03", "점 2개: 원 2개 + 사각형 1개(정점 6개)");

    // 사각형의 정점들이 선분에서 반지름만큼 떨어져 있는지 (수평선 -> y = ±5)
    bool widthOk = true;
    size_t base  = 8 * 3 * 2;
    for (size_t i = base; i < base + 6; ++i)
        widthOk = widthOk && std::fabs(std::fabs(StrokeRendererTestAccess::VertexPos(r, i).y) - 5.0f) < 1e-4f;
    Check(widthOk, "R04", "선분 사각형 폭 = 굵기 (법선 방향으로 ±반지름)");

    // 원의 정점이 모두 중심에서 반지름 이내인지
    bool circleOk = true;
    for (size_t i = 0; i < 8 * 3; ++i)
    {
        Point2 p = StrokeRendererTestAccess::VertexPos(r, i);
        circleOk = circleOk && std::sqrt(p.x * p.x + p.y * p.y) <= 5.0f + 1e-4f;
    }
    Check(circleOk, "R05", "원 정점이 모두 반지름 이내");

    // 지우개 선은 현재 배경색으로 그려지고, 배경을 바꾸면 따라간다
    Blackboard eraser;
    eraser.BeginStroke({ 10, 10 }, kRed, 6, true);
    StrokeRendererTestAccess::Build(r, eraser);
    bool greenOk = SameColor(StrokeRendererTestAccess::VertexColor(r, 0), eraser.GetBackgroundColor());
    eraser.SetBackground(BoardBackground::White);
    StrokeRendererTestAccess::Build(r, eraser);
    bool whiteOk = SameColor(StrokeRendererTestAccess::VertexColor(r, 0),
                             Blackboard::GetBackgroundColor(BoardBackground::White));
    Check(greenOk && whiteOk, "R06", "지우개 선 색 = 현재 배경색 (배경 변경 시 함께 변경)");

    Blackboard empty;
    Check(StrokeRendererTestAccess::Build(r, empty) == 0, "R07", "빈 칠판 -> 정점 0개");

    // 성능: 선 500개 x 점 200개 (점 10만 개)
    Blackboard heavy;
    for (int s = 0; s < 500; ++s)
    {
        heavy.BeginStroke({ 0, (float)s }, kRed, 8, false);
        for (int i = 1; i < 200; ++i)
            heavy.ContinueStroke({ i * 2.0f, (float)s + (i % 2) });
        heavy.EndStroke();
    }
    auto t0 = std::chrono::steady_clock::now();
    size_t count = StrokeRendererTestAccess::Build(r, heavy);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("         (점 100,000개 -> 정점 %zu개, %.1f MB, 생성 %.1f ms)\n", count, count * 24 / 1048576.0, ms);
    Check(ms < 200.0, "R08", "점 10만 개 정점 생성 시간 < 200ms");

    // 그리는 중(마지막 선이 길어짐)에는 마지막 선만 다시 만든다
    heavy.BeginStroke({ 5, 900 }, kWhite, 8, false);
    StrokeRendererTestAccess::Update(r, heavy);
    t0 = std::chrono::steady_clock::now();
    size_t firstChanged = 0;
    for (int i = 1; i <= 100; ++i)
    {
        heavy.ContinueStroke({ 5.0f + i * 3.0f, 900 });
        firstChanged = StrokeRendererTestAccess::Update(r, heavy);
    }
    double perPoint = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 100.0;
    StrokeRenderer fresh;
    StrokeRendererTestAccess::Build(fresh, heavy);
    std::printf("         (점 10만 개가 있는 칠판에서 그리는 중 점 1개 추가당 갱신: %.3f ms)\n", perPoint);
    Check(perPoint < 5.0 && firstChanged > count / 2 && StrokeRendererTestAccess::SameVertices(r, fresh), "R09",
          "그리는 중에는 마지막 선만 갱신 (결과는 전체 재생성과 동일)");

    // 전체 지우기 후 같은 개수의 선을 다시 그려도 정확히 다시 만든다
    Blackboard b2 = MakeSampleBoard();
    StrokeRenderer r2;
    StrokeRendererTestAccess::Update(r2, b2);
    b2.Clear();
    b2.BeginStroke({ 300, 300 }, kRed, 4, false); b2.ContinueStroke({ 320, 330 }); b2.EndStroke();
    b2.BeginStroke({ 100, 100 }, kWhite, 6, false); b2.EndStroke();
    StrokeRendererTestAccess::Update(r2, b2);
    StrokeRenderer fresh2;
    StrokeRendererTestAccess::Build(fresh2, b2);
    bool okClear = StrokeRendererTestAccess::SameVertices(r2, fresh2);

    // 불러오기 / 배경 변경 후에도 정확히 다시 만든다
    std::wstring path = TempPath(L"ut_render.board");
    MakeSampleBoard().SaveToFile(path);
    b2.LoadFromFile(path);
    StrokeRendererTestAccess::Update(r2, b2);
    StrokeRenderer fresh3;
    StrokeRendererTestAccess::Build(fresh3, b2);
    bool okLoad = StrokeRendererTestAccess::SameVertices(r2, fresh3);
    b2.SetBackground(BoardBackground::Black);
    StrokeRendererTestAccess::Update(r2, b2);
    StrokeRenderer fresh4;
    StrokeRendererTestAccess::Build(fresh4, b2);
    bool okBg = StrokeRendererTestAccess::SameVertices(r2, fresh4);
    Check(okClear && okLoad && okBg, "R10", "전체 지우기/불러오기/배경 변경 후 정점이 전체 재생성과 동일");
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);   // 한글 출력
    std::printf("=== Blackboard ===\n");
    TestStrokeEditing();
    TestBackground();
    TestSaveLoad();
    TestCorruptFiles();
    std::printf("=== StrokeRenderer (정점 생성) ===\n");
    TestGeometry();

    std::printf("\n결과: %d 통과, %d 실패\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
