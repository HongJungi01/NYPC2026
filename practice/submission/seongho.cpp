// ============================================================================
//  NYPC 2026 - "버섯 밭 점령 게임" Bot  [ver6: 2P(후공) 비대칭 전략 C/A/A2/D/E/F]
//
//  베이스: ver5(retake 공/수 분리 + 2P 비대칭 준비). 그 위에 2P 전용 개선 6종 추가.
//  선공은 구조적 우위가 있어 자가대국 후공 승률 ~15%. 공격/되잡기 가중치 직접 강화는 양쪽
//  대칭이라 차분 우위가 없음(검증됨). 따라서 후공의 비대칭 무기(상대 수를 보고 응수 / 종반정밀)에 집중.
//
//  ── [ver6 신규] 2P 전용(모두 amSecond && has_W2_* 일 때만 활성, 미설정 시 ver5 와 완전 동치) ──
//   [C] W2_ENDGAME_NZ      : 2P 종반 solve-to-terminal 트리거 임계값 확대(effEndgameNz).
//   [A] W2_DENIAL          : to_move==1 leaf 에서 상대 다음 최대 점령 직사각형 감점(자원 봉쇄).
//   [A2] W2_DENIAL_TOP2    : 동일 스캔(scanBestCapture2)으로 차순위까지 보조 감점.
//   [D] W2_BOOST/_FRAC     : 긴장 국면 예산 부스트 강화(effBoostPct/effBoostFrac).
//   [E] W2_RECAP           : 상대 즉시 최강 되잡기 수에 더 민감(effRecap).
//   [F] W2_REPLY_BOOST/_FRAC: 선공 첫 수 직후(myCnt==0 && oppCnt>0) 내 첫 응수에만 추가 예산.
//   ※ 안전성: env 전부 미설정이면 BENCH 평가값/노드수/선택수까지 ver5 와 동치. amSecond 단독으로는
//     동작 불변(2P env 미설정 시 1P 계수 사용). TT/hash/패리티/패스/폰더 로직 불변, t1/3 TLE 캡 유지.
//
//  ── (이하 ver5 주석) ────────────────────────────────────────────────────────
//  NYPC 2026 - "버섯 밭 점령 게임" Bot  [ver5: retake 공/수 분리 + 2P 비대칭 준비]
//
//  베이스: ver4(전략평가 기본활성 + 되잡기 페널티 + retake 스캔 swing 게이트).
//
//  ── [ver5 변경점] ───────────────────────────────────────────────────────────
//   [1] retake 안정성 항(공/수) 분리:
//         (구) e0 += wRetake * (opU - myU);
//         (신) e0 += wAttackOpp  * opU;   // 상대 불안정칸 = 내 공격기회(가점)
//              e0 -= wDefendMine * myU;   // 내 불안정칸    = 방어리스크(감점)
//       기본값 wAttackOpp = wDefendMine = wRetake(=35) → ver4 와 완전 동치.
//       env: W_ATTACK_OPP / W_DEFEND_MINE (없으면 W_RETAKE 값을 상속).
//   [2] 2P 비대칭 계수 준비(기본 비활성):
//         Solver.amSecond (= !game.first). 평가 계수 선택에만 영향.
//         TT/hash/패리티/패스/폰더 로직에는 절대 섞지 않음.
//         env: W2_ATTACK_OPP / W2_DEFEND_MINE / W2_ATTACK_BEST_OPP /
//              W2_DEFEND_BEST_MINE / W2_DEFEND_BEST_GATE
//         (amSecond==true 이고 해당 env 가 set 된 경우에만 1P 값 대신 적용)
//   [3] best 기반 신규 실험항(기본 0 → ver4 동치):
//         e0 += wAttackBestOpp  * oppBest;          env W_ATTACK_BEST_OPP
//         e0 -= wDefendBestMine * myBest (게이트);   env W_DEFEND_BEST_MINE / DEFEND_BEST_GATE
//   ※ 기본 상태(2P env 미설정, best 항 0)에서 BENCH 평가값/노드수/선택수는 ver4 와 동치.
//     amSecond 만으로는 동작이 바뀌지 않는다(2P env 미설정 시 1P 계수와 동일).
//
//  ── (이하 ver4 주석) ────────────────────────────────────────────────────────
//  [ver4: depth 핸디캡 개선 = retake 스캔 swing 게이트]
//
//  베이스: ver3(전략평가 기본활성 + 되잡기 페널티 + 6대 개선). 그 위에 depth 회복 1종 추가.
//
//  ── [ver4 신규] retake 스캔 swing 게이트 (depth 핸디캡 해소) ─────────────────
//   문제: ver3 는 전략 ON 시 leaf 마다 scanRetake(O(R^2*C^2) enumRects 1회)를 돌아, 같은
//         시간예산에서 전략 OFF 대비 도달 depth 가 1~2 낮았다(실측: OFF depth6 ↔ ON depth4).
//   해법: apply() 가 이번 수의 swing(capNew + 2*capOppTake)을 Undo 에 0-비용으로 기록.
//         leaf(horizonEval)에서 직전 수 swing 이 RETAKE_GATE(기본3) 미만이면 = '방금 큰 탈취가
//         없던 조용한 자리' → retake 스캔을 통째로 생략(전략항 0 근사). 큰 탈취 직후 자리(되잡기
//         페널티가 정작 필요한 곳)에서만 스캔한다.
//   효과(실측, 동일 보드/예산): 도달 depth 4→5 회복, leaf 중 스캔 비율 ~45%(55% 생략),
//         노드 최소(게이트3이 sweet spot — 더 높이면 평가가 거칠어져 재탐색↑). 착수는 ver3 와 동일.
//   안전성: RETAKE_GATE=-1 이면 항상 스캔 = ver3 와 노드수까지 완전 동치(BENCH 650993 검증).
//           ORD_SWING=0 + 전략 전부 0 이면 원본 ver2 와 동치(475406). VERIFY_MOVES 2226 포지션 통과.
//   ※ 추가 여지(미적용): 남은 45% 스캔 leaf 의 enumRects 를 부모 move 재활용으로 더 줄이는 개선은
//     복잡도/정확성 위험 대비 이득이 작아 보류. self-play 에서 depth 가 여전히 부족하면 그때 도입.
//
//  ── 이하 ver3 설명 ───────────────────────────────────────────────────────────
//
//  베이스: ver2(엔진 + 폰더 + 전략평가 3종). 그 위에 게임전략 개선 6+1 종을 추가.
//
//  ── [ver3 신규] 6대 개선 + 되잡기 페널티 ──────────────────────────────────
//   [개선1] 무브 오더링에 소유권 탈취 swing 반영: 정렬키 = key + 2*상대소유칸덮음.
//           상대 영토를 빼앗는 큰 수를 먼저 봐 alpha-beta 컷오프↑. env ORD_SWING=0 으로 끔.
//   [개선2] 종반 solve-to-terminal: 남은 버섯 nz<=ENDGAME_NZ(기본14) 이면 평시 예산캡 완화,
//           트리가 작은 종반을 정확한 minimax 값으로 깊이 푼다. (t1/3 TLE 안전캡은 항상 유지)
//   [개선3] 리테이크를 '수별 최대 swing(myBest/oppBest)'과 '장기 안정성(myU/oppU)'으로 분리.
//           scanRetake 를 2D prefix-sum 으로 재작성 → best 는 직사각형당 O(1)(저렴),
//           안정성 마킹(비쌈)은 wRetake!=0 일 때만(needMark).
//   [개선4] 위치보너스(테두리/코너)는 안정성 평가와 함께 tie-breaker 로 동작(ver2 유지).
//   [개선5] 전략 가중치 기본값 상수화: W_EDGE=8 W_CORNER=20 W_RETAKE=35 (env 없어도 켜짐).
//           + aspiration 초기 반폭/full-window 전환 상한을 전략 변동폭에 맞춰 확대(재탐색↓).
//   [개선6] 후반 리드 잠금 패스: 합법수가 많아도 '리드(matStm>=2칸)+내 점령칸이 상대 한 수에
//           크게 노출' 이면 PASS 를 후보로 추가(강제 아님, negamax 가 응수까지 보고 선택).
//   [되잡기 페널티] horizon effect 정적 근사(노드 증가 0): leaf 에서 '둘 차례가 한 수로 되받을
//           수 있는 최대 칸'을 그 쪽 가점으로(W_RECAP=50). extension(1ply 더보기)의 저비용 대체.
//
//  ※ 안전성: ORD_SWING=0 + W_EDGE=W_CORNER=W_RETAKE=W_RECAP=0 이면 원본 ver2 와 노드수까지
//     완전 동치(벤치 475406 노드 일치 검증). TEST_MAIN+VERIFY_MOVES 2226 포지션 무결성 통과.
//  ※ [중요] 전략 ON 은 leaf 마다 retake 스캔 1회 비용으로 실측상 도달 depth 가 OFF 대비 1~2 낮다.
//     → depth 손실 vs 평가정확도 의 우열은 self-play 승률로만 판단 가능. 반드시 A/B 검증 후 확정.
//     각 항목이 env 토글로 분리돼 있어 개별 효과를 대조할 수 있다.
//
//  ── 이하 ver2 설명 (엔진/폰더/시간예산은 동일) ────────────────────────────────
//
//  베이스: ver1(엔진 + 폰더 + 분기게이트 시간예산)을 복사. 그 위에 평가 함수 확장 3종 추가.
//  엔진/탐색/폰더/시간예산은 ver1 과 동일. 차이는 horizonEval() 의 위치/리테이크 항뿐.
//  새 가중치 기본값은 전부 0 → 미설정 시 ver1 과 완전 동치(벤치 노드수까지 동일 검증됨).
//
//  ── [ver2 신규] 전략 평가 (horizonEval 확장) ──────────────────────────────
//   [전략1] 위치 가중치: 내가 점령한 테두리(0행/9행/0열/16열)·코너 칸에 soft 보너스.
//           env W_EDGE / W_CORNER. 권장 제출값 W_EDGE=8 W_CORNER=20. (강제 아님=tie-breaker)
//   [전략2] 리테이크 안정성: 상대가 한 수로 덮어 탈취 가능한 내 점령칸 = 불안정 → 감점.
//   [전략3] 몰기(대칭): 내가 한 수로 덮어 탈취 가능한 상대 점령칸 = 가점.
//           [2·3] 공통 env W_RETAKE. 권장 제출값 W_RETAKE=35. leaf 1수 앞 스캔(scanRetake).
//   ※ 비용: W_RETAKE>0 일 때만 leaf 마다 O(R^2*C^2) 스캔 1회. 중반엔 탐색폭이 다소 줄고
//     (측정: 300ms 예산서 노드 2278→1431, 동일 depth), 종반엔 노드 적어 비용 무시 가능.
//   ※ A/B 권장: 기본 0(=ver1)로 빌드 후, env로 켜서 self-play 대조 → 효과 확인 뒤 상수화.
//
//  ── 시간예산 튜닝 (budgetFor) ─────────────────────────────────────────────
//   문제: 기존 decideBudget 은 내 시계(10초)의 ~50% 를 안 쓰고 버린다(로그 #32399).
//   해법: 합법수(nLegal)가 '중간대(긴장 국면)' 일 때만 예산을 부스트해 남는 슬랙을 거기에 집중.
//   env 키: TURN_DIV MOVE_CAP FRAC_DIV GATE_LO GATE_HI BOOST BOOST_CAP BOOST_FRAC (아래 Solver 필드 주석).
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
static const int TARGET = 10;  // [개선5] 점령 목표 합 — 룰 변형/디버깅 시 한 곳에서 관리

// ── 무브 오더링 보너스 (orderKey에서 m.key에 가산) ──
static const int ORD_TT      = 1000000;  // TT best move: 최우선
static const int ORD_KILLER  = 5000;     // killer move
static const int ORD_HIST_CAP = 4000;    // history 보너스 상한(killer 미만으로 클램프)

// ── 시간/예산 관련 상수 ──
static const int GREEDY_T1_MS   = 8;     // 잔여시간 이하면 탐욕 즉답
static const int BUDGET_MARGIN_MS = 250; // flush/overhead 안전마진

// ── [ver3 개선2] 종반 solve-to-terminal: 남은 버섯이 이 값 이하면 트리가 충분히 작다고 보고
//   budgetFor 의 평시 캡(moveCap/fracDiv)을 완화해 끝까지(또는 깊게) 풀도록 한다.
//   t1/3 TLE 안전캡은 유지(한 수가 시계를 비우지 못하게). env ENDGAME_NZ 로 튜닝.
static const int ENDGAME_NZ_DEFAULT = 14;

// ── [ver3 개선6] 리드 잠금 패스 게이트 상수 ──
//   matStm()(=나의 material lead, W_MAT 단위)이 LEAD_MARGIN 이상이고, 내 점령칸이 상대 한 수에
//   LEAD_PASS_THREAT 칸 이상 노출돼 있으면(=두면 흔들릴 위험) PASS 를 후보로 추가한다.
static const int LEAD_MARGIN      = 2 * 100;  // 2칸 이상 앞설 때만 (W_MAT=100)
static const int LEAD_PASS_THREAT = 3;        // 상대 한 수가 3칸 이상 되받을 수 있으면 위험

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
    // [개선3] age: 매 decide()마다 curGen++. 오래된(이번 턴에 안 쓰인) 엔트리를 우선 덮어쓴다.
    struct TTEnt { uint64_t key; int val; int best; int16_t depth; uint8_t flag; uint8_t age; };
    static const int TT_BITS = 19;            // 2^19 = 512K 엔트리 (~12MB, 캐시 친화)
    uint8_t curGen = 0;                       // [개선3] 현재 탐색 세대
    vector<TTEnt> tt;
    uint64_t ttMask = (1ULL << TT_BITS) - 1;

    array<array<int, 2>, MAXD> killer;
    // [개선4] history heuristic: 컷오프를 일으킨 수(from,to)에 depth^2 누적.
    //   인덱스 = encMove(0..R*C*R*C). killer보다 일반화된 ordering 신호.
    vector<int> history;
    vector<Move> mv[MAXD];          // ply별 무브 버퍼 재사용
    vector<int>  ordBuf[MAXD];      // [ver3] ply별 정렬키 캐시(수당 1회만 계산 → 정렬 내부 재계산 방지)

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

    // ── [전략1] 위치 가중치 (테두리+코너 soft 보너스) ───────────────────────────
    //   material(=W_MAT*칸수)에 더해, 내가 점령한 테두리/코너 칸에 작은 보너스를 준다.
    //   강제가 아니라 동점 상황의 tie-breaker. 첫 수에서 자연히 모서리를 선호하되
    //   합10 직사각형이 없으면 무리하지 않음. 상대 점령 테두리/코너는 대칭 감점.
    //   스케일: W_MAT=100 대비 작게(보너스 한 칸 = wEdge/wCorner). 0이면 완전 비활성.
    //   권장(제출) 값: W_EDGE=8 W_CORNER=20.
    //   [ver3 개선5] 기본값을 권장값으로 상수화. 제출 환경이 env 를 보장 못 해도 전략평가가 켜진다.
    //     ver1 동치(material-only)로 되돌리려면 env W_EDGE=0 W_CORNER=0 W_RETAKE=0.
    int wEdge   = 8;    // env W_EDGE   : 테두리(0행/9행/0열/16열) 칸당 보너스
    int wCorner = 20;   // env W_CORNER : 네 코너 칸당 추가 보너스(테두리 보너스에 가산)

    // ── [전략2·3] 리테이크 안정성 (leaf 1수 앞 체크) ───────────────────────────
    //   내가 점령한 칸 중 '상대가 단 한 수(합10 직사각형)로 덮어 탈취 가능한' 칸 = 불안정.
    //   불안정 칸당 페널티(wRetake). 같은 스캔으로 상대 점령칸의 불안정성도 측정해
    //   내 가점으로 환산(전략3 '몰기'를 대칭 근사). horizon(depth==0)에서만, 게이트로 호출.
    //   비용: 한 번의 O(R^2*C^2) 스캔(scanRetake). wRetake=0이면 스캔 생략(기존과 동일 속도).
    //   권장(제출) 값: W_RETAKE=35.  [ver3 개선5] 기본값 상수화(위 W_EDGE 와 동일 취지).
    int wRetake = 35;  // env W_RETAKE : 불안정 칸(내것=감점/상대것=가점) 1칸당 가중

    // ── [ver3 되잡기 페널티] horizon effect 정적 근사 (노드 증가 0) ────────────────
    //   leaf 직전 수가 '큰 탈취/점령'이었다면, 그 자리는 상대가 한 수로 되받을 수 있어 평가가
    //   부풀 수 있다(=horizon effect). extension(1ply 더보기) 대신, side-to-move(=상대)가
    //   '한 수로 되받을 수 있는 최대 칸 수' 를 추가 감점해 되잡힘을 정적으로 근사한다.
    //   scanRetake 의 oppBest(상대 1수 최대 탈취) 를 그대로 재사용 → 추가 스캔 비용 0.
    //   권장 0.5칸 가중(=W_MAT/2). wRetake=0이면 함께 비활성. env W_RECAP.
    int wRecap  = 50;  // env W_RECAP  : leaf 에서 상대가 한 수로 되받을 수 있는 칸당 추가 감점

    // ── [ver5] retake 공/수 분리 + best 기반 실험항 + 2P 비대칭 ──────────────────
    //   기존 wRetake*(opU-myU) 를 공격항(opU)/방어항(myU)으로 분리한다.
    //   기본값은 wRetake 를 상속(=35) → 분리 후에도 ver4 와 평가식 완전 동치.
    //   env: W_ATTACK_OPP / W_DEFEND_MINE (있으면 개별 override).
    int wAttackOpp  = 35;  // env W_ATTACK_OPP  : 상대 불안정칸(opU) 1칸당 가점(공격)
    int wDefendMine = 35;  // env W_DEFEND_MINE : 내 불안정칸(myU) 1칸당 감점(방어)
    //   best 기반 신규 실험항(기본 0 → ver4 동치). best 는 '단 한 수' 실제 위협에 가까움.
    int wAttackBestOpp  = 0;  // env W_ATTACK_BEST_OPP  : e0 += * oppBest (player0 공격)
    int wDefendBestMine = 0;  // env W_DEFEND_BEST_MINE : e0 -= * myBest (player0 방어), 게이트 적용
    int defendBestGate  = 0;  // env DEFEND_BEST_GATE   : >0 이면 myBest>=gate 일 때만 방어항 적용
    //   2P 전용 계수(기본 미설정 → 1P 와 동일). amSecond==true 이고 env set 시에만 적용.
    int w2AttackOpp     = 35; bool has_W2_ATTACK_OPP      = false;  // env W2_ATTACK_OPP
    int w2DefendMine    = 35; bool has_W2_DEFEND_MINE     = false;  // env W2_DEFEND_MINE
    int w2AttackBestOpp = 0;  bool has_W2_ATTACK_BEST_OPP = false;  // env W2_ATTACK_BEST_OPP
    int w2DefendBestMine= 0;  bool has_W2_DEFEND_BEST_MINE= false;  // env W2_DEFEND_BEST_MINE
    int w2DefendBestGate= 0;  bool has_W2_DEFEND_BEST_GATE= false;  // env W2_DEFEND_BEST_GATE
    //   2P 비대칭 플래그: !game.first. 평가 계수 선택에만 영향(TT/hash/패리티/패스/폰더 무관).
    bool amSecond = false;

    // ── [ver5] 유효 계수 헬퍼: amSecond 이고 해당 2P env 가 set 일 때만 2P 값 사용 ──
    inline int effAttackOpp()      const { return (amSecond && has_W2_ATTACK_OPP)      ? w2AttackOpp      : wAttackOpp; }
    inline int effDefendMine()     const { return (amSecond && has_W2_DEFEND_MINE)     ? w2DefendMine     : wDefendMine; }
    inline int effAttackBestOpp()  const { return (amSecond && has_W2_ATTACK_BEST_OPP) ? w2AttackBestOpp  : wAttackBestOpp; }
    inline int effDefendBestMine() const { return (amSecond && has_W2_DEFEND_BEST_MINE)? w2DefendBestMine : wDefendBestMine; }
    inline int effectiveDefendBestGate() const {
        return (amSecond && has_W2_DEFEND_BEST_GATE) ? w2DefendBestGate : defendBestGate;
    }

    // ── [ver6] 2P 비대칭 전용 계수(개선 C/A/A2/D/E/F). 모두 amSecond && has_W2_* 일 때만 활성 ──
    // [개선C] 2P 종반정밀: solve-to-terminal 트리거 임계값을 2P 에서만 키운다.
    int w2EndgameNz = 28; bool has_W2_ENDGAME_NZ = true;  // env W2_ENDGAME_NZ [ver2.1 기본활성]
    // [개선A] 2P 디나이얼: 상대 다음 수의 최대 점령 직사각형을 감점(자원 봉쇄).
    int w2Denial = 35;     bool has_W2_DENIAL = true;       // env W2_DENIAL [ver2.1 기본활성]
    // [개선A2] 디나이얼 top-2 보조항.
    int w2DenialTop2 = 0; bool has_W2_DENIAL_TOP2 = false;  // env W2_DENIAL_TOP2
    // [개선D] 2P 시간예산 재분배: 긴장 국면 부스트 강화.
    int w2Boost = 250;     bool has_W2_BOOST = false;        // env W2_BOOST
    int w2BoostFrac = 4;   bool has_W2_BOOST_FRAC = false;   // env W2_BOOST_FRAC
    // [개선E] 2P 되잡기 강화: 상대 즉시 최강 되잡기 수에 더 민감하게.
    int w2Recap = 50;      bool has_W2_RECAP = true;        // env W2_RECAP [ver2.1 기본활성]
    // [개선F] 2P 첫 응수 부스트: 선공 첫 수를 본 직후 첫 응수에만 추가 예산.
    int w2ReplyBoost = 100; bool has_W2_REPLY_BOOST = true; // env W2_REPLY_BOOST [ver2.1 기본활성]
    int w2ReplyFrac = 0;    bool has_W2_REPLY_FRAC = false;  // env W2_REPLY_FRAC

    // ── [2P_can_win_ver1·신규] 종료구조 직접대응 2개. 모두 amSecond && has_W2_* 일 때만 활성 ──
    // [개선G] 모빌리티 디나이얼: to_move==1(상대 차례) leaf 에서 '상대가 둘 수 있는 합법수 개수'를
    //   player0 관점 감점한다. 기존 DENIAL(상대 최대 점령 '칸수' 감점)과 직교 — 이쪽은 상대의 '수 선택지
    //   개수' 자체를 줄여 PASS(=종료)를 강요한다. 본 게임은 합법수 고갈→양쪽 PASS 로 끝나므로,
    //   상대 모빌리티를 먼저 말리면 2P 가 점수 앞선 채로 종료를 유도할 수 있다.
    //   w2Mobility==0 이면 스캔 생략(비용 0). 값이 클수록 상대 합법수 1개당 감점↑.
    int w2Mobility = 0;     bool has_W2_MOBILITY = false;     // env W2_MOBILITY
    //   모빌리티 카운트 상한(과도한 출렁임/aspiration 폭증 방지). 0=무제한. 권장 8~16.
    int w2MobilityCap = 12; bool has_W2_MOBILITY_CAP = false; // env W2_MOBILITY_CAP
    // [개선H] 종료임박 점수가중: 남은 합법수가 적은(=곧 PASS 종료될) 국면에서 2P 의 '현재 확정 점령칸
    //   리드(myCnt-oppCnt)'에 추가 가중을 준다. 영토 욕심(미래 탈취)보다 '종료 시점 점수'를 우선시켜,
    //   종료가 임박할수록 확정 1칸의 가치를 키운다. leaf 의 nLegal 추정이 비싸므로, 여기서는 nz 가
    //   termScoreNz 이하일 때만(=종반) material 항에 (1 + w2TermScore/100) 배 가중.
    //   w2TermScore==0 이면 비활성(기존과 동치). 값은 % (예: 50 = material 1.5배).
    int w2TermScore = 0;    bool has_W2_TERMSCORE = false;    // env W2_TERMSCORE
    int w2TermScoreNz = 24; bool has_W2_TERMSCORE_NZ = false; // env W2_TERMSCORE_NZ : 가중 적용 nz 임계

    inline int effEndgameNz() const {
        return (amSecond && has_W2_ENDGAME_NZ) ? w2EndgameNz : endgameNz;
    }
    inline int effDenial() const {
        return (amSecond && has_W2_DENIAL) ? w2Denial : 0;
    }
    inline int effDenialTop2() const {
        return (amSecond && has_W2_DENIAL_TOP2) ? w2DenialTop2 : 0;
    }
    inline int effMobility() const {
        return (amSecond && has_W2_MOBILITY) ? w2Mobility : 0;
    }
    inline int effMobilityCap() const {
        return (amSecond && has_W2_MOBILITY_CAP) ? w2MobilityCap : w2MobilityCap;
    }
    inline int effTermScore() const {
        return (amSecond && has_W2_TERMSCORE) ? w2TermScore : 0;
    }
    inline int effTermScoreNz() const {
        return (amSecond && has_W2_TERMSCORE_NZ) ? w2TermScoreNz : w2TermScoreNz;
    }
    inline int effBoostPct() const {
        return (amSecond && has_W2_BOOST) ? w2Boost : boostPct;
    }
    inline int effBoostFrac() const {
        return (amSecond && has_W2_BOOST_FRAC) ? w2BoostFrac : boostFrac;
    }
    inline int effRecap() const {
        return (amSecond && has_W2_RECAP) ? w2Recap : wRecap;
    }

    // ── 시간예산 튜닝 knobs (budgetFor) — 기본값 = self-play A/B 로 고른 추천 config ──
    //   budget = (t1-250)/max(4, nz/turnDiv) 를 base 로, nLegal 이 [gateLo,gateHi] 구간이면 boost.
    //   추천값(3 seed 페어 대조 vs ponder.exe: 99승62패 ↔ 기본 73승84패, ≈+15pp).
    //   기존 decideBudget 와 동치로 되돌리려면 env GATE_LO=0 GATE_HI=0 TURN_DIV=6.
    int turnDiv    = 9;     // env TURN_DIV  : estTurns=max(4,nz/turnDiv). ↑일수록 수당 예산↑(슬랙↓)
    int moveCapMs  = 1000;  // env MOVE_CAP  : 평시 수당 상한(ms)
    int fracDiv    = 8;     // env FRAC_DIV  : 평시 budget<=t1/fracDiv (고갈 방지)
    int gateLo     = 8;     // env GATE_LO   : 부스트 밴드 하한(nLegal). gateLo>gateHi 면 게이트 off
    int gateHi     = 26;    // env GATE_HI   : 부스트 밴드 상한(nLegal) — 중반 긴장 국면
    int boostPct   = 250;   // env BOOST     : 부스트 배율(%) — 100=배율 없음
    int boostCapMs = 2000;  // env BOOST_CAP : 부스트시 수당 상한(ms)
    int boostFrac  = 4;     // env BOOST_FRAC: 부스트시 budget<=t1/boostFrac (평시보다 완화)

    // [ver3 개선2] 종반 solve-to-terminal 임계값. nz<=endgameNz 면 budgetFor 캡 완화.
    int endgameNz  = ENDGAME_NZ_DEFAULT;  // env ENDGAME_NZ

    // [ver3 개선1] 무브 오더링에 소유권 탈취 swing 반영 여부. 1=반영(기본), 0=ver1 동치(key만).
    int ordSwing   = 1;     // env ORD_SWING=0 이면 끄고 원본 오더링으로

    // [ver4 개선2] retake 스캔 swing 게이트: leaf 직전 수의 swing(=capNew+2*capOppTake)이 이 값
    //   미만이면 retake 스캔(비쌈)을 건너뛴다. 큰 탈취가 없던 조용한 leaf 에서 enumRects 가 사라져
    //   depth 회복. -1 이면 게이트 끔(=항상 스캔, ver3 완전 동치). env RETAKE_GATE.
    int retakeGate = 3;     // env RETAKE_GATE (권장 3 = '소유권 탈취 1칸 이상' 또는 '3칸 이상 점령')

    // [2P_can_win_ver1] 시간 바닥 가드(time floor guard) — 1P/2P 공통 종반 시간고갈·PASS 방지.
    //   문제: budgetFor 의 estTurns=max(4,nz/turnDiv) 가 종반에 남은 턴을 과소평가하면 한 수 예산이
    //         과대해져 시계를 빨리 태운다. 누적 고갈 시 얕은 탐색 → gateLead 오판 PASS(합법수 있는데도).
    //   해결: '앞으로 둘 내 수'를 nz 기반으로 보수적으로 추정(reserveTurns)하고, 한 수가 잔여시간의
    //         1/reserveTurns 를 넘지 못하게 추가 상한을 건다. 이 캡은 항상 t1/3 안전캡과 함께 min 으로
    //         적용되며, 종반까지 '최소 reserveTurns 수'를 둘 시간을 남긴다.
    //   기본값 0 = 가드 OFF = ver6 와 완전 동치(BENCH/VERIFY 동일). env TIME_FLOOR_TURNS 로 켠다.
    //   권장 시작값 6~10 (잔여시간을 최소 N등분해 한 수가 1/N 초과 금지). 클수록 보수적(슬랙↑).
    int timeFloorTurns = 8;   // env TIME_FLOOR_TURNS : [ver2.1] 8=ON 보수적. 수당 budget<=t1/N 보장.

    // [2P_can_win_ver1·hardened] 시간 바닥 가드 보강 파라미터. 모두 timeFloorTurns>0 일 때만 의미가 있고,
    //   timeFloorTurns==0 이면 가드 블록 전체가 비활성이라 아래 값들도 budget 에 영향을 주지 않는다(ver6 동치).
    //   [약점1] 종반에 가드가 풀리는 문제: 기존 reserveTurns=max(timeFloorTurns, nz/2) 는 nz 가 줄면
    //           reserveTurns 도 줄어 t1/reserveTurns 상한이 느슨해진다(정작 위험한 종반에 무력화).
    //           → reserveTurns 의 '바닥'을 timeFloorTurns 로 고정하는 것은 그대로 두되, nz 기반 추정이
    //             종반에 바닥 아래로 떨어지지 않도록 항상 max 로 결합한다. 즉 한 수는 어떤 국면에서도
    //             잔여시간의 1/timeFloorTurns 를 초과하지 못한다(단조 비고갈 상한).
    //   [약점2] 버섯 게임은 한 수가 직사각형(합=10)으로 여러 칸을 한 번에 소거 → 실제 남은 '수'는
    //           nz/2 보다 적지만, 시간배분 관점에선 '최소 몇 수는 더 둔다'는 하한이 중요하다.
    //           turnEstDiv 로 남은 내 턴 추정 분모를 노출(기본 2 = 기존 nz/2 동일). 클수록 추정 턴↓ →
    //           가드가 덜 보수적. 작을수록(=1) 더 보수적(남은 수를 nz 만큼 크게 보고 더 잘게 분배).
    int turnEstDiv     = 2;   // env TURN_EST_DIV  : reserveTurns 의 nz 기반 추정 = max(1, nz/turnEstDiv).
    //   [추가] 종반 절대 시간 바닥(ms). nz<=floorEndgameNz 인 '진짜 종반'에서는 한 수 예산이 최소
    //           floorMinMoveMs 는 되도록 보장(단 t1/3 안전캡·avail 은 항상 우선). 얕은 탐색→gateLead
    //           오판 PASS 를 막는 마지막 안전장치. 0 이면 비활성(상한 가드만 동작).
    int floorEndgameNz = 16;  // env FLOOR_ENDGAME_NZ : 이 이하 nz 를 '종반'으로 보고 최소예산 보장 적용.
    int floorMinMoveMs = 80;   // env FLOOR_MIN_MOVE_MS: [ver2.1] 종반 한 수 최소 예산(ms). PASS 오판 방지.

    Solver() {
        mt19937_64 rng(0x9E3779B97F4A7C15ULL);
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++)
                for (int k = 0; k < 12; k++) Zcell[r][c][k] = rng();
        ZtoMove = rng();
        ZprevPass = rng();
        // env 정수 로더: 키가 있으면 변수에 대입(min 클램프 옵션).
        auto envInt = [](const char* key, int& var, int lo = INT_MIN) {
            if (const char* e = getenv(key)) var = max(lo, atoi(e));
        };
        envInt("W_TEMPO",   wTempo);
        envInt("PASS_GATE", passGate);
        envInt("W_EDGE",    wEdge);      // [전략1]
        envInt("W_CORNER",  wCorner);    // [전략1]
        envInt("W_RETAKE",  wRetake);    // [전략2·3]
        // [ver5] 공/수 분리: 먼저 W_RETAKE 값을 상속(기본 동치), 이후 개별 env 로 override.
        wAttackOpp  = wRetake;
        wDefendMine = wRetake;
        envInt("W_ATTACK_OPP",  wAttackOpp);
        envInt("W_DEFEND_MINE", wDefendMine);
        envInt("W_ATTACK_BEST_OPP",  wAttackBestOpp);
        envInt("W_DEFEND_BEST_MINE", wDefendBestMine);
        envInt("DEFEND_BEST_GATE",   defendBestGate);
        // [ver5] 2P 전용 계수: set 여부를 기록(has_*), 값은 1P 기본을 상속하되 env 우선.
        //   amSecond==true 이고 has_*==true 일 때만 effXXX() 가 2P 값을 쓴다.
        w2AttackOpp = wAttackOpp; w2DefendMine = wDefendMine;
        w2AttackBestOpp = wAttackBestOpp; w2DefendBestMine = wDefendBestMine;
        w2DefendBestGate = defendBestGate;
        auto env2 = [](const char* key, int& var, bool& has) {
            if (const char* e = getenv(key)) { var = atoi(e); has = true; }
        };
        env2("W2_ATTACK_OPP",       w2AttackOpp,      has_W2_ATTACK_OPP);
        env2("W2_DEFEND_MINE",      w2DefendMine,     has_W2_DEFEND_MINE);
        env2("W2_ATTACK_BEST_OPP",  w2AttackBestOpp,  has_W2_ATTACK_BEST_OPP);
        env2("W2_DEFEND_BEST_MINE", w2DefendBestMine, has_W2_DEFEND_BEST_MINE);
        env2("W2_DEFEND_BEST_GATE", w2DefendBestGate, has_W2_DEFEND_BEST_GATE);
        // [ver6] 2P 전용(C/A/A2/D/E/F). set 여부 기록 + 값 적재. amSecond && has_* 일 때만 활성.
        env2("W2_ENDGAME_NZ",  w2EndgameNz,  has_W2_ENDGAME_NZ);   // [C]
        env2("W2_DENIAL",      w2Denial,     has_W2_DENIAL);       // [A]
        env2("W2_DENIAL_TOP2", w2DenialTop2, has_W2_DENIAL_TOP2);  // [A2]
        env2("W2_BOOST",       w2Boost,      has_W2_BOOST);        // [D]
        env2("W2_BOOST_FRAC",  w2BoostFrac,  has_W2_BOOST_FRAC);   // [D]
        env2("W2_RECAP",       w2Recap,      has_W2_RECAP);        // [E]
        env2("W2_REPLY_BOOST", w2ReplyBoost, has_W2_REPLY_BOOST);  // [F]
        env2("W2_REPLY_FRAC",  w2ReplyFrac,  has_W2_REPLY_FRAC);   // [F]
        // [2P_can_win_ver1·신규] 종료구조 직접대응(G/H).
        env2("W2_MOBILITY",     w2Mobility,     has_W2_MOBILITY);     // [G] 상대 합법수 개수 봉쇄
        env2("W2_MOBILITY_CAP", w2MobilityCap,  has_W2_MOBILITY_CAP); // [G] 카운트 상한
        env2("W2_TERMSCORE",    w2TermScore,    has_W2_TERMSCORE);    // [H] 종료임박 점수가중(%)
        env2("W2_TERMSCORE_NZ", w2TermScoreNz,  has_W2_TERMSCORE_NZ); // [H] 가중 적용 nz 임계
        envInt("W_RECAP",   wRecap);     // [ver3 되잡기 페널티]
        envInt("TURN_DIV",   turnDiv,    1);
        envInt("MOVE_CAP",   moveCapMs,  1);
        envInt("FRAC_DIV",   fracDiv,    1);
        envInt("GATE_LO",    gateLo);
        envInt("GATE_HI",    gateHi);
        envInt("BOOST",      boostPct,   1);
        envInt("BOOST_CAP",  boostCapMs, 1);
        envInt("BOOST_FRAC", boostFrac,  1);
        envInt("ENDGAME_NZ", endgameNz,  0);   // [ver3 개선2]
        envInt("ORD_SWING",  ordSwing,   0);   // [ver3 개선1]
        envInt("RETAKE_GATE", retakeGate);     // [ver4 개선2] (-1 이면 게이트 끔)
        envInt("TIME_FLOOR_TURNS", timeFloorTurns, 0);  // [2P_can_win_ver1] 시간 바닥 가드(0=OFF)
        envInt("TURN_EST_DIV",     turnEstDiv,     1);  // [hardened] reserveTurns nz 추정 분모(>=1)
        envInt("FLOOR_ENDGAME_NZ", floorEndgameNz, 0);  // [hardened] 종반 최소예산 적용 nz 임계
        envInt("FLOOR_MIN_MOVE_MS", floorMinMoveMs, 0); // [hardened] 종반 한 수 최소 예산(ms, 0=OFF)
        tt.assign((size_t)1 << TT_BITS, TTEnt{ 0, 0, -1, 0, 0, 0 }); // depth=0 -> 빈 슬롯은 컷오프 불가
        history.assign((size_t)R * C * R * C, 0);   // [개선4] history table
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

    // ── 합10 유효 직사각형 공통 열거기 ────────────────────────────────────────
    //   genMoves/scanBestCapture/scanRetake가 공유하던 "행밴드 + 컬럼 prefix 누적" 패턴을
    //   하나로 통합. 각 유효 직사각형 (r1,c1)-(r2,c2)에 대해 cb(r1,c1,r2,c2,cntNZ) 호출.
    //   cntNZ = 직사각형 내부 버섯(num!=0) 칸 수(= 즉시 점령 칸수).
    //   유효 조건(원본과 동일): 합==TARGET && 네 변에 버섯 존재(top/bottom/left 보장).
    //   주의: 정확성 민감 로직 — 동작은 원본 3함수와 집합 동치(테스트로 검증).
    template <class CB>
    inline void enumRects(CB&& cb) const {
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
                // 모든 컬럼합>TARGET 이면 이 밴드 무효이고 r2를 키워도 무효 -> r2 루프 종료.
                if (minAll > TARGET) break;
                for (int c1 = 0; c1 < C; c1++) {
                    if (colCnt[c1] == 0) continue;        // 왼변: c1열에 버섯 필요
                    int s = 0, cntNZ = 0;
                    bool topP = false, botP = false;
                    for (int c2 = c1; c2 < C; c2++) {
                        s += colSum[c2];
                        cntNZ += colCnt[c2];
                        if (num[r1][c2]) topP = true;     // 위변
                        if (num[r2][c2]) botP = true;     // 아래변
                        if (s > TARGET) break;
                        if (s == TARGET && colCnt[c2] && topP && botP)
                            cb(r1, c1, r2, c2, cntNZ);
                    }
                }
            }
        }
    }

    // 현재 보드에서 side-to-move가 즉시 점령 가능한 최대 칸수 (이니셔티브 신호).
    int scanBestCapture() const {
        int best = 0;
        enumRects([&](int, int, int, int, int cntNZ) {
            if (cntNZ > best) best = cntNZ;
        });
        return best;
    }

    // [2P_can_win_ver1·개선G] 현재 num 레이어에서 '둘 수 있는 합법수 개수'를 센다(cap 에서 멈춤).
    //   모빌리티 디나이얼 평가에 사용. cap<=0 이면 무제한. enumRects 1회(genMoves 와 동일 패턴).
    int countLegalMoves(int cap) const {
        int cnt = 0;
        enumRects([&](int, int, int, int, int) {
            cnt++;
        });
        if (cap > 0 && cnt > cap) cnt = cap;
        return cnt;
    }

    // [ver6 개선A2] 한 번의 enumRects 로 최대/차순위 점령 칸수를 함께 산출(중복허용 단순 top-2).
    //   den==0 && den2==0 이면 호출부에서 호출 생략(비용 0).
    void scanBestCapture2(int& best, int& second) const {
        best = 0;
        second = 0;
        enumRects([&](int, int, int, int, int cntNZ) {
            if (cntNZ > best) {
                second = best;
                best = cntNZ;
            } else if (cntNZ > second) {
                second = cntNZ;
            }
        });
    }

    // [전략1] 위치 보너스: player0(나) 관점의 (내 테두리/코너 - 상대 테두리/코너) 가중합.
    //   owner 레이어만 읽는다. wEdge=wCorner=0이면 호출부에서 생략(비용 0).
    int positionBonus() const {
        if (!wEdge && !wCorner) return 0;
        int acc = 0;  // player0 관점 누적(나=+, 상대=-)
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) {
                int8_t o = owner[r][c];
                if (o < 0) continue;                       // 미점령(버섯 or 빈칸 아님) → 스킵
                bool edge   = (r == 0 || r == R - 1 || c == 0 || c == C - 1);
                if (!edge) continue;
                bool corner = (r == 0 || r == R - 1) && (c == 0 || c == C - 1);
                int val = wEdge + (corner ? wCorner : 0);
                acc += (o == 0 ? val : -val);
            }
        return acc;   // player0 관점. 호출부에서 side-to-move로 부호 보정.
    }

    // [전략2·3 + ver3] 리테이크 스캔(확장 + 최적화).
    //   myBest/oppBest = '단 한 수'가 덮는 점령칸 최댓값(즉시 위협). 되잡기 페널티(wRecap)에 사용.
    //     → 2D prefix-sum 으로 직사각형당 O(1) 계산(저렴).
    //   myUnstable/oppUnstable = '언젠가 한 수로' 덮일 수 있는 칸의 총 개수(장기 안정성, wRetake).
    //     → 중복 제거가 필요해 직사각형 내부 비트 마킹이 불가피(비쌈). needMark=true 일 때만 수행.
    //   비용: enumRects O(R^2*C^2) 1회 + (needMark 시) 직사각형 면적 합. wRetake=wRecap=0이면 호출 생략.
    void scanRetake(int& myUnstable, int& oppUnstable, int& myBest, int& oppBest,
                    bool needMark) const {
        myUnstable = 0; oppUnstable = 0; myBest = 0; oppBest = 0;
        // 2D prefix-sum: psMy[r][c] = (0,0)..(r-1,c-1) 내 점령칸 수, psOp = 상대 점령칸 수.
        static thread_local int psMy[R + 1][C + 1];
        static thread_local int psOp[R + 1][C + 1];
        for (int c = 0; c <= C; c++) { psMy[0][c] = 0; psOp[0][c] = 0; }
        for (int r = 0; r < R; r++) {
            psMy[r + 1][0] = 0; psOp[r + 1][0] = 0;
            for (int c = 0; c < C; c++) {
                int am = 0, ao = 0;
                if (num[r][c] == 0) { if (owner[r][c] == 0) am = 1; else if (owner[r][c] == 1) ao = 1; }
                psMy[r + 1][c + 1] = psMy[r][c + 1] + psMy[r + 1][c] - psMy[r][c] + am;
                psOp[r + 1][c + 1] = psOp[r][c + 1] + psOp[r + 1][c] - psOp[r][c] + ao;
            }
        }
        auto rectMy = [&](int r1, int c1, int r2, int c2) {
            return psMy[r2 + 1][c2 + 1] - psMy[r1][c2 + 1] - psMy[r2 + 1][c1] + psMy[r1][c1];
        };
        auto rectOp = [&](int r1, int c1, int r2, int c2) {
            return psOp[r2 + 1][c2 + 1] - psOp[r1][c2 + 1] - psOp[r2 + 1][c1] + psOp[r1][c1];
        };

        static thread_local uint32_t hitMy[R];   // (needMark 전용) 상대가 덮을 수 있는 내 칸
        static thread_local uint32_t hitOp[R];
        if (needMark) for (int r = 0; r < R; r++) { hitMy[r] = 0; hitOp[r] = 0; }
        int mB = 0, oB = 0;

        enumRects([&](int r1, int c1, int r2, int c2, int) {
            int coverMy = rectMy(r1, c1, r2, c2);   // O(1)
            int coverOp = rectOp(r1, c1, r2, c2);
            if (coverMy > mB) mB = coverMy;         // 상대 한 수가 빼앗을 수 있는 내 칸 최댓값
            if (coverOp > oB) oB = coverOp;         // 내 한 수가 빼앗을 수 있는 상대 칸 최댓값
            if (needMark && (coverMy || coverOp)) { // 장기 안정성: 중복 제거 위해 칸 단위 마킹
                for (int rr = r1; rr <= r2; rr++)
                    for (int cc = c1; cc <= c2; cc++)
                        if (num[rr][cc] == 0) {
                            if (owner[rr][cc] == 0) hitMy[rr] |= (1u << cc);
                            else if (owner[rr][cc] == 1) hitOp[rr] |= (1u << cc);
                        }
            }
        });
        if (needMark) for (int r = 0; r < R; r++) {
            myUnstable  += __builtin_popcount(hitMy[r]);
            oppUnstable += __builtin_popcount(hitOp[r]);
        }
        myBest = mB; oppBest = oB;
    }

    // horizon(depth==0) 전용 휴리스틱: material + 선턴 이니셔티브 + [전략1]위치 + [전략2·3]리테이크
    //   + [ver3 되잡기 페널티]. 모든 항은 player0 관점으로 만든 뒤 to_move 로 부호 보정.
    // [ver4] lastSwing = 직전 수가 덮은 점령칸 수(=apply 의 capNew+capOppTake). 이 값이 작으면
    //   '방금 큰 탈취가 없었다'는 뜻이라 되잡힐 것도/안정성 급변도 없어 retake 스캔(비쌈)을 건너뛴다.
    //   → leaf 의 대다수(조용한 자리)에서 O(R^2*C^2) enumRects 1회가 통째로 사라져 depth 회복.
    //   retakeGate<0(=항상 스캔)으로 부르면 ver3 와 완전 동일. env RETAKE_GATE 로 임계값 조정.
    inline int horizonEval(int lastSwing) const {
        int e0 = (myCnt - oppCnt) * W_MAT;        // player0 관점 material
        // [2P_can_win_ver1·개선H] 종료임박 점수가중: 2P 에서 nz<=termScoreNz(종반)일 때 material 리드에
        //   추가 가중. 본 게임은 합법수 고갈→PASS 종료라, 종반엔 미래 탈취보다 '지금 확정 점령 리드'가
        //   곧 최종점수가 된다. termW==0 이면 비활성(기존 동치). player0 관점이므로 부호변환 전 적용.
        {
            int termW = effTermScore();
            if (termW && countNonzero() <= effTermScoreNz()) {
                // e0 += material * termW/100  →  결과적으로 material 을 (1+termW/100) 배.
                e0 += (int)((long long)(myCnt - oppCnt) * W_MAT * termW / 100);
            }
        }
        if (wTempo)               e0 += (to_move == 0 ? wTempo : -wTempo) * scanBestCapture();
        if (wEdge || wCorner)     e0 += positionBonus();   // 이미 player0 관점
        // [ver6 개선A/A2] 2P 디나이얼: to_move==1(다음이 상대 차례)인 leaf 에서, 상대가 다음 수로
        //   점령할 수 있는 최대(및 차순위) 직사각형 칸수를 player0 관점 감점(상대 자원기회 봉쇄).
        //   den==0 && den2==0 이면 스캔 생략(비용 0). 기존 doScan/retake 게이트와 독립.
        {
            int den  = effDenial();
            int den2 = effDenialTop2();
            if ((den || den2) && to_move == 1) {
                int bestCap = 0, secondCap = 0;
                scanBestCapture2(bestCap, secondCap);
                e0 -= den  * bestCap;
                e0 -= den2 * secondCap;
            }
        }
        // [2P_can_win_ver1·개선G] 모빌리티 디나이얼: to_move==1(상대 차례) leaf 에서 상대 합법수 '개수'를
        //   player0 관점 감점. 상대 선택지 자체를 말려 PASS(종료)를 강요. mob==0 이면 스캔 생략(비용 0).
        {
            int mob = effMobility();
            if (mob && to_move == 1) {
                int nLegalOpp = countLegalMoves(effMobilityCap());
                e0 -= mob * nLegalOpp;
            }
        }
        int recapStm = 0;                          // to_move 관점 되잡기 보정(아래서 더함)
        // [ver5] 유효 계수(2P 비대칭 반영). needMark/doScan/평가 모두 이 값으로 일관 사용.
        const int atkOpp      = effAttackOpp();
        const int defMine     = effDefendMine();
        const int atkBestOpp  = effAttackBestOpp();
        const int defBestMine = effDefendBestMine();
        const int dGate       = effectiveDefendBestGate();
        const int recapW      = effRecap();   // [ver6 개선E] 2P 되잡기 강화(기본 wRecap 상속)
        // myU/opU 를 쓰는 항이 활성일 때만 칸단위 마킹 필요. best 전용 항은 마킹 불필요.
        const bool needMark = (atkOpp != 0) || (defMine != 0);
        // best 기반 신규 항(atkBestOpp/defBestMine)도 스캔이 필요(best 값은 마킹 없이도 산출).
        const bool useNewTerms = (atkOpp || defMine || atkBestOpp || defBestMine);
        // [ver4] swing 게이트: 직전 수가 retakeGate 미만이면 retake 스캔 생략(전략항 0 근사).
        bool doScan = (useNewTerms || recapW) && (retakeGate < 0 || lastSwing >= retakeGate);
        if (doScan) {
            int myU, opU, myBest, oppBest;
            scanRetake(myU, opU, myBest, oppBest, needMark);
            // [장기 안정성] 공/수 분리. player0 관점: 상대 불안정칸=가점, 내 불안정칸=감점.
            if (atkOpp)  e0 += atkOpp  * opU;
            if (defMine) e0 -= defMine * myU;
            // [ver5 실험항] best 기반(기본 0). oppBest=내 공격, myBest=내 방어(게이트 적용).
            if (atkBestOpp)  e0 += atkBestOpp * oppBest;
            if (defBestMine && (dGate <= 0 || myBest >= dGate)) e0 -= defBestMine * myBest;
            // [되잡기 페널티 = 즉시 전술] '지금 둘 차례'가 한 수로 되받을 수 있는 최대 칸을
            //   그 쪽 가점으로(=horizon effect 해소). to_move=0(나)이면 oppBest(내가 되받음),
            //   to_move=1(상대)이면 myBest(상대가 되받음)가 '둘 차례'의 즉시 위협이다.
            //   to_move 관점에서 항상 +(되받을 수 있으면 둘 차례가 유리)이므로 e_stm 에 직접 가산.
            if (recapW) recapStm = recapW * (to_move == 0 ? oppBest : myBest);
        }
        int eStm = (to_move == 0 ? e0 : -e0);     // side-to-move 관점으로 변환
        return eStm + recapStm;
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
                if (minAll > TARGET) break;
                for (int c1 = 0; c1 < C; c1++) {
                    if (colCnt[c1] == 0) continue;        // 왼변: c1열에 버섯 필요
                    int s = 0, cntNZ = 0;
                    bool topP = false, botP = false;
                    for (int c2 = c1; c2 < C; c2++) {
                        s += colSum[c2];
                        cntNZ += colCnt[c2];
                        if (num[r1][c2]) topP = true;     // 위변
                        if (num[r2][c2]) botP = true;     // 아래변
                        if (s > TARGET) break;
                        if (s == TARGET && colCnt[c2] && topP && botP) {
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
        // [ver4] 이번 수가 '덮은' 칸 분류(swing 게이트 + 되잡기 판정용, 추가 스캔 0):
        //   capNew  = 새로 점령한 버섯 칸(num!=0 였던 칸)
        //   capOppT = 상대 소유 점령칸을 탈취한 수(material 2배 swing)
        int capNew, capOppTake;
    };
    void apply(const Move& m, Undo& u) {
        u.pMy = myCnt; u.pOpp = oppCnt; u.pHash = hash; u.pTo = (int8_t)to_move; u.cnt = 0;
        u.capNew = 0; u.capOppTake = 0;
        int mover = to_move;
        for (int r = m.r1; r <= m.r2; r++)
            for (int c = m.c1; c <= m.c2; c++) {
                int8_t on = num[r][c], oo = owner[r][c];
                int8_t nn = on, no = oo; bool ch = false;
                if (on != 0) {
                    ch = true; nn = 0; no = (int8_t)mover;
                    if (mover == 0) myCnt++; else oppCnt++;
                    u.capNew++;                                  // [ver4] 새 버섯 점령
                } else if (oo != mover) {
                    ch = true; no = (int8_t)mover;
                    if (mover == 0) { myCnt++; if (oo == 1) { oppCnt--; u.capOppTake++; } }
                    else            { oppCnt++; if (oo == 0) { myCnt--; u.capOppTake++; } }
                }                                                // [ver4] 상대 소유칸 탈취
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
        u.capNew = 0; u.capOppTake = 0;                          // [ver4] 패스는 덮은 칸 없음
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
                        if (s > TARGET) break;
                        if (s == TARGET && colCnt[b] && tp && bp && rs > 0)
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
                        if (s > TARGET) break;
                        if (s == TARGET && colCnt[a] && tp && bp && rs > 0)
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

    // [개선3] TT 저장 교체 정책: 같은 포지션이면 depth-preferred,
    //   다른 포지션이면 옛 세대(age != curGen)를 우선 희생. negamax 두 곳에서 공용.
    inline void storeTT(TTEnt& slot, uint64_t hkey, int val, int bestMove,
                        int depth, uint8_t flag) {
        if (slot.key == hkey ? depth >= slot.depth
                             : (slot.age != curGen || depth >= slot.depth))
            slot = TTEnt{ hkey, val, bestMove, (int16_t)depth, flag, curGen };
    }

    // Negamax + AB + TT ---------------------------------------------------
    //   parentList==nullptr -> 루트: 전체스캔(genMoves). 그 외 -> 부모목록+적용수로 증분(deriveMoves).
    int negamax(int depth, int alpha, int beta, int ply,
                const vector<Move>* parentList, const Undo* appliedUndo, bool prevWasPass) {
        if (time_up()) return matStm();
        if (depth == 0) {
            // [ver4] 직전 수의 swing = 새 점령 + 2*소유권탈취. retake 스캔 게이트에 사용.
            //   루트 직속(appliedUndo==nullptr)은 depth>=1 이라 여기 도달 안 하지만 안전 가드.
            int lastSwing = appliedUndo ? (appliedUndo->capNew + 2 * appliedUndo->capOppTake)
                                        : INF;   // 정보 없으면 항상 스캔(보수적)
            return horizonEval(lastSwing);
        }

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

        // 무브 오더링: m.key(불변 유지) + 소유권탈취 swing + TT/killer 보너스를 비교 시점에만 가산.
        // (m.key를 직접 더하면 자식 deriveMoves로 survivor 복사될 때 키가 오염됨 -> 정렬키만 즉석 계산)
        // [ver3 개선1] m.key(=새 버섯 칸)만으론 상대 영토를 빼앗는 큰 수를 과소평가한다. 정렬 시점에
        //   owner 레이어를 직접 읽어 '이 수가 덮는 상대 소유칸 수'를 계산, swing=key+2*oppCov 로 정렬.
        //   (owner 읽기라 m.key 오염 없음. ordering 전용이므로 평가값/정확성과 무관.)
        int k0 = killer[ply][0], k1 = killer[ply][1];
        auto oppCovered = [&](const Move& m) -> int {
            if (isPass(m)) return 0;
            int cov = 0;
            for (int r = m.r1; r <= m.r2; r++)
                for (int c = m.c1; c <= m.c2; c++)
                    if (num[r][c] == 0 && owner[r][c] == 1) cov++;  // 상대 소유 점령칸을 덮음
            return cov;
        };
        // 정렬키를 수당 '한 번만' 계산해 캐시(ordBuf). 정렬 비교 안에서 oppCovered 를 반복 호출하면
        // O(area)*O(n log n) 로 폭증하므로 반드시 사전 계산. enc 를 상위 비트로 묶어 타이브레이크.
        vector<int>& okey = ordBuf[ply];
        okey.resize(buf.size());
        for (size_t i = 0; i < buf.size(); i++) {
            const Move& m = buf[i];
            int swing = m.key + (ordSwing ? 2 * oppCovered(m) : 0);  // ORD_SWING=0이면 원본 동치
            int e = encMove(m);
            int s;
            if (e == ttMove)            s = swing + ORD_TT;
            else if (e == k0 || e == k1) s = swing + ORD_KILLER;
            else { int h = history[e]; s = swing + (h > ORD_HIST_CAP ? ORD_HIST_CAP : h); }
            okey[i] = s;
        }
        // 인덱스 정렬(키 동률 시 encMove 오름차순 → 결정적). buf 와 okey 를 함께 재배열.
        {
            static thread_local vector<int> idx;
            idx.resize(buf.size());
            for (size_t i = 0; i < buf.size(); i++) idx[i] = (int)i;
            sort(idx.begin(), idx.end(), [&](int a, int b) {
                if (okey[a] != okey[b]) return okey[a] > okey[b];
                return encMove(buf[a]) < encMove(buf[b]);
            });
            static thread_local vector<Move> tmp;
            tmp.resize(buf.size());
            for (size_t i = 0; i < buf.size(); i++) tmp[i] = buf[idx[i]];
            buf.swap(tmp);
        }

        int best = -INF, bestMove = -1;

        // [수정: 상대가 패스했을 때 연속 패스로 즉시 종료하는 옵션을 최우선 확보]
        if (prevWasPass) {
            best = matStm();
            bestMove = encMove(PASS_MOVE);
            if (ply == 0) rootBest = PASS_MOVE;
            if (best > alpha) alpha = best;
            if (alpha >= beta) {
                uint8_t flag = (best <= origAlpha) ? 2 : (best >= beta ? 1 : 0);
                storeTT(slot, hkey, best, bestMove, depth, flag);
                return best;
            }
        }

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
                history[e] += depth * depth;   // [개선4] 깊은 곳의 컷오프일수록 큰 가중
                break;
            }
        }

        // 자발적 패스 후보: 아래 두 게이트 중 하나라도 충족 + 컷오프 전일 때만, 마지막에 평가.
        // (상대가 직전에 패스한 경우는 위에서 이미 최우선으로 평가했으므로 여기서는 !prevWasPass 일 때만.)
        //  (A) 기존: 좁은 포지션(합법수<=passGate) && 비열세(matStm>=0).
        //  (B) [ver3 개선6] 리드 잠금: 합법수가 많아도, 내가 충분히 앞서고(matStm>=리드마진) 게다가
        //      내 모든 착수가 상대 리테이크를 열어줄 위험이 큰 국면이면 PASS 를 후보로. 후반에
        //      불필요한 수로 영토를 흔들다 역전당하는 패턴을 줄인다. 단 '후보 추가'일 뿐 강제 아님 —
        //      negamax 가 PASS 후 상대 응수까지 보고 일반 수보다 나을 때만 실제 선택한다(오판 방지).
        bool gateNarrow = (passGate > 0 && (int)buf.size() <= passGate && matStm() >= 0);
        bool gateLead   = false;
        if (!gateNarrow && wRecap && matStm() >= LEAD_MARGIN) {
            // 내 점령칸 중 상대가 한 수로 되받을 수 있는 양이 클수록 '두면 손해' 국면.
            int myU, opU, myBest, oppBest;
            scanRetake(myU, opU, myBest, oppBest, /*needMark=*/false);  // best 만 필요(저렴)
            int myThreat = (to_move == 0 ? myBest : oppBest);   // '나'(둘 차례)의 점령칸 위협
            int myGain   = (to_move == 0 ? oppBest : myBest);   // 내가 둬서 얻을 수 있는 양
            gateLead = (myThreat >= LEAD_PASS_THREAT && myGain <= myThreat);
        }
        if (!prevWasPass && (gateNarrow || gateLead)
            && alpha < beta && !timedOut) {
            int v;
            Undo u; applyPass(u);
            v = -negamax(depth - 1, -beta, -alpha, ply + 1, &buf, &u, true);
            undo(u);
            
            if (timedOut) return best > -INF ? best : matStm();
            if (v > best) {
                best = v; bestMove = encMove(PASS_MOVE);
                if (ply == 0) rootBest = PASS_MOVE;
            }
            if (v > alpha) alpha = v;
        }

        uint8_t flag = (best <= origAlpha) ? 2 : (best >= beta ? 1 : 0);
        // [개선3] 교체 정책: 같은 포지션이면 depth-preferred, 다른 포지션이면 옛 세대를 우선 희생.
        storeTT(slot, hkey, best, bestMove, depth, flag);
        return best;
    }

    // 실보드(State) → 작업보드 복사 + hash 재계산. to_move/hashSeed는 호출부가 지정.
    //   hashSeed: decide=0(내 차례), ponderOppNode=ZtoMove(패리티 오프셋).
    void loadBoard(const State& S, int sideToMove, uint64_t hashSeed) {
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) { num[r][c] = S.num[r][c]; owner[r][c] = S.owner[r][c]; }
        myCnt = S.myCnt; oppCnt = S.oppCnt; to_move = sideToMove;
        hash = hashSeed;
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) hash ^= cellHash(r, c, num[r][c], owner[r][c]);
        for (auto& k : killer) { k[0] = -1; k[1] = -1; }
        nodes = 0; timedOut = false; reachedDepth = 0;
    }

    // 작업보드의 남은 버섯 수(nz) → 탐색 최대 깊이 상한.
    int countNonzero() const {
        int nz = 0;
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (num[r][c]) nz++;
        return nz;
    }

    // 턴당 시간예산: base(자기보정 분할) + 분기게이트 부스트. nLegal/nz 는 decide() 에서 이미 계산됨.
    //   기본 knobs(게이트 off)면 기존 decideBudget 공식과 동치. python 미러: logAnalyzer/analyze.py decide_budget().
    int budgetFor(int t1, int nz, int nLegal) const {
        int avail = max(1, t1 - BUDGET_MARGIN_MS);    // flush/overhead 안전마진
        int estTurns = max(4, nz / turnDiv);          // 남은 내 턴 추정 → 균등 분할 base
        int budget = avail / estTurns;
        int capMs = moveCapMs, fracCap = fracDiv;     // 평시 상한
        if (gateLo <= gateHi && nLegal >= gateLo && nLegal <= gateHi) {  // 긴장 국면만 부스트
            budget = (int)((long long)budget * effBoostPct() / 100);   // [ver6 개선D]
            capMs = boostCapMs; fracCap = effBoostFrac();              // [ver6 개선D]
        }
        // [ver3 개선2] 종반 solve-to-terminal: 남은 버섯이 적으면 트리가 작아 깊이 풀 수 있는데
        //   평시 캡(capMs/fracCap)이 탐색을 일찍 끊을 수 있다. nz<=endgameNz 면 캡을 대폭 완화해
        //   정확한 minimax 값으로 종반을 두게 한다. 단 t1/3 TLE 안전캡은 아래서 항상 유지된다.
        if (nz <= effEndgameNz()) {                   // [ver6 개선C] 2P 에서만 임계값 확대 가능
            capMs = max(capMs, boostCapMs);           // 평시 상한을 부스트 수준으로 올림
            fracCap = min(fracCap, 2);                // 잔여의 최대 1/2 까지 허용(t1/3 캡이 결국 지배)
        }
        // [ver6 개선F] 2P 첫 응수 부스트: 내 점령칸 0 && 상대 점령칸>0 (= 선공 첫 수 직후 내 첫 응수).
        //   budget/캡 산출 직후, 최종 안전캡(t1/3 등) 적용 전에만 부스트. budget 선택에만 영향.
        if (amSecond && has_W2_REPLY_BOOST && myCnt == 0 && oppCnt > 0) {
            budget = (int)((long long)budget * w2ReplyBoost / 100);
            if (has_W2_REPLY_FRAC && w2ReplyFrac > 0) {
                fracCap = min(fracCap, w2ReplyFrac);
            }
        }
        budget = min(budget, capMs);
        budget = min(budget, t1 / fracCap);           // 잔여시간 비율 상한(고갈 방지)
        // [2P_can_win_ver1·hardened] 시간 바닥 가드: 켜졌을 때만(timeFloorTurns>0) 동작.
        //   ── 상한(단조 비고갈) ──
        //   한 수 예산이 잔여시간의 1/reserveTurns 를 넘지 못하게 추가 상한.
        //   reserveTurns = max(timeFloorTurns, nz/turnEstDiv). nz/turnEstDiv 가 종반에 줄더라도
        //   timeFloorTurns 가 바닥을 고정 → 어떤 국면에서도 한 수는 잔여의 1/timeFloorTurns 이하.
        //   (기존엔 reserveTurns 가 nz 와 함께 줄어 종반에 상한이 풀렸다. 그 약점을 제거.)
        //   ── 하한(종반 최소예산) ──
        //   nz<=floorEndgameNz 인 진짜 종반에서 floorMinMoveMs(>0)면 한 수가 그 이하로 깎이지 않게
        //   끌어올린다 → 얕은 탐색으로 인한 gateLead 오판 PASS 를 막는다. 단 t1/3·avail 안전캡은
        //   아래/리턴에서 항상 우선하므로 시계를 비우는 일은 없다.
        //   기본 timeFloorTurns==0 이면 이 블록 전체가 비활성 → ver6 와 완전 동치(BENCH/VERIFY 동일).
        if (timeFloorTurns > 0) {
            int estMyTurns = max(1, nz / max(1, turnEstDiv));   // 남은 내 턴 추정(기본 nz/2)
            int reserveTurns = max(timeFloorTurns, estMyTurns); // 바닥을 timeFloorTurns 로 고정
            budget = min(budget, max(1, t1 / reserveTurns));    // 단조 비고갈 상한
            if (floorMinMoveMs > 0 && nz <= floorEndgameNz) {   // 종반 최소예산(상한 적용 후)
                int floorMs = min(floorMinMoveMs, t1 / 3);      // 최소예산도 t1/3 을 넘지 않게
                budget = max(budget, floorMs);
            }
        }
        budget = min(budget, t1 / 3);                 // TLE 안전: 한 수가 시계를 비우지 못하게(항상)
        budget = max(30, budget);
        return min(budget, avail);
    }

    // 루트에서 한 수 결정 ---------------------------------------------------
    //   t1 = 내게 남은 누적시간(ms). 예산은 genMoves(nLegal 확보) 직후 budgetFor 로 산출.
    //   prevWasPass = 상대의 직전 행동이 패스였는지(= 내가 지금 패스하면 연속패스로 종료).
    Move decide(const State& S, int t1, bool prevWasPass) {
        loadBoard(S, /*sideToMove=*/0, /*hashSeed=*/0);
        curGen++;   // [개선3] 새 턴 = 새 세대. 이전 턴 엔트리는 이제 '옛것'으로 간주돼 우선 희생.

        vector<Move> rootMoves;
        genMoves(rootMoves);
        if (rootMoves.empty()) return PASS_MOVE;   // 둘 수 없음 -> 패스

        // 남은 버섯 수로 최대 깊이 상한
        int nz = countNonzero();
        int maxDepth = min(MAXD - 1, max(1, nz));

        // 예산 산출(분기게이트) 후 deadline 설정. nLegal = 루트 합법수.
        int budget_ms = budgetFor(t1, nz, (int)rootMoves.size());
        auto t0 = Clock::now();
        deadline = t0 + chrono::milliseconds(max(1, budget_ms));

        // 시간 없는 즉답: depth1(=탐욕)만이라도 보장
        Move committed = rootMoves[0];
        for (const Move& m : rootMoves) if (m.key > committed.key) committed = m;

        int prevVal = 0; bool haveVal = false;
        // [ver3 개선5] 전략 가중치(wRetake/wCorner/wRecap)가 켜지면 한 수에 평가가 material 외
        //   추가 항으로 출렁여 3*W_MAT(=3칸) 창이 좁을 수 있다 → fail-low/high 재탐색 폭증.
        //   변동폭을 반영해 초기 반폭과 full-window 전환 상한을 함께 키운다(둘을 같이 키워야
        //   몇 번 fail에 곧장 full-window 로 떨어지지 않음).
        // [ver5] 공/수 분리 + best 항 반영. 2P 비대칭 시 유효 계수로 폭 산정(재탐색 폭증 방지).
        //   기본값: effAttackOpp+effDefendMine == 2*wRetake → ver4 evalSwing 과 동치.
        // [ver6] 디나이얼(A/A2)은 leaf 에서 TARGET 칸까지 출렁일 수 있고, 되잡기는 2P 강화값(effRecap)
        //   사용. aspiration 폭에 반영해 fail-low/high 재탐색 폭증을 막는다.
        const int evalSwing = effAttackOpp() + effDefendMine()
                            + effAttackBestOpp() + effDefendBestMine()
                            + wCorner + 2 * effRecap()
                            + TARGET * (effDenial() + effDenialTop2())  // 대략적 추가 변동폭
                            + effMobility() * (effMobilityCap() > 0 ? effMobilityCap() : 16)  // [G]
                            + (effTermScore() ? W_MAT * 2 : 0);          // [H] material 가중 출렁임 근사
        const int aspInit = 3 * W_MAT + evalSwing;                 // 초기 반폭
        const int aspFull = 12 * W_MAT + 4 * evalSwing;            // 이 이상이면 full-window
        for (int d = 1; d <= maxDepth; d++) {
            // [개선4] aspiration: 좁은 창으로 시작하되, 빗나가면 곧장 무한대로 가지 않고
            //   해당 방향만 폭을 2배씩 점진 확장(반대쪽 경계는 유지) -> 재탐색 비용 최소화.
            int a = -INF, b = INF;
            int delta = aspInit;                       // 초기 반폭(전략 변동폭 반영). fail마다 2배.
            if (haveVal && d >= 3) { a = prevVal - delta; b = prevVal + delta; }
            int v = 0;
            for (;;) {
                timedOut = false;
                rootBest = committed;
                v = negamax(d, a, b, 0, nullptr, nullptr, prevWasPass);
                if (timedOut) break;
                if (v <= a && a > -INF) {              // fail-low -> 아래 경계만 확장
                    delta *= 2;
                    a = (delta >= aspFull) ? -INF : prevVal - delta;
                    continue;
                }
                if (v >= b && b <  INF) {              // fail-high -> 위 경계만 확장
                    delta *= 2;
                    b = (delta >= aspFull) ? INF : prevVal + delta;
                    continue;
                }
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
        loadBoard(S, /*sideToMove=*/1, /*hashSeed=*/ZtoMove);   // 상대 차례 + 패리티 오프셋
        deadline = Clock::now() + chrono::hours(1);             // 사실상 무한 — stopFlag 로 종료

        int nz = countNonzero();
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

    Move pick(int t1) {
        if (t1 <= GREEDY_T1_MS) {               // 시간 거의 없음: 탐욕 즉답
            vector<Move> m; solver_genGreedy(m);
            if (m.empty()) return PASS_MOVE;
            Move best = m[0];
            for (auto& x : m) if (x.key > best.key) best = x;
            return best;
        }
        // 예산 산출은 solver.decide 내부(budgetFor, 분기게이트). 여기선 잔여시간 t1 만 전달.
        solver.amSecond = !first;   // [ver5] 평가 계수 선택에만 영향(TT/패리티/패스/폰더 무관)
        return solver.decide(S, t1, oppLastWasPass);
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
            game.solver.decide(game.S, 400, false);  // t1=400ms → budgetFor ~30-50ms(depth>=2 도달, deriveMoves 검증). VERIFY_MOVES가 노드마다 대조
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
    s.tt.assign(s.tt.size(), Solver::TTEnt{ 0,0,-1,0,0,0 });
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