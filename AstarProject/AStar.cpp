#include "MemoryPool.h"
#include <vector>
#include <algorithm>
#include <list>
#include <cmath>
#include <functional>
#include <ctime>
#include "AStar.h"

AStar::AStar(int mapWidth, int mapHeight)
    : _weight(1.0f)               // <--- [핵심] 가중치 1.0 필수 초기화!
    , _allowDiagonal(true)        // 대각선 허용 기본값
    , _heuristicType(HeuristicType::MANHATTAN) // 기본 휴리스틱
{
    Initialize(mapWidth, mapHeight);
}

AStar::~AStar()
{
    ClearNodes();
}

void AStar::Initialize(int mapWidth, int mapHeight)
{
    _mapWidth = mapWidth;
    _mapHeight = mapHeight;

    // 맵 데이터 크기 잡기 (false로 초기화)
    _mapGrid.assign(_mapWidth * _mapHeight, false);

    // 노드 맵 검색용 크기 잡기 (nullptr로 초기화)
    _nodeMap.resize(_mapWidth * _mapHeight);
    std::fill(_nodeMap.begin(), _nodeMap.end(), nullptr);
}

void AStar::SetObstacle(int x, int y, bool isWall)
{
    if (x < 0 || x >= _mapWidth || y < 0 || y >= _mapHeight) return;
    _mapGrid[y * _mapWidth + x] = isWall;
}

void AStar::ClearObstacles()
{
    std::fill(_mapGrid.begin(), _mapGrid.end(), false);
}

bool AStar::IsWalkable(int x, int y)
{
    if (x < 0 || x >= _mapWidth || y < 0 || y >= _mapHeight) return false;
    return !_mapGrid[y * _mapWidth + x]; // 벽(true)이면 false 반환
}

float AStar::CalculateH(Point current, Point end) {
	float dx = std::abs((float)(current.x - end.x));
	float dy = std::abs((float)(current.y - end.y));

	switch (_heuristicType) {
	case HeuristicType::MANHATTAN:
		return (dx + dy) * _weight;
	case HeuristicType::EUCLIDEAN:
		return std::sqrt(dx * dx + dy * dy) * _weight;
	}
	return 0.0f;
}

void AStar::StartPathFinding(Point start, Point end)
{
    // 1. 초기화
    ClearNodes();
    _lastPath.clear();
    _lastStart = start;
    _targetEnd = end; // 멤버 변수에 저장해둬야 Update에서 씀

    // 맵 초기화 (이전 흔적 지우기)
    // ClearNodes에서 _nodeMap을 부분 초기화하므로 여기선 패스하거나 안전하게 확인

    // 시작점 예외 처리
    if (!IsWalkable(start.x, start.y) || !IsWalkable(end.x, end.y))
    {
        _state = State::FAILED;
        return;
    }

    // 2. 시작 노드 등록
    float h = CalculateH(start, end);
    Node* startNode = _nodePool.Alloc(start.x, start.y, nullptr, 0.0f, h);
    _createdNodes.push_back(startNode);

    _openList.push_back(startNode);
    std::push_heap(_openList.begin(), _openList.end(), NodeCompare());

    int startIndex = start.y * _mapWidth + start.x;
    _nodeMap[startIndex] = startNode;

    // [상태 변경] 이제 탐색 중이다!
    _state = State::SEARCHING;
}

void AStar::UpdatePathFinding()
{
    // 탐색 중이 아니면 아무것도 안 함
    if (_state != State::SEARCHING) return;

    // OpenList가 비었으면 -> 갈 수 있는 길이 없음
    if (_openList.empty())
    {
        _state = State::FAILED;
        return;
    }

    // -------------------------------------------------------
    // [원래 while 루프 안에 있던 로직을 딱 한 번만 실행]
    // -------------------------------------------------------

    // 1. 노드 꺼내기
    std::pop_heap(_openList.begin(), _openList.end(), NodeCompare());
    Node* current = _openList.back();
    _openList.pop_back();

    // 2. Lazy Deletion 체크
    if (current->isClosed)
        return; // 이번 프레임은 그냥 넘김 (다음 호출때 다시 꺼냄)

    // 3. 방문 확정
    current->isClosed = true;
    _closedList.push_back(current);

    // 4. 목적지 도착 체크
    if (current->x == _targetEnd.x && current->y == _targetEnd.y)
    {
        _state = State::FINISHED; // 찾았다!

        // 경로 역추적
        Node* trace = current;
        while (trace)
        {
            _lastPath.push_back({ trace->x, trace->y });
            trace = trace->parent;
        }
        // [중요] 시작 -> 끝 순서로 뒤집기
        std::reverse(_lastPath.begin(), _lastPath.end());
        return;
    }

    // 5. 8방향 탐색
    for (int i = 0; i < 8; ++i)
    {
        int nextX = current->x + dx[i];
        int nextY = current->y + dy[i];

        if (!IsWalkable(nextX, nextY)) continue;
        if (nextX < 0 || nextX >= _mapWidth || nextY < 0 || nextY >= _mapHeight) continue;

        // 대각선 & 코너링 체크
        if (!_allowDiagonal && i >= 4) continue;
        if (i >= 4)
        {
            if (!IsWalkable(current->x, nextY) || !IsWalkable(nextX, current->y))
                continue;
        }

        int nextIndex = nextY * _mapWidth + nextX;
        Node* nextNode = _nodeMap[nextIndex];

        if (nextNode != nullptr && nextNode->isClosed) continue;

        float newG = current->g + cost[i];

        // Case A: 처음 방문
        if (nextNode == nullptr)
        {
            float newH = CalculateH({ nextX, nextY }, _targetEnd);
            nextNode = _nodePool.Alloc(nextX, nextY, current, newG, newH);
            _createdNodes.push_back(nextNode);
            _nodeMap[nextIndex] = nextNode;

            _openList.push_back(nextNode);
            std::push_heap(_openList.begin(), _openList.end(), NodeCompare());
        }
        // Case B: 더 좋은 경로 발견
        else if (newG < nextNode->g)
        {
            nextNode->g = newG;
            nextNode->f = newG + nextNode->h;
            nextNode->parent = current;

            _openList.push_back(nextNode);
            std::push_heap(_openList.begin(), _openList.end(), NodeCompare());
        }
    }
}

void AStar::ClearNodes()
{
    for (Node* node : _createdNodes)
    {
        int index = node->y * _mapWidth + node->x;
        _nodeMap[index] = nullptr;
        _nodePool.Free(node);
    }

    // 리스트 초기화
    _createdNodes.clear();

    // 물론 Open, ClosedList도 비워줍니다 (포인터만 날림)
    _openList.clear();
    _closedList.clear();
}

void AStar::Draw(DrawCallback drawFunc)
{
    if (drawFunc == nullptr) return;

    // 1. 전체 맵 (벽) 그리기
    for (int y = 0; y < _mapHeight; ++y)
    {
        for (int x = 0; x < _mapWidth; ++x)
        {
            if (_mapGrid[y * _mapWidth + x])
                drawFunc(x, y, NodeType::WALL);
        }
    }

    // 2. ClosedList (방문 함 - 빨강)
    for (Node* node : _closedList)
    {
        // 시작점과 도착점은 덮어쓰지 않기 위해 체크 가능
        drawFunc(node->x, node->y, NodeType::CLOSED);
    }

    // 3. OpenList (방문 예정 - 초록)
    for (Node* node : _openList)
    {
        if (node->isClosed) continue; // 중복 제거
        drawFunc(node->x, node->y, NodeType::OPEN);
    }

    // 4. 최종 경로 (파랑)
    for (const Point& p : _lastPath)
    {
        drawFunc(p.x, p.y, NodeType::PATH);
    }

    // 5. 시작점, 도착점 강조
    if (_lastStart.x != -1) drawFunc(_lastStart.x, _lastStart.y, NodeType::START);
    if (_lastEnd.x != -1) drawFunc(_lastEnd.x, _lastEnd.y, NodeType::END);
}

// 1. 랜덤하게 벽 뿌리기
void AStar::GenerateRandomMap(int fillPercent)
{
    // 기존 데이터 초기화
    ClearObstacles();
    ClearNodes(); // 길찾기 데이터도 날려야 안전
    _lastPath.clear();
    _state = State::READY;

    for (int y = 0; y < _mapHeight; ++y)
    {
        for (int x = 0; x < _mapWidth; ++x)
        {
            // 가장자리는 무조건 벽으로 막기 (나가는 길 방지)
            if (x == 0 || x == _mapWidth - 1 || y == 0 || y == _mapHeight - 1)
            {
                _mapGrid[y * _mapWidth + x] = true;
            }
            else
            {
                // fillPercent 확률로 벽 생성
                // (rand % 100) -> 0~99 사이 난수
                bool isWall = (std::rand() % 100) < fillPercent;
                _mapGrid[y * _mapWidth + x] = isWall;
            }
        }
    }
}

// 2. 주변 벽 개수 세기 (Smoothing용)
int AStar::GetSurroundingWallCount(int gridX, int gridY)
{
    int wallCount = 0;

    // 내 주변 8칸 + 나 자신까지 검사 (3x3 영역)
    for (int neighborY = gridY - 1; neighborY <= gridY + 1; ++neighborY)
    {
        for (int neighborX = gridX - 1; neighborX <= gridX + 1; ++neighborX)
        {
            // 맵 범위 밖이면 그냥 벽으로 칩니다 (동굴 안쪽으로 유도하기 위해)
            if (neighborX < 0 || neighborX >= _mapWidth || neighborY < 0 || neighborY >= _mapHeight)
            {
                wallCount++;
            }
            // 자기 자신은 제외하고 계산하는 경우도 있지만, 
            // 여기서는 자기 위치가 아닐 때만 세거나, 로직에 따라 포함하기도 함.
            // 보통 자기 자신 제외하고 셈.
            else if (neighborX != gridX || neighborY != gridY)
            {
                // 벽이면 카운트
                if (_mapGrid[neighborY * _mapWidth + neighborX])
                {
                    wallCount++;
                }
            }
        }
    }
    return wallCount;
}

// 3. 다듬기 (Smoothing) - 핵심 알고리즘
void AStar::SmoothMap()
{
    std::vector<bool> newMap = _mapGrid; // 현재 맵 복사본 생성

    for (int y = 0; y < _mapHeight; ++y)
    {
        for (int x = 0; x < _mapWidth; ++x)
        {
            int neighborWallTiles = GetSurroundingWallCount(x, y);

            // [규칙]
            // 주변에 벽이 4개보다 많으면 -> 나도 벽이 됨 (벽이 뭉침)
            if (neighborWallTiles > 4)
                newMap[y * _mapWidth + x] = true;
            // 주변에 벽이 4개보다 적으면 -> 나는 빈 땅이 됨 (구멍이 넓어짐)
            else if (neighborWallTiles < 4)
                newMap[y * _mapWidth + x] = false;

            // 4개면? -> 현상 유지 (기존 값 그대로)
        }
    }

    _mapGrid = newMap; // 맵 덮어쓰기
}