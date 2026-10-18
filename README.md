# 칠판 (DirectX 12 + ImGui)

마우스로 그림을 그리는 2D 칠판 프로그램입니다. 학생 과제용 단일 프로젝트입니다.

## 빌드 / 실행

1. Visual Studio 2022에 **"C++를 사용한 데스크톱 개발"** 워크로드(Windows 10/11 SDK 포함)를 설치합니다.
2. `BlackboardProj.sln`을 엽니다.
3. 구성을 `Debug | x64` 또는 `Release | x64`로 고르고 F5를 누릅니다.

실행 파일은 `Bin\<구성>\BlackboardProj.exe`에 만들어집니다. 외부 라이브러리는 Dear ImGui(v1.91.9b)뿐이고, `BlackboardProj/External/imgui`에 소스가 들어 있어 따로 설치할 것이 없습니다.

## 테스트

- **단위 테스트**: 솔루션의 `UnitTests` 프로젝트를 시작 프로젝트로 설정하고 실행합니다(칠판 데이터·저장/불러오기·선 정점 생성 35개).
- **E2E 테스트**: Linux + Wine 환경에서 `python3 Tests/E2E/e2e_wine.py <exe> <결과폴더>` (화면 조작 24개).
- 결과와 디버깅 지점은 [`Docs/QA_Report.md`](Docs/QA_Report.md)에 정리되어 있습니다.

## 사용법

| 조작 | 동작 |
|---|---|
| 왼쪽 버튼 드래그 (칠판 위) | 선 그리기 (지우개 도구면 배경색으로 지움) |
| 마우스 휠 (칠판 위) | 붓 굵기 ±1 (1~50px) |
| `<` / `>` 버튼 | 도구 패널 접기 / 펼치기 (슬라이드) |
| 기본 색상 8종 | 검정·흰색·빨강·주황·노랑·파랑·남색·보라 |
| 사용자 색상 | 색 박스 안의 포인트와 색상 막대를 드래그해 RGB 선택 |
| 스포이드 | 칠판을 클릭하면 그 픽셀의 RGB를 사용자 색상으로 가져옴 |
| 저장 / 불러오기 | `.board` 파일로 선 데이터 저장·로드 |
| 칠판 배경색 | 초록 / 흰색 / 검정 |

## 폴더 구조

```
BlackboardProj.sln
BlackboardProj/
  Source/
    Main.cpp, App.h/.cpp        프로그램 진입점, 창, 메시지 루프, 모듈 연결
    Core/Dx12Core.*             DirectX 12 Core
    Board/Blackboard.*          칠판 데이터(선 목록, 배경), 저장/로드
    Board/StrokeRenderer.*      선을 삼각형으로 만들어 그리기
    UI/UIManager.*              ImGui UI (도구 패널, 상태 표시줄)
  External/imgui/               Dear ImGui 소스
Tests/                          단위 테스트(UnitTests), E2E 테스트 스크립트
Docs/Report.md                  구현 보고서 (클래스/시퀀스 다이어그램 포함)
Docs/QA_Report.md               QA 보고서 (테스트 케이스, 결함, 디버깅 지점)
```
