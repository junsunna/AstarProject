#include "MemoryPool.h"
#include <windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <list>
#include <ctime>
#include <functional>
#include "AStar.h"

// --------------------------------------------------------
// 전역 변수 및 설정
// --------------------------------------------------------
AStar* g_pAStar = nullptr;
int MAP_WIDTH = 20;  // 맵 가로 격자 수
int MAP_HEIGHT = 20; // 맵 세로 격자 수

bool g_autoPlay = false; // 자동 재생 여부

// 뷰포트 설정
float g_scale = 30.0f;     // 초기 줌 레벨 (셀 크기)
int g_offsetX = 50;        // 맵 시작 위치 X
int g_offsetY = 50;        // 맵 시작 위치 Y

Point g_startPos = { 0, 0 };
Point g_endPos = { 19, 19 };

// 마우스 드래그 상태
bool g_isLeftMouseDown = false;
bool g_isRightMouseDown = false;

POINT g_lastMousePos = { 0, 0 }; // 마우스의 이전 프레임 위치 저장용
// --------------------------------------------------------
// 좌표 변환 헬퍼 함수
// --------------------------------------------------------
// 화면 좌표(마우스) -> 그리드 좌표
Point ScreenToGrid(int sx, int sy)
{
    int gx = (int)((sx - g_offsetX) / g_scale);
    int gy = (int)((sy - g_offsetY) / g_scale);
    return { gx, gy };
}

// 그리드 좌표 -> 화면 좌표(좌상단)
POINT GridToScreen(int gx, int gy)
{
    POINT p;
    p.x = (LONG)(gx * g_scale + g_offsetX);
    p.y = (LONG)(gy * g_scale + g_offsetY);
    return p;
}

// --------------------------------------------------------
// 그리기 함수
// --------------------------------------------------------
void Render(HDC hdc, HWND hWnd)
{
    // 1. 더블 버퍼링 설정
    RECT rect;
    GetClientRect(hWnd, &rect);
    int scrW = rect.right - rect.left;
    int scrH = rect.bottom - rect.top;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, scrW, scrH);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

    // 배경 지우기 (흰색)
    HBRUSH hBgBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(memDC, &rect, hBgBrush);
    DeleteObject(hBgBrush);

    // 폰트 설정 (줌 레벨에 맞춰 크기 조절 시도, 최소 크기 제한)
    int fontSize = (int)(g_scale * 0.3f);
    if (fontSize < 10) fontSize = 10;
    HFONT hFont = CreateFont(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);
    SetBkMode(memDC, TRANSPARENT);

    // ---------------------------------------------------
    // 2. 그리드 및 노드 그리기
    // ---------------------------------------------------

    // AStar 내부의 생성된 노드 정보를 가져옴 (Closed/Open 등 상태 확인용)
    const auto& allNodes = g_pAStar->GetAllNodes();

    // 빠른 조회를 위해 임시 맵 생성 (렌더링용)
    // index -> Node*
    std::vector<Node*> renderMap(MAP_WIDTH * MAP_HEIGHT, nullptr);
    for (Node* n : allNodes)
    {
        if (n->x >= 0 && n->x < MAP_WIDTH && n->y >= 0 && n->y < MAP_HEIGHT)
            renderMap[n->y * MAP_WIDTH + n->x] = n;
    }

    for (int y = 0; y < MAP_HEIGHT; ++y)
    {
        for (int x = 0; x < MAP_WIDTH; ++x)
        {
            POINT topLeft = GridToScreen(x, y);
            POINT bottomRight = GridToScreen(x + 1, y + 1);
            RECT cellRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };

            // 화면 밖이면 그리지 않음 (Culling)
            if (bottomRight.x < 0 || topLeft.x > scrW || bottomRight.y < 0 || topLeft.y > scrH)
                continue;

            // 색상 결정
            HBRUSH hBrush = nullptr;
            Node* node = renderMap[y * MAP_WIDTH + x];

            if (x == g_startPos.x && y == g_startPos.y) hBrush = CreateSolidBrush(RGB(0, 255, 0)); // 시작: 초록
            else if (x == g_endPos.x && y == g_endPos.y) hBrush = CreateSolidBrush(RGB(255, 0, 0)); // 끝: 빨강
            else if (!g_pAStar->IsWalkable(x, y)) hBrush = CreateSolidBrush(RGB(50, 50, 50)); // 벽: 짙은 회색
            else if (node)
            {
                if (node->isClosed) hBrush = CreateSolidBrush(RGB(200, 200, 255)); // Closed: 연한 파랑
                else hBrush = CreateSolidBrush(RGB(200, 255, 200)); // Open: 연한 초록
            }
            else hBrush = CreateSolidBrush(RGB(240, 240, 240)); // 기본: 아주 연한 회색

            // 경로 표시 (파란색 테두리 대신 진한 파랑 채우기로 덮어씌움)
            // 실제 경로는 FindPath 결과로 받아온 vector를 순회하며 그려도 되지만, 
            // 여기선 node 상태를 보고 판단하기 어려우므로 별도 path vector가 있으면 좋음.
            // 일단 Closed 노드 중 경로에 포함된 것만 따로 그리는 로직은 생략하고
            // FindPath 호출 후 반환된 경로 리스트를 이용해 덧칠합니다.

            FillRect(memDC, &cellRect, hBrush);
            FrameRect(memDC, &cellRect, (HBRUSH)GetStockObject(BLACK_BRUSH)); // 격자 라인
            DeleteObject(hBrush);

            // ---------------------------------------------------
            // [LOD] 상세 정보 그리기 (줌 레벨이 충분히 클 때)
            // ---------------------------------------------------
            if (node && g_scale > 40.0f) // 줌이 가까울 때만 텍스트 출력
            {
                std::wstringstream ss;
                ss << L"F:" << (int)node->f << L"\n";
                ss << L"G:" << (int)node->g << L"\n";
                ss << L"H:" << (int)node->h;

                RECT textRect = cellRect;
                textRect.left += 2; textRect.top += 2;
                DrawText(memDC, ss.str().c_str(), -1, &textRect, DT_LEFT);
            }

            // ---------------------------------------------------
            // [LOD] 부모 방향 화살표 (줌이 멀 때 혹은 가까울 때 선택)
            // 여기서는 축소했을 때 방향을 보기 위함이므로 항상 그리되,
            // 텍스트와 겹치지 않게 처리
            // ---------------------------------------------------
            if (node && node->parent && g_scale <= 40.0f) // 줌이 멀 때 화살표 위주
            {
                POINT center = { (topLeft.x + bottomRight.x) / 2, (topLeft.y + bottomRight.y) / 2 };
                POINT parentCenter = GridToScreen(node->parent->x, node->parent->y);
                parentCenter.x = (parentCenter.x + (long)(g_scale / 2));
                parentCenter.y = (parentCenter.y + (long)(g_scale / 2));

                HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 255));
                HPEN hOldPen = (HPEN)SelectObject(memDC, hPen);

                MoveToEx(memDC, center.x, center.y, nullptr);
                LineTo(memDC, parentCenter.x, parentCenter.y);

                SelectObject(memDC, hOldPen);
                DeleteObject(hPen);
            }
        }
    }

    // [추가] AStar가 현재 가지고 있는 경로 정보를 가져옴
    const std::vector<Point>& path = g_pAStar->GetPath();
    if (!path.empty())
    {
        HPEN hPathPen = CreatePen(PS_SOLID, 3, RGB(255, 215, 0)); // 골드 색상
        HPEN hOldPen = (HPEN)SelectObject(memDC, hPathPen);

        for (size_t i = 0; i < path.size() - 1; ++i)
        {
            POINT p1 = GridToScreen(path[i].x, path[i].y);
            POINT p2 = GridToScreen(path[i + 1].x, path[i + 1].y);

            // 중심점 연결
            int offset = (int)(g_scale / 2);
            MoveToEx(memDC, p1.x + offset, p1.y + offset, nullptr);
            LineTo(memDC, p2.x + offset, p2.y + offset);
        }
        SelectObject(memDC, hOldPen);
        DeleteObject(hPathPen);
    }

    // ---------------------------------------------------
    // UI 정보 출력 (좌측 상단)
    // ---------------------------------------------------
    std::wstringstream info;
    info << L"[Controls]\n";
    info << L"Wheel: Zoom\n";
    info << L"Ctrl + Click: Start Pos\n";
    info << L"Shift + Click: End Pos\n";
    info << L"'A' + Drag: Obstacle Toggle\n";
    info << L"'H': Toggle Heuristic (Curr: " << (g_pAStar->GetHeuristicType() == AStar::HeuristicType::MANHATTAN ? L"Manhattan" : L"Euclidean") << L")\n";
    info << L"'G': Toggle Diagonal (Curr: " << (g_pAStar->GetAllowDiagonal() ? L"Allowed(Euclidean)" : L"None(Manhattan)") << L")\n";

    RECT infoRect = { 10, 10, 400, 200 };
    SetTextColor(memDC, RGB(0, 0, 0));
    DrawText(memDC, info.str().c_str(), -1, &infoRect, DT_LEFT);

    // 3. 화면 복사
    BitBlt(hdc, 0, 0, scrW, scrH, memDC, 0, 0, SRCCOPY);

    // 리소스 해제
    SelectObject(memDC, hOldFont);
    DeleteObject(hFont);
    SelectObject(memDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
}

// --------------------------------------------------------
// 윈도우 프로시저
// --------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        // AStar 초기화
        g_pAStar = new AStar(MAP_WIDTH, MAP_HEIGHT);
        g_pAStar->Initialize(MAP_WIDTH, MAP_HEIGHT);
        SetTimer(hWnd, 1, 10, nullptr);
        break;
    case WM_TIMER:
    {
        // [핵심] 탐색 중이면 한 단계 진행하고 화면 갱신
        if (g_pAStar->GetState() == AStar::State::SEARCHING)
        {
            // 속도 조절: 한 프레임에 몇 개의 노드를 처리할지 결정
            // 1로 하면 천천히 퍼짐, 10으로 하면 빠르게 퍼짐
            for (int i = 0; i < 3; ++i)
            {
                g_pAStar->UpdatePathFinding();
                if (g_pAStar->GetState() != AStar::State::SEARCHING) break;
            }
            InvalidateRect(hWnd, nullptr, FALSE); // 화면 다시 그리기 요청
        }
    }
    break;
    case WM_DESTROY:
        delete g_pAStar;
        PostQuitMessage(0);
        break;

    case WM_MOUSEWHEEL:
    {
        // 줌 인/아웃
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        float oldScale = g_scale;

        if (delta > 0) g_scale *= 1.1f; // 10% 확대
        else g_scale *= 0.9f;          // 10% 축소

        if (g_scale < 5.0f) g_scale = 5.0f;
        if (g_scale > 100.0f) g_scale = 100.0f;

        // 마우스 커서 기준으로 줌 되도록 오프셋 조정 (심화)
        // 여기서는 간단히 화면 중심으로 줌 처리한다고 가정하거나,
        // 현재 오프셋 유지.
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    break;

    case WM_LBUTTONDOWN:
    {
        g_isLeftMouseDown = true;
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        Point p = ScreenToGrid(x, y);

        if (p.x >= 0 && p.x < MAP_WIDTH && p.y >= 0 && p.y < MAP_HEIGHT)
        {
            // Ctrl: 시작점
            if (GetKeyState(VK_CONTROL) & 0x8000)
            {
                g_startPos = p;
                g_pAStar->SetObstacle(p.x, p.y, false); // 시작점은 벽 아님
            }
            // Shift: 도착점
            else if (GetKeyState(VK_SHIFT) & 0x8000)
            {
                g_endPos = p;
               
                // [변경] 도착점 바뀌면 탐색 재시작 준비
                g_pAStar->StartPathFinding(g_startPos, g_endPos);
            }
            // 'A': 장애물 토글 (드래그 가능하도록 여기서 처리 시작)
            else if (GetKeyState('A') & 0x8000)
            {
                // 현재 상태의 반대로 설정하고 싶다면 로직 추가 필요.
                // 여기선 단순히 벽으로 만듦
                g_pAStar->SetObstacle(p.x, p.y, true);
            }
        }
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    break;

    case WM_LBUTTONUP:
        g_isLeftMouseDown = false;
        break;
    case WM_RBUTTONDOWN:
    {
        g_isRightMouseDown = true;
        g_lastMousePos.x = LOWORD(lParam);
        g_lastMousePos.y = HIWORD(lParam);
        SetCapture(hWnd); // 마우스가 화면 밖으로 나가도 이벤트를 받게 함
    }
    break;

    // 2. 우클릭 뗐을 때: "이동 끝!"
    case WM_RBUTTONUP:
    {
        g_isRightMouseDown = false;
        ReleaseCapture(); // 마우스 캡처 해제
    }
    break;

    // 3. 마우스 움직임 처리 (이동 + 드래그)
    case WM_MOUSEMOVE:
    {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        // [추가] 우클릭 드래그 중이라면 -> 화면 이동 (Panning)
        if (g_isRightMouseDown)
        {
            // 이동한 거리(Delta) 계산
            int dx = x - g_lastMousePos.x;
            int dy = y - g_lastMousePos.y;

            // 오프셋에 더해주기 (카메라 이동)
            g_offsetX += dx;
            g_offsetY += dy;

            // 현재 위치를 '이전 위치'로 갱신 (다음 프레임 계산용)
            g_lastMousePos.x = x;
            g_lastMousePos.y = y;

            InvalidateRect(hWnd, nullptr, FALSE); // 화면 갱신
        }

        // [기존] 좌클릭 드래그 중이라면 -> 벽 설치 (기존 코드 유지)
        else         if (g_isLeftMouseDown)
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            Point p = ScreenToGrid(x, y);

            if (p.x >= 0 && p.x < MAP_WIDTH && p.y >= 0 && p.y < MAP_HEIGHT)
            {
                // 드래그 중 'A'키가 눌려있으면 벽 설치
                if (GetKeyState('A') & 0x8000)
                {
                    g_pAStar->SetObstacle(p.x, p.y, true);
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        }
    }
    break;
    case WM_MBUTTONDOWN: // 휠 버튼 클릭
    {
        g_offsetX = 50;
        g_offsetY = 50;
        g_scale = 30.0f; // 줌도 초기화하고 싶으면 주석 해제
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    break;

    case WM_KEYDOWN:
    {
        // H: 휴리스틱 변경 (Manhattan <-> Euclidean)
        if (wParam == 'H')
        {
            auto current = g_pAStar->GetHeuristicType();
            if (current == AStar::HeuristicType::MANHATTAN)
                g_pAStar->SetHeuristicType(AStar::HeuristicType::EUCLIDEAN);
            else
                g_pAStar->SetHeuristicType(AStar::HeuristicType::MANHATTAN);

            InvalidateRect(hWnd, nullptr, FALSE);
        }
        // G: 이동 방식 변경 (대각선 허용 여부)
        // 사용자의 의도: G키는 유클리드(대각선), 떼거나 토글시 맨하튼(직선)
        else if (wParam == 'G')
        {
            bool allow = g_pAStar->GetAllowDiagonal();
            g_pAStar->SetAllowDiagonal(!allow);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        // [추가] 'R': 맵 랜덤 초기화 (Reset)
        else if (wParam == 'R')
        {
            // 45~50% 정도가 동굴 모양이 예쁘게 나옵니다.
            g_pAStar->GenerateRandomMap(47);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        // [추가] 'S': 한 단계 다듬기 (Smooth)
        else if (wParam == 'S')
        {
            g_pAStar->SmoothMap();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (wParam == VK_OEM_4) // '[' 키 코드
        {
            if (MAP_WIDTH > 10)
            {
                MAP_WIDTH -= 10;
                MAP_HEIGHT -= 10;

                // 1. 기존 객체 삭제 및 재생성
                delete g_pAStar;
                g_pAStar = new AStar(MAP_WIDTH, MAP_HEIGHT);

                // 2. 시작/도착점 안전한 곳으로 초기화
                g_startPos = { 0, 0 };
                g_endPos = { MAP_WIDTH - 1, MAP_HEIGHT - 1 };

                // 3. 맵 랜덤 생성 (크기 바뀌었으니 다시 그려야 함)
                g_pAStar->GenerateRandomMap(47);

                // 4. 화면 갱신
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        // ']' 키: 맵 확대 (최대 200x200 제한)
        else if (wParam == VK_OEM_6) // ']' 키 코드
        {
            if (MAP_WIDTH < 200)
            {
                MAP_WIDTH += 10;
                MAP_HEIGHT += 10;

                // 1. 기존 객체 삭제 및 재생성
                delete g_pAStar;
                g_pAStar = new AStar(MAP_WIDTH, MAP_HEIGHT);

                // 2. 시작/도착점 안전한 곳으로 초기화
                g_startPos = { 0, 0 };
                g_endPos = { MAP_WIDTH - 1, MAP_HEIGHT - 1 };

                // 3. 맵 랜덤 생성
                g_pAStar->GenerateRandomMap(47);

                // 4. 맵이 커졌으니 자동으로 줌 아웃 좀 해주면 센스 만점
                // (현재 스케일이 너무 크면 좀 줄여줌)
                if (g_scale > 800.0f / MAP_WIDTH)
                    g_scale = 800.0f / MAP_WIDTH;

                // 5. 화면 갱신
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        Render(hdc, hWnd);
        EndPaint(hWnd, &ps);
    }
    break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// --------------------------------------------------------
// 엔트리 포인트
// --------------------------------------------------------
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"AStarVizClass";
    wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

    RegisterClassExW(&wcex);

    HWND hWnd = CreateWindowW(L"AStarVizClass", L"A* Pathfinding Visualization", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 1024, 768, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}