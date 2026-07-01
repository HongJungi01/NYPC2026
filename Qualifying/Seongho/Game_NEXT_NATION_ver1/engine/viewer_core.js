/* ============================================================
   NEXT NATION viewer_core.js
   로그(log.txt) → 스냅샷 배열 → 원형 보로노이 보드 렌더
   외부 의존: d3-delaunay (보로노이 셀 생성용)
   이미지: assets/ 폴더의 PNG가 있으면 사용, 없으면 SVG 도형 폴백
   ============================================================ */

/* ---------- 규칙 상수 (sample-code.py 와 동일) ---------- */
const START_GOLD = 500, START_WARRIORS = 3;
const MOVE_COST = 10, TRAIN_COST = 120, WORK_INCOME = 15, UPKEEP = 2;
const HQ_MAX = 5, BASE_MAX = 3, HQ_HEAL = 1000, BASE_HEAL = 500;
// [upgrade_cost, warrior_hp, hp, turret, train_cap, work_cap]
const HQ_LEVELS = [
  [0,0,0,0,0,0],[0,4,10,1,1,1],[600,5,15,2,1,2],
  [1200,6,20,2,2,3],[2400,7,25,3,2,4],[3600,8,30,3,3,5]
];
// [cost, hp, turret, work_cap]
const BASE_LEVELS = [[0,0,0,0],[300,6,1,1],[600,12,1,2],[1000,18,2,3]];

/* ---------- 이미지 자산 ---------- */
/* game_elements_png 폴더의 공식 PNG를 우선 사용하고, 파일이 없으면 SVG 도형으로 폴백한다. */
const ASSET_SRC = {
  hqA:    'game_elements_png/A_headquarters.png',
  hqB:    'game_elements_png/B_headquarters.png',
  baseA:  'game_elements_png/A_base.png',
  baseB:  'game_elements_png/B_base.png',
};
const ASSETS = {}; // name -> {ok:bool, img:HTMLImageElement}
function preloadAssets(){
  for(const [name,src] of Object.entries(ASSET_SRC)){
    const img = new Image();
    ASSETS[name] = {ok:false, img};
    img.onload  = ()=>{ ASSETS[name].ok = true; if(STATE.snaps.length) render(); };
    img.onerror = ()=>{ ASSETS[name].ok = false; };
    img.src = src;
  }
}

/* ============================================================
   1) 파서
   ============================================================ */
function parseLog(text){
  const lines = text.split(/\r?\n/);
  let i = 0;
  const map = { N:0, K:0, x:[], y:[], strongholds:[], adj:[] };
  let result = null;

  // --- MAP 블록 ---
  while(i<lines.length && lines[i].trim()!=='MAP') i++;
  if(i>=lines.length) throw new Error('MAP 블록을 찾을 수 없습니다.');
  i++; // skip "MAP"
  {
    const [N,K] = lines[i++].trim().split(/\s+/).map(Number);
    map.N=N; map.K=K;
    map.x = lines[i++].trim().split(/\s+/).map(Number);
    map.y = lines[i++].trim().split(/\s+/).map(Number);
    const sh = lines[i++].trim().split(/\s+/);
    // "STRONGHOLDS a b c ..."
    map.strongholds = sh.slice(1).map(Number);
    map.adj = [];
    for(let r=0;r<N;r++){
      const t = lines[i++].trim().split(/\s+/).map(Number);
      const deg = t[0];
      map.adj.push(t.slice(1,1+deg));
    }
    // END MAP
    while(i<lines.length && lines[i].trim()!=='END MAP') i++;
    i++;
  }

  // --- 턴 블록들 ---
  const turns = [];
  while(i<lines.length){
    const ln = lines[i].trim();
    if(ln.startsWith('RESULT ')){ result = ln; break; }
    if(/^TURN \d+$/.test(ln)){
      const day = Number(ln.split(/\s+/)[1]);
      i++;
      const t = { day, cmdLeft:[], cmdRight:[], time:null,
                  upgrades:[], trained:[], moves:[], damages:[], sieges:[] };
      // COMMAND LEFT START ... END
      const readCmd = ()=>{
        const out=[];
        // expect "COMMAND <SIDE> START"
        i++; // skip START line
        while(i<lines.length && !/^COMMAND (LEFT|RIGHT) END$/.test(lines[i].trim())){
          out.push(lines[i].trim()); i++;
        }
        i++; // skip END line
        return out;
      };
      if(/^COMMAND LEFT START$/.test(lines[i].trim())) t.cmdLeft = readCmd();
      if(/^COMMAND RIGHT START$/.test(lines[i].trim())) t.cmdRight = readCmd();

      // TURN d RESULT
      while(i<lines.length && !/RESULT$/.test(lines[i].trim()) && !/^TURN \d+ RESULT$/.test(lines[i].trim())) i++;
      i++; // skip "TURN d RESULT"
      // result lines until "END TURN d"
      while(i<lines.length && !/^END TURN \d+$/.test(lines[i].trim())){
        const p = lines[i].trim().split(/\s+/);
        switch(p[0]){
          case 'TIME': // TIME LEFT ms tok RIGHT ms tok
            t.time = { lMs:+p[2], lTok:+p[3], rMs:+p[5], rTok:+p[6] }; break;
          case 'UPGRADE': // UPGRADE <A|B> <region>
            t.upgrades.push({side:p[1], region:+p[2]}); break;
          case 'TRAIN': // TRAIN id id ...
            t.trained = p.slice(1); break;
          case 'MOVE': // MOVE id region
            t.moves.push({id:p[1], region:+p[2]}); break;
          case 'DAMAGE': // DAMAGE cause id dmg
            t.damages.push({cause:p[1], id:p[2], dmg:+p[3]}); break;
          case 'SIEGE': // SIEGE <A|B> region dmg
            t.sieges.push({side:p[1], region:+p[2], dmg:+p[3]}); break;
        }
        i++;
      }
      i++; // skip END TURN
      turns.push(t);
    } else { i++; }
  }
  return { map, turns, result };
}

/* ============================================================
   2) 스냅샷 재구성
   초기 상태 + 각 턴 델타를 누적하여 day별 전체 상태 생성.
   금화는 로그에 없으므로 비용/수입 산식으로 추정.
   ============================================================ */
function sideOf(id){ return id[0]==='A' ? 'A' : 'B'; }

function buildSnapshots(parsed){
  const {map, turns} = parsed;
  const hqL = 0, hqR = map.N-1;

  // day 0 초기 상태
  const warriors = {}; // id -> {side, region, hp, state, target}
  for(let s=1;s<=START_WARRIORS;s++){
    warriors['A'+s] = {side:'A', region:hqL, hp:HQ_LEVELS[1][1], maxHp:HQ_LEVELS[1][1], state:'S', target:null};
    warriors['B'+s] = {side:'B', region:hqR, hp:HQ_LEVELS[1][1], maxHp:HQ_LEVELS[1][1], state:'S', target:null};
  }
  const buildings = {}; // region -> {side, type, level, hp}
  buildings[hqL] = {side:'A', type:'HQ', level:1, hp:HQ_LEVELS[1][2]};
  buildings[hqR] = {side:'B', type:'HQ', level:1, hp:HQ_LEVELS[1][2]};
  let gold = {A:START_GOLD, B:START_GOLD};
  let tokens = {A:5, B:5};

  const maxHp = (b)=> b.type==='HQ' ? HQ_LEVELS[b.level][2] : BASE_LEVELS[b.level][1];
  const workCap = (b)=> b.type==='HQ' ? HQ_LEVELS[b.level][5] : BASE_LEVELS[b.level][3];

  const snaps = [];
  // 헬퍼: 현 상태 깊은 복사로 스냅샷 저장
  const snapshot = (day, events, time)=>({
    day,
    warriors: JSON.parse(JSON.stringify(warriors)),
    buildings: JSON.parse(JSON.stringify(buildings)),
    gold:{...gold}, tokens:{...tokens}, events, time
  });

  snaps.push(snapshot(0, [], null)); // 시작 상태

  for(const t of turns){
    const events = [];
    if(t.time){ tokens = {A:t.time.lTok, B:t.time.rTok}; }
    const commandBuildings = JSON.parse(JSON.stringify(buildings));

    // --- 명령에서 "내가 낸 비용" 추정 ---
    // UPGRADE 비용: 명령 제출 측 기준. 로그 UPGRADE 는 양측 합쳐 결과만 줌.
    //   여기서는 결과 UPGRADE 리스트로 비용을 계산(소속 side 의 지갑에서 차감).
    for(const up of t.upgrades){
      const side = up.side==='A'?'A':'B';
      const b = buildings[up.region];
      if(!b){
        // 신규 기지 건설
        gold[side] -= BASE_LEVELS[1][0];
        buildings[up.region] = {side, type:'BASE', level:1, hp:BASE_LEVELS[1][1]};
        events.push({kind:'up', text:`${side} 기지 건설 @${up.region}`});
      } else if(b.side===side){
        const isHQ = b.type==='HQ';
        const mx = isHQ?HQ_MAX:BASE_MAX;
        if(b.level>=mx){
          gold[side] -= isHQ?HQ_HEAL:BASE_HEAL;
          b.hp = maxHp(b);
          events.push({kind:'up', text:`${side} ${b.type} 수리 @${up.region}`});
        } else {
          const cost = isHQ?HQ_LEVELS[b.level+1][0]:BASE_LEVELS[b.level+1][0];
          gold[side] -= cost;
          b.level++; b.hp = maxHp(b);
          events.push({kind:'up', text:`${side} ${b.type} → Lv${b.level} @${up.region}`});
        }
      }
    }

    // --- 이동 비용: 명령 측이 새로 낸 MOVE 명령에 대해서만 발생 ---
    // 결과 MOVE 는 "실제 한 칸 이동"이라 신규 명령과 다름.
    // 신규 명령 비용은 cmdLeft/cmdRight 의 MOVE 라인에서 추정.
    const chargeMoves = (cmds, side)=>{
      for(const c of cmds){
        const p=c.split(/\s+/);
        if(p[0]==='MOVE'){
          const id=p[1];
          const target=+p[2];
          if(warriors[id] && warriors[id].side===side && Number.isInteger(target)){
            warriors[id].target=target;
            warriors[id].state='M';
          }
          const b = commandBuildings[target];
          const cost = (b && b.side===side)?0:MOVE_COST;
          gold[side]-=cost;
        }
      }
    };
    chargeMoves(t.cmdLeft,'A'); chargeMoves(t.cmdRight,'B');

    // --- 훈련 비용 ---
    const trA = t.trained.filter(x=>x[0]==='A').length;
    const trB = t.trained.filter(x=>x[0]==='B').length;
    gold.A -= TRAIN_COST*trA; gold.B -= TRAIN_COST*trB;

    // --- 결과 MOVE 적용(전사 위치 갱신) ---
    for(const mv of t.moves){
      if(!warriors[mv.id]){
        // 이동 결과에 처음 등장하는 전사는 거의 없음. 방어적 생성.
        warriors[mv.id] = {side:sideOf(mv.id), region:mv.region, hp:HQ_LEVELS[1][1], maxHp:HQ_LEVELS[1][1], state:'S', target:null};
      } else {
        warriors[mv.id].region = mv.region;
        if(warriors[mv.id].target===mv.region){
          warriors[mv.id].target=null;
          warriors[mv.id].state='S';
        }
      }
      events.push({kind:'move', text:`${mv.id} → ${mv.region}`});
    }

    // --- 훈련된 전사 추가 ---
    for(const id of t.trained){
      const side = sideOf(id);
      const hqReg = side==='A'?hqL:hqR;
      const hqB = buildings[hqReg];
      const lvl = hqB?hqB.level:1;
      warriors[id] = {side, region:hqReg, hp:HQ_LEVELS[lvl][1], maxHp:HQ_LEVELS[lvl][1], state:'S', target:null};
      events.push({kind:'train', text:`${side} 전사 ${id} 훈련`});
    }

    // --- DAMAGE 적용 ---
    for(const d of t.damages){
      const w = warriors[d.id];
      if(w){ w.hp -= d.dmg; }
      const causeKo = d.cause==='TURRET'?'포탑':d.cause==='COMBAT'?'전투':'식비';
      events.push({kind:'dmg', text:`${d.id} −${d.dmg} (${causeKo})`});
    }
    // 체력 0 이하 전사 퇴각
    for(const id of Object.keys(warriors)){
      if(warriors[id].hp<=0) delete warriors[id];
    }

    // --- SIEGE 적용 ---
    for(const s of t.sieges){
      const b = buildings[s.region];
      if(b){ b.hp -= s.dmg; }
      events.push({kind:'siege', text:`${s.side} 건물 @${s.region} −${s.dmg}`});
    }
    for(const r of Object.keys(buildings)){
      if(buildings[+r].hp<=0) delete buildings[+r];
    }

    // --- 저녁: 노동 수입 (전투 후 상태 기준) ---
    // 각 건물에서 15 × min(아군 전사 수, work_cap)
    // 단, 규칙상 전사 한 명은 한 건물에만 동원. 같은 구역 다건물은 드물어 근사.
    for(const r of Object.keys(buildings)){
      const b = buildings[+r];
      let cnt=0;
      for(const id of Object.keys(warriors)){
        const w=warriors[id];
        if(w.side===b.side && w.region===+r) cnt++;
      }
      gold[b.side] += WORK_INCOME*Math.min(cnt, workCap(b));
    }

    // --- 저녁: 식비 (생존 전사 1인당 2, 부족분은 이미 DAMAGE HUNGER 로 반영됨) ---
    // HUNGER 피해는 위 DAMAGE 에서 처리됨. 여기서는 금화만 차감(있는 만큼).
    const aliveA = Object.values(warriors).filter(w=>w.side==='A').length;
    const aliveB = Object.values(warriors).filter(w=>w.side==='B').length;
    gold.A = Math.max(0, gold.A - UPKEEP*aliveA);
    gold.B = Math.max(0, gold.B - UPKEEP*aliveB);

    snaps.push(snapshot(t.day, events, t.time));
  }

  return snaps;
}

/* ============================================================
   3) 원형 보로노이 보드 렌더
   ============================================================ */
const STATE = {
  map:null, snaps:[], cells:null, layout:null,
  idx:0, playing:false, timer:null, result:null,
  selectedRegion:null
};
const SVG_NS='http://www.w3.org/2000/svg';
const VB = 1000; // viewBox 한 변
const VIEWER_UI_STYLE_ID = 'viewer-core-game-ui-upgrades';
const RULES_FILE = 'NEXT_NATION_게임규칙.md';
const EMBEDDED_RULES_MD = String.raw`
# NEXT NATION 게임 규칙

## 초기 상태
- 각 플레이어는 본부(레벨 1, HP 10), 금화 500, 전사 3명(HP 4)으로 시작합니다.
- 왼쪽 플레이어 전사: A1, A2, A3 / 오른쪽 플레이어 전사: B1, B2, B3

## 하루의 흐름
명령은 UPGRADE·MOVE·TRAIN 순서와 무관하게 제출하지만, 처리는 아래 순서로 진행됩니다.

**아침**: ① 건설 단계 → ② 이동 단계 → ③ 훈련 단계

**낮**: 전투 단계

**저녁**: ① 노동 단계 → ② 보급(식비) 단계

## 건설 UPGRADE d
- 본부 또는 거점에서만 가능합니다.
- 해당 구역에 아군 병사가 1명 이상 있어야 하고 적 병사는 없어야 합니다.
- 건물이 없으면 기지를 건설하고, 아군 건물이 있으면 업그레이드하거나 최대 레벨에서 수리합니다.
- 같은 구역에는 하루에 한 번만 건설 명령을 낼 수 있습니다.

## 본부 레벨 표

| 레벨 | 업그레이드 비용 | 전사 최대 HP | 본부 최대 HP | 포탑 공격력 | 훈련 가능 수 | 노동 가능 수 |
|---|---|---|---|---|---|---|
| 1 | – | 4 | 10 | 1 | 1 | 1 |
| 2 | 600G | 5 | 15 | 2 | 1 | 2 |
| 3 | 1,200G | 6 | 20 | 2 | 2 | 3 |
| 4 | 2,400G | 7 | 25 | 3 | 2 | 4 |
| 5 | 3,600G | 8 | 30 | 3 | 3 | 5 |
| 5+ 수리 | 1,000G | – | – | – | – | – |

## 기지 레벨 표

| 레벨 | 건설/업그레이드 비용 | 최대 HP | 포탑 공격력 | 노동 가능 수 |
|---|---|---|---|---|
| 1 | 300G | 6 | 1 | 1 |
| 2 | 600G | 12 | 1 | 2 |
| 3 | 1,000G | 18 | 2 | 3 |
| 3+ 수리 | 500G | – | – | – |

건물은 업그레이드 또는 수리 시 HP가 최대치까지 회복됩니다.

## 이동 MOVE a d
- 병사 a에게 최종 목적지 d를 지정합니다.
- 이동 명령 비용: 목적지가 아군 건물이면 **0G**, 그 외에는 **10G**.
- 이미 이동 중인 병사에게는 새 이동 명령을 내릴 수 없습니다.
- 매일 이동 단계에서, 현재 구역에 적 병사가 없을 때만 한 구역 이동합니다.
- 경로: 목적지까지 인접 구역 중심 간 유클리드 거리 올림값 합이 최소인 경로를 따릅니다.
- 최단 경로가 여러 개라면 다음 구역 번호가 더 작은 쪽을 선택합니다.

## 병사 생성 TRAIN n
- 본부에서 병사 n명을 생성합니다. (기지에서는 훈련 불가)
- 하루에 TRAIN 명령은 최대 한 번입니다.
- n은 0 이상, 본부 레벨의 훈련 가능 수 이하여야 합니다.
- 비용: 병사 1명당 **120G**. 생성된 병사의 최대 HP는 현재 본부 레벨에 따릅니다.

## 전투 (낮)
- 같은 구역에 양 진영이 존재할 때 전투가 발생합니다.
- 공격 횟수: **나의 전사 수 a번** 상대가 공격당하고, **상대 전사 수 b번** 내가 공격당합니다.
- 건물의 포탑은 전투 시작 시 포탑 공격력만큼 추가 공격을 수행합니다.
- 매 공격은 체력이 0이 아닌 적 병사 중 HP가 가장 낮은 병사를 대상으로 HP를 1 감소시킵니다.
- 동률이면 번호가 더 작은 병사가 먼저 피해를 받습니다.
- 공격할 적 병사가 없고 적 건물이 있으면 건물 HP가 1 감소합니다.
- 체력이 0이 된 병사는 전투가 끝난 뒤 퇴각합니다. (전투 중에는 계속 공격 횟수에 포함)

## 저녁
- **노동**: 각 건물에서 15 × min(아군 전사 수, 노동 가능 수)G를 획득합니다.
- **식비**: 병사 번호 오름차순으로 각 병사가 2G를 소모합니다.
  - 금화가 부족한 병사는 HP 1을 잃고, HP가 0이 되면 전장에서 벗어납니다.

## 시간 제한 및 초읽기
- 각 플레이어는 초읽기 **5개**를 가지고 시작합니다.
- 매 턴 **100ms** 이내에 명령(END)을 제출해야 합니다.
- 100ms 초과 시 ⌈초과ms / 100⌉개의 초읽기를 소모합니다.
- 남은 초읽기 없이 시간을 초과하면 TLE(시간 초과) 패배입니다.
- 유효하지 않은 명령 제출 시 즉시 WA(규칙 위반) 패배입니다.

## 게임 종료
- 200일 내에 한 플레이어의 본부가 파괴되면 상대가 승리합니다.
- 두 본부가 동시에 파괴되거나 200일이 지나도 양 본부가 남아 있으면 본부 HP가 더 높은 쪽이 승리합니다.
- 본부 HP도 같으면 무승부입니다.

---
*이 규칙은 내장 요약본입니다. 전체 규칙(인터랙션 프로토콜, 전장 생성 방식 등)은 NEXT_NATION_규칙(태훈_수정).md를 참조하세요.*
`;
const HQ_MARKER_SCALE = 2.25;
const WARRIOR_MARKER_SCALE = 2.43;

/* 중심 좌표를 원형 보드에 맞춰 정규화 + d3 Voronoi 셀 계산 */
function computeLayout(map){
  const N=map.N;
  // 좌표 정규화: 데이터는 원점 대칭, 반지름 ~10^4. 화면 원 안에 맞춤.
  let maxR=1;
  for(let k=0;k<N;k++) maxR=Math.max(maxR, Math.hypot(map.x[k], map.y[k]));
  const cx=VB/2, cy=VB/2, R=VB*0.46; // 보드 반지름
  const pts=[];
  for(let k=0;k<N;k++){
    const px = cx + (map.x[k]/maxR)*R;
    // SVG 는 y 아래로 증가 → 부호 반전해 수학 좌표계 느낌 유지
    const py = cy - (map.y[k]/maxR)*R;
    pts.push([px,py]);
  }
  // 원형 클리핑된 보로노이
  const delaunay = d3.Delaunay.from(pts);
  const pad=R*1.15;
  const voronoi = delaunay.voronoi([cx-pad, cy-pad, cx+pad, cy+pad]);
  const cells=[];
  for(let k=0;k<N;k++){
    const poly = voronoi.cellPolygon(k);
    cells.push(poly?clipToCircle(poly, cx, cy, R):null);
  }
  return {cx, cy, R, pts, cells};
}

/* 다각형을 원 안으로 클리핑(Sutherland–Hodgman, 원은 다각형 근사로) */
function clipToCircle(poly, cx, cy, R){
  // d3 셀은 첫 점이 끝에 중복(닫힌 형태) → 마지막 중복점 제거
  let p = poly.slice();
  if(p.length>1){
    const a=p[0], b=p[p.length-1];
    if(Math.abs(a[0]-b[0])<1e-6 && Math.abs(a[1]-b[1])<1e-6) p = p.slice(0,-1);
  }
  // 원을 72각형으로 근사(반시계). 화면 y-down 이라 시각적으로는 시계방향.
  const circle=[];
  const seg=72;
  for(let i=0;i<seg;i++){
    const ang=(i/seg)*Math.PI*2;
    circle.push([cx+Math.cos(ang)*R, cy+Math.sin(ang)*R]);
  }
  return clipPolygon(p, circle);
}
function clipPolygon(subject, clip){
  let output=subject.slice();
  for(let i=0;i<clip.length;i++){
    const A=clip[i], B=clip[(i+1)%clip.length];
    const input=output; output=[];
    if(input.length===0) break;
    // clip 다각형이 CCW(서명면적>0) → 내부는 edge 의 왼쪽: cross >= 0
    const inside=(p)=> (B[0]-A[0])*(p[1]-A[1])-(B[1]-A[1])*(p[0]-A[0]) >= 0;
    const inter=(P,Q)=>{
      const x1=P[0],y1=P[1],x2=Q[0],y2=Q[1];
      const x3=A[0],y3=A[1],x4=B[0],y4=B[1];
      const d=(x1-x2)*(y3-y4)-(y1-y2)*(x3-x4);
      if(Math.abs(d)<1e-9) return Q;
      const t=((x1-x3)*(y3-y4)-(y1-y3)*(x3-x4))/d;
      return [x1+t*(x2-x1), y1+t*(y2-y1)];
    };
    for(let j=0;j<input.length;j++){
      const cur=input[j], prv=input[(j+input.length-1)%input.length];
      if(inside(cur)){
        if(!inside(prv)) output.push(inter(prv,cur));
        output.push(cur);
      } else if(inside(prv)){
        output.push(inter(prv,cur));
      }
    }
  }
  return output;
}

function el(tag, attrs){
  const e=document.createElementNS(SVG_NS, tag);
  for(const k in attrs) e.setAttribute(k, attrs[k]);
  return e;
}

function escapeHtml(value){
  return String(value).replace(/[&<>"']/g, ch=>({
    '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;'
  }[ch]));
}

function render(){
  const svg=document.getElementById('board');
  svg.innerHTML='';
  if(!STATE.map || !STATE.snaps.length) return;
  const {map, layout}=STATE;
  const snap=STATE.snaps[STATE.idx];
  const SH=new Set(map.strongholds);
  const hqL=0, hqR=map.N-1;
  const selectedRegion = normalizeSelectedRegion();
  const selectedNeighbors = selectedRegion===null ? new Set() : new Set(map.adj[selectedRegion]||[]);
  let selectedRegionD = null;

  // 보드 바깥 원(테두리)
  svg.appendChild(el('circle',{cx:layout.cx, cy:layout.cy, r:layout.R+2,
    fill:'#fffdf4', stroke:'#c9d2df', 'stroke-width':2}));

  // --- 셀 ---
  for(let k=0;k<map.N;k++){
    const cell=layout.cells[k];
    if(!cell||cell.length<3) continue;
    const b=snap.buildings[k];
    let fill='var(--land)';
    if(SH.has(k)) fill='var(--gold-fill)';
    if(b){ fill = b.side==='A' ? 'var(--left)' : 'var(--right)'; }
    if(k===hqL) fill='var(--left-strong)';
    if(k===hqR) fill='var(--right-strong)';
    let stroke = SH.has(k)?'var(--gold)':'var(--line)';
    let strokeWidth = SH.has(k)?2:1;
    if(selectedNeighbors.has(k)){ stroke='#7a8798'; strokeWidth=Math.max(strokeWidth,1.8); }
    if(k===selectedRegion){ stroke='var(--gold)'; strokeWidth=3.4; }

    const d='M'+cell.map(p=>p[0].toFixed(1)+','+p[1].toFixed(1)).join('L')+'Z';
    const path=el('path',{d, fill,
      stroke, 'stroke-width':strokeWidth,
      'data-region':k, class:'cell',
      tabindex:0, role:'button', 'aria-label':regionTitle(k, snap)});
    const title=el('title',{});
    title.textContent=regionTitle(k, snap);
    path.appendChild(title);
    path.style.cursor='pointer';
    path.addEventListener('mousemove', e=>showTip(e,k));
    path.addEventListener('mouseleave', hideTip);
    path.addEventListener('click', ()=>onBoardClick(k));
    path.addEventListener('keydown', e=>{
      if(e.key==='Enter' || e.key===' '){
        e.preventDefault();
        onBoardClick(k);
      }
    });
    // 직접 플레이: 선택된 전사가 있는 칸 강조
    if(HP.active && HP.selWarrior){
      const sw=snap.warriors[HP.selWarrior];
      if(sw && sw.region===k){ path.setAttribute('stroke','var(--gold)'); path.setAttribute('stroke-width','3'); }
    }
    svg.appendChild(path);
    if(k===selectedRegion) selectedRegionD = d;
  }
  if(selectedRegionD) appendSelectedRegionHalo(svg, selectedRegionD);

  // --- 구역별 전사/건물 집계 후 마커 ---
  const warByRegion={};
  for(const id in snap.warriors){
    const w=snap.warriors[id];
    (warByRegion[w.region]=warByRegion[w.region]||[]).push({id,...w});
  }

  for(let k=0;k<map.N;k++){
    const [px,py]=layout.pts[k];
    const b=snap.buildings[k];
    // 건물 마커
    if(b){
      drawBuilding(svg, px, py, b);
      addRegionHitArea(svg, px, py, buildingHitRadius(b), k);
    }
    // 전사 수 배지
    const ws=warByRegion[k];
    if(ws&&ws.length){
      const a=ws.filter(w=>w.side==='A');
      const bb=ws.filter(w=>w.side==='B');
      const desiredWy=b ? py+warriorStackOffset(b) : py+25;
      const safeRadius=warriorGroupSafeRadius(a.length, bb.length);
      const [wx, wy]=regionMarkerAnchor(k, px, py, px, desiredWy, safeRadius);
      const scale=warriorScaleForAnchor(k, wx, wy, safeRadius);
      const warriorLayer=el('g',{});
      svg.appendChild(warriorLayer);
      addRegionHitArea(warriorLayer, wx, wy, 72*scale, k);
      if(a.length && bb.length){
        drawWarriorBadge(warriorLayer, wx-16*scale, wy, 'A', a, k, scale);
        drawWarriorBadge(warriorLayer, wx+16*scale, wy, 'B', bb, k, scale);
      } else if(a.length) {
        drawWarriorBadge(warriorLayer, wx, wy, 'A', a, k, scale);
      } else if(bb.length) {
        drawWarriorBadge(warriorLayer, wx, wy, 'B', bb, k, scale);
      }
    }
  }
  drawMoveGhosts(svg, snap);
  drawRegionNumbers(svg, snap);
  updatePanels(snap);
}

function addRegionHitArea(svg, x, y, r, region){
  const hit=el('circle',{cx:x, cy:y, r, fill:'transparent',
    'pointer-events':'all', 'data-region':region, class:'cell-hit'});
  hit.style.cursor='pointer';
  hit.addEventListener('mousemove', e=>showTip(e,region));
  hit.addEventListener('mouseleave', hideTip);
  hit.addEventListener('click', e=>{
    e.stopPropagation();
    onBoardClick(region);
  });
  svg.appendChild(hit);
}

function appendSelectedRegionHalo(svg, d){
  svg.appendChild(el('path',{d, fill:'rgba(255,213,74,.20)',
    stroke:'#111827', 'stroke-width':8, 'stroke-linejoin':'round',
    'pointer-events':'none', opacity:.92}));
  svg.appendChild(el('path',{d, fill:'none',
    stroke:'var(--gold)', 'stroke-width':4, 'stroke-linejoin':'round',
    'pointer-events':'none'}));
}

function regionMarkerAnchor(region, originX, originY, desiredX, desiredY, safeRadius=28){
  const cell=STATE.layout && STATE.layout.cells ? STATE.layout.cells[region] : null;
  if(!cell || cell.length<3) return [desiredX, desiredY];
  const candidates=[];
  const addCandidate=(x,y)=>{
    if(pointInPolygon([x,y], cell)) candidates.push([x,y]);
  };
  addCandidate(desiredX, desiredY);
  addCandidate(originX, originY);
  const avg=averagePolygonPoint(cell);
  addCandidate(avg[0], avg[1]);
  const box=polygonBounds(cell);
  for(let gx=0;gx<=8;gx++){
    for(let gy=0;gy<=8;gy++){
      const x=box.minX+(box.maxX-box.minX)*(gx/8);
      const y=box.minY+(box.maxY-box.minY)*(gy/8);
      addCandidate(x,y);
    }
  }
  for(let i=1;i<=24;i++){
    const t=i/24;
    const x=originX+(desiredX-originX)*t;
    const y=originY+(desiredY-originY)*t;
    addCandidate(x,y);
  }
  if(!candidates.length) return avg;

  const safeCandidates=candidates
    .map(p=>({p, edge:distanceToPolygonEdges(p, cell), want:Math.hypot(p[0]-desiredX, p[1]-desiredY)}))
    .filter(c=>c.edge>=safeRadius);
  if(safeCandidates.length){
    safeCandidates.sort((a,b)=>a.want-b.want || b.edge-a.edge);
    return nudgePointInside(safeCandidates[0].p, avg, cell, 0.12);
  }

  let best=candidates[0], bestScore=-Infinity;
  for(const p of candidates){
    const edge=distanceToPolygonEdges(p, cell);
    const want=Math.hypot(p[0]-desiredX, p[1]-desiredY);
    const score=edge*100 - want*0.02;
    if(score>bestScore){
      bestScore=score;
      best=p;
    }
  }
  return nudgePointInside(best, avg, cell, 0.12);
}

function warriorGroupSafeRadius(aCount, bCount){
  const largest=Math.max(aCount, bCount);
  const both=aCount>0 && bCount>0;
  if(largest>=4) return both ? 75 : 60;
  if(largest>=3) return both ? 69 : 51;
  if(largest>=2) return both ? 60 : 45;
  return both ? 51 : 36;
}

function warriorScaleForAnchor(region, x, y, safeRadius){
  const cell=STATE.layout && STATE.layout.cells ? STATE.layout.cells[region] : null;
  if(!cell || cell.length<3) return 1;
  const room=distanceToPolygonEdges([x,y], cell);
  if(room>=safeRadius) return 1;
  return Math.max(0.72, Math.min(1, (room-3) / Math.max(1, safeRadius)));
}

function nudgePointInside(point, target, poly, amount){
  const x=point[0]+(target[0]-point[0])*amount;
  const y=point[1]+(target[1]-point[1])*amount;
  return pointInPolygon([x,y], poly) ? [x,y] : point;
}

function pointInPolygon(point, poly){
  const [x,y]=point;
  let inside=false;
  for(let i=0,j=poly.length-1;i<poly.length;j=i++){
    const xi=poly[i][0], yi=poly[i][1];
    const xj=poly[j][0], yj=poly[j][1];
    const onY=(yi>y)!==(yj>y);
    if(onY){
      const atX=(xj-xi)*(y-yi)/(yj-yi)+xi;
      if(x<atX) inside=!inside;
    }
  }
  return inside;
}

function averagePolygonPoint(poly){
  const n=poly.length || 1;
  return [
    poly.reduce((sum,p)=>sum+p[0],0)/n,
    poly.reduce((sum,p)=>sum+p[1],0)/n
  ];
}

function polygonBounds(poly){
  return poly.reduce((box,p)=>({
    minX:Math.min(box.minX,p[0]), minY:Math.min(box.minY,p[1]),
    maxX:Math.max(box.maxX,p[0]), maxY:Math.max(box.maxY,p[1])
  }), {minX:Infinity,minY:Infinity,maxX:-Infinity,maxY:-Infinity});
}

function distanceToPolygonEdges(point, poly){
  let best=Infinity;
  for(let i=0;i<poly.length;i++){
    best=Math.min(best, distancePointToSegment(point, poly[i], poly[(i+1)%poly.length]));
  }
  return best;
}

function distancePointToSegment(p, a, b){
  const vx=b[0]-a[0], vy=b[1]-a[1];
  const wx=p[0]-a[0], wy=p[1]-a[1];
  const len2=vx*vx+vy*vy;
  const t=len2 ? Math.max(0, Math.min(1, (wx*vx+wy*vy)/len2)) : 0;
  const x=a[0]+vx*t, y=a[1]+vy*t;
  return Math.hypot(p[0]-x, p[1]-y);
}

function addWarriorHitArea(svg, x, y, r, id, region){
  const hit=el('circle',{cx:x, cy:y, r, fill:'transparent',
    'pointer-events':'all', 'data-warrior':id, 'data-region':region, class:'warrior-hit'});
  hit.style.cursor='pointer';
  hit.addEventListener('mousemove', e=>showTip(e,region));
  hit.addEventListener('mouseleave', hideTip);
  hit.addEventListener('click', e=>{
    e.stopPropagation();
    onWarriorClick(id, region);
  });
  svg.appendChild(hit);
}

function buildingMarkerSize(b){
  const base = b.type==='HQ' ? 26 : 20;
  return b.type==='HQ' ? base * HQ_MARKER_SCALE : base;
}

function buildingHitRadius(b){
  return b.type==='HQ' ? Math.max(56, buildingMarkerSize(b)*0.72) : 30;
}

function warriorStackOffset(b){
  return b && b.type==='HQ' ? buildingMarkerSize(b)+26 : 34;
}

function drawBuilding(svg, x, y, b){
  const side=b.side;
  const assetName=(b.type==='HQ'?'hq':'base')+(side==='A'?'A':'B');
  const A=ASSETS[assetName];
  const size = buildingMarkerSize(b);
  const sideColor = side==='A'?'var(--left-strong)':'var(--right-strong)';
  if(A&&A.ok){
    if(b.type!=='HQ'){
      svg.appendChild(el('rect',{x:x-size/2-3, y:y-size/2-3, width:size+6, height:size+6,
        rx:7, fill:'rgba(255,255,255,.82)', stroke:sideColor, 'stroke-width':2.2,
        'pointer-events':'none'}));
    }
    svg.appendChild(el('image',{x:x-size/2, y:y-size/2, width:size, height:size,
      href:A.img.src, 'preserveAspectRatio':'xMidYMid meet', 'pointer-events':'none'}));
    if(b.type!=='HQ'){
      svg.appendChild(el('rect',{x:x-size/2-3, y:y-size/2-3, width:size+6, height:size+6,
        rx:7, fill:'none', stroke:'#fff', 'stroke-width':.9, opacity:.9,
        'pointer-events':'none'}));
    }
  } else {
    // 폴백: HQ=별, BASE=집모양
    const col = side==='A'?'#7a241b':'#1f3d5c';
    const stroke = side==='A'?'#fff':'#fff';
    if(b.type==='HQ'){
      const star=starPath(x,y,size*0.5,col,stroke);
      star.setAttribute('pointer-events','none');
      svg.appendChild(star);
    } else {
      const s=size*0.42;
      svg.appendChild(el('rect',{x:x-s, y:y-s*0.7, width:s*2, height:s*1.4,
        rx:2, fill:col, stroke, 'stroke-width':1.2, 'pointer-events':'none'}));
      svg.appendChild(el('path',{d:`M${x-s-1},${y-s*0.7}L${x},${y-s*1.5}L${x+s+1},${y-s*0.7}Z`,
        fill:col, stroke, 'stroke-width':1.2, 'pointer-events':'none'}));
    }
  }
  // 레벨 표시
  const lbl=el('text',{x, y:y-size*0.62, 'text-anchor':'middle',
    'font-size':9, 'font-family':'var(--mono)', fill:'#fff',
    'font-weight':700, stroke:'#000','stroke-width':.6,'paint-order':'stroke',
    'pointer-events':'none'});
  lbl.textContent='L'+b.level;
  svg.appendChild(lbl);
  // HP 바
  const maxhp = b.type==='HQ'?HQ_LEVELS[b.level][2]:BASE_LEVELS[b.level][1];
  const w=size, hpw=w*Math.max(0,Math.min(1,b.hp/maxhp));
  svg.appendChild(el('rect',{x:x-w/2, y:y+size*0.5, width:w, height:3, rx:1.5, fill:'#000', opacity:.5, 'pointer-events':'none'}));
  svg.appendChild(el('rect',{x:x-w/2, y:y+size*0.5, width:hpw, height:3, rx:1.5,
    fill: b.hp/maxhp>0.4?'#7fb98a':'#e2664f', 'pointer-events':'none'}));
}

function drawRegionNumbers(svg, snap){
  if(!STATE.map || !STATE.layout) return;
  for(let k=0;k<STATE.map.N;k++){
    const [px,py]=STATE.layout.pts[k];
    const b=snap.buildings[k];
    const size=b ? buildingMarkerSize(b) : 0;
    const y=b ? py-size*0.68-9 : py+4;
    const text=el('text',{x:px, y,
      'text-anchor':'middle', 'font-size':b?12:10,
      'font-family':'var(--mono)', 'font-weight':800,
      fill:'#172033', stroke:'rgba(255,255,255,.92)', 'stroke-width':3.2,
      'paint-order':'stroke', 'pointer-events':'none', opacity:.9});
    text.textContent=String(k);
    svg.appendChild(text);
  }
}

function starPath(cx,cy,r,fill,stroke){
  let pts='';
  for(let i=0;i<10;i++){
    const rad=i%2===0?r:r*0.45;
    const a=-Math.PI/2 + i*Math.PI/5;
    pts+=(cx+Math.cos(a)*rad).toFixed(1)+','+(cy+Math.sin(a)*rad).toFixed(1)+' ';
  }
  return el('polygon',{points:pts.trim(), fill, stroke, 'stroke-width':1.2});
}

function drawWarriorBadge(svg, x, y, side, warriors, region, scale=1){
  const col=side==='A'?'var(--left-strong)':'var(--right-strong)';
  const r=8.5 * WARRIOR_MARKER_SCALE * scale;
  const ordered=warriors.slice().sort((a,b)=>parseInt(a.id.slice(1))-parseInt(b.id.slice(1)));
  const n=ordered.length;
  const shown=Math.min(n,5);
  const xGap = r * 0.82;
  const yGap = r * 0.72;
  const offsets = shown===1 ? [[0,0]]
    : shown===2 ? [[-xGap,0],[xGap,0]]
    : shown===3 ? [[-xGap,yGap*.45],[xGap,yGap*.45],[0,-yGap*.9]]
    : shown===4 ? [[-xGap,-yGap],[xGap,-yGap],[-xGap,yGap],[xGap,yGap]]
    : [[-xGap*1.15,-yGap],[0,-yGap],[xGap*1.15,-yGap],[-xGap*.55,yGap],[xGap*.55,yGap]];
  offsets.forEach(([dx,dy],i)=>{
    const warrior=ordered[i];
    const tx=x+dx, ty=y+dy;
    if(HP.active && HP.selWarrior===warrior.id){
      svg.appendChild(el('circle',{cx:tx, cy:ty, r:r+9, fill:'rgba(255,205,38,.25)',
        stroke:'#111827','stroke-width':4,'pointer-events':'none'}));
      svg.appendChild(el('circle',{cx:tx, cy:ty, r:r+5, fill:'none',
        stroke:'var(--gold)','stroke-width':3,'pointer-events':'none'}));
    }
    svg.appendChild(el('circle',{cx:tx, cy:ty, r, fill:col, stroke:'#fff',
      'stroke-width':2.2, 'pointer-events':'none'}));
    const t=el('text',{x:tx, y:ty+4*scale,'text-anchor':'middle','font-size':10*scale,
      fill:'#fff','font-weight':700,stroke:'#000','stroke-width':.6,
      'paint-order':'stroke','pointer-events':'none'});
    t.textContent=warrior.id.slice(1); svg.appendChild(t);
    drawWarriorHpBar(svg, tx, ty+r+5*scale, r*1.75, warrior, scale);
    addWarriorHitArea(svg, tx, ty, r+16*scale, warrior.id, region);
  });
  if(n>shown){
    const bx=x+32*scale, by=y+20*scale;
    svg.appendChild(el('circle',{cx:bx, cy:by, r:10*scale, fill:'#111820',
      stroke:'#fff','stroke-width':1.2,'pointer-events':'none'}));
    const t=el('text',{x:bx, y:by+3*scale,'text-anchor':'middle','font-size':9*scale,
      fill:'#fff','font-weight':700,'pointer-events':'none'});
    t.textContent='+'+(n-shown); svg.appendChild(t);
  }
}

function drawGhostWarrior(svg, snap, id, destRegion, w) {
  const { layout } = STATE;
  const [px, py] = layout.pts[destRegion];
  const b = snap.buildings[destRegion];
  const desiredWy = b ? py + warriorStackOffset(b) : py + 25;
  const safeRadius = warriorGroupSafeRadius(1, 0);
  const [wx, wy] = regionMarkerAnchor(destRegion, px, py, px, desiredWy, safeRadius);
  const scale = warriorScaleForAnchor(destRegion, wx, wy, safeRadius);

  const ghostGroup = el('g', { opacity: '0.35', 'pointer-events': 'none' });
  svg.appendChild(ghostGroup);
  drawWarriorBadge(ghostGroup, wx, wy, w.side, [{ ...w, id }], destRegion, scale);
  // warrior-hit 제거: pointer-events:all이 부모의 none을 override하므로 직접 삭제
  ghostGroup.querySelectorAll('.warrior-hit').forEach(h => h.remove());
}

function drawMoveGhosts(svg, snap) {
  if (!HP.active) return;
  const { layout } = STATE;
  if (!layout) return;

  // 아직 제출 안 된 큐 명령
  const pendingIds = new Set();
  if (HP.waitingTurn !== null) {
    for (const mv of HP.queueMoves) {
      const w = snap.warriors[mv.id];
      if (!w || w.side !== HP.side) continue;
      pendingIds.add(mv.id);
      drawGhostWarrior(svg, snap, mv.id, mv.region, w);
    }
  }

  // 이미 이동 중인 전사 (이전 턴에 제출, 아직 목적지 미도착)
  for (const [id, w] of Object.entries(snap.warriors)) {
    if (w.side !== HP.side) continue;
    if (!isWarriorMoving(w)) continue;
    if (pendingIds.has(id)) continue;
    drawGhostWarrior(svg, snap, id, w.target, w);
  }
}

function drawWarriorHpBar(svg, x, y, width, warrior, scale=1){
  const maxHp=Math.max(1, warrior.maxHp || warrior.hp || HQ_LEVELS[1][1]);
  const ratio=Math.max(0, Math.min(1, warrior.hp/maxHp));
  const h=4*scale;
  const fill=ratio>0.55 ? '#2f9d62' : ratio>0.25 ? '#d8a21b' : '#c24135';
  svg.appendChild(el('rect',{x:x-width/2, y, width, height:h, rx:2,
    fill:'rgba(17,24,39,.55)', 'pointer-events':'none'}));
  svg.appendChild(el('rect',{x:x-width/2, y, width:width*ratio, height:h, rx:2,
    fill, 'pointer-events':'none'}));
  svg.appendChild(el('rect',{x:x-width/2, y, width, height:h, rx:2,
    fill:'none', stroke:'rgba(255,255,255,.75)', 'stroke-width':.7,
    'pointer-events':'none'}));
}

/* ---------- game.js UI 흡수: 선택 구역 인스펙터 ---------- */
function injectGameUiStyles(){
  if(document.getElementById(VIEWER_UI_STYLE_ID)) return;
  const style=document.createElement('style');
  style.id=VIEWER_UI_STYLE_ID;
  style.textContent=`
    .region-inspector{padding:14px 16px;border-top:1px solid var(--line);background:#f8fafc}
    .ri-head{display:flex;align-items:flex-start;justify-content:space-between;gap:10px;margin-bottom:10px}
    .ri-title{font-size:13px;font-weight:800}
    .ri-meta{font-family:var(--mono);font-size:11px;color:var(--ink-dim);text-align:right;line-height:1.5}
    .ri-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:10px}
    .ri-stat{min-width:0;padding:9px;border:1px solid var(--line);border-radius:8px;background:var(--panel)}
    .ri-stat .k{display:block;color:var(--ink-dim);font-size:11px;margin-bottom:3px}
    .ri-stat .v{display:block;font-family:var(--mono);font-size:12px;font-weight:700;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
    .ri-units{display:grid;gap:6px}
    .ri-unit-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:center;min-height:32px;padding:7px 9px;border:1px solid var(--line);border-radius:7px;background:#fff;font-family:var(--mono);font-size:12px}
    .ri-unit-row.empty{color:var(--ink-dim);font-style:italic}
    .ri-unit-name{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:800}
    .ri-unit-name.a{color:var(--left)}
    .ri-unit-name.b{color:var(--right)}
    .ri-unit-meta{color:var(--ink-dim);font-size:11px}
    .ri-neighbors{margin-top:8px;color:var(--ink-dim);font-family:var(--mono);font-size:11px;line-height:1.5}
    .pp-queue .qrow.rich{align-items:flex-start;gap:8px;padding:3px 0}
    .pp-queue .qmain{display:flex;flex-direction:column;min-width:0}
    .pp-queue .qmain b{font-size:12px}
    .pp-queue .qmain em{color:var(--ink-dim);font-size:11px;font-style:normal;line-height:1.35}
    .pp-queue .qsum{display:grid;grid-template-columns:1fr auto;gap:4px 8px;margin-top:6px;padding-top:6px;border-top:1px solid var(--line);color:var(--ink-dim)}
    .pp-queue .qsum b{color:var(--gold)}
    .pp-queue .qsum .wide{grid-column:1 / -1}
  `;
  document.head.appendChild(style);
}

function ensureInspectorPanel(){
  injectGameUiStyles();
  let panel=document.getElementById('selectedRegionPanel');
  if(panel) return panel;
  panel=document.createElement('section');
  panel.id='selectedRegionPanel';
  panel.className='region-inspector';
  const aside=document.querySelector('aside');
  const players=aside ? aside.querySelector('.status-column .players') : null;
  if(players) players.insertAdjacentElement('afterend', panel);
  else if(aside) aside.insertBefore(panel, aside.firstChild);
  return panel;
}

function normalizeSelectedRegion(){
  if(!STATE.map) return null;
  const region=Number(STATE.selectedRegion);
  if(Number.isInteger(region) && region>=0 && region<STATE.map.N){
    return region;
  }
  STATE.selectedRegion=0;
  return 0;
}

function regionKind(region){
  if(!STATE.map) return '구역';
  if(region===0) return 'LEFT 본부';
  if(region===STATE.map.N-1) return 'RIGHT 본부';
  if(STATE.map.strongholds.includes(region)) return '거점';
  return '일반 구역';
}

function buildingLabel(b){
  if(!b) return '없음';
  return `${b.side} ${b.type==='HQ'?'본부':'기지'} Lv${b.level}`;
}

function buildingDetail(b){
  if(!b) return '건설 없음';
  const max=buildingMaxHp(b);
  const turret=buildingTurret(b);
  const work=buildingWorkCap(b);
  return `HP ${b.hp}/${max} · 포탑 ${turret} · 생산 ${work}`;
}

function regionTitle(region, snap){
  const ws=snap ? Object.values(snap.warriors).filter(w=>w.region===region) : [];
  const a=ws.filter(w=>w.side==='A').length;
  const b=ws.filter(w=>w.side==='B').length;
  const building=snap && snap.buildings ? snap.buildings[region] : null;
  const pieces=[`구역 ${region}`, regionKind(region)];
  if(building) pieces.push(buildingLabel(building));
  if(a || b) pieces.push(`A ${a} · B ${b}`);
  return pieces.join(' · ');
}

function formatNeighbors(region){
  const neighbors=STATE.map.adj[region]||[];
  const shown=neighbors.slice(0,12).join(', ');
  return neighbors.length>12 ? `${shown} 외 ${neighbors.length-12}개` : shown;
}

function unitsInRegion(snap, region, side){
  return Object.entries(snap.warriors)
    .filter(([id,w])=>w.region===region && w.side===side)
    .sort((a,b)=>parseInt(a[0].slice(1))-parseInt(b[0].slice(1)));
}

function regionUnitRows(side, rows){
  const label=side==='A'?'LEFT(A)':'RIGHT(B)';
  if(!rows.length){
    return `<div class="ri-unit-row empty"><span>${label}</span><span>전사 없음</span></div>`;
  }
  return rows.map(([id,w])=>`
    <div class="ri-unit-row">
      <span class="ri-unit-name ${side.toLowerCase()}">${escapeHtml(id)}</span>
      <span class="ri-unit-meta">HP ${escapeHtml(w.hp)}</span>
    </div>
  `).join('');
}

function updateRegionInspector(snap){
  const panel=ensureInspectorPanel();
  if(!panel || !STATE.map || !snap){
    if(panel) panel.innerHTML='<div class="ri-meta">로그를 불러오면 구역 정보가 표시됩니다.</div>';
    return;
  }
  const region=normalizeSelectedRegion();
  const building=snap.buildings[region];
  const aUnits=unitsInRegion(snap, region, 'A');
  const bUnits=unitsInRegion(snap, region, 'B');
  const coord=`${STATE.map.x[region]}, ${STATE.map.y[region]}`;
  const neighborText=formatNeighbors(region);
  panel.innerHTML=`
    <div class="ri-head">
      <div class="ri-title">구역 ${region}</div>
      <div class="ri-meta">${escapeHtml(regionKind(region))}<br>인접 ${STATE.map.adj[region].length}개</div>
    </div>
    <div class="ri-grid">
      <div class="ri-stat"><span class="k">건물</span><span class="v">${escapeHtml(buildingLabel(building))}</span></div>
      <div class="ri-stat"><span class="k">좌표</span><span class="v">${escapeHtml(coord)}</span></div>
      <div class="ri-stat"><span class="k">상태</span><span class="v">${escapeHtml(buildingDetail(building))}</span></div>
      <div class="ri-stat"><span class="k">전사</span><span class="v">A ${aUnits.length} · B ${bUnits.length}</span></div>
    </div>
    <div class="ri-units">
      ${regionUnitRows('A', aUnits)}
      ${regionUnitRows('B', bUnits)}
    </div>
    <div class="ri-neighbors">인접: ${escapeHtml(neighborText || '없음')}</div>
  `;
}

/* ---------- 사이드 패널 ---------- */
function updatePanels(snap){
  const map=STATE.map;
  const hqL=snap.buildings[0], hqR=snap.buildings[map.N-1];
  const set=(id,v)=>{
    const node=document.getElementById(id);
    if(node) node.textContent=v;
  };
  updateRegionInspector(snap);
  const selected=normalizeSelectedRegion();
  const liveSeed=document.getElementById('liveSeed')?.textContent || '–';
  const mode=HP.active ? 'HUMAN PLAY' : (liveSeed.startsWith('LOCAL') ? 'LOCAL' : 'REPLAY');
  set('sideDay', `DAY ${snap.day}`);
  const sideMode=document.getElementById('sideMode');
  if(sideMode) sideMode.innerHTML=`${escapeHtml(mode)}<br>시드 ${escapeHtml(liveSeed)}`;
  set('sideHint', `선택 구역 ${selected} · ${regionKind(selected)} · 인접 ${(map.adj[selected]||[]).length}개`);

  const hpA=hqL?hqL.hp:0, hpAmax=hqL?HQ_LEVELS[hqL.level][2]:30;
  const hpB=hqR?hqR.hp:0, hpBmax=hqR?HQ_LEVELS[hqR.level][2]:30;
  set('lHqHp', hqL?`${hpA} / ${hpAmax}`:'파괴됨');
  set('rHqHp', hqR?`${hpB} / ${hpBmax}`:'파괴됨');
  document.getElementById('lHqBar').style.width=(hpA/hpAmax*100)+'%';
  document.getElementById('rHqBar').style.width=(hpB/hpBmax*100)+'%';

  set('lGold', snap.gold.A); set('rGold', snap.gold.B);
  const wA=Object.values(snap.warriors).filter(w=>w.side==='A').length;
  const wB=Object.values(snap.warriors).filter(w=>w.side==='B').length;
  set('lWar', wA); set('rWar', wB);
  const bA=Object.values(snap.buildings).filter(b=>b.side==='A').length;
  const bB=Object.values(snap.buildings).filter(b=>b.side==='B').length;
  set('lBld', bA); set('rBld', bB);
  set('lTok', snap.tokens.A); set('rTok', snap.tokens.B);

  // 사건 목록
  const list=document.getElementById('evList');
  list.innerHTML='';
  if(!snap.events.length){
    list.innerHTML='<div class="ev empty">움직임 없음</div>';
  } else {
    for(const e of snap.events){
      const div=document.createElement('div');
      div.className='ev '+e.kind;
      const tag={up:'건설',move:'이동',train:'훈련',dmg:'피해',siege:'공성'}[e.kind]||'';
      div.innerHTML=`<span class="tag">${tag}</span>${e.text}`;
      list.appendChild(div);
    }
  }

  // 결과 배너 (마지막 날에서만)
  const banner=document.getElementById('resultBanner');
  if(STATE.idx===STATE.snaps.length-1 && STATE.result){
    banner.classList.add('show');
    const m=STATE.result.match(/RESULT (\S+) (\S+)/);
    const outcome=m?m[1]:'', reason=m?m[2]:'';
    const ko={LEFT_WIN:'LEFT 승리',RIGHT_WIN:'RIGHT 승리',DRAW:'무승부'}[outcome]||outcome;
    const rko={HQ_DESTROYED:'본부 파괴',TURN_LIMIT:'200일 종료',WA:'규칙 위반'}[reason]||reason;
    banner.style.color = outcome==='LEFT_WIN'?'var(--left)':outcome==='RIGHT_WIN'?'var(--right)':'var(--ink)';
    banner.textContent=`▣ ${ko} · ${rko}`;
  } else {
    banner.classList.remove('show');
  }
}

/* ---------- 툴팁 ---------- */
function showTip(e, region){
  const tip=document.getElementById('tip');
  const snap=STATE.snaps[STATE.idx];
  const b=snap.buildings[region];
  const ws=Object.entries(snap.warriors).filter(([id,w])=>w.region===region);
  const SH=new Set(STATE.map.strongholds);
  let html=`<b>구역 ${region}</b>`;
  if(region===0) html+=' · LEFT 본부';
  else if(region===STATE.map.N-1) html+=' · RIGHT 본부';
  else if(SH.has(region)) html+=' · 거점';
  html+='<br>';
  if(b){
    const mx=b.type==='HQ'?HQ_LEVELS[b.level][2]:BASE_LEVELS[b.level][1];
    const tur=b.type==='HQ'?HQ_LEVELS[b.level][3]:BASE_LEVELS[b.level][3];
    html+=`${b.side} ${b.type} Lv${b.level} · HP ${b.hp}/${mx} · 포탑 ${tur}<br>`;
  }
  if(ws.length){
    const a=ws.filter(([id,w])=>w.side==='A');
    const bb=ws.filter(([id,w])=>w.side==='B');
    if(a.length) html+=`A전사 ${a.length}: ${a.map(([id,w])=>id+'('+w.hp+')').join(', ')}<br>`;
    if(bb.length) html+=`B전사 ${bb.length}: ${bb.map(([id,w])=>id+'('+w.hp+')').join(', ')}`;
  }
  if(!b && !ws.length) html+='<span style="color:var(--ink-dim)">비어 있음</span>';
  tip.innerHTML=html;
  tip.style.display='block';
  const wrap=document.getElementById('boardWrap').getBoundingClientRect();
  let lx=e.clientX-wrap.left+14, ly=e.clientY-wrap.top+14;
  if(lx>wrap.width-240) lx=e.clientX-wrap.left-240;
  tip.style.left=lx+'px'; tip.style.top=ly+'px';
}
function hideTip(){ document.getElementById('tip').style.display='none'; }

/* ============================================================
   4) 타임라인 컨트롤
   ============================================================ */
function goTo(idx){
  STATE.idx=Math.max(0,Math.min(STATE.snaps.length-1, idx));
  document.getElementById('slider').value=STATE.idx;
  const day=STATE.snaps[STATE.idx].day;
  document.getElementById('dayLabel').textContent=`${day}일 / ${STATE.snaps.length-1}`;
  render();
}
function play(){
  if(STATE.playing){ stop(); return; }
  STATE.playing=true;
  document.getElementById('playBtn').textContent='⏸';
  const speed=+document.getElementById('speedSel').value;
  STATE.timer=setInterval(()=>{
    if(STATE.idx>=STATE.snaps.length-1){ stop(); return; }
    goTo(STATE.idx+1);
  }, speed);
}
function stop(){
  STATE.playing=false;
  document.getElementById('playBtn').textContent='▶';
  clearInterval(STATE.timer);
}

/* ============================================================
   6) 라이브 모드 (브리지 서버 연동)
   서버(viewer_server.py) 위에서 열렸을 때만 활성.
   - 새 전장: 랜덤 또는 시드 입력
   - SSE 로 맵/턴/결과를 실시간 수신해 점진적으로 스냅샷 누적
   ============================================================ */
const LIVE = { es:null, streamText:'', mapDone:false };
const LOCAL_SERVER_ORIGIN = 'http://localhost:8770';
const SERVER_COMMAND_DIR = '';
let API_BASE = '';

function isServed(){
  // http(s) 로 열렸으면 서버 가능성. file:// 이면 라이브 불가.
  return location.protocol==='http:' || location.protocol==='https:';
}
function isFileMode(){
  return location.protocol==='file:';
}
function apiUrl(path){
  return API_BASE ? API_BASE + path : path;
}
function staticUrl(path){
  return API_BASE ? API_BASE + '/' + path.replace(/^\/+/, '') : path;
}
function fetchWithTimeout(url, opts={}, timeoutMs=800){
  const ctrl=new AbortController();
  const timer=setTimeout(()=>ctrl.abort(), timeoutMs);
  return fetch(url, {...opts, signal:ctrl.signal}).finally(()=>clearTimeout(timer));
}
function postJson(path, data){
  const opts = {method:'POST', body:JSON.stringify(data)};
  opts.headers = API_BASE
    ? {'Content-Type':'text/plain'}
    : {'Content-Type':'application/json'};
  return fetch(apiUrl(path), opts);
}
function shellQuote(s){
  return '"' + String(s).replace(/(["\\$`])/g, '\\$1') + '"';
}
function localViewerDir(){
  if(!isFileMode()) return '';
  try{
    const path = decodeURIComponent(location.pathname);
    return path.replace(/\/[^/]*$/, '');
  }catch(_){ return ''; }
}
function serverHelpMessage(action){
  const dir = SERVER_COMMAND_DIR || localViewerDir();
  const cmd = dir ? `cd ${shellQuote(dir)}\npython3 viewer_server.py` : 'python3 viewer_server.py';
  return `${action}에는 채점기와 AI를 실행하는 로컬 서버가 필요합니다.\n\n터미널에서 아래 명령을 한 번 실행한 뒤 이 버튼을 다시 누르세요.\n\n${cmd}\n\n서버가 켜져 있으면 더블클릭으로 연 HTML에서도 바로 연결됩니다.`;
}

async function checkServer(){
  if(isFileMode()) return false;
  const bases = [];
  if(isServed()) bases.push('');
  bases.push(LOCAL_SERVER_ORIGIN);
  const seen = new Set();
  for(const base of bases){
    if(seen.has(base)) continue;
    seen.add(base);
    try{
      const r=await fetchWithTimeout(base + '/status',{cache:'no-store'}, 700);
      if(!r.ok) continue;
      await r.json();
      API_BASE = base;
      return true;
    }catch(_){ /* 다음 후보 확인 */ }
  }
  return false;
}

async function ensureServer(action){
  if(await checkServer()) return true;
  alert(serverHelpMessage(action));
  return false;
}

function startLiveStream(){
  // 기존 스트림 정리
  if(LIVE.es){ LIVE.es.close(); LIVE.es=null; }
  LIVE.streamText=''; LIVE.mapDone=false;
  STATE.snaps=[]; STATE.idx=0;

  const es=new EventSource(apiUrl('/events'));
  LIVE.es=es;
  document.getElementById('liveBadge').classList.add('show');

  es.addEventListener('map', ev=>{
    const {text}=JSON.parse(ev.data);
    LIVE.streamText = text; // 맵 블록(헤더 포함 아닐 수 있음)
    // 맵만으로 레이아웃 잡기 위해 임시 파싱
    try{
      // map 블록은 "MAP ... END MAP" 형태. parseLog 는 전체에서 MAP 찾으므로 그대로 사용 가능.
      const parsed=parseLog(text + '\nRESULT DRAW TURN_LIMIT\n');
      STATE.map=parsed.map;
      STATE.layout=computeLayout(parsed.map);
      STATE.snaps=buildSnapshots({map:parsed.map, turns:[]});
      STATE.result=null;
      STATE.selectedRegion=0;
      LIVE.mapDone=true;
      document.getElementById('emptyState').style.display='none';
      document.getElementById('legend').style.display='flex';
      const slider=document.getElementById('slider');
      slider.disabled=false; slider.max=0; slider.value=0;
      goTo(0);
    }catch(e){ console.error('map parse', e); }
  });

  es.addEventListener('turn', ev=>{
    if(!LIVE.mapDone) return;
    const {text}=JSON.parse(ev.data);
    LIVE.streamText += text;
    // 누적 텍스트 전체를 재파싱(턴 수가 200 이하라 부담 적음)
    rebuildFromStream(true);
  });

  es.addEventListener('result', ev=>{
    const {text}=JSON.parse(ev.data);
    LIVE.streamText += '\n'+text+'\n';
    STATE.result=text;
    rebuildFromStream(true);
  });

  es.addEventListener('done', ()=>{
    document.getElementById('liveBadge').classList.remove('show');
    if(LIVE.es){ LIVE.es.close(); LIVE.es=null; }
  });

  es.onerror=()=>{ /* 서버 종료/네트워크 — 조용히 무시, done 에서 정리 */ };
}

function rebuildFromStream(follow){
  try{
    const full = ensureMapHeader(LIVE.streamText);
    const parsed=parseLog(full + (STATE.result?'':'\nRESULT DRAW TURN_LIMIT\n'));
    STATE.map=parsed.map;
    if(!STATE.layout) STATE.layout=computeLayout(parsed.map);
    STATE.snaps=buildSnapshots(parsed);
    const slider=document.getElementById('slider');
    slider.max=STATE.snaps.length-1;
    if(follow){ goTo(STATE.snaps.length-1); }       // 라이브는 항상 최신 날 추적
    else if(STATE.idx>STATE.snaps.length-1) goTo(STATE.snaps.length-1);
  }catch(e){ /* 부분 블록일 수 있음 */ }
}

// 스트림 누적분에 MAP 블록이 한 번만 있도록 보장(map 이벤트가 streamText 시작에 있음)
function ensureMapHeader(text){
  return text.includes('MAP') ? text : text;
}

function makeRandomSeed(){
  const hi = Math.floor(Math.random()*0x100000000);
  const lo = Date.now() & 0xffffffff;
  return String((hi ^ lo) >>> 0);
}

function ensureVisibleSeed(forceNew){
  const input=document.getElementById('seedInput');
  const mode=document.querySelector('input[name="mapmode"]:checked')?.value || 'random';
  if(mode==='random'){
    if(forceNew || !/^\d+$/.test(input.value.trim())) input.value=makeRandomSeed();
    return input.value.trim();
  }
  return input.value.trim();
}

function syncSeedMode(forceNew){
  const seedMode=document.querySelector('input[name="mapmode"]:checked')?.value==='seed';
  const input=document.getElementById('seedInput');
  input.disabled=!seedMode;
  if(!seedMode) ensureVisibleSeed(forceNew);
  if(seedMode) input.focus();
}

async function newGame(){
  const mode=document.querySelector('input[name="mapmode"]:checked').value;
  const seed = ensureVisibleSeed(mode==='random');
  if(!/^\d+$/.test(seed)){
    alert('시드는 숫자로 입력하세요.'); return;
  }
  stop();
  if(HP.localTimer){ clearTimeout(HP.localTimer); HP.localTimer=null; }
  if(!await checkServer()){
    startLocalSpectate(seed);
    return;
  }
  try{
    const r=await postJson('/new', {seed: seed});
    const j=await r.json();
    document.getElementById('liveSeed').textContent=j.seed;
    startLiveStream();
  }catch(e){
    alert('대국을 시작하지 못했습니다. 서버(viewer_server.py)가 실행 중인지 확인하세요.');
  }
}

/* ============================================================
   7) 직접 플레이 (사람 vs AI)
   ============================================================ */
const HP = {
  active:false, side:'A', mySideWord:'LEFT', waitingTurn:null,
  selWarrior:null, queueMoves:[], queueUpgrades:[],
  pollTimer:null, lastLogLen:0, finished:false,
  local:false, localNextId:{A:4, B:4}, localTimer:null
};

async function humanNewGame(){
  const mode=document.querySelector('input[name="mapmode"]:checked').value;
  const seed = ensureVisibleSeed(mode==='random');
  if(!/^\d+$/.test(seed)){ alert('시드는 숫자로 입력하세요.'); return; }
  const side = document.querySelector('input[name="playside"]:checked')?.value || 'LEFT';
  stop();
  if(HP.localTimer){ clearTimeout(HP.localTimer); HP.localTimer=null; }
  if(isFileMode()){
    startLocalPractice(seed, side);
    return;
  }
  if(!await checkServer()){
    startLocalPractice(seed, side);
    return;
  }
  try{
    const r=await postJson('/human/new', {seed, side, ai:null});
    const j=await r.json();
    if(!j.ok){ alert('대국 시작 실패'); return; }
    HP.active=true; HP.local=false; HP.mySideWord=j.side; HP.side = j.side==='LEFT'?'A':'B';
    HP.waitingTurn=null; HP.queueMoves=[]; HP.queueUpgrades=[]; HP.selWarrior=null;
    HP.lastLogLen=0; HP.finished=false;
    document.getElementById('liveSeed').textContent=j.seed;
    document.getElementById('liveBadge').classList.add('show');
    document.getElementById('ppSide').textContent = j.side+'('+HP.side+')';
    startHumanPolling();
  }catch(e){
    startLocalPractice(seed, side);
  }
}

/* ---------- 브라우저 단독 연습 모드 ----------
   HTML을 file://로 더블클릭한 경우 Python 채점기를 실행할 수 없으므로,
   조작 연습용으로 JS 안에서 간단한 맵/AI/판정을 돌린다. */
function seedNumber(seed){
  if(seed && /^\d+$/.test(seed)) return Number(seed) >>> 0;
  return (Date.now() ^ Math.floor(Math.random()*0xffffffff)) >>> 0;
}
/* ============================================================
   XoShiro256++ RNG — Python testing-tool.py 와 완전히 동일한 구현.
   BigInt 64비트 연산으로 같은 시드 → 같은 맵이 보장됩니다.
   ============================================================ */
const _U64 = (1n << 64n) - 1n;

function _splitMix64(z) {
  z = BigInt(z) & _U64;
  const out = [];
  for (let i = 0; i < 4; i++) {
    z = (z + 0x9E3779B97F4A7C15n) & _U64;
    z = ((z ^ (z >> 30n)) * 0xBF58476D1CE4E5B9n) & _U64;
    z = ((z ^ (z >> 27n)) * 0x94D049BB133111EBn) & _U64;
    out.push(z ^ (z >> 31n));
  }
  return out;
}

function _rotl64(x, k) {
  return ((x << k) & _U64) | (x >> (64n - k));
}

class XoShiro256 {
  constructor(seed) { this.s = _splitMix64(seed); }

  nextU64() {
    const s = this.s;
    const res = (_rotl64((s[0] + s[3]) & _U64, 23n) + s[0]) & _U64;
    const t = (s[1] << 17n) & _U64;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
    s[2] = (s[2] ^ t) & _U64; s[3] = _rotl64(s[3], 45n);
    return res;
  }

  // [0, n) 균등 난수 (Lemire 비편향 기법)
  nextU64Rng(n) {
    const bn = BigInt(n);
    let x = this.nextU64(), m = x * bn, mLo = m & _U64;
    if (mLo < bn) {
      const thresh = ((1n << 64n) - bn) & _U64;
      const t = thresh % bn;
      while (mLo < t) { x = this.nextU64(); m = x * bn; mLo = m & _U64; }
    }
    return Number(m >> 64n);
  }

  // [lo, hi] 균등 난수 (부호없는, Number 반환)
  nextU64Incl(lo, hi) {
    const bLo = BigInt(lo), bHi = BigInt(hi);
    if (bLo === 0n && bHi === _U64) return Number(this.nextU64());
    return Number(bLo + BigInt(this.nextU64Rng(Number(bHi - bLo + 1n))));
  }

  // [lo, hi] 균등 난수 (부호있는, Number 반환)
  nextI64Incl(lo, hi) {
    const BIAS = 1n << 63n;
    const uLo = (BigInt(lo) + BIAS) & _U64, uHi = (BigInt(hi) + BIAS) & _U64;
    const u = (uLo === 0n && uHi === _U64)
      ? this.nextU64()
      : uLo + BigInt(this.nextU64Rng(Number(uHi - uLo + 1n)));
    const v = (u + BIAS) & _U64;
    return v >= BIAS ? Number(v - (1n << 64n)) : Number(v);
  }
}

// BigInt 반올림 정수 나눗셈 (부호 보존, Python _round_div 와 동일)
function _roundDivBig(num, den) {
  const an = num < 0n ? -num : num, ad = den < 0n ? -den : den;
  const res = (an + ad / 2n) / ad;
  return Number((num >= 0n) === (den > 0n) ? res : -res);
}

// 점 D가 삼각형 ABC 외접원 내부인지 (BigInt 정수 행렬식)
function _inCircle(ax, ay, bx, by, cx, cy, dx, dy) {
  const Ax = BigInt(ax - dx), Ay = BigInt(ay - dy);
  const Bx = BigInt(bx - dx), By = BigInt(by - dy);
  const Cx = BigInt(cx - dx), Cy = BigInt(cy - dy);
  const aa = Ax*Ax + Ay*Ay, bb = Bx*Bx + By*By, cc = Cx*Cx + Cy*Cy;
  return Ax*(By*cc - bb*Cy) - Ay*(Bx*cc - bb*Cx) + aa*(Bx*Cy - By*Cx);
}

// 삼각형 ABC 외심을 정수 좌표로 반환 (BigInt 연산)
function _circumcenterInt(ax, ay, bx, by, cx, cy) {
  const Ax=BigInt(ax), Ay=BigInt(ay), Bx=BigInt(bx), By=BigInt(by), Cx=BigInt(cx), Cy=BigInt(cy);
  const det = 2n*(Ax*(By-Cy) + Bx*(Cy-Ay) + Cx*(Ay-By));
  const sa = Ax*Ax+Ay*Ay, sb = Bx*Bx+By*By, sc = Cx*Cx+Cy*Cy;
  return [
    _roundDivBig(sa*(By-Cy) + sb*(Cy-Ay) + sc*(Ay-By), det),
    _roundDivBig(sa*(Cx-Bx) + sb*(Ax-Cx) + sc*(Bx-Ax), det),
  ];
}

// 다각형 꼭짓점의 무게중심 (신발끈 공식, BigInt)
function _centroidShoelace(pts) {
  let cross = 0n, cx = 0n, cy = 0n;
  for (let i = 0; i < pts.length; i++) {
    const [xi, yi] = [BigInt(pts[i][0]), BigInt(pts[i][1])];
    const [xj, yj] = [BigInt(pts[(i+1)%pts.length][0]), BigInt(pts[(i+1)%pts.length][1])];
    const c = xi*yj - xj*yi;
    cross += c; cx += (xi+xj)*c; cy += (yi+yj)*c;
  }
  if (cross === 0n) throw new Error('degenerated cell — try different seed');
  const d = 3n * cross;
  return [_roundDivBig(cx, d), _roundDivBig(cy, d)];
}

// 벡터(dx,dy)가 속한 사분면 (0~3)
function _quad(dx, dy) {
  if (dx > 0 && dy >= 0) return 0;
  if (dx <= 0 && dy > 0) return 1;
  if (dx < 0 && dy <= 0) return 2;
  return 3;
}

// 반시계(CCW) 각도 정렬 비교자
function _ccwCmp(site, a, b) {
  const ax = a[0]-site[0], ay = a[1]-site[1];
  const bx = b[0]-site[0], by = b[1]-site[1];
  const qa = _quad(ax, ay), qb = _quad(bx, by);
  if (qa !== qb) return qa - qb;
  const cr = BigInt(ax)*BigInt(by) - BigInt(ay)*BigInt(bx);
  return cr > 0n ? -1 : cr < 0n ? 1 : 0;
}

// 외적 z성분 (슈퍼삼각형 방향 판정용)
function _crossPt(ax, ay, bx, by, cx, cy) {
  return (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
}

// Bowyer-Watson Delaunay 삼각분할 (Python _delaunay_triangulate 와 동일)
function _delaunay(pts) {
  const n = pts.length, SUP = 300000;
  const all = [...pts, [-SUP,-SUP], [SUP,-SUP], [0,SUP]];
  let S0 = n, S1 = n+1, S2 = n+2;
  if (_crossPt(all[S0][0],all[S0][1],all[S1][0],all[S1][1],all[S2][0],all[S2][1]) <= 0)
    [S1, S2] = [S2, S1];

  let tris = [[S0, S1, S2]];
  const ekey = (u, v) => u < v ? `${u},${v}` : `${v},${u}`;

  for (let pi = 0; pi < n; pi++) {
    const [px, py] = pts[pi];
    const bad = [];
    for (let ti = 0; ti < tris.length; ti++) {
      const [a, b, c] = tris[ti];
      if (_inCircle(all[a][0],all[a][1],all[b][0],all[b][1],all[c][0],all[c][1],px,py) > 0n)
        bad.push(ti);
    }
    const shared = {};
    for (const ti of bad) {
      const [a,b,c] = tris[ti];
      for (const [u,v] of [[a,b],[b,c],[c,a]])
        shared[ekey(u,v)] = (shared[ekey(u,v)] || 0) + 1;
    }
    const bnd = [];
    for (const ti of bad) {
      const [a,b,c] = tris[ti];
      for (const [u,v] of [[a,b],[b,c],[c,a]])
        if (shared[ekey(u,v)] === 1) bnd.push([u,v]);
    }
    bad.sort((a, b) => b - a);
    for (const ti of bad) { tris[ti] = tris[tris.length-1]; tris.pop(); }
    for (const [u,v] of bnd) tris.push([u, v, pi]);
  }
  return tris.filter(([a,b,c]) => a < n && b < n && c < n);
}

// Voronoi 외심 → 무게중심으로 점 재배치 (Python _voronoi_centroids 와 동일)
function _voronoiCentroids(P, Q, tris) {
  const np = P.length;
  const adjT = Array.from({length: np}, () => []);
  tris.forEach((t, ti) => t.forEach(v => { if (v < np) adjT[v].push(ti); }));
  const all = [...P, ...Q];
  return P.map((site, i) => {
    const verts = adjT[i].map(ti => {
      const [a,b,c] = tris[ti];
      return _circumcenterInt(all[a][0],all[a][1],all[b][0],all[b][1],all[c][0],all[c][1]);
    });
    verts.sort((a, b) => _ccwCmp(site, a, b));
    return _centroidShoelace(verts);
  });
}

// Q 보조점: 반지름 15000 원을 24등분 (Python _q_points 와 동일)
function _qPoints() {
  return Array.from({length: 24}, (_, i) => {
    const θ = i * (2 * Math.PI / 24);
    return [Math.trunc(15000 * Math.cos(θ)), Math.trunc(15000 * Math.sin(θ))];
  });
}

// 맵 생성 (실패 시 시드+1로 재시도, Python generate_map 와 동일한 알고리즘)
function generatePracticeMap(seed) {
  try { return _genMap(seed); }
  catch(e) { return generatePracticeMap((seed + 1) >>> 0); }
}

function _genMap(seed) {
  const L = 10000, D = 100;
  const rng = new XoShiro256(seed);
  const NP = rng.nextU64Incl(25, 54), N = 2*NP + 1;

  // K 범위 계산 (Python 와 동일)
  const Klo = Math.floor((3*N + 19) / 20);   // ceil(0.15N)
  const Khi = Math.floor(N / 5);              // floor(0.2N)
  const Klo_odd = Klo % 2 === 0 ? Klo + 1 : Klo;
  const Khi_odd = Khi % 2 === 0 ? Khi - 1 : Khi;
  const KP = rng.nextU64Incl((Klo_odd-1)/2, (Khi_odd-1)/2);
  const K = 2*KP + 1;

  // 점 배치 (점대칭, 간격 D 이상)
  const Q = _qPoints(), D2 = D*D;
  const P = [[0, 0]];
  let placed = 0, attempts = 0, maxAtt = NP * 20000;
  while (placed < NP) {
    if (++attempts > maxAtt) throw new Error('point placement failed');
    const x = rng.nextI64Incl(-L, L), y = rng.nextI64Incl(-L, L);
    if (x*x + y*y > L*L) continue;
    if (P.some(([vx]) => vx === x)) continue;
    if (P.some(([vx,vy]) => (vx-x)*(vx-x)+(vy-y)*(vy-y) < D2)) continue;
    P.push([x, y], [-x, -y]); placed++;
  }
  P.sort((a, b) => a[0] - b[0]);

  // Delaunay → Voronoi 무게중심 (1회 재배치)
  const tris1 = _delaunay([...P, ...Q]);
  const Pp = _voronoiCentroids(P, Q, tris1);

  const seen = new Set();
  for (const p of Pp) {
    const k = `${p[0]},${p[1]}`;
    if (seen.has(k)) throw new Error('collapsed centroid — try different seed');
    seen.add(k);
  }

  // 두 번째 Delaunay로 인접 그래프 구성
  const tris2 = _delaunay([...Pp, ...Q]);
  const adjSet = Array.from({length: N}, () => new Set());
  for (const [a,b,c] of tris2)
    for (const [u,v] of [[a,b],[b,c],[c,a]])
      if (u < N && v < N) { adjSet[u].add(v); adjSet[v].add(u); }
  const adj = adjSet.map(s => [...s].sort((a,b) => a-b));

  // 거점 배치: Fisher-Yates 셔플 후 순서대로 탐색 (Python 와 동일)
  const strongSet = new Set([NP]);
  const cands = Array.from({length: NP-1}, (_, i) => i + 1);
  for (let i = cands.length - 1; i > 0; i--) {
    const j = rng.nextU64Incl(0, i);
    [cands[i], cands[j]] = [cands[j], cands[i]];
  }
  const mirror = r => N - 1 - r;
  for (const c of cands) {
    if (strongSet.size >= K) break;
    const m = mirror(c);
    if (strongSet.has(c) || strongSet.has(m)) continue;
    let bad = false;
    for (const r of strongSet)
      if (adj[c].includes(r) || adj[m].includes(r)) { bad = true; break; }
    if (bad || adj[c].includes(m)) continue;
    strongSet.add(c); strongSet.add(m);
  }
  if (strongSet.size < K) throw new Error('stronghold placement failed — try different seed');

  const strongholds = [...strongSet].sort((a,b) => a-b);
  return { N, K: strongholds.length, x: Pp.map(p=>p[0]), y: Pp.map(p=>p[1]), strongholds, adj };
}
function cloneState(obj){
  return JSON.parse(JSON.stringify(obj));
}
function setBoardReady(){
  const slider=document.getElementById('slider');
  slider.disabled=false; slider.max=STATE.snaps.length-1; slider.value=STATE.snaps.length-1;
  document.getElementById('emptyState').style.display='none';
  document.getElementById('legend').style.display='flex';
}
function startLocalPractice(seedText, sideWord){
  const seed=seedNumber(seedText);
  const map=generatePracticeMap(seed);
  HP.active=true; HP.local=true; HP.mySideWord=sideWord; HP.side=sideWord==='LEFT'?'A':'B';
  HP.waitingTurn=1; HP.queueMoves=[]; HP.queueUpgrades=[]; HP.selWarrior=null;
  HP.finished=false; HP.localNextId={A:4, B:4};
  if(HP.pollTimer) clearInterval(HP.pollTimer);
  if(HP.localTimer) clearTimeout(HP.localTimer);
  document.getElementById('liveSeed').textContent=`LOCAL ${seed}`;
  document.getElementById('liveBadge').classList.add('show');
  document.getElementById('ppSide').textContent=sideWord+'('+HP.side+')';
  STATE.map=map;
  STATE.layout=computeLayout(map);
  STATE.snaps=buildSnapshots({map, turns:[]});
  STATE.result=null;
  STATE.idx=0;
  STATE.selectedRegion=0;
  setBoardReady();
  onMyTurn(1, null);
  goTo(0);
  document.getElementById('ppHint').textContent=
    '브라우저 단독 연습 모드입니다. 전사를 클릭해 이동하고, 거점 도착 후 다음 턴에 건설하세요.';
}

function startLocalSpectate(seedText){
  const seed=seedNumber(seedText);
  const map=generatePracticeMap(seed);
  HP.active=false; HP.local=true; HP.mySideWord='LEFT'; HP.side='A';
  HP.waitingTurn=null; HP.queueMoves=[]; HP.queueUpgrades=[]; HP.selWarrior=null;
  HP.finished=false; HP.localNextId={A:4, B:4};
  if(HP.pollTimer) clearInterval(HP.pollTimer);
  if(HP.localTimer) clearTimeout(HP.localTimer);
  document.getElementById('liveSeed').textContent=`LOCAL ${seed}`;
  document.getElementById('liveBadge').classList.add('show');
  document.getElementById('playPanel').classList.remove('show');
  STATE.map=map;
  STATE.layout=computeLayout(map);
  STATE.snaps=buildSnapshots({map, turns:[]});
  STATE.result=null;
  STATE.idx=0;
  STATE.selectedRegion=0;
  setBoardReady();
  goTo(0);
  let turn=1;
  const runChunk=()=>{
    const started=performance.now();
    let keepGoing=true;
    while(turn<=200 && performance.now()-started<24){
      keepGoing=applyLocalTurn(turn, false, false);
      turn++;
      if(!keepGoing) break;
    }
    setBoardReady();
    goTo(STATE.snaps.length-1);
    if(keepGoing && turn<=200){
      HP.localTimer=setTimeout(runChunk, 0);
    } else {
      HP.localTimer=null;
      document.getElementById('liveBadge').classList.remove('show');
    }
  };
  HP.localTimer=setTimeout(runChunk, 0);
}
function edgeWeight(map, u, v){
  const dx=map.x[u]-map.x[v];
  const dy=map.y[u]-map.y[v];
  return Math.ceil(Math.hypot(dx, dy));
}
function dijkstraTo(map, target){
  map._distCache = map._distCache || {};
  if(map._distCache[target]) return map._distCache[target];
  const dist=Array(map.N).fill(Infinity);
  const used=Array(map.N).fill(false);
  dist[target]=0;
  for(let step=0;step<map.N;step++){
    let u=-1, best=Infinity;
    for(let i=0;i<map.N;i++){
      if(!used[i] && dist[i]<best){ best=dist[i]; u=i; }
    }
    if(u<0) break;
    used[u]=true;
    for(const v of map.adj[u]){
      const nd=dist[u]+edgeWeight(map,u,v);
      if(nd<dist[v]) dist[v]=nd;
    }
  }
  map._distCache[target]=dist;
  return dist;
}
function canReachRegion(map, from, to){
  if(!map || to<0 || to>=map.N) return false;
  return Number.isFinite(dijkstraTo(map, to)[from]);
}
function shortestNext(map, from, to){
  if(from===to) return from;
  const dist=dijkstraTo(map, to);
  let bestV=-1, bestScore=Infinity;
  for(const v of map.adj[from]){
    if(!Number.isFinite(dist[v])) continue;
    const score=edgeWeight(map, from, v)+dist[v];
    if(score<bestScore || (score===bestScore && v<bestV)){
      bestScore=score;
      bestV=v;
    }
  }
  return bestV<0 ? from : bestV;
}
function buildingMaxHp(b){
  return b.type==='HQ' ? HQ_LEVELS[b.level][2] : BASE_LEVELS[b.level][1];
}
function buildingTurret(b){
  return b.type==='HQ' ? HQ_LEVELS[b.level][3] : BASE_LEVELS[b.level][2];
}
function buildingWorkCap(b){
  return b.type==='HQ' ? HQ_LEVELS[b.level][5] : BASE_LEVELS[b.level][3];
}
function warriorSuffix(id){
  return parseInt(String(id).slice(1),10) || 0;
}
function warriorSort(a,b){
  const sa=String(a)[0], sb=String(b)[0];
  if(sa!==sb) return sa<sb ? -1 : 1;
  return warriorSuffix(a)-warriorSuffix(b);
}
function isWarriorMoving(w){
  return !!w && w.target!==null && w.target!==undefined && w.target!==w.region;
}
function enemyPresentAt(snap, side, region){
  return Object.values(snap.warriors)
    .some(w=>w.hp>0 && w.side!==side && w.region===region);
}
function localSideCommands(side){
  const snap=STATE.snaps[STATE.snaps.length-1];
  const cmds={upgrades:[], moves:[], train:0};
  if(HP.active && HP.local && HP.waitingTurn!==null && side===HP.side){
    cmds.upgrades=HP.queueUpgrades.slice();
    cmds.moves=HP.queueMoves.map(m=>({...m}));
    cmds.train=Math.max(0, Math.floor(+document.getElementById('ppTrain').value||0));
    return cmds;
  }
  const hq=side==='A'?0:STATE.map.N-1;
  const enemyHq=side==='A'?STATE.map.N-1:0;
  const myWar=Object.entries(snap.warriors)
    .filter(([id,w])=>w.side===side && w.hp>0)
    .sort((a,b)=>warriorSort(a[0],b[0]));
  for(const [id,w] of myWar.slice(0,5)){
    if(isWarriorMoving(w)) continue;
    const target=chooseLocalAiTarget(side, w, snap, enemyHq);
    if(target!==w.region) cmds.moves.push({id, region:target});
  }
  const hqB=snap.buildings[hq];
  if(hqB && snap.gold[side]>=TRAIN_COST && HQ_LEVELS[hqB.level][4]>0){
    cmds.train=1;
  }
  const SH=new Set(STATE.map.strongholds);
  for(const [id,w] of myWar){
    const b=snap.buildings[w.region];
    if((SH.has(w.region) || w.region===hq) && (!b || b.side===side)){
      cmds.upgrades.push(w.region);
      break;
    }
  }
  return cmds;
}

function chooseLocalAiTarget(side, w, snap, enemyHq){
  const SH=STATE.map.strongholds.slice().sort((a,b)=>{
    const da=dijkstraTo(STATE.map,a)[w.region];
    const db=dijkstraTo(STATE.map,b)[w.region];
    return da-db || a-b;
  });
  for(const r of SH){
    const b=snap.buildings[r];
    if(!b || b.side===side) return r;
  }
  return enemyHq;
}

function applyLocalUpgrade(snap, side, region, events, builtToday){
  const SH=new Set(STATE.map.strongholds);
  const isHQ=(region===0 || region===STATE.map.N-1);
  if(builtToday && builtToday.has(region)) return;
  const hasWorker=Object.values(snap.warriors).some(w=>w.hp>0 && w.side===side && w.region===region);
  const hasEnemy=enemyPresentAt(snap, side, region);
  if(hasEnemy) return;
  if(!hasWorker || (!SH.has(region) && !isHQ)) return;
  const b=snap.buildings[region];
  if(!b){
    if(snap.gold[side] < BASE_LEVELS[1][0] || isHQ) return;
    snap.gold[side]-=BASE_LEVELS[1][0];
    snap.buildings[region]={side,type:'BASE',level:1,hp:BASE_LEVELS[1][1]};
    if(builtToday) builtToday.add(region);
    events.push({kind:'up', text:`${side} 기지 건설 @${region}`});
    return;
  }
  if(b.side!==side) return;
  const maxLevel=b.type==='HQ'?HQ_MAX:BASE_MAX;
  if(b.level>=maxLevel){
    const cost=b.type==='HQ'?HQ_HEAL:BASE_HEAL;
    if(snap.gold[side] < cost) return;
    snap.gold[side]-=cost;
    b.hp=buildingMaxHp(b);
    if(builtToday) builtToday.add(region);
    events.push({kind:'up', text:`${side} ${b.type} 수리 @${region}`});
    return;
  }
  const cost=b.type==='HQ'?HQ_LEVELS[b.level+1][0]:BASE_LEVELS[b.level+1][0];
  if(snap.gold[side] < cost) return;
  snap.gold[side]-=cost;
  b.level++;
  b.hp=buildingMaxHp(b);
  if(builtToday) builtToday.add(region);
  events.push({kind:'up', text:`${side} ${b.type} → Lv${b.level} @${region}`});
}
function applyLocalUpgrades(snap, side, cmds, events, builtToday){
  for(const region of cmds.upgrades) applyLocalUpgrade(snap, side, region, events, builtToday);
}
function applyLocalMoveCommands(snap, side, cmds, buildingsAtCommand){
  for(const mv of cmds.moves){
    const w=snap.warriors[mv.id];
    const target=Number(mv.region);
    if(!w || w.side!==side || w.hp<=0) continue;
    if(isWarriorMoving(w)) continue;
    if(!Number.isInteger(target) || target<0 || target>=STATE.map.N || target===w.region) continue;
    if(!canReachRegion(STATE.map, w.region, target)) continue;
    const b=buildingsAtCommand ? buildingsAtCommand[target] : snap.buildings[target];
    const cost=(b && b.side===side) ? 0 : MOVE_COST;
    if(snap.gold[side] < cost) continue;
    snap.gold[side]-=cost;
    w.target=target;
    w.state='M';
  }
}
// 훈련 명령의 유효성을 게임 규칙에 따라 엄격히 판정한다.
//  - n 은 0 이상의 정수여야 한다.
//  - n 은 본부의 "훈련 가능 전사 수"(같은 턴 건설 단계가 끝난 뒤의 본부 레벨 기준) 이하여야 한다.
//  - 본부가 없으면(파괴되었으면) 훈련할 수 없다.
//  - 비용(120G/명)을 모두 지불할 금화가 있어야 한다.
// 위 중 하나라도 위반이면 {ok:false, reason, train:0} 을 반환한다.
// 유효하면 금화를 차감하고 {ok:true, train} 을 반환한다.
// snap 은 이미 건설 단계가 적용된 상태여야 한다(applyLocalTurn 의 호출 순서를 따른다).
function chargeLocalTraining(snap, side, cmds){
  const requested=cmds.train|0;
  if(requested===0) return {ok:true, train:0};
  if(!Number.isInteger(cmds.train) || cmds.train<0)
    return {ok:false, reason:'TRAIN_N_INVALID', train:0};

  const hq=side==='A'?0:STATE.map.N-1;
  const hqB=snap.buildings[hq];
  if(!hqB || hqB.type!=='HQ' || hqB.side!==side)
    return {ok:false, reason:'TRAIN_NO_HQ', train:0};

  const cap=HQ_LEVELS[hqB.level][4];
  if(requested>cap)
    return {ok:false, reason:'TRAIN_OVER_CAP', train:0};

  const cost=TRAIN_COST*requested;
  if(snap.gold[side] < cost)
    return {ok:false, reason:'TRAIN_NO_GOLD', train:0};

  snap.gold[side]-=cost;
  return {ok:true, train:requested};
}
function spawnLocalTraining(snap, side, train, events){
  const hq=side==='A'?0:STATE.map.N-1;
  const hqB=snap.buildings[hq];
  for(let i=0;i<train;i++){
    const id=side+(HP.localNextId[side]++);
    const hp=hqB ? HQ_LEVELS[hqB.level][1] : HQ_LEVELS[1][1];
    snap.warriors[id]={side, region:hq, hp, maxHp:hp, state:'S', target:null};
    events.push({kind:'train', text:`${side} 전사 ${id} 훈련`});
  }
}
function applyLocalMovement(snap, events){
  const enemyOf={A:new Set(), B:new Set()};
  for(const w of Object.values(snap.warriors)){
    if(w.hp<=0) continue;
    if(w.side==='A') enemyOf.B.add(w.region);
    else enemyOf.A.add(w.region);
  }
  for(const id of Object.keys(snap.warriors).sort(warriorSort)){
    const w=snap.warriors[id];
    if(!w || w.hp<=0 || !isWarriorMoving(w)) continue;
    const target=w.target;
    if(w.region===target){
      w.target=null; w.state='S';
      continue;
    }
    if(enemyOf[w.side].has(w.region)) continue;
    const next=shortestNext(STATE.map, w.region, target);
    if(next===w.region) continue;
    w.region=next;
    events.push({kind:'move', text:`${id} → ${next}`});
    if(w.region===target){
      w.target=null;
      w.state='S';
    }
  }
}

function damageOneLocalWarrior(snap, region, side){
  let bestId=null, bestHp=Infinity, bestSuffix=Infinity;
  for(const [id,w] of Object.entries(snap.warriors)){
    if(w.region!==region || w.side!==side || w.hp<=0) continue;
    const suffix=warriorSuffix(id);
    if(w.hp<bestHp || (w.hp===bestHp && suffix<bestSuffix)){
      bestId=id; bestHp=w.hp; bestSuffix=suffix;
    }
  }
  if(!bestId) return null;
  snap.warriors[bestId].hp-=1;
  return bestId;
}
function addLocalDamage(total, cause, id){
  const key=cause+'|'+id;
  total[key]=(total[key]||0)+1;
}
function emitLocalDamageEvents(total, events){
  const causeKo={TURRET:'포탑', COMBAT:'전투', HUNGER:'식비'};
  const order={TURRET:0, COMBAT:1, HUNGER:2};
  Object.entries(total)
    .map(([key,dmg])=>{
      const [cause,id]=key.split('|');
      return {cause,id,dmg};
    })
    .sort((a,b)=>order[a.cause]-order[b.cause] || warriorSort(a.id,b.id))
    .forEach(d=>events.push({kind:'dmg', text:`${d.id} −${d.dmg} (${causeKo[d.cause]||d.cause})`}));
}
function resolveLocalTurn(snap, events){
  const counts={};
  for(const w of Object.values(snap.warriors)){
    if(w.hp<=0) continue;
    if(!counts[w.region]) counts[w.region]={A:0,B:0};
    counts[w.region][w.side]++;
  }
  for(const r of Object.keys(snap.buildings)){
    if(!counts[r]) counts[r]={A:0,B:0};
  }

  const damageTotal={};
  const siegeDamage={};
  for(const regionText of Object.keys(counts).map(Number).sort((a,b)=>a-b)){
    const region=+regionText;
    const c=counts[region];
    const b=snap.buildings[region];
    const bA=b && b.side==='A';
    const bB=b && b.side==='B';
    const turret=b ? buildingTurret(b) : 0;
    const leftPresent=c.A>0 || bA;
    const rightPresent=c.B>0 || bB;
    if(!leftPresent || !rightPresent) continue;

    const leftCap=c.A+(bA?turret:0);
    const rightCap=c.B+(bB?turret:0);
    let leftIdle=0, rightIdle=0;
    for(let i=0;i<leftCap;i++){
      const cause=bA && i<turret ? 'TURRET' : 'COMBAT';
      const id=damageOneLocalWarrior(snap, region, 'B');
      if(id) addLocalDamage(damageTotal, cause, id);
      else leftIdle++;
    }
    for(let i=0;i<rightCap;i++){
      const cause=bB && i<turret ? 'TURRET' : 'COMBAT';
      const id=damageOneLocalWarrior(snap, region, 'A');
      if(id) addLocalDamage(damageTotal, cause, id);
      else rightIdle++;
    }
    if(b){
      const idleAgainstBuilding=b.side==='A' ? rightIdle : leftIdle;
      if(idleAgainstBuilding>0) siegeDamage[region]=idleAgainstBuilding;
    }
  }
  emitLocalDamageEvents(damageTotal, events);

  for(const region of Object.keys(snap.buildings).map(Number).sort((a,b)=>a-b)){
    const dmg=siegeDamage[region]||0;
    if(dmg<=0) continue;
    const b=snap.buildings[region];
    const dealt=Math.min(dmg, b.hp);
    b.hp-=dealt;
    events.push({kind:'siege', text:`${b.side} 건물 @${region} −${dealt}`});
    if(b.hp<=0) delete snap.buildings[region];
  }
  for(const id of Object.keys(snap.warriors)){
    if(snap.warriors[id].hp<=0) delete snap.warriors[id];
  }
  const consumed=new Set();
  for(const region of Object.keys(snap.buildings).map(Number).sort((a,b)=>a-b)){
    const b=snap.buildings[region];
    const workers=Object.entries(snap.warriors)
      .filter(([id,w])=>w.hp>0 && w.side===b.side && w.region===region && !consumed.has(id))
      .map(([id])=>id)
      .sort(warriorSort);
    const take=Math.min(workers.length, buildingWorkCap(b));
    for(let i=0;i<take;i++){
      consumed.add(workers[i]);
      snap.gold[b.side]+=WORK_INCOME;
    }
  }
  const hungerTotal={};
  for(const side of ['A','B']){
    const ids=Object.entries(snap.warriors)
      .filter(([id,w])=>w.hp>0 && w.side===side)
      .map(([id])=>id)
      .sort(warriorSort);
    for(const id of ids){
      if(snap.gold[side]>=UPKEEP) snap.gold[side]-=UPKEEP;
      else {
        snap.warriors[id].hp-=1;
        addLocalDamage(hungerTotal, 'HUNGER', id);
      }
    }
  }
  emitLocalDamageEvents(hungerTotal, events);
  for(const id of Object.keys(snap.warriors)){
    if(snap.warriors[id].hp<=0) delete snap.warriors[id];
  }
}
function localResultFor(base, turn){
  const leftAlive=!!base.buildings[0];
  const rightAlive=!!base.buildings[STATE.map.N-1];
  if(!leftAlive && !rightAlive) return 'RESULT DRAW HQ_DESTROYED';
  if(!leftAlive) return 'RESULT RIGHT_WIN HQ_DESTROYED';
  if(!rightAlive) return 'RESULT LEFT_WIN HQ_DESTROYED';
  if(turn<200) return null;
  const hpA=base.buildings[0].hp;
  const hpB=base.buildings[STATE.map.N-1].hp;
  if(hpA>hpB) return 'RESULT LEFT_WIN TURN_LIMIT';
  if(hpB>hpA) return 'RESULT RIGHT_WIN TURN_LIMIT';
  return 'RESULT DRAW TURN_LIMIT';
}
function applyLocalTurn(turn, continueHuman=true, renderAfter=true){
  const base=cloneState(STATE.snaps[STATE.snaps.length-1]);
  const events=[];
  const cmdsA=localSideCommands('A');
  const cmdsB=localSideCommands('B');
  const commandBuildings=cloneState(base.buildings);
  const builtToday=new Set();
  applyLocalUpgrades(base, 'A', cmdsA, events, builtToday);
  applyLocalUpgrades(base, 'B', cmdsB, events, builtToday);
  applyLocalMoveCommands(base, 'A', cmdsA, commandBuildings);
  applyLocalMoveCommands(base, 'B', cmdsB, commandBuildings);
  // TRAIN_OVER_CAP·TRAIN_NO_GOLD 는 게임 종료 대신 자동 조정.
  // 연습 모드에서 실수로 한도를 초과해도 게임이 강제 종료되지 않도록 한다.
  const tryClampTrain = (side, cmds) => {
    const res = chargeLocalTraining(base, side, cmds);
    if(res.ok) return res;
    if(res.reason === 'TRAIN_OVER_CAP' || res.reason === 'TRAIN_NO_GOLD'){
      const hqReg = side==='A' ? 0 : STATE.map.N-1;
      const hqB = base.buildings[hqReg];
      const cap = hqB ? HQ_LEVELS[hqB.level][4] : 0;
      const affordable = Math.max(0, Math.floor(base.gold[side] / TRAIN_COST));
      const before = cmds.train;
      cmds.train = Math.min(cap, affordable);
      if(before !== cmds.train)
        events.push({kind:'dmg',
          text:`${side} 훈련 한도 초과 → ${cmds.train}명으로 자동 조정 (본부 한도 ${cap}명)`});
      return chargeLocalTraining(base, side, cmds);
    }
    return res;
  };
  const trainResA = tryClampTrain('A', cmdsA);
  const trainResB = tryClampTrain('B', cmdsB);
  // 규칙: 유효하지 않은 명령은 즉시 규칙 위반(WA) 패배.
  // 동시에 위반이면 무승부.
  if(!trainResA.ok || !trainResB.ok){
    let result;
    if(!trainResA.ok && !trainResB.ok) result='RESULT DRAW VIOLATION';
    else if(!trainResA.ok) result='RESULT RIGHT_WIN VIOLATION';
    else result='RESULT LEFT_WIN VIOLATION';
    base.day=turn;
    base.events=events;
    base.events.push({kind:'dmg', text:`규칙 위반: ${!trainResA.ok?'A '+trainResA.reason:''}${(!trainResA.ok&&!trainResB.ok)?' / ':''}${!trainResB.ok?'B '+trainResB.reason:''}`});
    base.time={lMs:0,lTok:5,rMs:0,rTok:5};
    STATE.snaps.push(base);
    STATE.result=result;
    HP.finished=true; HP.waitingTurn=null;
    if(renderAfter){
      setBoardReady();
      goTo(STATE.snaps.length-1);
      document.getElementById('playPanel').classList.remove('show');
      document.getElementById('liveBadge').classList.remove('show');
      render();
    }
    return false;
  }
  const trainA=trainResA.train;
  const trainB=trainResB.train;
  applyLocalMovement(base, events);
  spawnLocalTraining(base, 'A', trainA, events);
  spawnLocalTraining(base, 'B', trainB, events);
  resolveLocalTurn(base, events);
  base.day=turn;
  base.events=events;
  base.time={lMs:0,lTok:5,rMs:0,rTok:5};
  STATE.snaps.push(base);
  if(renderAfter){
    setBoardReady();
    goTo(STATE.snaps.length-1);
  }
  const result=localResultFor(base, turn);
  if(result){
    STATE.result=result;
    HP.finished=true; HP.waitingTurn=null;
    if(renderAfter){
      document.getElementById('playPanel').classList.remove('show');
      document.getElementById('liveBadge').classList.remove('show');
      render();
    }
    return false;
  }
  if(continueHuman){
    HP.waitingTurn=turn+1;
    onMyTurn(HP.waitingTurn, null);
  }
  return true;
}

function startHumanPolling(){
  if(HP.pollTimer) clearInterval(HP.pollTimer);
  HP.pollTimer=setInterval(pollHuman, 500);
  pollHuman();
}

async function pollHuman(){
  if(!HP.active) return;
  try{
    const r=await fetch(apiUrl('/human/state'),{cache:'no-store'});
    const s=await r.json();
    if(s.log && s.log.length!==HP.lastLogLen){
      HP.lastLogLen=s.log.length;
      const hasResult=/RESULT \S+ \S+/.test(s.log);
      rebuildHumanBoard(s.log, hasResult);
    }
    if(s.finished && !HP.finished){
      HP.finished=true;
      document.getElementById('liveBadge').classList.remove('show');
      document.getElementById('playPanel').classList.remove('show');
      clearInterval(HP.pollTimer);
      if(STATE.snaps.length) goTo(STATE.snaps.length-1);
      return;
    }
    if(s.error && !s.waiting_turn){
      document.getElementById('playPanel').classList.add('show');
      document.getElementById('ppTurn').textContent='대국 시작 실패';
      document.getElementById('ppHint').textContent=
        '상대 AI 또는 채점기 실행에 실패했습니다. 서버는 sample-code.py를 자동 탐색하도록 수정되어 있으니, 서버를 다시 켠 뒤 시도해 주세요.';
      document.getElementById('liveBadge').classList.remove('show');
      clearInterval(HP.pollTimer);
      return;
    }
    if(s.waiting_turn && s.waiting_turn!==HP.waitingTurn){
      HP.waitingTurn=s.waiting_turn;
      onMyTurn(s.waiting_turn, s.map);
    }
  }catch(e){ /* 무시 */ }
}

function rebuildHumanBoard(logText, hasResult){
  try{
    const parsed=parseLog(logText + (hasResult?'':'\nRESULT DRAW TURN_LIMIT\n'));
    STATE.map=parsed.map;
    STATE.result = hasResult ? parsed.result : null;
    if(!STATE.layout) STATE.layout=computeLayout(parsed.map);
    STATE.snaps=buildSnapshots(parsed);
    const slider=document.getElementById('slider');
    slider.disabled=false; slider.max=STATE.snaps.length-1;
    document.getElementById('emptyState').style.display='none';
    document.getElementById('legend').style.display='flex';
    goTo(STATE.snaps.length-1);
  }catch(e){ /* 부분 로그 */ }
}

function onMyTurn(turn, mapInfo){
  if((!STATE.snaps || !STATE.snaps.length) && mapInfo){
    buildBoardFromHumanMap(mapInfo);
  }
  document.getElementById('playPanel').classList.add('show');
  document.getElementById('ppTurn').textContent=`내 차례 · ${turn}일`;
  document.getElementById('ppHint').textContent=
    '전사를 클릭한 뒤 목적지 구역을 클릭하면 이동. 거점/본부는 아군 전사가 그 칸에 있을 때 건설·업그레이드.';
  updateTrainCap();
  renderQueue();
}

function buildBoardFromHumanMap(m){
  const lines=['MAP', m.nk, m.x, m.y, 'STRONGHOLDS '+m.strongholds];
  for(const a of m.adj) lines.push(a);
  lines.push('END MAP');
  const text=lines.join('\n')+'\nRESULT DRAW TURN_LIMIT\n';
  const parsed=parseLog(text);
  STATE.map=parsed.map;
  STATE.layout=computeLayout(parsed.map);
  STATE.snaps=buildSnapshots({map:parsed.map, turns:[]});
  STATE.result=null;
  STATE.selectedRegion=0;
  document.getElementById('emptyState').style.display='none';
  document.getElementById('legend').style.display='flex';
  const slider=document.getElementById('slider');
  slider.disabled=false; slider.max=0; slider.value=0;
  goTo(0);
}

// 사람 플레이어가 이번 턴 큐에 쌓은 UPGRADE 명령을 반영했을 때의
// "건설 단계가 끝난 뒤" 본부 레벨을 계산한다.
// 규칙상 훈련 cap 은 같은 턴 건설 단계 이후의 본부 레벨을 따른다.
// 같은 구역(본부)에 대한 건설 명령은 하루 최대 한 번이므로 레벨업은 1회만 반영한다.
function projectedHqLevel(snap, side){
  const hqReg = side==='A' ? 0 : STATE.map.N-1;
  const hq = snap.buildings[hqReg];
  if(!hq || hq.type!=='HQ' || hq.side!==side) return 0;
  let level = hq.level;
  // 큐에 본부 구역에 대한 업그레이드가 있으면 한 단계 상승(최대 레벨 이하).
  if(HP.queueUpgrades && HP.queueUpgrades.includes(hqReg) && level < HQ_MAX){
    level += 1;
  }
  return level;
}

function updateTrainCap(){
  const snap=commandSnap() || STATE.snaps[STATE.idx];
  if(!snap || !STATE.map) return;
  const hqLevel = projectedHqLevel(snap, HP.side);
  const hqCap = hqLevel>0 ? HQ_LEVELS[hqLevel][4] : 0;
  const reserved=humanQueuedCost(false);
  const affordable=Math.max(0, Math.floor(((snap.gold[HP.side]||0)-reserved)/TRAIN_COST));
  const cap=Math.min(hqCap, affordable);
  const input=document.getElementById('ppTrain');
  input.max=cap;
  if(+input.value>cap) input.value=cap;
  if(+input.value<0) input.value=0;
  const btns=document.getElementById('ppTrainBtns');
  if(btns){
    const cur=+input.value;
    let html='';
    for(let n=1;n<=hqCap;n++){
      const ok=n<=cap;
      html+=`<button type="button" class="pp-train-btn${cur===n?' active':''}" data-n="${n}"${ok?'':' disabled'} title="${n}명 훈련 · ${n*TRAIN_COST}G">${n}명</button>`;
    }
    if(hqCap===0) html=`<span style="color:var(--ink-dim);font-size:11px">본부 없음</span>`;
    btns.innerHTML=html;
    btns.querySelectorAll('.pp-train-btn').forEach(btn=>{
      btn.addEventListener('click',()=>{
        const n=+btn.dataset.n;
        input.value=(+input.value===n)?0:n;
        renderQueue();
      });
    });
  }
  document.getElementById('ppTrainCap').textContent=hqCap>0
    ?`(본부 한도 ${hqCap}명 · 금화 최대 ${affordable}명)`
    :`(훈련 불가)`;
}

function isUpgradeableRegion(region){
  const SH=new Set(STATE.map.strongholds);
  return SH.has(region) || region===0 || region===STATE.map.N-1;
}

function upgradeBlockReason(region, snap){
  if(!isUpgradeableRegion(region)) return '건설은 거점·본부에서만 가능합니다.';
  const friendly=Object.values(snap.warriors)
    .some(w=>w.side===HP.side && w.region===region);
  if(!friendly) return `구역 ${region}에 아군 전사가 있어야 건설할 수 있습니다. 이동한 다음 턴에 다시 명령하세요.`;
  const enemy=Object.values(snap.warriors)
    .some(w=>w.side!==HP.side && w.region===region);
  if(enemy) return `구역 ${region}에 적 전사가 있어 건설할 수 없습니다.`;

  const b=snap.buildings[region];
  const isHQ=region===0 || region===STATE.map.N-1;
  if(!b){
    if(isHQ) return '본부 칸에는 새 기지를 건설할 수 없습니다.';
    const reserved=humanQueuedCost();
    if(snap.gold[HP.side]-reserved < BASE_LEVELS[1][0]){
      return `금화가 부족합니다. 현재 예약 ${reserved}G를 제외하면 기지 건설에 ${BASE_LEVELS[1][0]}G가 필요합니다.`;
    }
    return '';
  }
  if(b.side!==HP.side) return `구역 ${region}은 적 건물이 있어 업그레이드할 수 없습니다.`;

  const maxLevel=b.type==='HQ'?HQ_MAX:BASE_MAX;
  const cost=b.level>=maxLevel
    ? (b.type==='HQ'?HQ_HEAL:BASE_HEAL)
    : (b.type==='HQ'?HQ_LEVELS[b.level+1][0]:BASE_LEVELS[b.level+1][0]);
  const reserved=humanQueuedCost();
  if(snap.gold[HP.side]-reserved < cost){
    return `금화가 부족합니다. 현재 예약 ${reserved}G를 제외하면 ${cost}G가 필요합니다.`;
  }
  return '';
}

function onBoardClick(region){
  STATE.selectedRegion=region;
  if(!HP.active || HP.waitingTurn===null){
    render();
    return;
  }
  const snap=STATE.snaps[STATE.idx];
  const myHere=Object.entries(snap.warriors)
    .filter(([id,w])=>w.side===HP.side && w.region===region);
  const canUpgradeHere=isUpgradeableRegion(region);
  if(HP.selWarrior){
    const cur=snap.warriors[HP.selWarrior];
    if(cur && region===cur.region && canUpgradeHere){
      addUpgrade(region);
      HP.selWarrior=null;
      render();
      return;
    }
    if(cur && region!==cur.region){
      if(addMove(HP.selWarrior, region)) HP.selWarrior=null;
      render(); return;
    }
    if(cur && region===cur.region){ // 같은 칸 → 선택 해제
      HP.selWarrior=null;
      document.getElementById('ppHint').textContent='선택을 해제했습니다.';
      render(); return;
    }
  }
  if(myHere.length){
    HP.selWarrior=myHere.map(([id])=>id)
      .sort((a,b)=>parseInt(a.slice(1))-parseInt(b.slice(1)))[0];
    document.getElementById('ppHint').textContent=
      `${HP.selWarrior} 선택됨 — 목적지 구역을 클릭하세요. (현재 칸을 다시 누르면 해제, 같은 칸을 다시 누르면 건설)`;
    render(); return;
  }
  if(canUpgradeHere){
    addUpgrade(region);
    render();
    return;
  }
  document.getElementById('ppHint').textContent='건설은 거점·본부에서만 가능합니다.';
}

function onWarriorClick(id, region){
  STATE.selectedRegion=region;
  if(!HP.active || HP.waitingTurn===null){ onBoardClick(region); return; }
  const snap=STATE.snaps[STATE.idx];
  const w=snap.warriors[id];
  if(!w || w.side!==HP.side){ onBoardClick(region); return; }
  if(isWarriorMoving(w)){
    document.getElementById('ppHint').textContent=
      `${id}는 이미 ${w.target}번 구역으로 이동 중입니다. 목적지에 도착한 뒤 다시 명령할 수 있습니다.`;
    render();
    return;
  }
  if(HP.selWarrior===id){
    HP.selWarrior=null;
    document.getElementById('ppHint').textContent=`${id} 선택을 해제했습니다.`;
    render();
    return;
  }
  HP.selWarrior=id;
  document.getElementById('ppHint').textContent=
    `${id} 선택됨 — 목적지 구역을 클릭하세요. (같은 병사를 다시 누르면 해제)`;
  render();
}

function addMove(id, region){
  const snap=commandSnap();
  const w=snap && snap.warriors[id];
  if(!w || w.side!==HP.side){
    document.getElementById('ppHint').textContent='내 병사만 이동 명령을 받을 수 있습니다.';
    return false;
  }
  if(isWarriorMoving(w)){
    document.getElementById('ppHint').textContent=`${id}는 이미 ${w.target}번 구역으로 이동 중입니다.`;
    return false;
  }
  if(region===w.region){
    document.getElementById('ppHint').textContent='현재 위치와 다른 목적지를 선택하세요.';
    return false;
  }
  if(!canReachRegion(STATE.map, w.region, region)){
    document.getElementById('ppHint').textContent=`구역 ${region}까지 이동 가능한 경로가 없습니다.`;
    return false;
  }
  HP.queueMoves=HP.queueMoves.filter(m=>m.id!==id);
  HP.queueMoves.push({id, region});
  const cost=moveCostForRegion(region, snap);
  document.getElementById('ppHint').textContent=`${id} → ${region} 이동 추가됨. 비용 ${cost}G.`;
  renderQueue();
  return true;
}
function addUpgrade(region){
  if(HP.queueUpgrades.includes(region)){
    document.getElementById('ppHint').textContent=`구역 ${region} 은 이미 목록에 있습니다.`;
    return false;
  }
  const snap=STATE.snaps[STATE.idx];
  const reason=upgradeBlockReason(region, snap);
  if(reason){
    document.getElementById('ppHint').textContent=reason;
    return false;
  }
  HP.queueUpgrades.push(region);
  const b=snap.buildings[region];
  const what = !b ? '기지 건설' : (b.type==='HQ'?'본부':'기지')+' 업그레이드/수리';
  document.getElementById('ppHint').textContent=`구역 ${region} ${what} 추가됨. 비용 ${upgradeCostForRegion(region, snap)}G.`;
  renderQueue();
  return true;
}

function commandSnap(){
  return STATE.snaps[STATE.snaps.length-1] || STATE.snaps[STATE.idx] || null;
}

function upgradeCostForRegion(region, snap){
  if(!snap) return 0;
  const b=snap.buildings[region];
  if(!b) return BASE_LEVELS[1][0];
  if(b.side!==HP.side) return 0;
  const maxLevel=b.type==='HQ'?HQ_MAX:BASE_MAX;
  if(b.level>=maxLevel) return b.type==='HQ'?HQ_HEAL:BASE_HEAL;
  return b.type==='HQ'?HQ_LEVELS[b.level+1][0]:BASE_LEVELS[b.level+1][0];
}

function upgradeLabelForRegion(region, snap){
  const b=snap && snap.buildings[region];
  if(!b) return '기지 건설';
  if(b.side!==HP.side) return '적 건물';
  const maxLevel=b.type==='HQ'?HQ_MAX:BASE_MAX;
  if(b.level>=maxLevel) return `${b.type==='HQ'?'본부':'기지'} 수리`;
  return `${b.type==='HQ'?'본부':'기지'} Lv${b.level+1}`;
}

function moveCostForRegion(region, snap){
  const b=snap && snap.buildings[region];
  return (b && b.side===HP.side) ? 0 : MOVE_COST;
}

function humanQueuedCost(includeTrain=true){
  const snap=commandSnap();
  if(!snap) return 0;
  const upgrades=HP.queueUpgrades.reduce((sum, region)=>sum+upgradeCostForRegion(region, snap), 0);
  const moves=HP.queueMoves.reduce((sum, move)=>sum+moveCostForRegion(move.region, snap), 0);
  const trainInput=document.getElementById('ppTrain');
  const train=(includeTrain && trainInput) ? Math.max(0, Math.floor(+trainInput.value||0))*TRAIN_COST : 0;
  return upgrades+moves+train;
}

function clampTrainInput(){
  updateTrainCap();
  const input=document.getElementById('ppTrain');
  return Math.max(0, Math.floor(+input.value||0));
}

function renderQueue(){
  updateTrainCap();
  const q=document.getElementById('ppQueue');
  const snap=commandSnap();
  const rows=[];
  HP.queueUpgrades.forEach((r,i)=>{
    const cost=upgradeCostForRegion(r, snap);
    rows.push(`
      <div class="qrow rich">
        <span class="qmain"><b>UPGRADE ${r}</b><em>${escapeHtml(upgradeLabelForRegion(r, snap))} · ${cost}G</em></span>
        <button data-t="u" data-i="${i}" title="취소">✕</button>
      </div>
    `);
  });
  HP.queueMoves.forEach((m,i)=>{
    const cost=moveCostForRegion(m.region, snap);
    rows.push(`
      <div class="qrow rich">
        <span class="qmain"><b>MOVE ${escapeHtml(m.id)} → ${m.region}</b><em>${regionKind(m.region)} · ${cost}G</em></span>
        <button data-t="m" data-i="${i}" title="취소">✕</button>
      </div>
    `);
  });
  const tn=Math.max(0, Math.floor(+document.getElementById('ppTrain').value||0));
  if(tn>0){
    rows.push(`
      <div class="qrow rich">
        <span class="qmain"><b>TRAIN ${tn}</b><em>병사 생성 · ${tn*TRAIN_COST}G</em></span>
        <span style="color:var(--ink-dim)">생성</span>
      </div>
    `);
  }
  const total=humanQueuedCost();
  const gold=snap ? snap.gold[HP.side] : 0;
  const summary=rows.length ? `
    <div class="qsum">
      <span>예약 비용</span><b>${total}G</b>
      <span class="wide">예상 잔액 ${gold-total}G</span>
    </div>
  ` : '';
  q.innerHTML = rows.length ? rows.join('')+summary : '<div class="pp-empty">아직 명령 없음</div>';
  q.querySelectorAll('button[data-t]').forEach(btn=>{
    btn.addEventListener('click', ()=>{
      const t=btn.dataset.t, i=+btn.dataset.i;
      if(t==='u') HP.queueUpgrades.splice(i,1); else HP.queueMoves.splice(i,1);
      renderQueue();
    });
  });
}

async function submitHuman(){
  if(HP.waitingTurn===null){ alert('아직 내 차례가 아닙니다.'); return; }
  const lines=[];
  for(const r of HP.queueUpgrades) lines.push(`UPGRADE ${r}`);
  for(const m of HP.queueMoves) lines.push(`MOVE ${m.id} ${m.region}`);
  const tn=clampTrainInput();
  if(tn>0) lines.push(`TRAIN ${tn}`);
  const turn=HP.waitingTurn;
  if(HP.local){
    applyLocalTurn(turn);
    HP.queueMoves=[]; HP.queueUpgrades=[]; HP.selWarrior=null;
    document.getElementById('ppTrain').value=0;
    renderQueue();
    render();
    return;
  }
  try{
    await postJson('/human/submit', {turn, lines});
    HP.queueMoves=[]; HP.queueUpgrades=[]; HP.selWarrior=null; HP.waitingTurn=null;
    document.getElementById('ppTrain').value=0;
    renderQueue();
    document.getElementById('ppHint').textContent='제출 완료. 채점기·상대 처리를 기다리는 중...';
  }catch(e){ alert('제출 실패: 서버 확인'); }
}

function clearQueue(){
  HP.queueMoves=[]; HP.queueUpgrades=[]; HP.selWarrior=null;
  document.getElementById('ppTrain').value=0;
  renderQueue(); render();
  document.getElementById('ppHint').textContent='명령을 초기화했습니다.';
}

/* ============================================================
   9) 내 AI 테스트 (실행 파일 선택 → /new 에 경로 전달)
   서버가 viewer.html 폴더를 스캔해 실행 파일 목록을 반환한다.
   ============================================================ */
async function fetchBinaries(){
  const sel = document.getElementById('cppExeSelect');
  const prev = sel.value;
  sel.innerHTML = '<option value="">불러오는 중...</option>';
  if(!await checkServer()){
    sel.innerHTML = '<option value="">직접 플레이</option>';
    updateBattleBtn();
    return;
  }
  try{
    const r = await fetch(apiUrl('/list-binaries'), {cache:'no-store'});
    const list = await r.json();
    sel.innerHTML = '<option value="">직접 플레이</option>';
    for(const b of list){
      const opt = document.createElement('option');
      opt.value = b.path;
      opt.textContent = b.name;
      sel.appendChild(opt);
    }
    if(prev && Array.from(sel.options).some(o=>o.value===prev)){
      sel.value = prev;
    }
  }catch(e){
    sel.innerHTML = '<option value="">직접 플레이</option>';
  }
  updateBattleBtn();
}

function updateBattleBtn(){
  const sel = document.getElementById('cppExeSelect');
  const btn = document.getElementById('cppStartBtn');
  const hasExe = !!sel.value;
  btn.textContent = hasExe ? 'AI 대전 시작' : '▶ 직접 플레이';
  btn.classList.toggle('play-new', hasExe);
  btn.classList.toggle('play-human', !hasExe);
}

async function startBattle(){
  const binary = document.getElementById('cppExeSelect').value;
  if(binary) await startCppGame();
  else await humanNewGame();
}

async function startCppGame(){
  const binary = document.getElementById('cppExeSelect').value;
  if(!binary){ alert('실행 파일을 선택하세요.'); return; }
  const mode = document.querySelector('input[name="mapmode"]:checked')?.value || 'random';
  const seed = ensureVisibleSeed(mode === 'random');
  if(!/^\d+$/.test(seed)){ alert('시드는 숫자로 입력하세요.'); return; }
  const humanSide = document.querySelector('input[name="playside"]:checked')?.value || 'LEFT';
  stop();
  if(!await checkServer()){
    alert(serverHelpMessage('사람 vs AI 대전'));
    return;
  }
  try{
    const r = await postJson('/human/new', {seed, side: humanSide, ai: binary});
    const j = await r.json();
    if(!j.ok){ alert('대국 시작 실패: ' + (j.error || '알 수 없는 오류')); return; }
    HP.active=true; HP.local=false; HP.mySideWord=j.side; HP.side = j.side==='LEFT'?'A':'B';
    HP.waitingTurn=null; HP.queueMoves=[]; HP.queueUpgrades=[]; HP.selWarrior=null;
    HP.lastLogLen=0; HP.finished=false;
    document.getElementById('liveSeed').textContent=j.seed;
    document.getElementById('liveBadge').classList.add('show');
    document.getElementById('ppSide').textContent = j.side+'('+HP.side+')';
    startHumanPolling();
  }catch(e){
    alert('대국을 시작하지 못했습니다. 서버(viewer_server.py)가 실행 중인지 확인하세요.');
  }
}

/* ============================================================
   8) 로드 진입점
   ============================================================ */
function loadLogText(text){
  try{
    const parsed=parseLog(text);
    STATE.map=parsed.map;
    STATE.result=parsed.result;
    STATE.layout=computeLayout(parsed.map);
    STATE.snaps=buildSnapshots(parsed);
    STATE.idx=0;
    STATE.selectedRegion=0;
    const slider=document.getElementById('slider');
    slider.max=STATE.snaps.length-1; slider.value=0; slider.disabled=false;
    document.getElementById('emptyState').style.display='none';
    document.getElementById('legend').style.display='flex';
    goTo(0);
  }catch(err){
    alert('로그 파싱 실패: '+err.message);
    console.error(err);
  }
}

function markdownToHtml(markdown){
  const lines=String(markdown).replace(/\r\n/g,'\n').split('\n');
  const out=[];
  let inCode=false, listOpen=false, tableOpen=false;
  const closeList=()=>{ if(listOpen){ out.push('</ul>'); listOpen=false; } };
  const closeTable=()=>{ if(tableOpen){ out.push('</tbody></table>'); tableOpen=false; } };
  const isTableRow=(l)=> l.trim().startsWith('|') && l.trim().endsWith('|');
  const isSepRow=(l)=> /^\|[-| :]+\|$/.test(l.trim().replace(/\s/g,''));
  for(let li=0;li<lines.length;li++){
    const line=lines[li];
    if(line.trim().startsWith('```')){
      if(inCode){ out.push('</code></pre>'); inCode=false; }
      else { closeList(); closeTable(); out.push('<pre><code>'); inCode=true; }
      continue;
    }
    if(inCode){ out.push(escapeHtml(line)+'\n'); continue; }
    const h=line.match(/^(#{1,3})\s+(.+)$/);
    if(h){
      closeList(); closeTable();
      out.push(`<h${h[1].length}>${inlineMarkdown(h[2])}</h${h[1].length}>`);
      continue;
    }
    if(isTableRow(line)){
      const next=lines[li+1]||'';
      if(!tableOpen){
        if(isSepRow(next)){
          closeList();
          out.push('<table style="border-collapse:collapse;font-size:12px;margin:8px 0">');
          out.push('<thead><tr>');
          const cells=line.trim().slice(1,-1).split('|');
          cells.forEach(c=>out.push(`<th style="border:1px solid var(--line);padding:4px 8px;background:var(--panel-2)">${inlineMarkdown(c.trim())}</th>`));
          out.push('</tr></thead><tbody>');
          tableOpen=true;
          li++; // skip separator
          continue;
        }
      } else {
        if(isSepRow(line)) continue;
        out.push('<tr>');
        const cells=line.trim().slice(1,-1).split('|');
        cells.forEach(c=>out.push(`<td style="border:1px solid var(--line);padding:4px 8px">${inlineMarkdown(c.trim())}</td>`));
        out.push('</tr>');
        continue;
      }
    } else {
      closeTable();
    }
    const lm=line.match(/^\s*[-*]\s+(.+)$/);
    if(lm){
      if(!listOpen){ out.push('<ul>'); listOpen=true; }
      out.push(`<li>${inlineMarkdown(lm[1])}</li>`);
      continue;
    }
    if(!line.trim()){ closeList(); continue; }
    closeList();
    out.push(`<p>${inlineMarkdown(line)}</p>`);
  }
  closeList(); closeTable();
  if(inCode) out.push('</code></pre>');
  return out.join('\n');
}

function inlineMarkdown(text){
  return escapeHtml(text)
    .replace(/`([^`]+)`/g,'<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g,'<strong>$1</strong>');
}

async function openRules(){
  const modal=document.getElementById('rulesModal');
  const body=document.getElementById('rulesBody');
  modal.classList.add('show');
  body.innerHTML=markdownToHtml(EMBEDDED_RULES_MD);
  if(isFileMode()) return;
  try{
    const r=await fetch(staticUrl(RULES_FILE),{cache:'no-store'});
    if(!r.ok) throw new Error('rules missing');
    body.innerHTML=markdownToHtml(await r.text());
  }catch(_){
    body.innerHTML=markdownToHtml(EMBEDDED_RULES_MD);
  }
}

function closeRules(){
  document.getElementById('rulesModal').classList.remove('show');
}

/* ---------- 이벤트 바인딩 ---------- */
function bind(){
  preloadAssets();
  // 서버가 없어도 브라우저 단독 모드로 새 전장/직접 플레이가 가능하다.
  checkServer().then(ok=>{
    document.getElementById('cpptestPanel').style.display='flex';
    if(ok) fetchBinaries();
  });
  // 시드/랜덤 토글
  document.querySelectorAll('input[name="mapmode"]').forEach(r=>{
    r.addEventListener('change', ()=>syncSeedMode(r.value==='random'));
    r.addEventListener('click', ()=>{ if(r.value==='random') syncSeedMode(true); });
  });
  syncSeedMode(true);
  document.getElementById('rulesBtn').addEventListener('click', openRules);
  document.getElementById('rulesClose').addEventListener('click', closeRules);
  document.getElementById('rulesModal').addEventListener('click', e=>{ if(e.target.id==='rulesModal') closeRules(); });
  document.getElementById('ppSubmit').addEventListener('click', submitHuman);
  document.getElementById('ppClear').addEventListener('click', clearQueue);
  document.getElementById('ppTrain').addEventListener('input', renderQueue);
  document.getElementById('seedInput').addEventListener('keydown', e=>{ if(e.key==='Enter') startBattle(); });
  document.getElementById('randomSeedBtn').addEventListener('click', ()=>{
    const seed=makeRandomSeed();
    const seedRadio=document.querySelector('input[name="mapmode"][value="seed"]');
    if(seedRadio && !seedRadio.checked){ seedRadio.checked=true; syncSeedMode(false); }
    const input=document.getElementById('seedInput');
    input.disabled=false;
    input.value=seed;
  });
  document.getElementById('cppRefreshBtn').addEventListener('click', fetchBinaries);
  document.getElementById('cppStartBtn').addEventListener('click', startBattle);
  document.getElementById('cppExeSelect').addEventListener('change', updateBattleBtn);

  document.getElementById('fileInput').addEventListener('change', e=>{
    const f=e.target.files[0]; if(!f) return;
    const r=new FileReader();
    r.onload=()=>loadLogText(r.result);
    r.readAsText(f);
  });
  const wrap=document.getElementById('boardWrap');
  ['dragover','dragenter'].forEach(ev=>wrap.addEventListener(ev,e=>{e.preventDefault();}));
  wrap.addEventListener('drop', e=>{
    e.preventDefault();
    const f=e.dataTransfer.files[0]; if(!f) return;
    const r=new FileReader(); r.onload=()=>loadLogText(r.result); r.readAsText(f);
  });
  document.getElementById('sampleBtn').addEventListener('click', ()=>{
    fetch(staticUrl('sample_log.txt')).then(r=>{
      if(!r.ok) throw new Error('sample_log.txt 없음');
      return r.text();
    }).then(loadLogText).catch(()=>{
      if(isFileMode()){
        startLocalSpectate(ensureVisibleSeed(false) || makeRandomSeed());
        return;
      }
      alert('샘플 로그(sample_log.txt)를 찾을 수 없습니다. 로컬 서버로 열었는지 확인하세요.');
    });
  });
  document.getElementById('slider').addEventListener('input', e=>{stop();goTo(+e.target.value);});
  document.getElementById('firstBtn').addEventListener('click',()=>{stop();goTo(0);});
  document.getElementById('lastBtn').addEventListener('click',()=>{stop();goTo(STATE.snaps.length-1);});
  document.getElementById('prevBtn').addEventListener('click',()=>{stop();goTo(STATE.idx-1);});
  document.getElementById('nextBtn').addEventListener('click',()=>{stop();goTo(STATE.idx+1);});
  document.getElementById('playBtn').addEventListener('click', play);
  document.getElementById('speedSel').addEventListener('change',()=>{if(STATE.playing){stop();play();}});
  window.addEventListener('keydown', e=>{
    if(e.key==='Escape') closeRules();
    if(e.key==='ArrowRight'){stop();goTo(STATE.idx+1);}
    if(e.key==='ArrowLeft'){stop();goTo(STATE.idx-1);}
    if(e.key===' '){e.preventDefault();play();}
  });
}
document.addEventListener('DOMContentLoaded', bind);
