// ============================================================================
//  engine.hpp — NYPC 2026 버섯밭 봇 탐색 엔진 스냅샷
//
//  출처: submission/main.cpp @ b771a9f  (Move / State / playMove / Solver 부분)
//  - submission/main.cpp 는 FROZEN(수정 금지)이고 main() I/O 루프가 박혀 있어
//    그대로 include 할 수 없다. 또한 "로그를 만든 그 엔진"으로 채점해야 충실하므로
//    엔진 클래스만 이 헤더로 그대로 복사한다(중복 감수).
//  - ⚠ submission/main.cpp 의 엔진이 갱신되면 이 스냅샷도 재동기화해야 한다.
//  - main() 과 Game 구조체(프로토콜 결선)는 제외했다. 분석 진입점은 analyze_engine.cpp.
// ============================================================================
#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <unordered_map>
#include <cstdint>
#include <cstdlib>
#include <climits>

using namespace std;
using Clock = chrono::steady_clock;

static const int R = 10;   // rows
static const int C = 17;   // cols
static const int INF = 1000000;
static const int MAXD = 64;

// ---------------------------------------------------------------------------
// Move: 직사각형 (r1,c1)-(r2,c2).  key = 정렬용(즉시 점령 칸수).
// ---------------------------------------------------------------------------
struct Move {
    int8_t r1, c1, r2, c2;
    int key;
};
static inline int encMove(const Move& m) {
    return ((m.r1 * C + m.c1) * (R * C)) + (m.r2 * C + m.c2);
}
static inline bool isPass(const Move& m) { return m.r1 < 0; }
static const Move PASS_MOVE = { -1, -1, -1, -1, 0 };

// ---------------------------------------------------------------------------
// 게임 상태 (실보드 & 탐색보드 공용)
//   num[r][c]   : 1..9 버섯 숫자, 0 = 제거됨
//   owner[r][c] : num!=0 이면 -1(없음), num==0 이면 0(나)/1(상대)
// ---------------------------------------------------------------------------
struct State {
    int8_t num[R][C];
    int8_t owner[R][C];
    int myCnt = 0, oppCnt = 0;

    void initEmptyOwners() {
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) owner[r][c] = -1;
        myCnt = oppCnt = 0;
    }
};

// 실보드에 수를 직접 반영 (undo 불필요).  mover: 0=나, 1=상대
static void playMove(State& S, const Move& m, int mover) {
    if (isPass(m)) return;
    for (int r = m.r1; r <= m.r2; r++)
        for (int c = m.c1; c <= m.c2; c++) {
            if (S.num[r][c] != 0) {                  // 버섯 점령 -> 제거 + 소유
                S.num[r][c] = 0;
                S.owner[r][c] = (int8_t)mover;
                if (mover == 0) S.myCnt++; else S.oppCnt++;
            } else if (S.owner[r][c] != mover) {     // 이미 제거됨 -> 소유권 탈취
                int8_t oo = S.owner[r][c];
                S.owner[r][c] = (int8_t)mover;
                if (mover == 0) { S.myCnt++; if (oo == 1) S.oppCnt--; }
                else            { S.oppCnt++; if (oo == 0) S.myCnt--; }
            }
        }
}

// ---------------------------------------------------------------------------
// 탐색 엔진
// ---------------------------------------------------------------------------
struct Solver {
    // 작업용 보드 (실보드 복사본)
    int8_t num[R][C];
    int8_t owner[R][C];
    int myCnt, oppCnt;
    int to_move;            // 0=나, 1=상대
    uint64_t hash;

    // Zobrist: idx 1..9 = 버섯 숫자, 10=제거+소유0, 11=제거+소유1
    uint64_t Zcell[R][C][12];
    uint64_t ZtoMove;

    // TT: flat open-addressing (index = hash & mask, full-key 검증).
    struct TTEnt { uint64_t key; int val; int best; int16_t depth; uint8_t flag; };
    static const int TT_BITS = 19;            // 2^19 = 512K 엔트리 (~12MB, 캐시 친화)
    vector<TTEnt> tt;
    uint64_t ttMask = (1ULL << TT_BITS) - 1;

    array<array<int, 2>, MAXD> killer;
    vector<Move> mv[MAXD];          // ply별 무브 버퍼 재사용

    Clock::time_point deadline;
    bool timedOut = false;
    long long nodes = 0;

    Move rootBest;

    // 평가 가중치
    static const int W_MAT = 100;   // material 1칸 = 100
    int wTempo = 0;

    Solver() {
        mt19937_64 rng(0x9E3779B97F4A7C15ULL);
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++)
                for (int k = 0; k < 12; k++) Zcell[r][c][k] = rng();
        ZtoMove = rng();
        if (const char* e = getenv("W_TEMPO")) wTempo = atoi(e);
        tt.assign((size_t)1 << TT_BITS, TTEnt{ 0, 0, -1, 0, 0 });
    }

    inline uint64_t cellHash(int r, int c, int8_t n, int8_t o) const {
        if (n != 0) return Zcell[r][c][n];
        return Zcell[r][c][10 + o];
    }
    inline bool time_up() {
        if (timedOut) return true;
        if (Clock::now() >= deadline) { timedOut = true; return true; }
        return false;
    }
    // side-to-move 관점 material (스케일 적용). 터미널/시간초과에서 사용 = 정확.
    inline int matStm() const {
        int e = myCnt - oppCnt;           // 항상 나(player0) 관점
        return (to_move == 0 ? e : -e) * W_MAT;
    }

    // 현재 보드에서 side-to-move가 즉시 점령 가능한 최대 칸수 (이니셔티브 신호).
    int scanBestCapture() const {
        int colSum[C], colCnt[C];
        int best = 0;
        for (int r1 = 0; r1 < R; r1++) {
            for (int c = 0; c < C; c++) { colSum[c] = 0; colCnt[c] = 0; }
            for (int r2 = r1; r2 < R; r2++) {
                for (int c = 0; c < C; c++) {
                    int v = num[r2][c];
                    colSum[c] += v;
                    if (v) colCnt[c]++;
                }
                for (int c1 = 0; c1 < C; c1++) {
                    if (colCnt[c1] == 0) continue;
                    int s = 0, cntNZ = 0; bool topP = false, botP = false;
                    for (int c2 = c1; c2 < C; c2++) {
                        s += colSum[c2];
                        cntNZ += colCnt[c2];
                        if (num[r1][c2]) topP = true;
                        if (num[r2][c2]) botP = true;
                        if (s > 10) break;
                        if (s == 10 && colCnt[c2] && topP && botP)
                            if (cntNZ > best) best = cntNZ;
                    }
                }
            }
        }
        return best;
    }

    // horizon(depth==0) 전용 휴리스틱: material + 선턴 이니셔티브.
    inline int horizonEval() const {
        return wTempo ? matStm() + wTempo * scanBestCapture() : matStm();
    }

    // 유효 직사각형 전부 생성 (현재 num 레이어 기준)
    void genMoves(vector<Move>& out) {
        out.clear();
        int colSum[C], colCnt[C];
        for (int r1 = 0; r1 < R; r1++) {
            for (int c = 0; c < C; c++) { colSum[c] = 0; colCnt[c] = 0; }
            for (int r2 = r1; r2 < R; r2++) {
                int minAll = INT_MAX;
                for (int c = 0; c < C; c++) {
                    int v = num[r2][c];
                    colSum[c] += v;
                    if (v) colCnt[c]++;
                    if (colSum[c] < minAll) minAll = colSum[c];
                }
                if (minAll > 10) break;
                for (int c1 = 0; c1 < C; c1++) {
                    if (colCnt[c1] == 0) continue;        // 왼변: c1열에 버섯 필요
                    int s = 0, cntNZ = 0;
                    bool topP = false, botP = false;
                    for (int c2 = c1; c2 < C; c2++) {
                        s += colSum[c2];
                        cntNZ += colCnt[c2];
                        if (num[r1][c2]) topP = true;     // 위변
                        if (num[r2][c2]) botP = true;     // 아래변
                        if (s > 10) break;
                        if (s == 10 && colCnt[c2] && topP && botP) {
                            out.push_back({ (int8_t)r1,(int8_t)c1,(int8_t)r2,(int8_t)c2, cntNZ });
                        }
                    }
                }
            }
        }
    }

    // apply / undo --------------------------------------------------------
    struct Undo {
        int cnt;
        uint8_t cr[R * C], cc[R * C];
        int8_t  pn[R * C], po[R * C];
        int pMy, pOpp; uint64_t pHash; int8_t pTo;
    };
    void apply(const Move& m, Undo& u) {
        u.pMy = myCnt; u.pOpp = oppCnt; u.pHash = hash; u.pTo = (int8_t)to_move; u.cnt = 0;
        int mover = to_move;
        for (int r = m.r1; r <= m.r2; r++)
            for (int c = m.c1; c <= m.c2; c++) {
                int8_t on = num[r][c], oo = owner[r][c];
                int8_t nn = on, no = oo; bool ch = false;
                if (on != 0) {
                    ch = true; nn = 0; no = (int8_t)mover;
                    if (mover == 0) myCnt++; else oppCnt++;
                } else if (oo != mover) {
                    ch = true; no = (int8_t)mover;
                    if (mover == 0) { myCnt++; if (oo == 1) oppCnt--; }
                    else            { oppCnt++; if (oo == 0) myCnt--; }
                }
                if (ch) {
                    hash ^= cellHash(r, c, on, oo);
                    u.cr[u.cnt] = (uint8_t)r; u.cc[u.cnt] = (uint8_t)c;
                    u.pn[u.cnt] = on; u.po[u.cnt] = oo; u.cnt++;
                    num[r][c] = nn; owner[r][c] = no;
                    hash ^= cellHash(r, c, nn, no);
                }
            }
        hash ^= ZtoMove; to_move ^= 1;
    }
    void undo(const Undo& u) {
        to_move = u.pTo; hash = u.pHash; myCnt = u.pMy; oppCnt = u.pOpp;
        for (int i = 0; i < u.cnt; i++) {
            num[u.cr[i]][u.cc[i]] = u.pn[i];
            owner[u.cr[i]][u.cc[i]] = u.po[i];
        }
    }

    // 패스 적용/되돌리기 (보드 불변, side-to-move만 토글) — 분석에서 패스 후보 평가용
    void applyPass(Undo& u) {
        u.pMy = myCnt; u.pOpp = oppCnt; u.pHash = hash; u.pTo = (int8_t)to_move; u.cnt = 0;
        hash ^= ZtoMove; to_move ^= 1;
    }

    // Negamax + AB + TT ---------------------------------------------------
    int negamax(int depth, int alpha, int beta, int ply) {
        if (time_up()) return matStm();
        if (depth == 0) return horizonEval();

        nodes++;
        const int origAlpha = alpha;
        int ttMove = -1;
        TTEnt& slot = tt[hash & ttMask];
        if (slot.key == hash) {
            if (slot.depth >= depth) {
                if (slot.flag == 0) return slot.val;
                if (slot.flag == 1 && slot.val >= beta)  return slot.val;
                if (slot.flag == 2 && slot.val <= alpha) return slot.val;
            }
            ttMove = slot.best;
        }

        vector<Move>& buf = mv[ply];
        genMoves(buf);
        if (buf.empty()) return matStm();     // 양쪽 모두 둘 수 없음 = 종료 (정확)

        // 무브 오더링: 즉시 점령수 + TT/killer 보너스
        int k0 = killer[ply][0], k1 = killer[ply][1];
        for (Move& m : buf) {
            int e = encMove(m);
            if (e == ttMove) m.key += 1000000;
            else if (e == k0 || e == k1) m.key += 5000;
        }
        sort(buf.begin(), buf.end(), [](const Move& a, const Move& b) { return a.key > b.key; });

        int best = -INF, bestMove = -1;
        for (size_t i = 0; i < buf.size(); i++) {
            const Move m = buf[i];
            Undo u; apply(m, u);
            int v;
            if (i == 0) {
                v = -negamax(depth - 1, -beta, -alpha, ply + 1);
            } else {
                v = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1); // PVS null window
                if (v > alpha && v < beta)
                    v = -negamax(depth - 1, -beta, -alpha, ply + 1);
            }
            undo(u);
            if (timedOut) return best > -INF ? best : matStm();

            if (v > best) {
                best = v; bestMove = encMove(m);
                if (ply == 0) rootBest = m;
            }
            if (v > alpha) alpha = v;
            if (alpha >= beta) {
                int e = encMove(m);
                if (e != killer[ply][0]) { killer[ply][1] = killer[ply][0]; killer[ply][0] = e; }
                break;
            }
        }

        uint8_t flag = (best <= origAlpha) ? 2 : (best >= beta ? 1 : 0);
        if (slot.key != hash || depth >= slot.depth)
            slot = TTEnt{ hash, best, bestMove, (int16_t)depth, flag };
        return best;
    }

    // 분석/탐색 공용: 외부에서 채운 num/owner/myCnt/oppCnt/to_move 로부터 hash·killer 초기화
    void primeFromBoard() {
        hash = 0;
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) hash ^= cellHash(r, c, num[r][c], owner[r][c]);
        for (auto& k : killer) { k[0] = -1; k[1] = -1; }
        nodes = 0; timedOut = false;
    }

    // 루트에서 한 수 결정 ---------------------------------------------------
    Move decide(const State& S, int budget_ms) {
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) { num[r][c] = S.num[r][c]; owner[r][c] = S.owner[r][c]; }
        myCnt = S.myCnt; oppCnt = S.oppCnt; to_move = 0;

        hash = 0;
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) hash ^= cellHash(r, c, num[r][c], owner[r][c]);

        for (auto& k : killer) { k[0] = -1; k[1] = -1; }
        nodes = 0; timedOut = false;
        auto t0 = Clock::now();
        deadline = t0 + chrono::milliseconds(max(1, budget_ms));

        vector<Move> rootMoves;
        genMoves(rootMoves);
        if (rootMoves.empty()) return PASS_MOVE;   // 둘 수 없음 -> 패스

        int nz = 0;
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (num[r][c]) nz++;
        int maxDepth = min(MAXD - 1, max(1, nz));

        Move committed = rootMoves[0];
        for (const Move& m : rootMoves) if (m.key > committed.key) committed = m;

        int prevVal = 0; bool haveVal = false;
        for (int d = 1; d <= maxDepth; d++) {
            int a = -INF, b = INF;
            if (haveVal && d >= 3) { a = prevVal - 3 * W_MAT; b = prevVal + 3 * W_MAT; }
            int v = 0;
            for (;;) {
                timedOut = false;
                rootBest = committed;
                v = negamax(d, a, b, 0);
                if (timedOut) break;
                if (v <= a && a > -INF) { a = -INF; continue; }
                if (v >= b && b <  INF) { b =  INF; continue; }
                break;
            }
            if (timedOut) break;
            committed = rootBest;
            prevVal = v; haveVal = true;
            if (Clock::now() >= deadline) break;
        }
        return committed;
    }
};
