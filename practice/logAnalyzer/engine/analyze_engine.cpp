// ============================================================================
//  analyze_engine.cpp — 패배 로그 분석용 "루트 채점" CLI
//
//  engine.hpp(submission/main.cpp 엔진 스냅샷)를 재사용해, 한 포지션에서
//  모든 합법수를 가지치기 없이 전수 평가(정확한 per-move eval)하고 JSON으로 출력.
//
//  입력(stdin, 토큰 순서 고정):
//    NUM                       # 키워드
//    <10줄 × 17 digit>         # 숫자 레이어 (제거=0)
//    OWN                       # 키워드
//    <10줄 × 17 char>          # 소유권: '.'=없음, '0'=둘차례(player0), '1'=상대
//    PLAYED r1 c1 r2 c2        # 채점할 실제 둔 수 (패스 = -1 -1 -1 -1)
//    MAXDEPTH D                # 루트 반복 채점 깊이 상한
//    CAPMS ms                  # 포지션당 시간 상한(루트 deepening) — 초과 시 직전 완료 depth 사용
//    BUDGETS m1 m2 ...         # (선택) decide() 시간예산 스윕. 없으면 키워드만.
//
//  ※ side-to-move 는 항상 player0 으로 정규화돼 들어온다(상대 턴 채점은 Python이 스왑).
//  ※ myCnt/oppCnt 는 owner 레이어에서 직접 카운트(단일 출처).
// ============================================================================
#include "engine.hpp"

static string moveJson(const Move& m) {
    ostringstream o;
    o << "[" << (int)m.r1 << "," << (int)m.c1 << "," << (int)m.r2 << "," << (int)m.c2 << "]";
    return o.str();
}
static bool sameMove(const Move& a, const Move& b) {
    if (isPass(a) || isPass(b)) return isPass(a) && isPass(b);
    return a.r1 == b.r1 && a.c1 == b.c1 && a.r2 == b.r2 && a.c2 == b.c2;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    string kw;
    // --- NUM ---
    cin >> kw; // "NUM"
    string numRow[R];
    for (int r = 0; r < R; r++) cin >> numRow[r];
    // --- OWN ---
    cin >> kw; // "OWN"
    string ownRow[R];
    for (int r = 0; r < R; r++) cin >> ownRow[r];
    // --- PLAYED ---
    cin >> kw; // "PLAYED"
    int pr1, pc1, pr2, pc2; cin >> pr1 >> pc1 >> pr2 >> pc2;
    Move played = { (int8_t)pr1,(int8_t)pc1,(int8_t)pr2,(int8_t)pc2, 0 };
    // --- MAXDEPTH ---
    cin >> kw; cin >> kw; int maxDepthReq = atoi(kw.c_str());
    // --- CAPMS ---
    cin >> kw; cin >> kw; int capMs = atoi(kw.c_str());
    // --- INCLUDEPASS ---  (1이면 합법수 있어도 PASS를 후보에 포함; 기본 0)
    cin >> kw; cin >> kw; int includePass = atoi(kw.c_str());
    // --- BUDGETS ---
    cin >> kw; // "BUDGETS"
    vector<int> budgets;
    { int b; while (cin >> b) budgets.push_back(b); }

    // 보드 적재 + State 보존(budgetSweep 용)
    Solver solver;
    State st;
    int myCnt = 0, oppCnt = 0;
    for (int r = 0; r < R; r++)
        for (int c = 0; c < C; c++) {
            int8_t n = (int8_t)(numRow[r][c] - '0');
            char oc = ownRow[r][c];
            int8_t o = (oc == '0') ? 0 : (oc == '1') ? 1 : -1;
            solver.num[r][c] = n; solver.owner[r][c] = o;
            st.num[r][c] = n;     st.owner[r][c] = o;
            if (o == 0) myCnt++; else if (o == 1) oppCnt++;
        }
    solver.myCnt = myCnt; solver.oppCnt = oppCnt; solver.to_move = 0;
    st.myCnt = myCnt; st.oppCnt = oppCnt;
    solver.primeFromBoard();

    // 후보수: 합법수 전부. PASS 는 (강제 패스 / 실제로 패스를 둠 / includePass) 일 때만 포함.
    //   ⚠ 탐색 내부 genMoves 는 PASS 를 만들지 않으므로(상대는 트리에서 패스 불가),
    //     루트에 PASS 를 넣으면 그 가치가 과대평가된다 → 기본은 제외(엔진 실제 선택지로 채점).
    vector<Move> roots; solver.genMoves(roots);
    int nLegal = (int)roots.size();
    bool playedIsPass = (pr1 < 0);
    if (includePass || playedIsPass || nLegal == 0) roots.push_back(PASS_MOVE);

    // 남은 버섯으로 깊이 상한 (decide 와 동일 정신)
    int nz = 0;
    for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (solver.num[r][c]) nz++;
    int maxDepth = min(maxDepthReq, min(MAXD - 1, max(1, nz)));

    struct RE { Move m; int eval; };
    struct TraceRow { int depth; Move best; int bestEval; int playedEval; bool playedFound; };
    vector<TraceRow> trace;
    vector<RE> finalEvals;   // 가장 깊게 완료된 depth 결과
    int usedDepth = 0;

    solver.deadline = Clock::now() + chrono::milliseconds(max(1, capMs));

    for (int d = 1; d <= maxDepth; d++) {
        solver.timedOut = false;
        vector<RE> evals; evals.reserve(roots.size());
        bool incomplete = false;
        for (const Move& m : roots) {
            int v;
            Solver::Undo u;
            if (isPass(m)) { solver.applyPass(u); v = -solver.negamax(d - 1, -INF, INF, 1); solver.undo(u); }
            else           { solver.apply(m, u);  v = -solver.negamax(d - 1, -INF, INF, 1); solver.undo(u); }
            if (solver.timedOut) { incomplete = true; break; }
            evals.push_back({ m, v });
        }
        if (incomplete) break;

        usedDepth = d; finalEvals = evals;
        // depth d 의 best / playedEval 기록
        int bi = 0;
        for (int i = 1; i < (int)evals.size(); i++) if (evals[i].eval > evals[bi].eval) bi = i;
        int pe = 0; bool pf = false;
        for (auto& e : evals) if (sameMove(e.m, played)) { pe = e.eval; pf = true; break; }
        trace.push_back({ d, evals[bi].m, evals[bi].eval, pe, pf });

        if (Clock::now() >= solver.deadline) break;
    }

    // 최종 메트릭 (가장 깊은 완료 depth)
    int bestEval = -INF, worstEval = INF, bestIdx = -1;
    for (int i = 0; i < (int)finalEvals.size(); i++) {
        if (finalEvals[i].eval > bestEval) { bestEval = finalEvals[i].eval; bestIdx = i; }
        if (finalEvals[i].eval < worstEval) worstEval = finalEvals[i].eval;
    }
    // played rank/quality (sorted desc copy)
    vector<RE> sorted = finalEvals;
    sort(sorted.begin(), sorted.end(), [](const RE& a, const RE& b) { return a.eval > b.eval; });
    int playedEval = 0, rank = -1; bool playedFound = false;
    for (int i = 0; i < (int)sorted.size(); i++)
        if (sameMove(sorted[i].m, played)) { playedEval = sorted[i].eval; rank = i + 1; playedFound = true; break; }

    double cellLoss = playedFound ? (double)(bestEval - playedEval) / Solver::W_MAT : 0.0;
    double qualityPct = 100.0;
    if (playedFound && bestEval != worstEval)
        qualityPct = (double)(playedEval - worstEval) * 100.0 / (double)(bestEval - worstEval);

    // convergenceDepth: played 가 "최적"(= best와 동일 평가, 동률 허용)으로 평가되는 첫 depth.
    // 엔진의 유일 argmax 와 일치하는지가 아니라, 손해 0(동률 best)인지를 본다.
    int convDepth = -1;
    for (auto& t : trace) if (t.playedFound && t.playedEval == t.bestEval) { convDepth = t.depth; break; }

    // budgetSweep: decide() 재현 + 선택수의 cellLoss(가장 깊은 depth 기준).
    //   "예산 ms 로 두면 엔진이 무슨 수를 두고, 그게 최선 대비 몇 칸 손해인가"를 본다.
    struct Sweep { int ms; Move m; double cl; bool clFound; };
    vector<Sweep> sweep;
    for (int ms : budgets) {
        Move mv = solver.decide(st, ms);
        double cl = 0.0; bool clf = false;
        for (auto& e : finalEvals) if (sameMove(e.m, mv)) { cl = (double)(bestEval - e.eval) / Solver::W_MAT; clf = true; break; }
        sweep.push_back({ ms, mv, cl, clf });
    }

    // ---- JSON 출력 ----
    ostringstream o;
    o << "{";
    o << "\"nLegal\":" << nLegal;
    o << ",\"usedDepth\":" << usedDepth;
    o << ",\"nodes\":" << solver.nodes;
    o << ",\"bestMove\":" << (bestIdx >= 0 ? moveJson(finalEvals[bestIdx].m) : "null");
    o << ",\"bestEval\":" << bestEval;
    o << ",\"played\":{";
    o << "\"move\":" << moveJson(played);
    o << ",\"found\":" << (playedFound ? "true" : "false");
    o << ",\"eval\":" << (playedFound ? playedEval : 0);
    o << ",\"rank\":" << rank;
    o << ",\"qualityPct\":" << qualityPct;
    o << ",\"cellLoss\":" << cellLoss;
    o << "}";
    o << ",\"convergenceDepth\":" << (convDepth >= 0 ? to_string(convDepth) : string("null"));
    o << ",\"trace\":[";
    for (size_t i = 0; i < trace.size(); i++) {
        if (i) o << ",";
        o << "{\"depth\":" << trace[i].depth
          << ",\"best\":" << moveJson(trace[i].best)
          << ",\"bestEval\":" << trace[i].bestEval
          << ",\"playedEval\":" << (trace[i].playedFound ? to_string(trace[i].playedEval) : string("null"))
          << "}";
    }
    o << "]";
    o << ",\"budgetSweep\":[";
    for (size_t i = 0; i < sweep.size(); i++) {
        if (i) o << ",";
        o << "{\"ms\":" << sweep[i].ms << ",\"move\":" << moveJson(sweep[i].m)
          << ",\"cellLoss\":" << (sweep[i].clFound ? to_string(sweep[i].cl) : string("null")) << "}";
    }
    o << "]";
    o << "}";
    cout << o.str() << "\n";
    return 0;
}
