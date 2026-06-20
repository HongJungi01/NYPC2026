// ============================================================================
//  NYPC 2026 - 버섯밭 점령 게임 / 기반 도구 T4: 수 생성 (move generation)
//
//  이 파일 하나로 "참조 구현 + 차분 검증 하니스"를 모두 담는다.
//   - 모드 A (fullScan)      : 행밴드 압축 전체 스캔. 정답 기준선(오라클).
//   - 모드 B (Incremental..) : 합기준 국소 증분. 채택 대상(검증 통과 시).
//   - bruteForceScan         : isValid 4중 루프(규칙 §3 직역). 오라클의 오라클.
//
//  검증 게이트: 무작위/적대적 보드에서 임의 수 시퀀스를 진행하며, 매 수마다
//   "모드 B 집합 == 모드 A 집합" 을 비교한다. 단 한 건이라도 불일치하면 덤프 후 abort.
//   불일치 0건이어야 모드 B를 채택한다.
//
//  빌드: g++ -O2 -std=c++17 moveGen.cpp -o moveGen
//  실행: ./moveGen [seed] [target_positions]
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
//  모드 B: 합기준 국소 증분 유지기
//   - num   : 현재 보드(숫자 레이어)
//   - moves : 현재 유효수 집합 (encMove). init 후 항상 최신 상태 유지가 목표.
// ============================================================================
struct IncrementalMoveGen {
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

        IncrementalMoveGen inc;
        inc.init(board);

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
            positions++;

            // (2) 액션 선택: 70% 유효수 / 20% 임의 직사각형 제거(스트레스) / 10% 단일칸(소유권/패스 경로)
            int dice = rng() % 10;
            if (!expected.empty() && dice < 7) {
                auto it = expected.begin();
                advance(it, rng() % expected.size());
                int r1, c1, r2, c2; decMove(*it, r1, c1, r2, c2);
                inc.applyMove(r1, c1, r2, c2);
            } else if (dice < 9) {
                int r1 = rng() % R, r2 = r1 + rng() % (R - r1);
                int c1 = rng() % C, c2 = c1 + rng() % (C - c1);
                inc.applyMove(r1, c1, r2, c2);
            } else {
                int r1 = rng() % R, c1 = rng() % C;
                inc.applyMove(r1, c1, r1, c1);    // D가 비어있으면(이미 0) 불변 경로 테스트
            }

            // 보드 비면 종료
            bool any = false;
            for (int r = 0; r < R && !any; r++)
                for (int c = 0; c < C; c++)
                    if (inc.num[r][c]) { any = true; break; }
            if (!any) break;
        }
    }

    fprintf(stderr, "[OK] seed=%u positions=%lld — 모드 B(국소 증분) == 모드 A(전체 스캔) 전부 일치. "
                    "(fullScan==bruteForce 교차검증 포함)\n", seed, positions);
    return 0;
}
