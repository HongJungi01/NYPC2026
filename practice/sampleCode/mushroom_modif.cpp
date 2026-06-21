#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <climits>
#include <random>
#include <unordered_map>

using namespace std;
using chrono::steady_clock;
using chrono::milliseconds;
using TimePoint = chrono::time_point<steady_clock>;

static const int NR = 10, NC = 17;
static const int N  = NR * NC;

// ─── Game state ───
struct State {
    int8_t cell[NR][NC]; // board values 0-9
    int8_t own[NR][NC];  // 1=me, -1=opp, 0=neutral
    bool my_turn;
    bool last_pass;
    int  net;            // cumulative (my cells - opp cells)
    uint64_t hash;       // [추가] Zobrist 해시 (증분 갱신)
};

struct Move { int r1, c1, r2, c2; bool is_pass() const { return r1 < 0; } };
static const Move PASS_MOVE = {-1,-1,-1,-1};
static inline bool same_move(const Move& a, const Move& b) {
    return a.r1==b.r1 && a.c1==b.c1 && a.r2==b.r2 && a.c2==b.c2;
}

// ─── [추가] Zobrist 해시 테이블 ───
static uint64_t Z_CELL[N][10];
static uint64_t Z_OWN [N][3];   // 0=빈칸 1=나 2=상대
static uint64_t Z_TURN, Z_PASS;

static inline int own_enc(int o) { return o==0 ? 0 : (o==1 ? 1 : 2); }

static void init_zobrist() {
    std::mt19937_64 rng(20260620ULL);
    for (int i = 0; i < N; i++) {
        for (int v = 0; v < 10; v++) Z_CELL[i][v] = rng();
        for (int o = 0; o < 3;  o++) Z_OWN [i][o] = rng();
    }
    Z_TURN = rng();
    Z_PASS = rng();
}

static uint64_t compute_hash(const State& s) {
    uint64_t h = 0;
    for (int r = 0; r < NR; r++)
        for (int c = 0; c < NC; c++) {
            int idx = r*NC + c;
            h ^= Z_CELL[idx][s.cell[r][c]];
            h ^= Z_OWN [idx][own_enc(s.own[r][c])];
        }
    if (s.my_turn)  h ^= Z_TURN;
    if (s.last_pass) h ^= Z_PASS;
    return h;
}

// ─── Fast move generation (O(R²·C) prefix trick) ───
static int  p2c[N*9+2];
static int  dirty[N+2];
static int  ndirty = 0;
static bool p2c_init = false;

static vector<Move> gen_moves(const int8_t cell[NR][NC]) {
    if (!p2c_init) { fill(p2c, p2c+N*9+2, -1); p2c_init = true; }

    // [추가] 행별 '0이 아닌 칸' 누적 개수 → 변(edge) 검사를 O(1)로
    int nz[NR][NC+1];
    for (int r = 0; r < NR; r++) {
        nz[r][0] = 0;
        for (int c = 0; c < NC; c++) nz[r][c+1] = nz[r][c] + (cell[r][c] ? 1 : 0);
    }

    vector<Move> out;
    out.reserve(64);
    for (int r1 = 0; r1 < NR; r1++) {
        int cs[NC] = {};
        for (int r2 = r1; r2 < NR; r2++) {
            for (int c = 0; c < NC; c++) cs[c] += cell[r2][c];
            int P[NC+1]; P[0] = 0;
            for (int c = 0; c < NC; c++) P[c+1] = P[c] + cs[c];
            for (int i = 0; i < ndirty; i++) p2c[dirty[i]] = -1;
            ndirty = 0;
            for (int c2 = 0; c2 < NC; c2++) {
                if (cs[c2] > 0 && p2c[P[c2]] < 0) { p2c[P[c2]] = c2; dirty[ndirty++] = P[c2]; }
                if (!cs[c2]) continue;
                int t = P[c2+1] - 10;
                if (t < 0 || p2c[t] < 0) continue;
                int c1 = p2c[t];
                // [변경] 위/아래 변 검사 O(1) (좌/우는 cs>0 로 보장)
                if (nz[r1][c2+1] - nz[r1][c1] > 0 && nz[r2][c2+1] - nz[r2][c1] > 0)
                    out.push_back({r1,c1,r2,c2});
            }
        }
    }
    return out;
}

// ─── Delta without applying ───
static int move_delta(const State& s, const Move& m) {
    int d = 0;
    for (int r = m.r1; r <= m.r2; r++)
        for (int c = m.c1; c <= m.c2; c++) {
            int o = s.own[r][c];
            if (s.my_turn) d += (o==0)?1:(o==-1)?2:0;
            else           d += (o==0)?-1:(o==1)?-2:0;
        }
    return d;
}

// ─── Apply move, return new state (+ 증분 해시 갱신) ───
static State apply_move(const State& s, const Move& m) {
    State ns = s;                 // hash 포함 복사
    ns.my_turn = !s.my_turn;
    ns.hash ^= Z_TURN;            // 턴 토글
    if (m.is_pass()) {
        ns.last_pass = true;
        if (!s.last_pass) ns.hash ^= Z_PASS;   // false->true
        return ns;
    }
    if (s.last_pass) ns.hash ^= Z_PASS;        // true->false
    ns.last_pass = false;
    int8_t ov = s.my_turn ? 1 : -1;
    int ovenc  = s.my_turn ? 1 : 2;
    for (int r = m.r1; r <= m.r2; r++)
        for (int c = m.c1; c <= m.c2; c++) {
            int idx = r*NC + c;
            int o  = s.own[r][c];
            int cv = s.cell[r][c];
            int d = 0;
            if (s.my_turn) d = (o==0)?1:(o==-1)?2:0;
            else           d = (o==0)?-1:(o==1)?-2:0;
            ns.net += d;
            if (cv) { ns.hash ^= Z_CELL[idx][cv]; ns.hash ^= Z_CELL[idx][0]; }
            int oe = own_enc(o);
            if (oe != ovenc) { ns.hash ^= Z_OWN[idx][oe]; ns.hash ^= Z_OWN[idx][ovenc]; }
            ns.cell[r][c] = 0;
            ns.own[r][c]  = ov;
        }
    return ns;
}

// ─── 상수 ───
const int INF     = 1000000000;   // 1e9
const int TIMEOUT = INT_MIN;      // [수정] -INF 와 충돌하지 않는 별도 센티넬
const int WIN     = 1000000;      // 더블 패스 종료 점수
const int EXACT = 0, LOWER = 1, UPPER = 2;

// [추가] 후보 가지치기 한도. 0 이하 = 끔(전수 탐색).
// 파이썬은 느려서 켜야 깊이가 나오지만, C++는 빨라서 끄는 게 더 강함(실측 확인).
// 보드가 비정상적으로 커서 시간초과가 날 때만 양수로 켜기.
const int ROOT_LIMIT   = 0;   // 기본 OFF
const int BRANCH_LIMIT = 0;   // 기본 OFF

// 평가 함수: 점수차*100 + 기동성.
// (참고) '위협 항'(상대 최대 즉시이득 감산)은 파이썬판엔 넣었지만,
//        C++는 빠르게 깊이 파서 탐색이 이미 상대 반응을 보므로 위협 항이
//        오히려 소극적으로 만들어 약해짐(실측 7승9패). 그래서 C++은 미사용.
static int evaluate(const State& s) {
    auto moves = gen_moves(s.cell);
    int mobility = (int)moves.size();
    int score = s.my_turn ? s.net : -s.net;
    return score * 100 + mobility;
}

// ─── [추가] 트랜스포지션 테이블 ───
struct TTEntry { int depth; int val; int8_t flag; Move best; };
static unordered_map<uint64_t, TTEntry> TT;

static int negamax(const State& s, int depth, int alpha, int beta,
                   TimePoint end_time, Move& best_move, bool is_root) {
    if (steady_clock::now() >= end_time) return TIMEOUT;

    int alpha_orig = alpha;

    // ── TT 조회 ──
    Move tt_move = PASS_MOVE;
    bool have_tt = false;
    auto it = TT.find(s.hash);
    if (it != TT.end()) {
        const TTEntry& e = it->second;
        if (e.depth >= depth) {
            if (e.flag == EXACT) { if (is_root) best_move = e.best; return e.val; }
            else if (e.flag == LOWER) { if (e.val > alpha) alpha = e.val; }
            else if (e.flag == UPPER) { if (e.val < beta)  beta  = e.val; }
            if (alpha >= beta) { if (is_root) best_move = e.best; return e.val; }
        }
        tt_move = e.best; have_tt = true;
    }

    if (depth == 0) return evaluate(s);

    auto moves = gen_moves(s.cell);
    moves.push_back(PASS_MOVE);          // 전략적 패스 항상 후보

    // ── 수 정렬: TT 최선수 먼저 → delta 큰 순 → 패스 맨 뒤 ──
    int n = (int)moves.size();
    vector<pair<int,int>> ord(n);
    for (int i = 0; i < n; i++) {
        const Move& m = moves[i];
        int pr;
        if (have_tt && same_move(m, tt_move)) pr = INT_MAX;
        else if (m.is_pass())                 pr = INT_MIN;
        else { int d = move_delta(s, m); pr = s.my_turn ? d : -d; }
        ord[i] = {pr, i};
    }
    sort(ord.begin(), ord.end(), [](const pair<int,int>&a, const pair<int,int>&b){
        return a.first > b.first;
    });

    int limit = is_root ? ROOT_LIMIT : BRANCH_LIMIT;
    int explored_real = 0;

    int max_val = -INF;
    Move local_best = PASS_MOVE;
    for (auto& pi : ord) {
        const Move& m = moves[pi.second];
        if (!m.is_pass()) {
            if (limit > 0 && explored_real >= limit) continue;  // 가지치기
            explored_real++;
        }
        int val;
        if (s.last_pass && m.is_pass()) {
            int final_score = s.my_turn ? s.net : -s.net;
            val = final_score > 0 ? WIN : (final_score < 0 ? -WIN : 0);
        } else {
            State ns = apply_move(s, m);
            Move dummy;
            int child = negamax(ns, depth-1, -beta, -alpha, end_time, dummy, false);
            if (child == TIMEOUT) return TIMEOUT;   // TT 저장 없이 즉시 반환
            val = -child;
        }
        if (val > max_val) { max_val = val; local_best = m; if (is_root) best_move = m; }
        if (val > alpha) alpha = val;
        if (alpha >= beta) break;
    }

    // ── TT 저장 (깊이 우선 교체) ──
    int flag = (max_val <= alpha_orig) ? UPPER : (max_val >= beta ? LOWER : EXACT);
    auto it2 = TT.find(s.hash);
    if (it2 == TT.end() || it2->second.depth <= depth)
        TT[s.hash] = TTEntry{depth, max_val, (int8_t)flag, local_best};
    return max_val;
}

static Move alpha_beta_search(const State& root_state, int budget_ms) {
    TT.clear();
    auto start_time = steady_clock::now();
    auto end_time = start_time + milliseconds(budget_ms - 5);

    Move best = PASS_MOVE;
    for (int depth = 1; depth <= 24 && steady_clock::now() < end_time; depth++) {
        Move cur = PASS_MOVE;
        int score = negamax(root_state, depth, -INF, INF, end_time, cur, true);
        if (score != TIMEOUT) best = cur;
        else break;
    }
    return best;
}

// ─── Game class ───
class Game {
private:
    int8_t board[NR][NC];
    int8_t owner[NR][NC];
    bool   first;
    bool   passed;
    int    net_score;
    int    turn_count;

public:
    Game() = default;

    Game(const vector<vector<int>>& b, bool f)
        : first(f), passed(false), net_score(0), turn_count(0) {
        memset(owner, 0, sizeof(owner));
        for (int r = 0; r < NR; r++)
            for (int c = 0; c < NC; c++)
                board[r][c] = (int8_t)b[r][c];
    }

    bool isValid(int r1, int c1, int r2, int c2) {
        int s = 0;
        bool r1f=false, c1f=false, r2f=false, c2f=false;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (board[r][c]) {
                    s += board[r][c];
                    if (r==r1)r1f=true; if(r==r2)r2f=true;
                    if (c==c1)c1f=true; if(c==c2)c2f=true;
                }
        return s==10 && r1f && r2f && c1f && c2f;
    }

    vector<int> calculateMove(int myTime, int oppTime) {
        turn_count++;

        State s;
        memcpy(s.cell, board, sizeof(board));
        memcpy(s.own,  owner, sizeof(owner));
        s.my_turn   = true;
        s.last_pass = passed;
        s.net       = net_score;
        s.hash      = compute_hash(s);   // [추가] 루트 해시 1회 계산

        int remaining_est = max(3, 30 - turn_count);
        int budget_ms = max(300, min(2500, myTime / remaining_est));

        Move best = alpha_beta_search(s, budget_ms);
        return {best.r1, best.c1, best.r2, best.c2};
    }

    void updateOpponentAction(const vector<int>& action, int time) {
        updateMove(action[0], action[1], action[2], action[3], false);
    }

    void updateMove(int r1, int c1, int r2, int c2, bool isMyMove) {
        if (r1 < 0) { passed = true; return; }
        int8_t ov = isMyMove ? (int8_t)1 : (int8_t)-1;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++) {
                int o = owner[r][c];
                int d = 0;
                if (isMyMove) d = (o==0)?1:(o==-1)?2:0;
                else          d = (o==0)?-1:(o==1)?-2:0;
                net_score  += d;
                board[r][c]  = 0;
                owner[r][c]  = ov;
            }
        passed = false;
    }
};

// ─── Main ───
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);

    init_zobrist();   // [추가] Zobrist 초기화 (반드시 입력 전에)

    Game game;
    bool first = false;

    while (true) {
        string line;
        if (!getline(cin, line)) break;
        istringstream iss(line);
        string cmd;
        if (!(iss >> cmd)) continue;

        if (cmd == "READY") {
            string turn; iss >> turn;
            first = (turn == "FIRST");
            cout << "OK" << endl;
        } else if (cmd == "INIT") {
            vector<vector<int>> b;
            string row;
            while (iss >> row) {
                vector<int> br;
                for (char ch : row) br.push_back(ch - '0');
                b.push_back(br);
            }
            game = Game(b, first);
        } else if (cmd == "TIME") {
            int mt, ot; iss >> mt >> ot;
            auto ret = game.calculateMove(mt, ot);
            game.updateMove(ret[0], ret[1], ret[2], ret[3], true);
            cout << ret[0] << " " << ret[1] << " " << ret[2] << " " << ret[3] << endl;
        } else if (cmd == "OPP") {
            int r1,c1,r2,c2,t; iss >> r1 >> c1 >> r2 >> c2 >> t;
            game.updateOpponentAction({r1,c1,r2,c2}, t);
        } else if (cmd == "FINISH") {
            break;
        }
    }
    return 0;
}