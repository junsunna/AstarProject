#pragma once

struct Point
{
    int x;
    int y;

    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Point& other) const { return !(*this == other); }
};

// -----------------------------------------------------------
// 1. Node 구조체
// -----------------------------------------------------------
struct Node
{
    int x;
    int y;
    Node* parent;

    float g; // 시작점으로부터의 이동 거리 (float)
    float h; // 목적지와의 절대적 거리 (float)
    float f; // f = g + h (float)

    bool isClosed;
    // MemoryPool의 Alloc에서 호출할 생성자
    // placement new를 통해 할당과 동시에 초기화됩니다.
    Node(int _x, int _y, Node* _parent, float _g, float _h)
        : x(_x), y(_y), parent(_parent), g(_g), h(_h), f(_g + _h), isClosed(false) // false로 초기화
    {
    }
};

struct NodeCompare
{
    bool operator()(const Node* a, const Node* b) const
    {
        // [추가] F값이 거의 같다면 (float 오차 고려)
        if (std::abs(a->f - b->f) < 0.0001f)
        {
            // H가 더 작은(목적지에 가까운) 노드가 우선순위를 갖도록 함
            // (std::push_heap은 true일 때 a를 뒤로 보냄 -> 즉, a가 b보다 '나쁜' 놈이라는 뜻)
            // H가 크면 나쁜 놈이므로 true 반환
            return a->h > b->h;
        }
        return a->f > b->f; // 오름차순 (Min-Heap)
    }
};

// -----------------------------------------------------------
// 2. AStar 클래스 선언
// -----------------------------------------------------------
class AStar
{
public:
    enum class HeuristicType { MANHATTAN, EUCLIDEAN };
    enum class NodeType { NONE, OPEN, CLOSED, PATH, WALL, START, END };

    // [추가] 현재 탐색 상태를 나타내는 열거형
    enum class State { READY, SEARCHING, FINISHED, FAILED };    
public:
    // 생성자에서 메모리 풀의 초기 크기를 설정합니다.
    AStar(int mapWidth, int mapHeight);
    ~AStar();

    // 1. 초기화: 맵 크기 설정 및 메모리 확보
    void Initialize(int mapWidth, int mapHeight);


    // 2. 장애물 설정
    void SetObstacle(int x, int y, bool isWall);
    void ClearObstacles(); // 모든 장애물 제거

    // 길찾기 실행
    void StartPathFinding(Point start, Point end); // 1. 탐색 시작 준비
    void UpdatePathFinding();                      // 2. 한 단계(노드 하나) 처리

    // [추가] 현재 상태 확인용
    State GetState() const { return _state; }
    const std::vector<Point>& GetPath() const { return _lastPath; } // 완성된 경로 반환

    bool IsWalkable(int x, int y); // 벽인지 체크

    // 설정 관련
    void SetHeuristicType(HeuristicType type) { _heuristicType = type; }
    void SetHeuristicWeight(float weight) { _weight = weight; } // 가중치 (기본 1.0)
    void SetAllowDiagonal(bool allow) { _allowDiagonal = allow; } // 대각선 이동 허용 여부

    // 시각화 함수 (추후 구현)
    using DrawCallback = std::function<void(int x, int y, NodeType type)>;
    void Draw(DrawCallback drawFunc);

    // 시각화 전용: 만들어진 모든 노드 리스트 반환 (const로 안전하게)
    const std::vector<Node*>& GetAllNodes() const { return _createdNodes; }

    // 현재 설정 상태 확인용 (화면에 띄우기 위해)
    HeuristicType GetHeuristicType() const { return _heuristicType; }
    bool GetAllowDiagonal() const { return _allowDiagonal; }

    // 1. 맵을 랜덤한 노이즈로 채우기 (fillPercent: 벽이 될 확률, 보통 45~50)
    void GenerateRandomMap(int fillPercent = 45);

    // 2. 세포 자동자 규칙으로 한 단계 다듬기
    void SmoothMap();

private:
    // -------------------------------------------------------
    // 내부 헬퍼 함수
    // -------------------------------------------------------

    int GetSurroundingWallCount(int gridX, int gridY);
    // 휴리스틱 계산 (옵션에 따라 분기)
    float CalculateH(Point current, Point end);

    // 탐색 종료 후 사용한 모든 노드 반납
    void ClearNodes();

private:
    // -------------------------------------------------------
    // 멤버 변수
    // -------------------------------------------------------
    const int dx[8] = { 0, 0, -1, 1, - 1, 1, -1, 1 };
    const int dy[8] = { -1, 1, 0, 0 , -1, -1, 1, 1 };
    const float cost[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.414f, 1.414f, 1.414f, 1.414f };
    
    int _mapWidth;
    int _mapHeight;

    HeuristicType _heuristicType;
    float _weight;
    bool _allowDiagonal;

    // [메모리 풀]
    // Node 객체를 관리하는 풀. AStar 클래스 멤버로 존재.
    procademy::CMemoryPool<Node> _nodePool{ 1000, true };

    // [탐색용 컨테이너]
    // 여기에 들어가는 Node*는 모두 _nodePool에서 Alloc된 것들입니다.
    std::vector<Node*> _openList;   // 힙(Heap)으로 사용할 벡터
    std::vector<Node*> _closedList; // 방문한 노드 보관 (반납용)
    std::vector<Node*> _createdNodes;

    // 빠른 검색을 위한 보조 컨테이너 (좌표로 방문 여부 확인용)
    // 예: _visited[y][x] -> 해당 위치에 생성된 Node 포인터
    std::vector<Node*> _nodeMap;

    // [맵 데이터]
    // true: 벽(장애물), false: 이동 가능
    std::vector<bool> _mapGrid;

    // 마지막 경로 저장 (Draw용)
    std::vector<Point> _lastPath;
    Point _lastStart{ -1, -1 };
    Point _lastEnd{ -1, -1 };

    State _state = State::READY;
    Point _targetEnd = { -1, -1 }; // 목적지 저장용
};