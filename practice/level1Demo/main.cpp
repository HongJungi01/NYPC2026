// ============================================================================
//  NYPC 2026 - "버섯 밭 점령 게임" Bot
//
//  Architecture (adapted from the Connexion finals engine, perfect-info版):
//   - 완전정보 결정론 2인 제로섬  ->  MCTS 불필요. Negamax + Alpha-Beta 채택.
//   - 상태: 숫자 레이어(num) + 소유권 레이어(owner)  (GAME_RULES §3)
//   - 수생성: 행밴드 + prefix(컬럼합) 누적으로 유효 직사각형 O(R^2 * C^2) 열거
//   - 탐색: iterative deepening Negamax + AB + TT(Zobrist) + killer + 무브오더링
//   - 시간관리: 누적 10초 -> 턴당 예산. 시간초과 시 직전 depth 결과 커밋.
//   - endgame: 트리가 작아지면 deepening이 자연히 종료까지 풀어 정확해짐.
// ============================================================================
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
#include <cstdio>

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

    // 증분 보조 구조 (deriveMoves 가속용): 작업보드 num과 항상 동기 유지.
    //   rowCnt/colTotCnt : 행/열별 버섯 수 -> 활성 바운딩박스(bbox) 산출.
    //   rowCols/rowColN  : 행별 비영(non-zero) 컬럼 목록(순서 무관) -> sparse 누적.
    int rowCnt[R];
    int colTotCnt[C];
    int8_t rowCols[R][C];
    int rowColN[R];

    void rebuildAux() {
        for (int c = 0; c < C; c++) colTotCnt[c] = 0;
        for (int r = 0; r < R; r++) {
            rowCnt[r] = 0; rowColN[r] = 0;
            for (int c = 0; c < C; c++)
                if (num[r][c]) { rowCols[r][rowColN[r]++] = (int8_t)c; rowCnt[r]++; colTotCnt[c]++; }
        }
    }
    inline void auxRemove(int r, int c) {              // 버섯 제거 시 (num: nonzero->0)
        rowCnt[r]--; colTotCnt[c]--;
        int n = rowColN[r];
        for (int i = 0; i < n; i++)
            if (rowCols[r][i] == (int8_t)c) { rowCols[r][i] = rowCols[r][n - 1]; rowColN[r] = n - 1; break; }
    }
    inline void auxRestore(int r, int c) {             // undo 시 (num: 0->nonzero)
        rowCnt[r]++; colTotCnt[c]++;
        rowCols[r][rowColN[r]++] = (int8_t)c;
    }

    // Zobrist: idx 1..9 = 버섯 숫자, 10=제거+소유0, 11=제거+소유1
    uint64_t Zcell[R][C][12];
    uint64_t ZtoMove;

    // TT: flat open-addressing (index = hash & mask, full-key 검증).
    // 수 간에 비우지 않음 -> 동일 포지션 재방문 시 재사용. flag 0 exact /1 lower /2 upper.
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

    // 평가 가중치 (스케일 일치 필수: material/터미널/horizon 동일 단위)
    static const int W_MAT = 100;   // material 1칸 = 100
    // 선턴 이니셔티브(다음 최대 점령) 가중. self-play A/B 결과 효과 없음(neutral~negative):
    //   모든 수가 capture라 quiescence 개념이 없고 수 집합이 양쪽 공유 -> 차분 신호 부재.
    //   기본 0(= material-only, 최강). env W_TEMPO로 재실험 가능.
    int wTempo = 0;

    Solver() {
        mt19937_64 rng(0x9E3779B97F4A7C15ULL);
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++)
                for (int k = 0; k < 12; k++) Zcell[r][c][k] = rng();
        ZtoMove = rng();
        if (const char* e = getenv("W_TEMPO")) wTempo = atoi(e);
        tt.assign((size_t)1 << TT_BITS, TTEnt{ 0, 0, -1, 0, 0 }); // depth=0 -> 빈 슬롯은 컷오프 불가
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
    // genMoves의 band 로직을 리스트/정렬 없이 best만 추출.
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
    // wTempo=0이면 leaf 스캔 생략 -> material-only와 동일 성능.
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
                // 모든 컬럼합>10 이면 제로컬럼이 없다는 뜻 -> 현재 밴드 무효이고
                // r2를 더 키워도 각 컬럼합은 증가만 하므로 전부 무효 -> r2 루프 종료.
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
                    if (on != 0) auxRemove(r, c);        // 버섯 제거 -> aux 갱신
                }
            }
        hash ^= ZtoMove; to_move ^= 1;
    }
    void undo(const Undo& u) {
        to_move = u.pTo; hash = u.pHash; myCnt = u.pMy; oppCnt = u.pOpp;
        for (int i = 0; i < u.cnt; i++) {
            num[u.cr[i]][u.cc[i]] = u.pn[i];
            owner[u.cr[i]][u.cc[i]] = u.po[i];
            if (u.pn[i] != 0) auxRestore(u.cr[i], u.cc[i]);   // 버섯 복원 -> aux 갱신
        }
    }

    // 증분 수생성 --------------------------------------------------------
    // 부모 노드의 유효수 목록(parent)과 방금 적용한 수의 Undo(u, 제거칸 정보 보유)로부터
    // 자식 노드의 유효수 목록(out)을 재계산. 보드는 이미 자식 상태(apply 후)여야 한다.
    //
    //   유효수 = num 레이어만의 함수(소유권 무관). apply는 num을 감소만 시킴.
    //   다음 보드의 유효수 = 생존(survivor) ⊎ 신생(newborn)  [서로소, 합=전체]
    //     생존  = parent 중 제거칸을 하나도 포함하지 않는 수 (내부 불변 -> 그대로 유효)
    //     신생  = 지금 유효 && 제거칸 >=1 포함 (예: 합 11이 안쪽 제거로 10이 됨)
    //   두 집합은 "제거칸 포함" 여부로 정확히 갈리므로 중복 없음.
    void deriveMoves(const vector<Move>& parent, const Undo& u, vector<Move>& out) {
        out.clear();
        // 제거된 버섯 칸 수집 (pn != 0) + 그 바운딩박스 D
        int rcr[R * C], rcc[R * C], nrem = 0;
        int rMinD = R, rMaxD = -1, cMinD = C, cMaxD = -1;
        for (int i = 0; i < u.cnt; i++)
            if (u.pn[i] != 0) {
                int r = u.cr[i], c = u.cc[i];
                rcr[nrem] = r; rcc[nrem] = c; nrem++;
                if (r < rMinD) rMinD = r;  if (r > rMaxD) rMaxD = r;
                if (c < cMinD) cMinD = c;  if (c > cMaxD) cMaxD = c;
            }
        if (nrem == 0) { out = parent; return; }  // 버섯 제거 없음(소유권만) -> 수 집합 불변

        // 1) 생존수: parent 중 제거칸을 포함하지 않는 것. key(즉시점령수)는 불변이라 그대로 복사.
        for (const Move& p : parent) {
            bool hit = false;
            if (!(p.r1 > rMaxD || p.r2 < rMinD || p.c1 > cMaxD || p.c2 < cMinD)) // D박스와 교차할 때만 정밀검사
                for (int k = 0; k < nrem; k++)
                    if (rcr[k] >= p.r1 && rcr[k] <= p.r2 && rcc[k] >= p.c1 && rcc[k] <= p.c2) { hit = true; break; }
            if (!hit) out.push_back(p);
        }

        // 2) 신생수: 현재 bbox 내에서, 제거행을 포함하는 밴드만 스캔.
        //    (신생 사각형의 네 변은 버섯을 닿으므로 사각형 전체가 현재 bbox 안에 있음 -> 완전성 보장)
        int rMin = R, rMax = -1, cMin = C, cMax = -1;
        for (int r = 0; r < R; r++) if (rowCnt[r])    { if (r < rMin) rMin = r; rMax = r; }
        for (int c = 0; c < C; c++) if (colTotCnt[c]) { if (c < cMin) cMin = c; cMax = c; }
        if (rMax < 0) return;  // 버섯 전무

        int colSum[C], colCnt[C], remCol[C];
        // r1 은 제거행 위쪽(<=rMaxD)에서만 시작 가능(밴드가 제거행을 포함하려면 r1<=rMaxD).
        for (int r1 = rMin; r1 <= rMax && r1 <= rMaxD; r1++) {
            for (int c = cMin; c <= cMax; c++) { colSum[c] = 0; colCnt[c] = 0; remCol[c] = 0; }
            for (int r2 = r1; r2 <= rMax; r2++) {
                int n2 = rowColN[r2];                              // sparse 누적: 비영 컬럼만
                for (int k = 0; k < n2; k++) {
                    int c = rowCols[r2][k];
                    if (c >= cMin && c <= cMax) { colSum[c] += num[r2][c]; colCnt[c]++; }
                }
                for (int k = 0; k < nrem; k++)                     // 행 r2의 제거칸 표시
                    if (rcr[k] == r2) { int cc = rcc[k]; if (cc >= cMin && cc <= cMax) remCol[cc]++; }
                if (r2 < rMinD) continue;                          // 밴드가 아직 제거행 미포함
                for (int c1 = cMin; c1 <= cMax; c1++) {
                    if (colCnt[c1] == 0) continue;                 // 왼변에 버섯 필요
                    int s = 0, cntNZ = 0, remSum = 0; bool topP = false, botP = false;
                    for (int c2 = c1; c2 <= cMax; c2++) {
                        s += colSum[c2]; cntNZ += colCnt[c2]; remSum += remCol[c2];
                        if (num[r1][c2]) topP = true;
                        if (num[r2][c2]) botP = true;
                        if (s > 10) break;
                        if (s == 10 && colCnt[c2] && topP && botP && remSum > 0)  // 제거칸 포함 == 신생
                            out.push_back({ (int8_t)r1,(int8_t)c1,(int8_t)r2,(int8_t)c2, cntNZ });
                    }
                }
            }
        }
    }

#ifdef VERIFY_MOVES
    // 디버그: 증분 결과(got)를 전체스캔 기준(genMoves)과 집합 비교. 불일치 시 abort.
    void verifyMoves(const vector<Move>& got) {
        vector<Move> ref; genMoves(ref);
        vector<int> a, b;
        for (auto& m : got) a.push_back(encMove(m));
        for (auto& m : ref) b.push_back(encMove(m));
        sort(a.begin(), a.end()); sort(b.begin(), b.end());
        if (a != b) {
            fprintf(stderr, "[VERIFY_MOVES] MISMATCH derived=%zu ref=%zu\n", got.size(), ref.size());
            abort();
        }
    }
#endif

    // Negamax + AB + TT ---------------------------------------------------
    //   parentList==nullptr -> 루트: 전체스캔(genMoves). 그 외 -> 부모목록+적용수로 증분(deriveMoves).
    int negamax(int depth, int alpha, int beta, int ply,
                const vector<Move>* parentList, const Undo* appliedUndo) {
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
            ttMove = slot.best;   // TT 컷오프 실패 시에만 수생성 -> 증분 비용 절약
        }

        vector<Move>& buf = mv[ply];
#ifdef FORCE_FULLSCAN
        (void)parentList; (void)appliedUndo; genMoves(buf);   // 벤치 기준선: 매 노드 전체스캔
#else
        if (parentList == nullptr) genMoves(buf);
        else                       deriveMoves(*parentList, *appliedUndo, buf);
#endif
#ifdef VERIFY_MOVES
        verifyMoves(buf);
#endif
        if (buf.empty()) return matStm();     // 양쪽 모두 둘 수 없음 = 종료 (정확)

        // 무브 오더링: 즉시 점령수(m.key, 불변 유지) + TT/killer 보너스를 비교 시점에만 가산.
        // (m.key를 직접 더하면 자식 deriveMoves로 복사될 때 키가 오염됨 -> 정렬키만 즉석 계산)
        int k0 = killer[ply][0], k1 = killer[ply][1];
        auto orderKey = [&](const Move& m) -> int {
            int e = encMove(m);
            if (e == ttMove) return m.key + 1000000;
            if (e == k0 || e == k1) return m.key + 5000;
            return m.key;
        };
        sort(buf.begin(), buf.end(),
             [&](const Move& a, const Move& b) { return orderKey(a) > orderKey(b); });

        int best = -INF, bestMove = -1;
        for (size_t i = 0; i < buf.size(); i++) {
            const Move m = buf[i];
            Undo u; apply(m, u);
            int v;
            if (i == 0) {
                v = -negamax(depth - 1, -beta, -alpha, ply + 1, &buf, &u);
            } else {
                v = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1, &buf, &u); // PVS null window
                if (v > alpha && v < beta)
                    v = -negamax(depth - 1, -beta, -alpha, ply + 1, &buf, &u);
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
        // depth-preferred 교체: 같은 포지션이 아니거나 더 깊을 때만 덮어씀.
        if (slot.key != hash || depth >= slot.depth)
            slot = TTEnt{ hash, best, bestMove, (int16_t)depth, flag };
        return best;
    }

    // 루트에서 한 수 결정 ---------------------------------------------------
    Move decide(const State& S, int budget_ms) {
        // 실보드 -> 작업보드 복사
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) { num[r][c] = S.num[r][c]; owner[r][c] = S.owner[r][c]; }
        myCnt = S.myCnt; oppCnt = S.oppCnt; to_move = 0;
        rebuildAux();                         // 증분 보조구조 초기화 (작업보드 기준)

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

        // 남은 버섯 수로 최대 깊이 상한
        int nz = 0;
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (num[r][c]) nz++;
        int maxDepth = min(MAXD - 1, max(1, nz));

        // 시간 없는 즉답: depth1(=탐욕)만이라도 보장
        Move committed = rootMoves[0];
        for (const Move& m : rootMoves) if (m.key > committed.key) committed = m;

        int prevVal = 0; bool haveVal = false;
        for (int d = 1; d <= maxDepth; d++) {
            // aspiration: 직전 값 주변 좁은 창으로 시작, 빗나가면 넓혀서 재탐색
            int a = -INF, b = INF;
            if (haveVal && d >= 3) { a = prevVal - 3 * W_MAT; b = prevVal + 3 * W_MAT; }
            int v = 0;
            for (;;) {
                timedOut = false;
                rootBest = committed;
                v = negamax(d, a, b, 0, nullptr, nullptr);   // 루트: 전체스캔으로 시작
                if (timedOut) break;
                if (v <= a && a > -INF) { a = -INF; continue; }   // fail-low -> 넓힘
                if (v >= b && b <  INF) { b =  INF; continue; }   // fail-high -> 넓힘
                break;
            }
            if (timedOut) break;            // 미완료 depth는 버림
            committed = rootBest;
            prevVal = v; haveVal = true;
            if (Clock::now() >= deadline) break;
        }
        return committed;
    }
};

// ---------------------------------------------------------------------------
// Game: 실보드 관리 + 프로토콜 연결
// ---------------------------------------------------------------------------
struct Game {
    State S;
    bool first = false;
    Solver solver;

    void init(const vector<string>& rows) {
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++)
                S.num[r][c] = (int8_t)(rows[r][c] - '0');
        S.initEmptyOwners();
    }

    int nonzeroCount() const {
        int n = 0;
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (S.num[r][c]) n++;
        return n;
    }

    int decideBudget(int t1) const {
        int avail = max(1, t1 - 250);          // flush/overhead 안전마진
        int nz = nonzeroCount();
        // 자기보정 분할 + "남은 시간의 일정 비율" 상한으로 고갈 방지(TLE 안전).
        int estTurns = max(4, nz / 6);          // 남은 내 턴 추정 (보수적)
        int budget = avail / estTurns;
        budget = min(budget, t1 / 8);           // 한 수에 잔여 시간의 1/8 초과 금지 (고갈 방지)
        budget = max(30, min(budget, 1000));
        return min(budget, avail);
    }

    Move pick(int t1) {
        if (t1 <= 8) {                          // 시간 거의 없음: 탐욕 즉답
            vector<Move> m; solver_genGreedy(m);
            if (m.empty()) return PASS_MOVE;
            Move best = m[0];
            for (auto& x : m) if (x.key > best.key) best = x;
            return best;
        }
        return solver.decide(S, decideBudget(t1));
    }

    // 탐욕 폴백용: 실보드에서 직접 수생성
    void solver_genGreedy(vector<Move>& out) {
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) { solver.num[r][c] = S.num[r][c]; }
        solver.genMoves(out);
    }
};

#if !defined(TEST_MAIN) && !defined(BENCH_MAIN)
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Game game;
    string cmd;
    while (cin >> cmd) {
        if (cmd == "READY") {
            string role; cin >> role;
            game.first = (role == "FIRST");
            cout << "OK\n" << flush;
        } else if (cmd == "INIT") {
            vector<string> rows;
            for (int i = 0; i < R; i++) { string s; cin >> s; rows.push_back(s); }
            game.init(rows);
        } else if (cmd == "TIME") {
            int t1, t2; cin >> t1 >> t2;
            Move mv = game.pick(t1);
            playMove(game.S, mv, 0);            // 내 수 실보드 반영
            cout << (int)mv.r1 << " " << (int)mv.c1 << " "
                 << (int)mv.r2 << " " << (int)mv.c2 << "\n" << flush;
        } else if (cmd == "OPP") {
            int r1, c1, r2, c2, t; cin >> r1 >> c1 >> r2 >> c2 >> t;
            Move m = { (int8_t)r1,(int8_t)c1,(int8_t)r2,(int8_t)c2, 0 };
            playMove(game.S, m, 1);             // 상대 수 반영 (패스면 무시)
        } else if (cmd == "FINISH") {
            break;
        } else {
            string rest; getline(cin, rest);    // 알 수 없는 커맨드 무시
        }
    }
    return 0;
}
#endif

#ifdef TEST_MAIN
// ---------------------------------------------------------------------------
// 정확성 테스트 (TEST_MAIN + VERIFY_MOVES 로 빌드).
//   랜덤 보드에서 무작위 실수(實手)로 진행하며 매 포지션마다 탐색을 돌린다.
//   탐색의 모든 노드에서 verifyMoves()가 deriveMoves 결과를 전체스캔과 대조 -> 불일치 시 abort.
// ---------------------------------------------------------------------------
int main() {
    mt19937 rng(0xC0FFEEu);
    const int GAMES = 80;
    long long positions = 0;
    for (int gi = 0; gi < GAMES; gi++) {
        Game game;
        vector<string> rows;
        for (int r = 0; r < R; r++) {
            string s;
            for (int c = 0; c < C; c++) s += char('1' + rng() % 9);
            rows.push_back(s);
        }
        game.init(rows);

        int mover = 0;
        for (int ply = 0; ply < 80; ply++) {
            // 현재 포지션에서 검증 탐색 (VERIFY_MOVES가 노드마다 대조)
            game.solver.decide(game.S, 25);
            positions++;

            // 실보드를 무작위 유효수로 한 칸 진행 (genMoves는 num만 사용)
            vector<Move> rm;
            for (int r = 0; r < R; r++)
                for (int c = 0; c < C; c++) game.solver.num[r][c] = game.S.num[r][c];
            game.solver.genMoves(rm);
            if (rm.empty()) break;
            playMove(game.S, rm[rng() % rm.size()], mover);
            mover ^= 1;
        }
        if ((gi + 1) % 10 == 0) fprintf(stderr, "  ...game %d ok\n", gi + 1);
    }
    fprintf(stderr, "[TEST_MAIN] OK — %lld positions searched, no move-gen mismatch.\n", positions);
    return 0;
}
#endif

#ifdef BENCH_MAIN
// ---------------------------------------------------------------------------
// 성능 벤치 (BENCH_MAIN). 고정 보드/고정 깊이로 동일 트리를 완전 탐색하고 시간 측정.
//   기본 빌드        = deriveMoves 경로
//   -DFORCE_FULLSCAN = 매 노드 전체스캔 (기준선)
//   동일 트리(노드수 동일)이므로 시간차 = 수생성 비용 차이.
// ---------------------------------------------------------------------------
#ifndef BENCH_DEPTH
#define BENCH_DEPTH 6
#endif
int main() {
    vector<string> rows = {
        "52466443835422223","13254826938785712","87598398564241564",
        "72722154162442227","43968227685821641","27727492381231148",
        "56138649164249527","36281415328768234","62682694189422313",
        "62269566965435457"
    };
    Game game; game.init(rows);
    Solver& s = game.solver;
    // 선택적 사전 진행: 무작위 유효수로 보드를 희소하게 만든 뒤 측정 (중후반 시뮬레이션)
#ifndef BENCH_PREPLAY
#define BENCH_PREPLAY 0
#endif
    {
        mt19937 rng(777);
        for (int p = 0; p < BENCH_PREPLAY; p++) {
            vector<Move> rm;
            for (int r = 0; r < R; r++)
                for (int c = 0; c < C; c++) s.num[r][c] = game.S.num[r][c];
            s.genMoves(rm);
            if (rm.empty()) break;
            playMove(game.S, rm[rng() % rm.size()], p & 1);
        }
    }
    int remain = 0;
    for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (game.S.num[r][c]) remain++;
    fprintf(stderr, "[BENCH] preplay=%d  mushrooms_remaining=%d\n", BENCH_PREPLAY, remain);
    for (int r = 0; r < R; r++)
        for (int c = 0; c < C; c++) { s.num[r][c] = game.S.num[r][c]; s.owner[r][c] = game.S.owner[r][c]; }
    s.myCnt = 0; s.oppCnt = 0; s.to_move = 0;
    s.rebuildAux();
    s.hash = 0;
    for (int r = 0; r < R; r++)
        for (int c = 0; c < C; c++) s.hash ^= s.cellHash(r, c, s.num[r][c], s.owner[r][c]);
    for (auto& k : s.killer) { k[0] = -1; k[1] = -1; }
    s.tt.assign(s.tt.size(), Solver::TTEnt{ 0,0,-1,0,0 });
    s.nodes = 0; s.timedOut = false;
    s.deadline = Clock::now() + chrono::hours(1);   // 사실상 무제한

    s.genMoves(s.mv[0]);
    auto t0 = Clock::now();
    int v = s.negamax(BENCH_DEPTH, -INF, INF, 0, nullptr, nullptr);
    auto t1 = Clock::now();
    double ms = chrono::duration<double, milli>(t1 - t0).count();
#ifdef FORCE_FULLSCAN
    const char* mode = "FULLSCAN";
#else
    const char* mode = "DERIVE  ";
#endif
    fprintf(stderr, "[BENCH %s] depth=%d val=%d nodes=%lld time=%.1fms  Mnodes/s=%.2f\n",
            mode, BENCH_DEPTH, v, s.nodes, ms, s.nodes / ms / 1000.0);
    return 0;
}
#endif
