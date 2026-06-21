// ============================================================================
//  NYPC 2026 - "버섯 밭 점령 게임" Bot  [PONDER 판]
//
//  베이스: submission/main.cpp(현 최고 성적)를 그대로 복사 + "폰더(ponder)" 기능만 추가.
//  엔진/탐색/평가/시간관리는 main.cpp 와 동일(격리 A/B 용). 차이는 아래 PONDER 블록 + 루트 TT 가드뿐.
//
//  Architecture (adapted from the Connexion finals engine, perfect-info版):
//   - 완전정보 결정론 2인 제로섬  ->  MCTS 불필요. Negamax + Alpha-Beta 채택.
//   - 상태: 숫자 레이어(num) + 소유권 레이어(owner)  (GAME_RULES §3)
//   - 수생성: 행밴드 + prefix(컬럼합) 누적으로 유효 직사각형 O(R^2 * C^2) 열거
//   - 탐색: iterative deepening Negamax + AB + TT(Zobrist) + killer + 무브오더링
//   - 시간관리: 누적 10초 -> 턴당 예산. 시간초과 시 직전 depth 결과 커밋.
//   - endgame: 트리가 작아지면 deepening이 자연히 종료까지 풀어 정확해짐.
//
//  ── PONDER (상대 고민 시간 활용) ──────────────────────────────────────────
//   내가 수를 둔 뒤 상대가 고민하는 동안 내 프로세스는 다음 입력을 기다리며 놀고 있다.
//   그 "공짜 wall-clock"(내 시계엔 안 잡힘)에 백그라운드 스레드로 "상대 결정 노드"
//   (= 내 수 직후, 상대가 둘 차례인 포지션)를 그대로 탐색해 TT(치환표)를 채운다.
//   이 노드를 펼치면 상대의 '모든' 후보수 + 그 이후 내 응수들이 한꺼번에 TT 에 들어간다.
//   → 상대가 실제로 어떤 수를 두든, 그 수 직후(내 차례) 포지션이 이미 그 깊이로 TT 에 있다.
//     (단일 예측수만 펼치지 않으므로 "예측 빗나가면 전부 헛수고" 가 없다.)
//
//   ※ 패리티 정렬: TT 키엔 ply 마다 ZtoMove 가 접혀 들어가, 같은 보드라도 '도달 ply 홀짝'이
//      다르면 키가 달라진다. 상대 노드는 다음 실제 탐색의 루트보다 1 ply 앞이므로, 폰더 루트
//      해시에 ZtoMove 를 한 번 더 접어(아래 ponderOppNode) 패리티를 맞춘다. 그러면 폰더가
//      만든 엔트리가 다음 실제 탐색의 노드 키와 정확히 일치한다.
//   ※ 루트 TT 가드: 워밍으로 루트에 깊은 exact 엔트리가 생기면 negamax 가 루트에서 즉시
//      컷오프해 rootBest 를 안 정하는 사고를 막으려, ply==0 에선 TT 컷오프를 끈다(아래).
//   ※ 동기화: 다음 입력(OPP/TIME) 도착 즉시 atomic stopFlag 로 폰더 중단·join → 그 후에만
//      메인이 solver 를 만진다(동시접근 0). TT 는 join 으로 안전 가시화. 폰더는 내 예산 미사용.
//   ※ 토글: env PONDER_OFF 면 폰더 끔(A/B용), PONDER_DEBUG 면 매 수 도달깊이/노드수 stderr.
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
#include <thread>
#include <atomic>

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
    uint64_t ZprevPass;        // "직전 수가 패스"를 TT 키에 반영 (같은 보드라도 값이 달라짐)

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
    // [PONDER] 백그라운드 폰더 스레드를 메인이 즉시 중단시키는 플래그(매 노드 time_up 에서 점검).
    //   폰더 외 일반 탐색에선 항상 false 라 동작 영향 없음.
    std::atomic<bool> stopFlag{ false };
    int reachedDepth = 0;          // [PONDER] 마지막 decide/ponder 가 완료한 최대 깊이(PONDER_DEBUG 용)

    Move rootBest;

    // 평가 가중치 (스케일 일치 필수: material/터미널/horizon 동일 단위)
    static const int W_MAT = 100;   // material 1칸 = 100
    // 선턴 이니셔티브(다음 최대 점령) 가중. self-play A/B 결과 효과 없음(neutral~negative):
    //   모든 수가 capture라 quiescence 개념이 없고 수 집합이 양쪽 공유 -> 차분 신호 부재.
    //   기본 0(= material-only, 최강). env W_TEMPO로 재실험 가능.
    int wTempo = 0;
    // 자발적 패스 게이트: 한 노드 합법수 <= passGate && side-to-move 비열세일 때만 PASS를 후보로.
    //   0 = 패스 비활성(= 기존 엔진과 동일). env PASS_GATE로 설정/튜닝.
    int passGate = 7;

    Solver() {
        mt19937_64 rng(0x9E3779B97F4A7C15ULL);
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++)
                for (int k = 0; k < 12; k++) Zcell[r][c][k] = rng();
        ZtoMove = rng();
        ZprevPass = rng();
        if (const char* e = getenv("W_TEMPO")) wTempo = atoi(e);
        if (const char* e = getenv("PASS_GATE")) passGate = atoi(e);
        tt.assign((size_t)1 << TT_BITS, TTEnt{ 0, 0, -1, 0, 0 }); // depth=0 -> 빈 슬롯은 컷오프 불가
    }

    inline uint64_t cellHash(int r, int c, int8_t n, int8_t o) const {
        if (n != 0) return Zcell[r][c][n];
        return Zcell[r][c][10 + o];
    }
    inline bool time_up() {
        if (timedOut) return true;
        if (stopFlag.load(std::memory_order_relaxed)) { timedOut = true; return true; } // [PONDER] 외부 중단
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
    // 패스 적용: 보드 불변, side-to-move만 토글. u.cnt=0이라 undo()의 셀 복원 루프는 무동작.
    //   deriveMoves(parent, u, child)는 u.cnt=0 -> nrem=0 -> child=parent (수집합 불변).
    void applyPass(Undo& u) {
        u.pMy = myCnt; u.pOpp = oppCnt; u.pHash = hash; u.pTo = (int8_t)to_move; u.cnt = 0;
        hash ^= ZtoMove; to_move ^= 1;
    }

    // 증분 수생성 (moveGen.cpp에서 검증한 로직: 합기준 국소 증분 + 4방향 적응형 앵커) ------
    //  부모 유효수(parent) + 방금 적용한 수의 Undo(u, 제거칸 정보)로 자식 유효수(out)를 재계산.
    //  보드(num)는 이미 자식 상태(apply 후)여야 한다. 유효수 = num 레이어만의 함수.
    //   생존수 = parent 중 제거칸 미포함(내부 불변) -> 키 그대로.  신생수 = 제거칸 포함 && 지금 유효.
    void deriveMoves(const vector<Move>& parent, const Undo& u, vector<Move>& out) {
        out.clear();
        // 제거된 '버섯' 칸(pn!=0)만 수집 + bbox.
        int dr[R * C], dc[R * C], nrem = 0;
        int dRmin = R, dRmax = -1, dCmin = C, dCmax = -1;
        for (int i = 0; i < u.cnt; i++)
            if (u.pn[i] != 0) {
                int r = u.cr[i], c = u.cc[i];
                dr[nrem] = r; dc[nrem] = c; nrem++;
                if (r < dRmin) dRmin = r;
                if (r > dRmax) dRmax = r;
                if (c < dCmin) dCmin = c;
                if (c > dCmax) dCmax = c;
            }
        if (nrem == 0) { out = parent; return; }   // 숫자 변화 없음(소유권만) -> 수 집합 불변

        // 1) 생존수: parent 중 제거칸을 하나도 포함하지 않는 것. key(즉시점령수) 불변 -> 그대로 복사.
        for (const Move& p : parent) {
            bool hit = false;
            if (!(p.r1 > dRmax || p.r2 < dRmin || p.c1 > dCmax || p.c2 < dCmin))
                for (int k = 0; k < nrem; k++)
                    if (dr[k] >= p.r1 && dr[k] <= p.r2 && dc[k] >= p.c1 && dc[k] <= p.c2) { hit = true; break; }
            if (!hit) out.push_back(p);
        }

        // 2) 신생수: 제거칸 포함 && 지금 유효. 제거박스 가까운 모서리에 앵커(4방향) + remSum>0 게이트.
        bool rowFlip = (R - dRmin) < (dRmax + 1);   // true -> 아래쪽 앵커(위로 누적)
        bool colFlip = (C - dCmin) < (dCmax + 1);   // true -> 오른쪽 앵커(왼쪽으로 확장)
        int colSum[C], colCnt[C], remCol[C];

        auto hscan = [&](int br1, int br2) {        // 한 밴드의 가로 스캔 (colFlip로 좌/우 앵커)
            if (!colFlip) {
                for (int a = 0; a <= dCmax; a++) {
                    if (colCnt[a] == 0) continue;
                    int s = 0, cnt = 0, rs = 0; bool tp = false, bp = false;
                    for (int b = a; b < C; b++) {
                        s += colSum[b]; cnt += colCnt[b]; rs += remCol[b];
                        if (num[br1][b]) tp = true;
                        if (num[br2][b]) bp = true;
                        if (s > 10) break;
                        if (s == 10 && colCnt[b] && tp && bp && rs > 0)
                            out.push_back({ (int8_t)br1,(int8_t)a,(int8_t)br2,(int8_t)b, cnt });
                    }
                }
            } else {
                for (int b = C - 1; b >= dCmin; b--) {
                    if (colCnt[b] == 0) continue;
                    int s = 0, cnt = 0, rs = 0; bool tp = false, bp = false;
                    for (int a = b; a >= 0; a--) {
                        s += colSum[a]; cnt += colCnt[a]; rs += remCol[a];
                        if (num[br1][a]) tp = true;
                        if (num[br2][a]) bp = true;
                        if (s > 10) break;
                        if (s == 10 && colCnt[a] && tp && bp && rs > 0)
                            out.push_back({ (int8_t)br1,(int8_t)a,(int8_t)br2,(int8_t)b, cnt });
                    }
                }
            }
        };

        if (!rowFlip) {                              // 위쪽 앵커: br1 0..dRmax, 아래로 누적
            for (int br1 = 0; br1 <= dRmax; br1++) {
                for (int c = 0; c < C; c++) { colSum[c] = 0; colCnt[c] = 0; remCol[c] = 0; }
                for (int br2 = br1; br2 < R; br2++) {
                    for (int c = 0; c < C; c++) { int v = num[br2][c]; colSum[c] += v; if (v) colCnt[c]++; }
                    for (int k = 0; k < nrem; k++) if (dr[k] == br2) remCol[dc[k]]++;
                    if (br2 < dRmin) continue;
                    hscan(br1, br2);
                }
            }
        } else {                                     // 아래쪽 앵커: br2 R-1..dRmin, 위로 누적
            for (int br2 = R - 1; br2 >= dRmin; br2--) {
                for (int c = 0; c < C; c++) { colSum[c] = 0; colCnt[c] = 0; remCol[c] = 0; }
                for (int br1 = br2; br1 >= 0; br1--) {
                    for (int c = 0; c < C; c++) { int v = num[br1][c]; colSum[c] += v; if (v) colCnt[c]++; }
                    for (int k = 0; k < nrem; k++) if (dr[k] == br1) remCol[dc[k]]++;
                    if (br1 > dRmax) continue;
                    hscan(br1, br2);
                }
            }
        }
    }

#ifdef VERIFY_MOVES
    // 디버그: 증분 결과(got)를 전체스캔(genMoves)과 집합 비교. 불일치 시 abort.
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
                const vector<Move>* parentList, const Undo* appliedUndo, bool prevWasPass) {
        if (time_up()) return matStm();
        if (depth == 0) return horizonEval();

        nodes++;
        const int origAlpha = alpha;
        // 보드 hash는 그대로 두고, TT 키에만 prevWasPass 비트를 접어 넣는다(같은 보드/턴이라도
        // 직전 패스 여부로 종료 가능성이 달라 값이 다르므로 분리 필요).
        const uint64_t hkey = prevWasPass ? (hash ^ ZprevPass) : hash;
        int ttMove = -1;
        TTEnt& slot = tt[hkey & ttMask];
        if (slot.key == hkey) {
            // [PONDER] 루트(ply==0)에선 TT 컷오프 금지: 폰더가 채운 깊은 exact 엔트리로 즉시
            //   리턴하면 rootBest 가 안 정해져 둔 수가 깨진다. 루트는 항상 무브 루프를 돌되
            //   ttMove 로 정렬만 가속(자식들은 대부분 TT 히트라 거의 공짜). ply>0 는 기존과 동일.
            if (ply > 0 && slot.depth >= depth) {
                if (slot.flag == 0) return slot.val;
                if (slot.flag == 1 && slot.val >= beta)  return slot.val;
                if (slot.flag == 2 && slot.val <= alpha) return slot.val;
            }
            ttMove = slot.best;
        }

        vector<Move>& buf = mv[ply];
#ifdef FORCE_FULLSCAN
        (void)parentList; (void)appliedUndo; genMoves(buf);   // 벤치 기준선: 매 노드 전체스캔
#else
        if (parentList == nullptr) genMoves(buf);             // 루트: 전체스캔
        else                       deriveMoves(*parentList, *appliedUndo, buf);  // 그 외: 증분
#endif
#ifdef VERIFY_MOVES
        verifyMoves(buf);
#endif
        if (buf.empty()) return matStm();     // 양쪽 모두 둘 수 없음 = 종료 (정확)

        // 무브 오더링: m.key(불변 유지) + TT/killer 보너스를 비교 시점에만 가산.
        // (m.key를 직접 더하면 자식 deriveMoves로 survivor 복사될 때 키가 오염됨 -> 정렬키만 즉석 계산)
        int k0 = killer[ply][0], k1 = killer[ply][1];
        auto orderKey = [&](const Move& m) -> int {
            int e = encMove(m);
            if (e == ttMove) return m.key + 1000000;
            if (e == k0 || e == k1) return m.key + 5000;
            return m.key;
        };
        sort(buf.begin(), buf.end(),
             [&](const Move& a, const Move& b) {
                 int ka = orderKey(a), kb = orderKey(b);
                 if (ka != kb) return ka > kb;
                 return encMove(a) < encMove(b);   // 동률 -> 생성순서 무관 결정적 타이브레이크
             });

        int best = -INF, bestMove = -1;
        for (size_t i = 0; i < buf.size(); i++) {
            const Move m = buf[i];
            Undo u; apply(m, u);
            int v;
            if (i == 0) {
                v = -negamax(depth - 1, -beta, -alpha, ply + 1, &buf, &u, false);
            } else {
                v = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1, &buf, &u, false); // PVS null window
                if (v > alpha && v < beta)
                    v = -negamax(depth - 1, -beta, -alpha, ply + 1, &buf, &u, false);
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

        // 자발적 패스 후보: 게이트(좁은 포지션 && 비열세) 충족 + 컷오프 전일 때만, 마지막에 평가.
        //   실수로 이미 alpha가 높아져 대부분 컷 -> 비용 작음. 동률은 실수 우선(> 만 갱신)이라 무용 패스 회피.
        if (passGate > 0 && (int)buf.size() <= passGate && matStm() >= 0
            && alpha < beta && !timedOut) {
            int v;
            if (prevWasPass) {
                v = matStm();                                    // 연속 패스 -> 종료(내 관점, 정확)
            } else {
                Undo u; applyPass(u);
                v = -negamax(depth - 1, -beta, -alpha, ply + 1, &buf, &u, true);
                undo(u);
            }
            if (timedOut) return best > -INF ? best : matStm();
            if (v > best) {
                best = v; bestMove = encMove(PASS_MOVE);
                if (ply == 0) rootBest = PASS_MOVE;
            }
            if (v > alpha) alpha = v;
        }

        uint8_t flag = (best <= origAlpha) ? 2 : (best >= beta ? 1 : 0);
        // depth-preferred 교체: 같은 포지션이 아니거나 더 깊을 때만 덮어씀.
        if (slot.key != hkey || depth >= slot.depth)
            slot = TTEnt{ hkey, best, bestMove, (int16_t)depth, flag };
        return best;
    }

    // 루트에서 한 수 결정 ---------------------------------------------------
    //   prevWasPass = 상대의 직전 행동이 패스였는지(= 내가 지금 패스하면 연속패스로 종료).
    Move decide(const State& S, int budget_ms, bool prevWasPass) {
        // 실보드 -> 작업보드 복사
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) { num[r][c] = S.num[r][c]; owner[r][c] = S.owner[r][c]; }
        myCnt = S.myCnt; oppCnt = S.oppCnt; to_move = 0;

        hash = 0;
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) hash ^= cellHash(r, c, num[r][c], owner[r][c]);

        for (auto& k : killer) { k[0] = -1; k[1] = -1; }
        nodes = 0; timedOut = false; reachedDepth = 0;
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
                v = negamax(d, a, b, 0, nullptr, nullptr, prevWasPass);   // 루트: 전체스캔으로 시작
                if (timedOut) break;
                if (v <= a && a > -INF) { a = -INF; continue; }   // fail-low -> 넓힘
                if (v >= b && b <  INF) { b =  INF; continue; }   // fail-high -> 넓힘
                break;
            }
            if (timedOut) break;            // 미완료 depth는 버림
            committed = rootBest;
            prevVal = v; haveVal = true; reachedDepth = d;   // [PONDER] 완료 깊이 기록(디버그)
            if (Clock::now() >= deadline) break;
        }
        return committed;
    }

    // [PONDER] 상대 결정 노드(내 수 직후, 상대가 둘 차례) 탐색으로 TT 워밍 — stopFlag 로 종료까지.
    //   핵심: 루트 해시에 ZtoMove 를 한 번 더 접어 패리티를 '다음 실제 탐색'(상대 수 직후, 내 차례)
    //   과 정렬한다. 그러면 상대의 어떤 후보수 X 직후 포지션(내 차례)이 폰더에서 만든 자식 노드와
    //   같은 hkey 를 갖게 되어, 다음 실제 탐색이 그 깊이를 그대로 재사용한다(예측 불필요·헛수고 없음).
    //   to_move=1 로 두므로 negamax/eval 은 상대 관점에서 정확히 전개된다.
    void ponderOppNode(const State& S, bool prevWasPass) {
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) { num[r][c] = S.num[r][c]; owner[r][c] = S.owner[r][c]; }
        myCnt = S.myCnt; oppCnt = S.oppCnt; to_move = 1;        // 상대가 둘 차례

        hash = ZtoMove;                                         // 패리티 오프셋(아래 cellHash 들과 XOR)
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) hash ^= cellHash(r, c, num[r][c], owner[r][c]);

        for (auto& k : killer) { k[0] = -1; k[1] = -1; }
        nodes = 0; timedOut = false; reachedDepth = 0;
        deadline = Clock::now() + chrono::hours(1);             // 사실상 무한 — stopFlag 로 종료

        int nz = 0;
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (num[r][c]) nz++;
        int maxDepth = min(MAXD - 1, max(1, nz));
        for (int d = 1; d <= maxDepth; d++) {
            timedOut = false;
            negamax(d, -INF, INF, 0, nullptr, nullptr, prevWasPass);  // 전 윈도우(워밍 → 엔트리 많이)
            if (timedOut) break;                               // stopFlag 로 중단되면 종료
            reachedDepth = d;
        }
    }
};

// ---------------------------------------------------------------------------
// Game: 실보드 관리 + 프로토콜 연결
// ---------------------------------------------------------------------------
struct Game {
    State S;
    bool first = false;
    Solver solver;
    bool oppLastWasPass = false;   // 상대 직전 수가 패스였는지 (decide의 prevWasPass로 전달)

    // [PONDER] 상대 고민 시간 동안 도는 백그라운드 탐색 스레드.
    std::thread ponderThread;
    bool ponderEnabled = (getenv("PONDER_OFF") == nullptr);   // env PONDER_OFF 면 폰더 끔(A/B용)
    bool ponderDebug   = (getenv("PONDER_DEBUG") != nullptr);  // env PONDER_DEBUG 면 도달깊이 stderr
    ~Game() { stopPonder(); }

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
        return solver.decide(S, decideBudget(t1), oppLastWasPass);
    }

    // 탐욕 폴백용: 실보드에서 직접 수생성
    void solver_genGreedy(vector<Move>& out) {
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) { solver.num[r][c] = S.num[r][c]; }
        solver.genMoves(out);
    }

    // ── PONDER ──────────────────────────────────────────────────────────
    // afterMyMove = 내 수가 반영된 보드(상대가 둘 차례). 그 노드를 통째로 펼쳐 TT 워밍.
    void startPonder(const State& afterMyMove, bool myMoveWasPass) {
        if (!ponderEnabled) return;
        stopPonder();                                          // 혹시 살아있으면 정리(보수적)
        solver.stopFlag.store(false, std::memory_order_relaxed);
        ponderThread = std::thread([this, afterMyMove, myMoveWasPass]() {
            solver.ponderOppNode(afterMyMove, myMoveWasPass);  // 상대 결정 노드 펼치기(패리티 정렬)
        });
    }

    void stopPonder() {
        if (!ponderThread.joinable()) return;
        solver.stopFlag.store(true, std::memory_order_relaxed);
        ponderThread.join();                                   // 폰더 종료 보장 → 이후 메인이 solver 독점
        solver.stopFlag.store(false, std::memory_order_relaxed); // 다음 실제 탐색은 정상 동작
    }
};

#if !defined(TEST_MAIN) && !defined(BENCH_MAIN)
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Game game;
    string cmd;
    while (cin >> cmd) {
        // [PONDER] 어떤 커맨드든 들어왔다는 건 상대 고민이 끝났다는 뜻 → 폰더 즉시 중단·join.
        //   (cin>>cmd 가 블로킹된 동안 폰더가 상대 결정 노드를 펼쳐 TT 를 채웠다. 이후 solver 는 메인 독점.)
        game.stopPonder();
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
            Move mv = game.pick(t1);            // 실제 탐색 — 폰더가 채운 TT로 같은 예산에 더 깊이.
            playMove(game.S, mv, 0);            // 내 수 실보드 반영
            cout << (int)mv.r1 << " " << (int)mv.c1 << " "
                 << (int)mv.r2 << " " << (int)mv.c2 << "\n" << flush;
            if (game.ponderDebug)
                fprintf(stderr, "[ponder] move depth=%d nodes=%lld\n", game.solver.reachedDepth, game.solver.nodes);
            // [PONDER] 내 수 직후(상대 차례) 보드에서 폰더 시작. 다음 입력 올 때까지 백그라운드 탐색.
            game.startPonder(game.S, isPass(mv));
        } else if (cmd == "OPP") {
            int r1, c1, r2, c2, t; cin >> r1 >> c1 >> r2 >> c2 >> t;
            Move m = { (int8_t)r1,(int8_t)c1,(int8_t)r2,(int8_t)c2, 0 };
            playMove(game.S, m, 1);             // 상대 수 반영 (패스면 무시)
            game.oppLastWasPass = (r1 < 0);     // 다음 내 턴의 prevWasPass
        } else if (cmd == "FINISH") {
            break;
        } else {
            string rest; getline(cin, rest);    // 알 수 없는 커맨드 무시
        }
    }
    game.stopPonder();                          // 종료 정리(소멸자도 보장하지만 명시적으로)
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
    const int GAMES = 50;
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
            game.solver.decide(game.S, 10, false);  // VERIFY_MOVES가 노드마다 대조
            positions++;
            vector<Move> rm;
            for (int r = 0; r < R; r++)
                for (int c = 0; c < C; c++) game.solver.num[r][c] = game.S.num[r][c];
            game.solver.genMoves(rm);
            if (rm.empty()) break;
            playMove(game.S, rm[rng() % rm.size()], mover);
            mover ^= 1;
        }
        if ((gi + 1) % 20 == 0) fprintf(stderr, "  ...game %d ok\n", gi + 1);
    }
    fprintf(stderr, "[TEST_MAIN] OK - %lld positions searched, no move-gen mismatch.\n", positions);
    return 0;
}
#endif

#ifdef BENCH_MAIN
// ---------------------------------------------------------------------------
// 성능 벤치 (BENCH_MAIN). 고정 보드/고정 깊이로 동일 트리를 완전 탐색하고 시간 측정.
//   기본 빌드        = deriveMoves 경로
//   -DFORCE_FULLSCAN = 매 노드 전체스캔 (기준선)  -> 동일 트리이므로 시간차 = 수생성 비용 차이.
// ---------------------------------------------------------------------------
#ifndef BENCH_DEPTH
#define BENCH_DEPTH 7
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
    for (int r = 0; r < R; r++)
        for (int c = 0; c < C; c++) { s.num[r][c] = game.S.num[r][c]; s.owner[r][c] = game.S.owner[r][c]; }
    s.myCnt = 0; s.oppCnt = 0; s.to_move = 0;
    s.hash = 0;
    for (int r = 0; r < R; r++)
        for (int c = 0; c < C; c++) s.hash ^= s.cellHash(r, c, s.num[r][c], s.owner[r][c]);
    for (auto& k : s.killer) { k[0] = -1; k[1] = -1; }
    s.tt.assign(s.tt.size(), Solver::TTEnt{ 0,0,-1,0,0 });
    s.nodes = 0; s.timedOut = false;
    s.deadline = Clock::now() + chrono::hours(1);   // 사실상 무제한

    auto t0 = Clock::now();
    int v = s.negamax(BENCH_DEPTH, -INF, INF, 0, nullptr, nullptr, false);
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
