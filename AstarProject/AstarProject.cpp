#include "MemoryPool.h"
#include <windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <list>
#include <ctime>
#include <iomanip>
#include <functional>
#include "AStar.h"

// --------------------------------------------------------
// 전역 변수 및 설정
// --------------------------------------------------------
AStar* g_pAStar = nullptr;
int MAP_WIDTH = 20;  // 맵 가로 격자 수
int MAP_HEIGHT = 20; // 맵 세로 격자 수
float g_cameraSpeed = 10.0f; // WASD 이동 속도

// 뷰포트 설정
float g_scale = 30.0f;     // 초기 줌 레벨
int g_offsetX = 50;        // 맵 시작 위치 X
int g_offsetY = 50;        // 맵 시작 위치 Y

Point g_startPos = { 0, 0 };
Point g_endPos = { 19, 19 };

// 마우스 상태 및 드래그 로직
bool g_isLeftMouseDown = false;
bool g_isRightMouseDown = false;
POINT g_lastMousePos = { 0, 0 };

// [추가] 드래그 시 벽을 설치할지(true), 지울지(false) 결정하는 플래그
bool g_isDrawingWalls = true;

// 함수 전방 선언
void FitMapToScreen(HWND hWnd);

// --------------------------------------------------------
// 좌표 변환 헬퍼 함수
// --------------------------------------------------------
Point ScreenToGrid(int sx, int sy)
{
    int gx = (int)((sx - g_offsetX) / g_scale);
    int gy = (int)((sy - g_offsetY) / g_scale);
    return { gx, gy };
}

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
    RECT rect;
    GetClientRect(hWnd, &rect);
    int scrW = rect.right - rect.left;
    int scrH = rect.bottom - rect.top;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, scrW, scrH);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

    // 배경 지우기
    HBRUSH hBgBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(memDC, &rect, hBgBrush);
    DeleteObject(hBgBrush);

    // 폰트 설정
    int fontSize = (int)(g_scale * 0.3f);
    if (fontSize < 10) fontSize = 10;
    HFONT hFont = CreateFont(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);
    SetBkMode(memDC, TRANSPARENT);

    // 맵 그리기 준비
    const auto& allNodes = g_pAStar->GetAllNodes();
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

            // Culling
            if (bottomRight.x < 0 || topLeft.x > scrW || bottomRight.y < 0 || topLeft.y > scrH)
                continue;

            // 1. 셀 배경색 채우기
            HBRUSH hBrush = nullptr;
            Node* node = renderMap[y * MAP_WIDTH + x];

            if (x == g_startPos.x && y == g_startPos.y) hBrush = CreateSolidBrush(RGB(0, 255, 0));
            else if (x == g_endPos.x && y == g_endPos.y) hBrush = CreateSolidBrush(RGB(255, 0, 0));
            else if (!g_pAStar->IsWalkable(x, y)) hBrush = CreateSolidBrush(RGB(50, 50, 50));
            else if (node)
            {
                if (node->isClosed) hBrush = CreateSolidBrush(RGB(200, 200, 255));
                else hBrush = CreateSolidBrush(RGB(200, 255, 200));
            }
            else hBrush = CreateSolidBrush(RGB(240, 240, 240));

            FillRect(memDC, &cellRect, hBrush);
            FrameRect(memDC, &cellRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            DeleteObject(hBrush);

            // 2. [복구] 부모 노드 방향 표시 (파란 선)
            // 줌 레벨이 너무 작지 않으면(너무 멀지 않으면) 항상 그리도록 변경
            if (node && node->parent && g_scale > 10.0f)
            {
                POINT center = { (topLeft.x + bottomRight.x) / 2, (topLeft.y + bottomRight.y) / 2 };
                POINT parentCenter = GridToScreen(node->parent->x, node->parent->y);
                parentCenter.x = (parentCenter.x + (long)(g_scale / 2));
                parentCenter.y = (parentCenter.y + (long)(g_scale / 2));

                // 선 두께는 줌 레벨에 비례하되 최대 3
                int penWidth = (int)(g_scale / 15.0f);
                if (penWidth < 1) penWidth = 1;
                if (penWidth > 3) penWidth = 3;

                HPEN hPen = CreatePen(PS_SOLID, penWidth, RGB(0, 0, 255)); // 파란색 화살표
                HPEN hOldPen = (HPEN)SelectObject(memDC, hPen);

                MoveToEx(memDC, center.x, center.y, nullptr);
                LineTo(memDC, parentCenter.x, parentCenter.y);

                // 끝에 작은 점 찍어서 방향 명확히 (선택사항)
                // SetPixel(memDC, parentCenter.x, parentCenter.y, RGB(0,0,255));

                SelectObject(memDC, hOldPen);
                DeleteObject(hPen);
            }

            // 3. [수정] 텍스트 정보 (float 표시)
            // 줌이 충분히 가까울 때만 텍스트 출력
            if (node && g_scale > 40.0f)
            {
                std::wstringstream ss;
                // [변경] 소수점 1자리 고정
                ss << std::fixed << std::setprecision(1);

                ss << L"F:" << node->f << L"\n";
                ss << L"G:" << node->g << L"\n";
                ss << L"H:" << node->h;

                RECT textRect = cellRect;
                textRect.left += 2; textRect.top += 2;

                // 글씨가 잘 보이게 검은색 설정
                SetTextColor(memDC, RGB(0, 0, 0));
                DrawText(memDC, ss.str().c_str(), -1, &textRect, DT_LEFT);
            }
        }
    }

    // 경로 그리기 (골드 색상)
    const std::vector<Point>& path = g_pAStar->GetPath();
    if (!path.empty())
    {
        HPEN hPathPen = CreatePen(PS_SOLID, 4, RGB(255, 215, 0)); // 조금 더 두껍게(4)
        HPEN hOldPen = (HPEN)SelectObject(memDC, hPathPen);

        for (size_t i = 0; i < path.size() - 1; ++i)
        {
            POINT p1 = GridToScreen(path[i].x, path[i].y);
            POINT p2 = GridToScreen(path[i + 1].x, path[i + 1].y);

            int offset = (int)(g_scale / 2);
            MoveToEx(memDC, p1.x + offset, p1.y + offset, nullptr);
            LineTo(memDC, p2.x + offset, p2.y + offset);
        }
        SelectObject(memDC, hOldPen);
        DeleteObject(hPathPen);
    }

    // UI 정보 출력 (기존 유지)
    SelectObject(memDC, hOldFont);
    DeleteObject(hFont);

    HFONT hUIFont = CreateFont(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
    HFONT hOldUIFont = (HFONT)SelectObject(memDC, hUIFont);

    std::wstringstream info;
    info << L"[Controls]\n";
    info << L"WASD: Camera Move\n";
    info << L"Wheel: Zoom\n";
    info << L"Ctrl/Shift + Click: Start/End Pos\n";
    info << L"'E' + Click/Drag: Draw/Erase Wall\n";
    info << L"'X': Smooth Map\n";
    info << L"'R' / 'F': Random Map / Fit Screen\n";
    info << L"'[' / ']': Map Resize\n";

    // [▼▼▼ 여기에 상태 표시 코드 추가 ▼▼▼]
    info << L"----------------------------\n";

    // 현재 휴리스틱 상태 표시
    info << L"[H] Heuristic: ";
    if (g_pAStar->GetHeuristicType() == AStar::HeuristicType::MANHATTAN)
        info << L"Manhattan (Grid)";
    else
        info << L"Euclidean (Direct)";
    info << L"\n";

    // 현재 대각선 이동 허용 여부 표시
    info << L"[G] Diagonal: ";
    if (g_pAStar->GetAllowDiagonal())
        info << L"Allowed";
    else
        info << L"Blocked";
    info << L"\n";
    // [▲▲▲ 여기까지 추가 ▲▲▲]

    RECT infoBgRect = { 10, 10, 360, 240 };
    HBRUSH hSemiTransBrush = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(memDC, &infoBgRect, hSemiTransBrush);
    DeleteObject(hSemiTransBrush);

    RECT infoRect = { 15, 15, 400, 300 };
    SetTextColor(memDC, RGB(0, 0, 0));
    DrawText(memDC, info.str().c_str(), -1, &infoRect, DT_LEFT);

    SelectObject(memDC, hOldUIFont);
    DeleteObject(hUIFont);
    BitBlt(hdc, 0, 0, scrW, scrH, memDC, 0, 0, SRCCOPY);
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
        g_pAStar = new AStar(MAP_WIDTH, MAP_HEIGHT);
        g_pAStar->Initialize(MAP_WIDTH, MAP_HEIGHT);
        SetTimer(hWnd, 1, 10, nullptr);
        break;

    case WM_TIMER:
    {
        // WASD 카메라 이동
        if (GetForegroundWindow() == hWnd)
        {
            bool moved = false;
            if (GetAsyncKeyState('W') & 0x8000) { g_offsetY += (int)g_cameraSpeed; moved = true; }
            if (GetAsyncKeyState('S') & 0x8000) { g_offsetY -= (int)g_cameraSpeed; moved = true; } // S는 이제 이동키
            if (GetAsyncKeyState('A') & 0x8000) { g_offsetX += (int)g_cameraSpeed; moved = true; } // A는 이제 이동키
            if (GetAsyncKeyState('D') & 0x8000) { g_offsetX -= (int)g_cameraSpeed; moved = true; }
            if (moved) InvalidateRect(hWnd, nullptr, FALSE);
        }

        // 길찾기 애니메이션
        if (g_pAStar->GetState() == AStar::State::SEARCHING)
        {
            for (int i = 0; i < 3; ++i)
            {
                g_pAStar->UpdatePathFinding();
                if (g_pAStar->GetState() != AStar::State::SEARCHING) break;
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    }
    break;

    case WM_DESTROY:
        delete g_pAStar;
        PostQuitMessage(0);
        break;

    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta > 0) g_scale *= 1.1f;
        else g_scale *= 0.9f;
        if (g_scale < 5.0f) g_scale = 5.0f;
        if (g_scale > 100.0f) g_scale = 100.0f;
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
            if (GetKeyState(VK_CONTROL) & 0x8000)
            {
                g_startPos = p;
                g_pAStar->SetObstacle(p.x, p.y, false);
            }
            else if (GetKeyState(VK_SHIFT) & 0x8000)
            {
                g_endPos = p;
                g_pAStar->StartPathFinding(g_startPos, g_endPos);
            }
            // [수정] 'E' 키를 누르고 클릭하면 드래그 모드 결정
            else if (GetKeyState('E') & 0x8000) // A -> E로 변경
            {
                // 클릭한 곳이 비어있으면(Walkable) -> 벽 설치 모드(true)
                // 클릭한 곳이 벽이면(!Walkable)    -> 벽 제거 모드(false)
                g_isDrawingWalls = g_pAStar->IsWalkable(p.x, p.y);

                g_pAStar->SetObstacle(p.x, p.y, g_isDrawingWalls);
            }
        }
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    break;

    case WM_LBUTTONUP:
        g_isLeftMouseDown = false;
        break;

    case WM_RBUTTONDOWN:
        g_isRightMouseDown = true;
        g_lastMousePos.x = LOWORD(lParam);
        g_lastMousePos.y = HIWORD(lParam);
        SetCapture(hWnd);
        break;

    case WM_RBUTTONUP:
        g_isRightMouseDown = false;
        ReleaseCapture();
        break;

    case WM_MOUSEMOVE:
    {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        if (g_isRightMouseDown)
        {
            int dx = x - g_lastMousePos.x;
            int dy = y - g_lastMousePos.y;
            g_offsetX += dx;
            g_offsetY += dy;
            g_lastMousePos.x = x;
            g_lastMousePos.y = y;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        // [수정] 좌클릭 드래그 로직
        else if (g_isLeftMouseDown)
        {
            Point p = ScreenToGrid(x, y);
            if (p.x >= 0 && p.x < MAP_WIDTH && p.y >= 0 && p.y < MAP_HEIGHT)
            {
                // 'E'키가 눌린 상태에서 드래그 중이라면
                if (GetKeyState('E') & 0x8000)
                {
                    // 클릭했을 때 결정된 모드(설치/제거)를 계속 적용
                    g_pAStar->SetObstacle(p.x, p.y, g_isDrawingWalls);
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        }
    }
    break;

    case WM_MBUTTONDOWN:
        g_offsetX = 50;
        g_offsetY = 50;
        g_scale = 30.0f;
        InvalidateRect(hWnd, nullptr, FALSE);
        break;

    case WM_KEYDOWN:
    {
        if (wParam == 'F') FitMapToScreen(hWnd);
        else if (wParam == 'H')
        {
            auto current = g_pAStar->GetHeuristicType();
            g_pAStar->SetHeuristicType(current == AStar::HeuristicType::MANHATTAN ? AStar::HeuristicType::EUCLIDEAN : AStar::HeuristicType::MANHATTAN);
        }
        else if (wParam == 'G') g_pAStar->SetAllowDiagonal(!g_pAStar->GetAllowDiagonal());
        else if (wParam == 'R') g_pAStar->GenerateRandomMap(47);
        // [수정] Smooth Map 키 변경: S -> X
        else if (wParam == 'X') g_pAStar->SmoothMap();
        else if (wParam == VK_OEM_4) // '['
        {
            if (MAP_WIDTH > 10)
            {
                MAP_WIDTH -= 10; MAP_HEIGHT -= 10;
                delete g_pAStar; g_pAStar = new AStar(MAP_WIDTH, MAP_HEIGHT);
                g_startPos = { 0, 0 }; g_endPos = { MAP_WIDTH - 1, MAP_HEIGHT - 1 };
                g_pAStar->GenerateRandomMap(47);
            }
        }
        else if (wParam == VK_OEM_6) // ']'
        {
            if (MAP_WIDTH < 200)
            {
                MAP_WIDTH += 10; MAP_HEIGHT += 10;
                delete g_pAStar; g_pAStar = new AStar(MAP_WIDTH, MAP_HEIGHT);
                g_startPos = { 0, 0 }; g_endPos = { MAP_WIDTH - 1, MAP_HEIGHT - 1 };
                FitMapToScreen(hWnd);
            }
        }
        InvalidateRect(hWnd, nullptr, FALSE);
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
    case WM_SIZE:
        // FitMapToScreen(hWnd); 
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void FitMapToScreen(HWND hWnd)
{
    RECT rect;
    GetClientRect(hWnd, &rect);
    int scrW = rect.right - rect.left;
    int scrH = rect.bottom - rect.top;
    float scaleX = (float)scrW / MAP_WIDTH;
    float scaleY = (float)scrH / MAP_HEIGHT;
    g_scale = (scaleX < scaleY ? scaleX : scaleY) - 1.0f;
    if (g_scale < 1.0f) g_scale = 1.0f;
    g_offsetX = (int)((scrW - MAP_WIDTH * g_scale) / 2);
    g_offsetY = (int)((scrH - MAP_HEIGHT * g_scale) / 2);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, LoadIcon(hInstance, IDI_APPLICATION), LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), nullptr, L"AStarVizClass", LoadIcon(wcex.hInstance, IDI_APPLICATION) };
    RegisterClassExW(&wcex);
    HWND hWnd = CreateWindowW(L"AStarVizClass", L"A* Pathfinding Visualization", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 1024, 768, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return FALSE;
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}