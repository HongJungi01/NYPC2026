// ============================================================================
//  NYPC 2026 - 버섯밭 점령 게임 / 기반 도구 T4: 노드 생성 (node generation)
//
//  moveGen → nodeGen: 생성기는 탐색 트리의 '노드 생성기'다. 각 유효수(=자식 노드)에
//  그 수가 만드는 '점수 변화량 Δ(cap, steal)'를 함께 부착해 낸다.
//   - 수의 존재(유효성)  = num 레이어만의 함수 (180만 포지션 검증된 코어, 불변).
//   - 노드의 값 Δ        = owner 레이어 의존 주석 한 겹 (생성 시점 산출, 캐싱 안 함).
//
//  이 파일 하나로 "참조 구현 + 차분 검증 하니스"를 모두 담는다.
//   - 모드 A (fullScan)       : 행밴드 압축 전체 스캔. 유효수 집합의 정답 기준선(오라클).
//   - 모드 B (IncrementalNode): 합기준 국소 증분. 유효수 집합 유지 (채택됨).
//   - fullScanNodes           : 모드 A에 colOpp[c] 누적기 하나를 얹어 각 노드에 Δ 부착.
//   - scoreDeltaOracle        : 직사각형의 (cap,steal) 직접 집계 = playMove 점수규칙 오라클.
//   - bruteForceScan          : isValid 4중 루프(규칙 §3 직역). 오라클의 오라클.
//
//  검증 게이트: 무작위/적대적 보드 + 무작위 소유권에서 임의 수 시퀀스를 진행하며, 매 수마다
//   (1) "모드 B 집합 == 모드 A 집합", (2) "nodeGen Δ == playMove 오라클" 을 비교.
//   단 한 건이라도 불일치하면 덤프 후 abort. 불일치 0건이어야 채택한다.
//
//  빌드: g++ -O2 -std=c++17 nodeGen.cpp -o nodeGen
//   (네거티브 컨트롤: -DNODEGEN_NEG_STEAL 로 steal 누락을 주입하면 하니스가 즉시 abort 해야 함.)
//  실행: ./nodeGen [seed] [target_positions]
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <set>
#include <utility>
#include <iterator>
#include <algorithm>
#include <random>

using namespace std;

static const int R = 10;     // 행
static const int C = 17;     // 열
static const int TARGET = 10; // 목표 합

// ── Move 인코딩: (r1,c1,r2,c2) → 고유 정수 (집합 비교/정렬용) ──
static inline int encMove(int r1, int c1, int r2, int c2) {
    return ((r1 * C + c1) * (R * C)) + (r2 * C + c2);
}
static inline void decMove(int e, int& r1, int& c1, int& r2, int& c2) {
    int b = e % (R * C), a = e / (R * C);
    r1 = a / C; c1 = a % C; r2 = b / C; c2 = b % C;
}

// ── 유효성 검사 (규칙 §3 그대로) — 순수 브루트포스 기준선용 ──
//    합=10  &&  네 변(위/아래 행, 좌/우 열) 각각에 0이 아닌 칸 ≥ 1.
static bool isValid(const int num[R][C], int r1, int c1, int r2, int c2) {
    int sum = 0; bool rt = false, rb = false, cl = false, cr = false;
    for (int r = r1; r <= r2; r++)
        for (int c = c1; c <= c2; c++) {
            int v = num[r][c];
            if (v) {
                sum += v;
                if (r == r1) rt = true;
                if (r == r2) rb = true;
                if (c == c1) cl = true;
                if (c == c2) cr = true;
            }
        }
    return sum == TARGET && rt && rb && cl && cr;
}

// ── 순수 브루트포스 스캔 (오라클의 오라클; 가끔만 호출) ──
static void bruteForceScan(const int num[R][C], set<int>& out) {
    out.clear();
    for (int r1 = 0; r1 < R; r1++)
        for (int r2 = r1; r2 < R; r2++)
            for (int c1 = 0; c1 < C; c1++)
                for (int c2 = c1; c2 < C; c2++)
                    if (isValid(num, r1, c1, r2, c2))
                        out.insert(encMove(r1, c1, r2, c2));
}

// ── 한 밴드 [r1,r2]의 가로 스캔 (모드 A·B 공유) ──
//    colSum[c]/colCnt[c] : 밴드 내 c열의 (숫자합, 버섯수). 호출 전 누적돼 있어야 함.
//    범위 제한은 거리가 아니라 합: s>TARGET 이면 즉시 break.
static inline void scanBandRow(int r1, int r2, const int num[R][C],
                               const int colSum[C], const int colCnt[C],
                               set<int>& out) {
    for (int c1 = 0; c1 < C; c1++) {
        if (colCnt[c1] == 0) continue;            // 왼변: c1열에 버섯 필요
        int s = 0; bool topP = false, botP = false;
        for (int c2 = c1; c2 < C; c2++) {
            s += colSum[c2];
            if (num[r1][c2]) topP = true;          // 위변
            if (num[r2][c2]) botP = true;          // 아래변
            if (s > TARGET) break;                 // ★ 합기준 범위 제한 (가로) ★
            if (s == TARGET && colCnt[c2] && topP && botP) // 오른변 = colCnt[c2]>0
                out.insert(encMove(r1, c1, r2, c2));
        }
    }
}

// ── 모드 A: 행밴드 압축 전체 스캔 (오라클) ──  O(R^2 * C * 가로스캔)
static void fullScan(const int num[R][C], set<int>& out) {
    out.clear();
    int colSum[C], colCnt[C];
    for (int r1 = 0; r1 < R; r1++) {
        for (int c = 0; c < C; c++) { colSum[c] = 0; colCnt[c] = 0; }
        for (int r2 = r1; r2 < R; r2++) {
            for (int c = 0; c < C; c++) {
                int v = num[r2][c];
                colSum[c] += v;
                if (v) colCnt[c]++;
            }
            scanBandRow(r1, r2, num, colSum, colCnt, out);
        }
    }
}

// ============================================================================
//  nodeGen 주석 — 각 유효수(노드)에 점수 변화량 Δ(cap, steal)를 부착.
//   cap   : 새로 점령하는 버섯 칸 수 (num != 0)
//   steal : 이미 0이며 '상대' 소유인 칸 수 (탈취 → 나 +1 / 상대 -1)
//   margin Δ = cap + 2*steal  (탈취가 2배 가치).  유효성(수의 존재)은 num-only로 불변;
//   Δ만 owner 레이어에 의존하는 주석 한 겹.
// ============================================================================
struct Node { int r1, c1, r2, c2; int cap, steal; };

// 오라클: 직사각형에 mover가 두었을 때의 (cap, steal)을 직접 집계.
//  engine.hpp playMove(L66-81)의 점수 규칙과 동치 — 버섯이면 점령(cap),
//  이미 0인 '상대' 소유 칸이면 탈취(steal). (owner==mover 재점령은 변화 0.)
static inline void scoreDeltaOracle(const int num[R][C], const int owner[R][C], int mover,
                                    int r1, int c1, int r2, int c2, int& cap, int& steal) {
    const int opp = 1 - mover;
    cap = 0; steal = 0;
    for (int r = r1; r <= r2; r++)
        for (int c = c1; c <= c2; c++) {
            if (num[r][c] != 0)          cap++;
            else if (owner[r][c] == opp) steal++;
        }
}

// nodeGen 전체 스캔: fullScan과 같은 유효수 집합을 찾되, 밴드에 colOpp[c] 누적기 하나를
//  더 얹어 각 노드의 (cap, steal)을 O(1)로 동반 산출. (생성 시점 주석 — 캐싱하지 않음.)
static void fullScanNodes(const int num[R][C], const int owner[R][C], int mover,
                          std::vector<Node>& out) {
    out.clear();
    const int opp = 1 - mover;
    int colSum[C], colCnt[C], colOpp[C];      // colOpp[c]: 밴드 내 c열의 (num==0 && owner==opp) 칸 수
    for (int r1 = 0; r1 < R; r1++) {
        for (int c = 0; c < C; c++) { colSum[c] = 0; colCnt[c] = 0; colOpp[c] = 0; }
        for (int r2 = r1; r2 < R; r2++) {
            for (int c = 0; c < C; c++) {
                int v = num[r2][c];
                colSum[c] += v;
                if (v) colCnt[c]++;
                else if (owner[r2][c] == opp) colOpp[c]++;
            }
            for (int c1 = 0; c1 < C; c1++) {
                if (colCnt[c1] == 0) continue;          // 왼변: c1열에 버섯 필요
                int s = 0, cap = 0, steal = 0; bool topP = false, botP = false;
                for (int c2 = c1; c2 < C; c2++) {
                    s     += colSum[c2];
                    cap   += colCnt[c2];                // 러닝합 = 직사각형 내 버섯 칸 수
                    steal += colOpp[c2];                // 러닝합 = 직사각형 내 상대 소유 0칸 수
                    if (num[r1][c2]) topP = true;       // 위변
                    if (num[r2][c2]) botP = true;       // 아래변
                    if (s > TARGET) break;              // ★ 합기준 범위 제한 ★
                    if (s == TARGET && colCnt[c2] && topP && botP)
#ifdef NODEGEN_NEG_STEAL
                        out.push_back({ r1, c1, r2, c2, cap, 0 });      // 네거티브 컨트롤: steal 누락
#else
                        out.push_back({ r1, c1, r2, c2, cap, steal });
#endif
                }
            }
        }
    }
}

// 소유권 갱신(= engine.hpp playMove의 owner 부분). 반드시 inc.applyMove(버섯→0) '전에' 호출.
//  버섯(num!=0)은 mover가 점령, 이미 0인 '상대' 소유 칸은 탈취 → owner=mover.
static void applyOwner(int owner[R][C], const int num[R][C], int mover,
                       int r1, int c1, int r2, int c2) {
    for (int r = r1; r <= r2; r++)
        for (int c = c1; c <= c2; c++)
            if (num[r][c] != 0 || owner[r][c] != mover) owner[r][c] = mover;
}

// ============================================================================
//  모드 B: 합기준 국소 증분 유지기
//   - num   : 현재 보드(숫자 레이어)
//   - moves : 현재 유효수 집합 (encMove). init 후 항상 최신 상태 유지가 목표.
// ============================================================================
struct IncrementalNodeGen {
    int num[R][C];
    set<int> moves;

    void init(const int b[R][C]) {
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) num[r][c] = b[r][c];
        fullScan(num, moves);               // 첫 수: 전체 열거 후 저장
    }

    // 직사각형 (r1,c1)-(r2,c2) 영역의 버섯을 제거하고 집합을 증분 갱신.
    //  (적용 수가 sum=10 유효수일 필요는 없음 — 증분 로직은 '제거된 칸 D'에만 의존.)
    void applyMove(int r1, int c1, int r2, int c2) {
        // 1) D = 영역 내 현재 비영(nonzero) 칸 수집 + 제거. 제거칸 bbox [dRmin,dRmax]×[dCmin,dCmax] 기록.
        vector<pair<int,int>> D;
        int dRmin = R, dRmax = -1, dCmin = C, dCmax = -1;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (num[r][c]) {
                    D.push_back({r, c});
                    num[r][c] = 0;
                    if (r < dRmin) dRmin = r;
                    if (r > dRmax) dRmax = r;
                    if (c < dCmin) dCmin = c;
                    if (c > dCmax) dCmax = c;
                }
        if (D.empty()) return;              // 숫자 변화 없음(소유권만) → 집합 불변

        // 2) 생존수 처리: D 칸을 하나라도 포함한 기존 수는 버섯을 잃어 무효 → 삭제.
        //    ★ 반드시 신생수 추가보다 먼저 ★ (신생수도 D를 포함하므로 순서가 뒤집히면 같이 지워짐)
        for (auto it = moves.begin(); it != moves.end();) {
            int mr1, mc1, mr2, mc2; decMove(*it, mr1, mc1, mr2, mc2);
            bool hit = false;
            if (!(mr1 > dRmax || mr2 < dRmin))          // 행 bbox로 빠른 배제
                for (auto& p : D)
                    if (p.first >= mr1 && p.first <= mr2 &&
                        p.second >= mc1 && p.second <= mc2) { hit = true; break; }
            if (hit) it = moves.erase(it); else ++it;
        }

        // 3) 신생수 추가: 제거박스에 **가까운 모서리에 앵커**를 맞춰(4방향) 재누적을 최소화하며 재유도.
        //    방향과 무관하게 같은 신생수 집합 — 세로 br1<=dRmax && br2>=dRmin, 가로 c1<=dCmax && c2>=dCmin,
        //    윈도우 remSum>0 게이트로 신생수만 삽입. (생존수는 2단계서 안 지워져 이미 집합에 있음.)
        //    rowFlip/colFlip: outer 반복(=colSum reset/재누적) 수가 적은 쪽(=박스에 가까운 변)을 앵커로.
        bool rowFlip = (R - dRmin) < (dRmax + 1);   // true → 아래쪽 앵커(위로 누적)
        bool colFlip = (C - dCmin) < (dCmax + 1);   // true → 오른쪽 앵커(왼쪽으로 확장)
        int colSum[C], colCnt[C], remCol[C];        // 밴드 [br1,br2]의 열 누적(합/버섯수/제거칸수)

        // 한 밴드 [br1,br2]의 가로 스캔(colFlip로 좌/우 앵커 분기). 어느 쪽이든 실좌표 그대로 emit.
        auto hscan = [&](int br1, int br2) {
            if (!colFlip) {                          // 왼쪽 앵커: 왼끝 a∈[0,dCmax], 오른쪽으로 확장
                for (int a = 0; a <= dCmax; a++) {
                    if (colCnt[a] == 0) continue;    // 왼변 버섯 필요
                    int s = 0, rs = 0; bool tp = false, bp = false;
                    for (int b = a; b < C; b++) {
                        s += colSum[b]; rs += remCol[b];
                        if (num[br1][b]) tp = true;
                        if (num[br2][b]) bp = true;
                        if (s > TARGET) break;
                        if (s == TARGET && colCnt[b] && tp && bp && rs > 0)
                            moves.insert(encMove(br1, a, br2, b));
                    }
                }
            } else {                                 // 오른쪽 앵커: 오른끝 b∈[dCmin,C-1], 왼쪽으로 확장
                for (int b = C - 1; b >= dCmin; b--) {
                    if (colCnt[b] == 0) continue;    // 오른변 버섯 필요
                    int s = 0, rs = 0; bool tp = false, bp = false;
                    for (int a = b; a >= 0; a--) {
                        s += colSum[a]; rs += remCol[a];
                        if (num[br1][a]) tp = true;
                        if (num[br2][a]) bp = true;
                        if (s > TARGET) break;
                        if (s == TARGET && colCnt[a] && tp && bp && rs > 0)
                            moves.insert(encMove(br1, a, br2, b));
                    }
                }
            }
        };

        // 밴드 누적(rowFlip로 위/아래 앵커 분기). 둘 다 [br1,br2]의 colSum/colCnt/remCol를 동일 구성.
        if (!rowFlip) {                              // 위쪽 앵커: 윗변 br1∈[0,dRmax], 아래로 누적
            for (int br1 = 0; br1 <= dRmax; br1++) {
                for (int c = 0; c < C; c++) { colSum[c] = 0; colCnt[c] = 0; remCol[c] = 0; }
                for (int br2 = br1; br2 < R; br2++) {
                    for (int c = 0; c < C; c++) { int v = num[br2][c]; colSum[c] += v; if (v) colCnt[c]++; }
                    for (auto& p : D) if (p.first == br2) remCol[p.second]++;
                    if (br2 < dRmin) continue;       // 밴드가 아직 D 행 미포함
                    hscan(br1, br2);
                }
            }
        } else {                                     // 아래쪽 앵커: 아랫변 br2∈[dRmin,R-1], 위로 누적
            for (int br2 = R - 1; br2 >= dRmin; br2--) {
                for (int c = 0; c < C; c++) { colSum[c] = 0; colCnt[c] = 0; remCol[c] = 0; }
                for (int br1 = br2; br1 >= 0; br1--) {
                    for (int c = 0; c < C; c++) { int v = num[br1][c]; colSum[c] += v; if (v) colCnt[c]++; }
                    for (auto& p : D) if (p.first == br1) remCol[p.second]++;
                    if (br1 > dRmax) continue;       // 밴드가 아직 D 행 미포함
                    hscan(br1, br2);
                }
            }
        }
    }
};

// ============================================================================
//  차분 검증 하니스
// ============================================================================
static void dumpBoard(const int num[R][C]) {
    fprintf(stderr, "board:\n");
    for (int r = 0; r < R; r++) {
        for (int c = 0; c < C; c++) fprintf(stderr, "%d", num[r][c]);
        fprintf(stderr, "\n");
    }
}
static void dumpMove(const char* tag, int e) {
    int r1, c1, r2, c2; decMove(e, r1, c1, r2, c2);
    fprintf(stderr, "  %s (%d,%d)-(%d,%d)\n", tag, r1, c1, r2, c2);
}
static void dumpDiff(const set<int>& inc, const set<int>& exp) {
    for (int e : inc) if (!exp.count(e)) dumpMove("ONLY-IN-INC ", e);
    for (int e : exp) if (!inc.count(e)) dumpMove("ONLY-IN-FULL", e);
}

// 적대적 보드: 0-복도를 심어 와이드 신생수를 유도하기 쉽게 만든다.
static void makeAdversarial(int board[R][C], mt19937& rng) {
    for (int r = 0; r < R; r++)
        for (int c = 0; c < C; c++)
            // 작은 값(1~3) 위주 → 합 10 직사각형이 넓게 퍼지기 쉬움
            board[r][c] = 1 + rng() % 3;
}

int main(int argc, char** argv) {
    unsigned seed = (argc > 1) ? (unsigned)strtoul(argv[1], nullptr, 10) : 0xC0FFEEu;
    long long TARGET_POS = (argc > 2) ? strtoll(argv[2], nullptr, 10) : 150000LL;
    mt19937 rng(seed);

    long long positions = 0;
    long long sinceBrute = 0;
    const int  BRUTE_EVERY = 400;   // fullScan 자체를 가끔 브루트포스로 교차검증

    for (int g = 0; positions < TARGET_POS; g++) {
        int board[R][C];
        // 보드 다양화: 절반은 균등 1~9, 절반은 적대적(작은 값) 보드
        if (g & 1)
            for (int r = 0; r < R; r++)
                for (int c = 0; c < C; c++) board[r][c] = 1 + rng() % 9;
        else
            makeAdversarial(board, rng);

        IncrementalNodeGen inc;
        inc.init(board);
        int owner[R][C];                       // 소유권 레이어: 시작은 전부 버섯 → -1(소유 없음)
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) owner[r][c] = -1;

        for (int ply = 0; ply < 300 && positions < TARGET_POS; ply++) {
            // (1) 검증: 증분 집합 == 전체 스캔 집합
            set<int> expected;
            fullScan(inc.num, expected);
            if (inc.moves != expected) {
                fprintf(stderr, "[MISMATCH] game=%d ply=%d  inc=%zu exp=%zu\n",
                        g, ply, inc.moves.size(), expected.size());
                dumpBoard(inc.num);
                dumpDiff(inc.moves, expected);
                abort();
            }
            // (1') 오라클 교차검증: fullScan == bruteForce (가끔)
            if (++sinceBrute >= BRUTE_EVERY) {
                sinceBrute = 0;
                set<int> bf; bruteForceScan(inc.num, bf);
                if (bf != expected) {
                    fprintf(stderr, "[ORACLE-BUG] fullScan != bruteForce game=%d ply=%d\n", g, ply);
                    dumpBoard(inc.num);
                    dumpDiff(expected, bf);
                    abort();
                }
            }
            // (2) nodeGen Δ 검증: 주석 스캔이 같은 수집합을 찾고, 각 노드의 (cap,steal)이
            //     오라클(=playMove 점수규칙)과 일치하는지. mover는 ply마다 교대(0/1).
            int mover = ply & 1;
            vector<Node> nodes;
            fullScanNodes(inc.num, owner, mover, nodes);
            set<int> nodeSet;
            for (auto& nd : nodes) nodeSet.insert(encMove(nd.r1, nd.c1, nd.r2, nd.c2));
            if (nodeSet != expected) {           // 주석 스캔이 유효수 집합을 정확히 재현하는가
                fprintf(stderr, "[NODE-SET-MISMATCH] game=%d ply=%d  nodes=%zu exp=%zu\n",
                        g, ply, nodeSet.size(), expected.size());
                dumpBoard(inc.num); dumpDiff(nodeSet, expected); abort();
            }
            for (auto& nd : nodes) {             // 각 노드의 Δ == 오라클
                int cap, steal;
                scoreDeltaOracle(inc.num, owner, mover, nd.r1, nd.c1, nd.r2, nd.c2, cap, steal);
                if (nd.cap != cap || nd.steal != steal) {
                    fprintf(stderr, "[NODE-DELTA-MISMATCH] game=%d ply=%d mover=%d (%d,%d)-(%d,%d) "
                            "scan(cap=%d,steal=%d) oracle(cap=%d,steal=%d)\n",
                            g, ply, mover, nd.r1, nd.c1, nd.r2, nd.c2,
                            nd.cap, nd.steal, cap, steal);
                    dumpBoard(inc.num); abort();
                }
            }
            positions++;

            // (3) 액션 선택: 70% 유효수 / 20% 임의 직사각형 제거(스트레스) / 10% 단일칸(소유권/패스 경로)
            int ar1, ac1, ar2, ac2;
            int dice = rng() % 10;
            if (!expected.empty() && dice < 7) {
                auto it = expected.begin();
                advance(it, rng() % expected.size());
                decMove(*it, ar1, ac1, ar2, ac2);
            } else if (dice < 9) {
                ar1 = rng() % R; ar2 = ar1 + rng() % (R - ar1);
                ac1 = rng() % C; ac2 = ac1 + rng() % (C - ac1);
            } else {
                ar1 = rng() % R; ac1 = rng() % C; ar2 = ar1; ac2 = ac1;  // 단일칸: 이미 0이면 소유권만 변경
            }
            // 소유권 먼저 갱신(현재 num 기준) → 그 다음 inc.applyMove가 버섯을 0으로.
            applyOwner(owner, inc.num, mover, ar1, ac1, ar2, ac2);
            inc.applyMove(ar1, ac1, ar2, ac2);

            // 보드 비면 종료
            bool any = false;
            for (int r = 0; r < R && !any; r++)
                for (int c = 0; c < C; c++)
                    if (inc.num[r][c]) { any = true; break; }
            if (!any) break;
        }
    }

    fprintf(stderr, "[OK] seed=%u positions=%lld — 모드 B==모드 A 수집합 일치 + "
                    "nodeGen Δ(cap,steal)==playMove 오라클 전부 일치. "
                    "(fullScan==bruteForce 교차검증 포함)\n", seed, positions);
    return 0;
}
