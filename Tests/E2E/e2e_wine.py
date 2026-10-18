#!/usr/bin/env python3
"""칠판 프로그램 E2E(화면 조작) 테스트.

Linux에서 Wine(+vkd3d, 소프트웨어 Vulkan)으로 BlackboardProj.exe를 실행하고
xdotool로 마우스를 조작한 뒤, 화면 캡처의 픽셀 색을 검사한다.

필요: Xvfb(DISPLAY), wine, xdotool, ImageMagick(import), Python Pillow
사용: python3 e2e_wine.py <BlackboardProj.exe 경로> <결과 폴더>
"""
import os
import subprocess
import sys
import time

from PIL import Image

EXE = os.path.abspath(sys.argv[1])
OUT = os.path.abspath(sys.argv[2])
os.makedirs(OUT, exist_ok=True)

ENV = dict(os.environ, WINEDEBUG="-all", LANG="C.UTF-8", LC_ALL="C.UTF-8")

# ---- UI 좌표 (클라이언트 좌표, 1280x800 기준) ----
UI = {
    "collapse": (236, 19),
    "save": (67, 71), "load": (193, 71), "clear": (130, 98),
    "pen": (46, 150), "eraser": (129, 150), "dropper": (214, 150),
    "palette_y": 202,
    "palette_x": {"black": 19, "white": 51, "red": 83, "orange": 114, "yellow": 146,
                  "blue": 177, "navy": 208, "purple": 240},
    "sv_box": (8, 243, 224, 459),       # x0, y0, x1, y1
    "bg": {"green": (20, 606), "white": (83, 606), "black": (146, 606)},
    "expand": (14, 26),
    "idle": (130, 700),                  # 패널 빈 곳 (커서 미리보기가 그려지지 않는 위치)
}


def rgb(r, g, b):
    return (round(r * 255), round(g * 255), round(b * 255))


BG = {"green": rgb(0.18, 0.36, 0.23), "white": rgb(0.96, 0.96, 0.94), "black": rgb(0.10, 0.10, 0.10)}
COL = {"white": (255, 255, 255), "black": (0, 0, 0), "red": rgb(0.90, 0.22, 0.21),
       "blue": rgb(0.12, 0.53, 0.90), "navy": rgb(0.12, 0.23, 0.54)}

results = []
origin = (0, 0)


def sh(*args, **kw):
    return subprocess.run(list(args), env=ENV, capture_output=True, text=True, **kw)


def window_id():
    out = sh("xdotool", "search", "--name", "DirectX").stdout.split()
    return out[0] if out else None


def find_origin():
    """클라이언트 영역(자식 창)의 화면 좌표."""
    wid = window_id()
    tree = sh("xwininfo", "-tree", "-id", wid).stdout
    for line in tree.splitlines():
        line = line.strip()
        if line.startswith("0x") and "+" in line:
            parts = line.split()[-1].split("+")    # '+4+30'
            return int(parts[1]), int(parts[2])
    info = sh("xwininfo", "-id", wid).stdout
    x = int([l for l in info.splitlines() if "Absolute upper-left X" in l][0].split()[-1])
    y = int([l for l in info.splitlines() if "Absolute upper-left Y" in l][0].split()[-1])
    return x, y


def move(x, y):
    sh("xdotool", "mousemove", str(origin[0] + x), str(origin[1] + y))
    time.sleep(0.05)


def click(p, wait=0.4):
    move(*p)
    time.sleep(0.15)
    sh("xdotool", "mousedown", "1")
    time.sleep(0.12)
    sh("xdotool", "mouseup", "1")
    time.sleep(wait)


def drag(points, step=6):
    """points 를 잇는 경로로 드래그 (중간 점을 step px 간격으로 보간)."""
    move(*points[0])
    time.sleep(0.15)
    sh("xdotool", "mousedown", "1")
    time.sleep(0.1)
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        n = max(1, int(max(abs(x1 - x0), abs(y1 - y0)) / step))
        for i in range(1, n + 1):
            move(x0 + (x1 - x0) * i // n, y0 + (y1 - y0) * i // n)
    time.sleep(0.1)
    sh("xdotool", "mouseup", "1")
    time.sleep(0.3)


def wheel(n, at=None, fast=False):
    if at:
        move(*at)
        time.sleep(0.2)
    button = "4" if n > 0 else "5"
    if fast:   # 한 프레임 안에 여러 칸이 들어오도록 빠르게
        sh("xdotool", "click", "--repeat", str(abs(n)), "--delay", "1", button)
    else:
        for _ in range(abs(n)):
            sh("xdotool", "click", button)
            time.sleep(0.12)
    time.sleep(0.3)


def shot(name):
    move(*UI["idle"])
    time.sleep(0.5)
    path = os.path.join(OUT, name + ".png")
    sh("import", "-window", "root", path)
    img = Image.open(path).convert("RGB")
    w, h = client_size()
    return img.crop((origin[0], origin[1], origin[0] + w, origin[1] + h))


def client_size():
    wid = window_id()
    tree = sh("xwininfo", "-tree", "-id", wid).stdout
    for line in tree.splitlines():
        line = line.strip()
        if line.startswith("0x") and "x" in line.split()[-2]:
            w, h = line.split()[-2].split("+")[0].split("x")
            return int(w), int(h)
    return 1280, 800


def near(c, expected, tol=3):
    return all(abs(a - b) <= tol for a, b in zip(c, expected))


def thickness(img, x, y_from, y_to, color, tol=10):
    """x 열에서 color 에 가까운 픽셀 수 (세로 선 굵기 측정)."""
    return sum(1 for y in range(y_from, y_to) if near(img.getpixel((x, y)), color, tol))


def record(tc, name, ok, detail=""):
    results.append((tc, name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {tc:6s} {name}  {detail}", flush=True)


def select_color(name):
    click((UI["palette_x"][name], UI["palette_y"]))


def file_dialog(path):
    """Wine 파일 대화상자에 경로를 입력하고 확인."""
    time.sleep(2.0)
    wid = None
    for _ in range(20):
        ids = sh("xdotool", "search", "--onlyvisible", "--class", "").stdout.split()
        main = window_id()
        cand = [i for i in ids if i != main and sh("xdotool", "getwindowname", i).stdout.strip()]
        if cand:
            wid = cand[-1]
            break
        time.sleep(0.5)
    if wid:
        sh("xdotool", "windowfocus", "--sync", wid)
    time.sleep(0.5)
    sh("xdotool", "key", "ctrl+a")
    sh("xdotool", "type", "--delay", "20", path)
    time.sleep(0.3)
    sh("xdotool", "key", "Return")
    time.sleep(2.0)


def board_region_is(img, color, box, tol=3):
    x0, y0, x1, y1 = box
    for y in range(y0, y1, 7):
        for x in range(x0, x1, 7):
            if not near(img.getpixel((x, y)), color, tol):
                return False, (x, y, img.getpixel((x, y)))
    return True, None


# ======================================================================
def main():
    global origin
    proc = subprocess.Popen(["wine", EXE], env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            cwd=os.path.dirname(EXE))
    for _ in range(60):
        if window_id():
            break
        time.sleep(0.5)
    time.sleep(6)
    origin = find_origin()
    print("client origin:", origin, "size:", client_size())

    # TC01 시작 화면
    img = shot("tc01_start")
    ok = near(img.getpixel((700, 400)), BG["green"]) and img.getpixel((130, 700))[1] < 40
    record("TC01", "시작: 초록 배경 + 좌측 패널 표시", ok, f"bg={img.getpixel((700, 400))}")

    # TC02 기본 펜(흰색, 8px)으로 가로선
    drag([(400, 200), (700, 200)])
    img = shot("tc02_line")
    t = thickness(img, 550, 180, 220, COL["white"])
    record("TC02", "드래그로 선 그리기 (흰색, 8px)", near(img.getpixel((550, 200)), COL["white"]) and 7 <= t <= 9,
           f"굵기={t}px")

    # TC03 클릭 한 번 -> 점
    click((400, 300))
    img = shot("tc03_dot")
    record("TC03", "클릭만 해도 점이 찍힘", near(img.getpixel((400, 300)), COL["white"]),
           f"pixel={img.getpixel((400, 300))}")

    # TC04 팔레트 빨강
    select_color("red")
    drag([(400, 250), (700, 250)])
    img = shot("tc04_red")
    record("TC04", "팔레트(빨강) 선택 후 그리기", near(img.getpixel((550, 250)), COL["red"]),
           f"pixel={img.getpixel((550, 250))} 기대={COL['red']}")

    # TC05 휠 위로 5칸 -> 13px
    wheel(5, at=(800, 400))
    drag([(400, 340), (700, 340)])
    img = shot("tc05_wheel_up")
    t = thickness(img, 550, 310, 370, COL["red"])
    record("TC05", "휠 위로 5칸 -> 굵기 13px", 12 <= t <= 14, f"굵기={t}px")

    # TC06 휠 아래로 많이 -> 최소 1px
    wheel(-30, at=(800, 400))
    drag([(400, 400), (700, 400)])
    img = shot("tc06_wheel_min")
    t = thickness(img, 550, 390, 410, COL["red"], tol=60)
    record("TC06", "휠로 최소값 고정 (1px)", 1 <= t <= 2, f"굵기={t}px")

    # TC07 휠 위로 많이 -> 최대 50px
    wheel(60, at=(800, 400))
    drag([(400, 480), (700, 480)])
    img = shot("tc07_wheel_max")
    t = thickness(img, 550, 430, 540, COL["red"])
    record("TC07", "휠로 최대값 고정 (50px)", 49 <= t <= 51, f"굵기={t}px")

    # TC08 패널 위에서 휠 -> 굵기 변하지 않음 (현재 50)
    wheel(-10, at=UI["idle"])
    drag([(400, 600), (700, 600)])
    img = shot("tc08_wheel_on_panel")
    t = thickness(img, 550, 560, 650, COL["red"])
    record("TC08", "패널 위 휠은 굵기에 영향 없음", 49 <= t <= 51, f"굵기={t}px")

    # TC09 빠른 휠: 한 프레임에 여러 칸 (10px로 맞춘 뒤 빠르게 5칸 -> 15px 기대)
    wheel(-60, at=(800, 400))   # 1px
    wheel(9, at=(800, 400))     # 10px
    wheel(5, at=(800, 400), fast=True)
    drag([(800, 120), (1100, 120)])
    img = shot("tc09_fast_wheel")
    t = thickness(img, 950, 90, 150, COL["red"])
    record("TC09", "휠을 빠르게 5칸 굴려도 5만큼 증가 (10->15px)", 14 <= t <= 16, f"굵기={t}px")

    # TC10 지우개
    click(UI["eraser"])
    drag([(550, 180), (550, 260)])   # 흰 선(200)과 빨간 선(250)을 세로로 지움
    img = shot("tc10_eraser")
    ok = near(img.getpixel((550, 200)), BG["green"]) and near(img.getpixel((550, 250)), BG["green"]) \
        and near(img.getpixel((450, 200)), COL["white"])
    record("TC10", "지우개: 지운 곳만 배경색", ok, f"지운곳={img.getpixel((550, 200))}")

    # TC11 배경 흰색 -> 배경과 지운 자국 모두 흰색
    click(UI["bg"]["white"])
    img = shot("tc11_bg_white")
    ok = near(img.getpixel((1000, 700)), BG["white"]) and near(img.getpixel((550, 200)), BG["white"]) \
        and near(img.getpixel((450, 250)), COL["red"])
    record("TC11", "배경 흰색: 지우개 자국도 새 배경색을 따라감", ok,
           f"bg={img.getpixel((1000, 700))} 지운곳={img.getpixel((550, 200))}")

    # TC12 배경 검정
    click(UI["bg"]["black"])
    img = shot("tc12_bg_black")
    record("TC12", "배경 검정", near(img.getpixel((1000, 700)), BG["black"]), f"bg={img.getpixel((1000, 700))}")
    click(UI["bg"]["green"])

    # TC13 스포이드: 빨간 선에서 색 추출 -> 그 색으로 그려짐
    click(UI["dropper"])
    click((450, 250))
    wheel(-60, at=(800, 400)); wheel(9, at=(800, 400))   # 10px
    drag([(800, 700), (1100, 700)])
    img = shot("tc13_eyedropper")
    record("TC13", "스포이드: 빨간 선 색 추출 후 펜으로 전환", near(img.getpixel((950, 700)), COL["red"]),
           f"pixel={img.getpixel((950, 700))}")

    # TC14 스포이드: 배경 클릭 -> 배경색으로 그림 (보이지 않아야 함)
    click(UI["dropper"])
    click((1000, 300))
    drag([(900, 250), (1200, 250)])
    img = shot("tc14_eyedropper_bg")
    record("TC14", "스포이드: 배경색 추출", near(img.getpixel((1050, 250)), BG["green"]),
           f"pixel={img.getpixel((1050, 250))}")

    # TC15 사용자 색상: 박스 왼쪽 아래(검정 쪽)로 포인트 이동
    x0, y0, x1, y1 = UI["sv_box"]
    drag([(x0 + 100, y0 + 100), (x0 + 2, y1 - 2)])
    drag([(800, 550), (1100, 550)])
    img = shot("tc15_picker")
    c = img.getpixel((950, 550))
    record("TC15", "사용자 색상 박스 포인트 드래그 -> 그 색으로 그림", max(c) < 15, f"pixel={c}")

    # TC16 패널에서 시작한 드래그는 칠판에 그리지 않음
    select_color("white")
    before = shot("tc16_before").getpixel((600, 650))
    drag([(130, 650), (700, 650)])
    img = shot("tc16_panel_drag")
    record("TC16", "패널에서 시작한 드래그는 그리지 않음", near(img.getpixel((600, 650)), before),
           f"pixel={img.getpixel((600, 650))}")

    # TC17 칠판에서 시작해 패널 위로 지나가도 선이 이어짐 + 패널 접기
    drag([(600, 740), (60, 740)])
    click(UI["collapse"], wait=1.0)
    img = shot("tc17_collapsed")
    ok_hidden = near(img.getpixel((130, 400)), BG["green"])
    ok_line = near(img.getpixel((100, 740)), COL["white"])
    record("TC17", "< 버튼: 패널 접힘, 패널 밑으로 이어진 선이 보임", ok_hidden and ok_line,
           f"패널자리={img.getpixel((130, 400))} 선={img.getpixel((100, 740))}")

    # TC18 접힌 상태에서 왼쪽 영역에 그리기, > 로 다시 펼치기
    drag([(60, 300), (200, 300)])
    img = shot("tc18_draw_left")
    ok_draw = near(img.getpixel((130, 300)), COL["white"])
    click(UI["expand"], wait=1.0)
    img = shot("tc18_expanded")
    ok_open = img.getpixel((130, 700))[1] < 40
    record("TC18", "접힌 상태에서 왼쪽 그리기 + > 로 펼치기", ok_draw and ok_open)

    # TC19 창 밖에서 버튼을 떼면 선이 끝남 (다시 들어올 때 그려지지 않음)
    move(900, 380)
    sh("xdotool", "mousedown", "1")
    time.sleep(0.1)
    for x in range(900, 1290, 30):
        move(x, 380)
    sh("xdotool", "mousemove", "1500", str(origin[1] + 380))
    time.sleep(0.2)
    sh("xdotool", "mouseup", "1")
    time.sleep(0.3)
    for x in range(1270, 1000, -20):
        move(x, 430)
    img = shot("tc19_release_outside")
    record("TC19", "창 밖에서 버튼을 떼면 선이 끝남", near(img.getpixel((1100, 430)), BG["green"]),
           f"pixel={img.getpixel((1100, 430))}")

    # TC20 저장 -> 전체 지우기 -> 불러오기
    save_path = "Z:" + os.path.join(OUT, "qa_save.board").replace("/", "\\")
    before = shot("tc20_before_save")
    click(UI["save"], wait=0.2)
    file_dialog(save_path)
    saved = False
    for _ in range(30):   # Wine 파일 대화상자가 느리므로 파일이 생길 때까지 기다린다
        saved = os.path.exists(os.path.join(OUT, "qa_save.board"))
        if saved:
            break
        time.sleep(0.5)
    click(UI["clear"])
    cleared = shot("tc20_cleared")
    ok_clear, _ = board_region_is(cleared, BG["green"], (300, 40, 1270, 760))
    click(UI["load"], wait=0.2)
    file_dialog(save_path)
    after = shot("tc20_after_load")
    diff = sum(1 for y in range(0, 760, 3) for x in range(270, 1280, 3)
               if not near(before.getpixel((x, y)), after.getpixel((x, y)), 2))
    record("TC20", "저장 -> 전체 지우기 -> 불러오기 시 동일 화면", saved and ok_clear and diff == 0,
           f"파일={saved} 지우기={ok_clear} 다른픽셀={diff}")

    # TC21 잘못된 파일 불러오기 -> 기존 그림 유지
    bad = os.path.join(OUT, "bad.board")
    with open(bad, "wb") as f:
        f.write(b"NOTABOARDFILE")
    click(UI["load"], wait=0.2)
    file_dialog("Z:" + bad.replace("/", "\\"))
    img = shot("tc21_bad_load")
    diff = sum(1 for y in range(0, 760, 3) for x in range(270, 1280, 3)
               if not near(before.getpixel((x, y)), img.getpixel((x, y)), 2))
    record("TC21", "잘못된 파일 불러오기 -> 기존 그림 유지", diff == 0, f"다른픽셀={diff}")

    # TC22 창 크기 변경 (작게 / 크게)
    wid = window_id()
    sh("xdotool", "windowsize", wid, "1000", "650")
    time.sleep(2.0)
    origin = find_origin()
    w, h = client_size()
    drag([(700, 500), (950, 500)])
    img = shot("tc22_resize_small")
    ok_small = near(img.getpixel((900, 500)), COL["white"]) and near(img.getpixel((w - 5, 300)), BG["green"], 3)
    sh("xdotool", "windowsize", wid, "1500", "950")
    time.sleep(2.0)
    origin = find_origin()
    w, h = client_size()
    drag([(1300, 850), (1450, 850)])
    img = shot("tc22_resize_big")
    ok_big = near(img.getpixel((1400, 850)), COL["white"]) and near(img.getpixel((w - 5, 300)), BG["green"], 3)
    record("TC22", "창 크기 변경 후 렌더링/좌표 정상", ok_small and ok_big, f"client={w}x{h}")

    # TC23 많은 선 (부하): 200개 선 그린 뒤에도 응답
    t0 = time.time()
    for i in range(200):
        drag([(300 + (i % 20) * 50, 60 + (i // 20) * 70), (330 + (i % 20) * 50, 100 + (i // 20) * 70)], step=10)
    elapsed = time.time() - t0
    img = shot("tc23_stress")
    alive = proc.poll() is None
    record("TC23", "선 200개 연속 그리기 후 정상 동작", alive, f"{elapsed:.1f}s")

    # TC25 확인용 그림(Samples/test_drawing.board) 불러오기 -> 기대 화면과 픽셀 단위로 동일
    samples = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Samples"))
    click(UI["load"], wait=0.2)
    file_dialog("Z:" + os.path.join(samples, "test_drawing.board").replace("/", "\\"))
    img = shot("tc25_sample_load")
    expected = Image.open(os.path.join(samples, "test_drawing_expected.png")).convert("RGB")
    x0, y0 = 420, 18   # 그림이 그려진 위치 (클라이언트 좌표)
    diff = sum(1 for y in range(expected.height) for x in range(expected.width)
               if not near(img.getpixel((x0 + x, y0 + y)), expected.getpixel((x, y)), 1))
    record("TC25", "확인용 그림 불러오기 -> 기대 화면과 동일", diff == 0 and near(img.getpixel((1100, 400)), BG["white"]),
           f"다른픽셀={diff}/{expected.width * expected.height}")

    alive = proc.poll() is None
    record("TC24", "전체 테스트 동안 프로그램 종료/크래시 없음", alive)

    sh("xdotool", "key", "--window", window_id() or "0", "alt+F4")
    time.sleep(2)
    if proc.poll() is None:
        proc.kill()

    passed = sum(1 for r in results if r[2])
    print(f"\n결과: {passed} 통과, {len(results) - passed} 실패")
    with open(os.path.join(OUT, "e2e_results.txt"), "w") as f:
        for tc, name, ok, detail in results:
            f.write(f"{tc}\t{'PASS' if ok else 'FAIL'}\t{name}\t{detail}\n")


if __name__ == "__main__":
    main()
