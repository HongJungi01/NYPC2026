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

/* ---------- 이미지 자산 (assets/ 폴더에서 교체 가능) ---------- */
/* 파일이 없으면 onerror 로 폴백 → SVG 도형으로 그림.
   이미지를 바꾸려면 같은 이름의 PNG 를 assets/ 에 넣으세요. */
const ASSET_SRC = {
  hqA:    'assets/hq_left.png',
  hqB:    'assets/hq_right.png',
  baseA:  'assets/base_left.png',
  baseB:  'assets/base_right.png',
  warriorA:'assets/warrior_left.png',
  warriorB:'assets/warrior_right.png',
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
    warriors['A'+s] = {side:'A', region:hqL, hp:HQ_LEVELS[1][1], state:'S', target:null};
    warriors['B'+s] = {side:'B', region:hqR, hp:HQ_LEVELS[1][1], state:'S', target:null};
  }
  const buildings = {}; // region -> {side, type, level, hp}
  buildings[hqL] = {side:'A', type:'HQ', level:1, hp:HQ_LEVELS[1][2]};
  buildings[hqR] = {side:'B', type:'HQ', level:1, hp:HQ_LEVELS[1][2]};
  let gold = {A:START_GOLD, B:START_GOLD};
  let tokens = {A:5, B:5};

  const maxHp = (b)=> b.type==='HQ' ? HQ_LEVELS[b.level][2] : BASE_LEVELS[b.level][2];
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

    // --- 명령에서 "내가 낸 비용" 추정 ---
    // UPGRADE 비용: 명령 제출 측 기준. 로그 UPGRADE 는 양측 합쳐 결과만 줌.
    //   여기서는 결과 UPGRADE 리스트로 비용을 계산(소속 side 의 지갑에서 차감).
    for(const up of t.upgrades){
      const side = up.side==='A'?'A':'B';
      const b = buildings[up.region];
      if(!b){
        // 신규 기지 건설
        gold[side] -= BASE_LEVELS[1][0];
        buildings[up.region] = {side, type:'BASE', level:1, hp:BASE_LEVELS[1][2]};
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
          const target=+p[2];
          const b = buildings[target];
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
        warriors[mv.id] = {side:sideOf(mv.id), region:mv.region, hp:HQ_LEVELS[1][1], state:'S', target:null};
      } else {
        warriors[mv.id].region = mv.region;
      }
      events.push({kind:'move', text:`${mv.id} → ${mv.region}`});
    }

    // --- 훈련된 전사 추가 ---
    for(const id of t.trained){
      const side = sideOf(id);
      const hqReg = side==='A'?hqL:hqR;
      const hqB = buildings[hqReg];
      const lvl = hqB?hqB.level:1;
      warriors[id] = {side, region:hqReg, hp:HQ_LEVELS[lvl][1], state:'S', target:null};
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
const HQ_MARKER_SCALE = 1.5;
const WARRIOR_MARKER_SCALE = 1.5;

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
      const wy=b ? py+warriorStackOffset(b) : py+25;
      addRegionHitArea(svg, px, wy, 46, k);
      if(a.length && bb.length){
        drawWarriorBadge(svg, px-16, wy, 'A', a, k);
        drawWarriorBadge(svg, px+16, wy, 'B', bb, k);
      } else if(a.length) {
        drawWarriorBadge(svg, px, wy, 'A', a, k);
      } else if(bb.length) {
        drawWarriorBadge(svg, px, wy, 'B', bb, k);
      }
    }
  }
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
  return b.type==='HQ' ? 44 : 30;
}

function warriorStackOffset(b){
  return b && b.type==='HQ' ? 45 : 34;
}

function drawBuilding(svg, x, y, b){
  const side=b.side;
  const assetName=(b.type==='HQ'?'hq':'base')+(side==='A'?'A':'B');
  const A=ASSETS[assetName];
  const size = buildingMarkerSize(b);
  if(A&&A.ok){
    svg.appendChild(el('image',{x:x-size/2, y:y-size/2, width:size, height:size,
      href:A.img.src, 'preserveAspectRatio':'xMidYMid meet', 'pointer-events':'none'}));
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
  const maxhp = b.type==='HQ'?HQ_LEVELS[b.level][2]:BASE_LEVELS[b.level][2];
  const w=size, hpw=w*Math.max(0,Math.min(1,b.hp/maxhp));
  svg.appendChild(el('rect',{x:x-w/2, y:y+size*0.5, width:w, height:3, rx:1.5, fill:'#000', opacity:.5, 'pointer-events':'none'}));
  svg.appendChild(el('rect',{x:x-w/2, y:y+size*0.5, width:hpw, height:3, rx:1.5,
    fill: b.hp/maxhp>0.4?'#7fb98a':'#e2664f', 'pointer-events':'none'}));
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

function drawWarriorBadge(svg, x, y, side, warriors, region){
  const A=ASSETS['warrior'+side];
  const col=side==='A'?'var(--left-strong)':'var(--right-strong)';
  const r=8.5 * WARRIOR_MARKER_SCALE;
  const ordered=warriors.slice().sort((a,b)=>parseInt(a.id.slice(1))-parseInt(b.id.slice(1)));
  const n=ordered.length;
  const shown=Math.min(n,5);
  const spread = WARRIOR_MARKER_SCALE;
  const offsets = shown===1 ? [[0,0]]
    : shown===2 ? [[-9*spread,0],[9*spread,0]]
    : shown===3 ? [[-16*spread,0],[0,0],[16*spread,0]]
    : shown===4 ? [[-10*spread,-8*spread],[10*spread,-8*spread],[-10*spread,8*spread],[10*spread,8*spread]]
    : [[-16*spread,-7*spread],[0,-7*spread],[16*spread,-7*spread],[-8*spread,8*spread],[8*spread,8*spread]];
  offsets.forEach(([dx,dy],i)=>{
    const warrior=ordered[i];
    const tx=x+dx, ty=y+dy;
    if(HP.active && HP.selWarrior===warrior.id){
      svg.appendChild(el('circle',{cx:tx, cy:ty, r:r+9, fill:'rgba(255,205,38,.25)',
        stroke:'#111827','stroke-width':4,'pointer-events':'none'}));
      svg.appendChild(el('circle',{cx:tx, cy:ty, r:r+5, fill:'none',
        stroke:'var(--gold)','stroke-width':3,'pointer-events':'none'}));
    }
    if(A&&A.ok){
      svg.appendChild(el('image',{x:tx-r, y:ty-r, width:r*2, height:r*2,
        href:A.img.src, 'pointer-events':'none'}));
    } else {
      svg.appendChild(el('circle',{cx:tx, cy:ty, r, fill:col, stroke:'#fff',
        'stroke-width':1.4, 'pointer-events':'none'}));
    }
    const t=el('text',{x:tx, y:ty+4,'text-anchor':'middle','font-size':10,
      fill:'#fff','font-weight':700,stroke:'#000','stroke-width':.6,
      'paint-order':'stroke','pointer-events':'none'});
    t.textContent=warrior.id.slice(1); svg.appendChild(t);
    addWarriorHitArea(svg, tx, ty, r+8, warrior.id, region);
  });
  if(n>shown){
    const bx=x+32, by=y+20;
    svg.appendChild(el('circle',{cx:bx, cy:by, r:10, fill:'#111820',
      stroke:'#fff','stroke-width':1.2,'pointer-events':'none'}));
    const t=el('text',{x:bx, y:by+3,'text-anchor':'middle','font-size':9,
      fill:'#fff','font-weight':700,'pointer-events':'none'});
    t.textContent='+'+(n-shown); svg.appendChild(t);
  }
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
  const players=aside ? aside.querySelector('.players') : null;
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
  const set=(id,v)=>document.getElementById(id).textContent=v;
  updateRegionInspector(snap);

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
    const mx=b.type==='HQ'?HQ_LEVELS[b.level][2]:BASE_LEVELS[b.level][2];
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
  const bases = [];
  if(isServed()) bases.push('');
  bases.push(LOCAL_SERVER_ORIGIN);
  const seen = new Set();
  for(const base of bases){
    if(seen.has(base)) continue;
    seen.add(base);
    try{
      const r=await fetch(base + '/status',{cache:'no-store'});
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
  if(!await ensureServer('관전 시작')) return;
  const mode=document.querySelector('input[name="mapmode"]:checked').value;
  const seed = ensureVisibleSeed(mode==='random');
  if(!/^\d+$/.test(seed)){
    alert('시드는 숫자로 입력하세요.'); return;
  }
  stop();
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
  local:false, localNextId:{A:4, B:4}
};

async function humanNewGame(){
  const mode=document.querySelector('input[name="mapmode"]:checked').value;
  const seed = ensureVisibleSeed(mode==='random');
  if(!/^\d+$/.test(seed)){ alert('시드는 숫자로 입력하세요.'); return; }
  const side = document.querySelector('input[name="playside"]:checked')?.value || 'LEFT';
  stop();
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
function randFromSeed(seed){
  let a=seed>>>0;
  return function(){
    a += 0x6D2B79F5;
    let t=a;
    t=Math.imul(t ^ (t>>>15), t|1);
    t^=t+Math.imul(t ^ (t>>>7), t|61);
    return ((t ^ (t>>>14))>>>0) / 4294967296;
  };
}
function randInt(rnd, lo, hi){
  return lo + Math.floor(rnd()*(hi-lo+1));
}
function polygonCentroid(poly){
  let area=0, cx=0, cy=0;
  for(let i=0;i<poly.length;i++){
    const [x1,y1]=poly[i], [x2,y2]=poly[(i+1)%poly.length];
    const cross=x1*y2-x2*y1;
    area+=cross;
    cx+=(x1+x2)*cross;
    cy+=(y1+y2)*cross;
  }
  area*=0.5;
  if(Math.abs(area)<1e-9){
    const n=poly.length||1;
    return [
      poly.reduce((s,p)=>s+p[0],0)/n,
      poly.reduce((s,p)=>s+p[1],0)/n
    ];
  }
  return [cx/(6*area), cy/(6*area)];
}
function generatePracticeMap(seed){
  // 대회 설명의 전장 생성 규칙을 JS로 옮긴 연습용 구현.
  // L=10^4, D=100, A=24, N=2N'+1, K=2K'+1.
  const L=10000, D=100, A=24;
  const rnd=randFromSeed(seed);
  const Np=randInt(rnd,25,54);
  const N=2*Np+1;
  const kLo=Math.ceil((0.15*N-1)/2);
  const kHi=Math.floor((0.2*N-1)/2);
  const Kp=randInt(rnd,kLo,kHi);

  const pts=[[0,0]];
  while(pts.length<N){
    let picked=null;
    for(let tries=0;tries<20000;tries++){
      const x=randInt(rnd,-L,L);
      const y=randInt(rnd,-L,L);
      if(x*x+y*y>L*L) continue;
      let ok=true;
      for(const [px,py] of pts){
        if(x===px){ ok=false; break; }
        if(Math.hypot(x-px,y-py)<D){ ok=false; break; }
      }
      if(ok){ picked=[x,y]; break; }
    }
    if(!picked) throw new Error('전장 점 생성 실패');
    pts.push(picked,[-picked[0],-picked[1]]);
  }
  pts.sort((a,b)=>a[0]-b[0] || a[1]-b[1]);

  const q=[];
  for(let i=0;i<A;i++){
    const ang=(i/A)*Math.PI*2;
    q.push([Math.cos(ang)*1.5*L, Math.sin(ang)*1.5*L]);
  }
  const first=d3.Delaunay.from(pts.concat(q)).voronoi([-2*L,-2*L,2*L,2*L]);
  const centered=[];
  for(let i=0;i<N;i++){
    const cell=first.cellPolygon(i);
    centered.push(cell ? polygonCentroid(cell.slice(0,-1)) : pts[i]);
  }
  const delaunay=d3.Delaunay.from(centered.concat(q));
  const adj=[];
  for(let i=0;i<N;i++){
    adj.push(Array.from(delaunay.neighbors(i)).filter(v=>v<N).sort((a,b)=>a-b));
  }

  const strongSet=new Set([Np]);
  let guard=0;
  while(strongSet.size<2*Kp+1 && guard++<200000){
    const t=randInt(rnd,0,N-1);
    const mirror=N-1-t;
    if(strongSet.has(t) || strongSet.has(mirror)) continue;
    let bad=false;
    for(const r of strongSet){
      if(adj[t].includes(r) || adj[mirror].includes(r)){ bad=true; break; }
    }
    if(bad) continue;
    if(adj[t].includes(mirror)) continue;
    strongSet.add(t);
    strongSet.add(mirror);
  }
  if(strongSet.size<2*Kp+1) return generatePracticeMap((seed+1)>>>0);
  const strongholds=Array.from(strongSet).sort((a,b)=>a-b);
  return {
    N, K:strongholds.length,
    x:centered.map(p=>Math.round(p[0])), y:centered.map(p=>Math.round(p[1])),
    strongholds, adj
  };
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
function shortestNext(map, from, to){
  if(from===to) return from;
  const q=[from], prev=Array(map.N).fill(-1);
  prev[from]=from;
  for(let qi=0;qi<q.length;qi++){
    const cur=q[qi];
    for(const nx of map.adj[cur]){
      if(prev[nx]!==-1) continue;
      prev[nx]=cur;
      if(nx===to){
        let step=to;
        while(prev[step]!==from) step=prev[step];
        return step;
      }
      q.push(nx);
    }
  }
  return from;
}
function buildingMaxHp(b){
  return b.type==='HQ' ? HQ_LEVELS[b.level][2] : BASE_LEVELS[b.level][2];
}
function buildingTurret(b){
  return b.type==='HQ' ? HQ_LEVELS[b.level][3] : BASE_LEVELS[b.level][2];
}
function buildingWorkCap(b){
  return b.type==='HQ' ? HQ_LEVELS[b.level][5] : BASE_LEVELS[b.level][3];
}
function localSideCommands(side){
  const snap=STATE.snaps[STATE.snaps.length-1];
  const cmds={upgrades:[], moves:[], train:0};
  if(side===HP.side){
    cmds.upgrades=HP.queueUpgrades.slice();
    cmds.moves=HP.queueMoves.map(m=>({...m}));
    cmds.train=Math.max(0, Math.floor(+document.getElementById('ppTrain').value||0));
    return cmds;
  }
  const hq=side==='A'?0:STATE.map.N-1;
  const enemyHq=side==='A'?STATE.map.N-1:0;
  const myWar=Object.entries(snap.warriors).filter(([id,w])=>w.side===side);
  for(const [id,w] of myWar.slice(0,3)){
    const nx=shortestNext(STATE.map, w.region, enemyHq);
    if(nx!==w.region) cmds.moves.push({id, region:nx});
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
function applyLocalUpgrade(snap, side, region, events){
  const SH=new Set(STATE.map.strongholds);
  const isHQ=(region===0 || region===STATE.map.N-1);
  const hasWorker=Object.values(snap.warriors).some(w=>w.side===side && w.region===region);
  if(!hasWorker || (!SH.has(region) && !isHQ)) return;
  const b=snap.buildings[region];
  if(!b){
    if(snap.gold[side] < BASE_LEVELS[1][0] || isHQ) return;
    snap.gold[side]-=BASE_LEVELS[1][0];
    snap.buildings[region]={side,type:'BASE',level:1,hp:BASE_LEVELS[1][2]};
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
    events.push({kind:'up', text:`${side} ${b.type} 수리 @${region}`});
    return;
  }
  const cost=b.type==='HQ'?HQ_LEVELS[b.level+1][0]:BASE_LEVELS[b.level+1][0];
  if(snap.gold[side] < cost) return;
  snap.gold[side]-=cost;
  b.level++;
  b.hp=buildingMaxHp(b);
  events.push({kind:'up', text:`${side} ${b.type} → Lv${b.level} @${region}`});
}
function applyLocalCommands(snap, side, cmds, events){
  for(const region of cmds.upgrades) applyLocalUpgrade(snap, side, region, events);
  const hq=side==='A'?0:STATE.map.N-1;
  const hqB=snap.buildings[hq];
  const cap=hqB ? HQ_LEVELS[hqB.level][4] : 0;
  const train=Math.max(0, Math.min(cap, cmds.train|0));
  for(let i=0;i<train;i++){
    if(snap.gold[side] < TRAIN_COST) break;
    snap.gold[side]-=TRAIN_COST;
    const id=side+(HP.localNextId[side]++);
    const hp=hqB ? HQ_LEVELS[hqB.level][1] : HQ_LEVELS[1][1];
    snap.warriors[id]={side, region:hq, hp, state:'S', target:null};
    events.push({kind:'train', text:`${side} 전사 ${id} 훈련`});
  }
  for(const mv of cmds.moves){
    const w=snap.warriors[mv.id];
    if(!w || w.side!==side) continue;
    if(!STATE.map.adj[w.region].includes(mv.region)) continue;
    const b=snap.buildings[mv.region];
    if(!(b && b.side===side)) snap.gold[side]=Math.max(0, snap.gold[side]-MOVE_COST);
    w.region=mv.region;
    events.push({kind:'move', text:`${mv.id} → ${mv.region}`});
  }
}
function resolveLocalTurn(snap, events){
  const byRegion={};
  for(const [id,w] of Object.entries(snap.warriors)){
    (byRegion[w.region]=byRegion[w.region]||[]).push([id,w]);
  }
  for(const [regionText, ws] of Object.entries(byRegion)){
    const region=+regionText;
    const a=ws.filter(([id,w])=>w.side==='A');
    const b=ws.filter(([id,w])=>w.side==='B');
    if(a.length && b.length){
      for(const [id,w] of a){ w.hp-=b.length; events.push({kind:'dmg', text:`${id} −${b.length} (전투)`}); }
      for(const [id,w] of b){ w.hp-=a.length; events.push({kind:'dmg', text:`${id} −${a.length} (전투)`}); }
    }
    const building=snap.buildings[region];
    if(building){
      const enemies=ws.filter(([id,w])=>w.side!==building.side);
      const friends=ws.filter(([id,w])=>w.side===building.side);
      const turret=building.type==='HQ'?HQ_LEVELS[building.level][3]:BASE_LEVELS[building.level][2];
      for(const [id,w] of enemies){
        w.hp-=turret;
        events.push({kind:'dmg', text:`${id} −${turret} (포탑)`});
      }
      if(enemies.length && !friends.length){
        building.hp-=enemies.length;
        events.push({kind:'siege', text:`${building.side} 건물 @${region} −${enemies.length}`});
      }
    }
  }
  for(const id of Object.keys(snap.warriors)){
    if(snap.warriors[id].hp<=0) delete snap.warriors[id];
  }
  for(const r of Object.keys(snap.buildings)){
    if(snap.buildings[r].hp<=0) delete snap.buildings[r];
  }
  for(const [regionText,b] of Object.entries(snap.buildings)){
    const region=+regionText;
    const workers=Object.values(snap.warriors).filter(w=>w.side===b.side && w.region===region).length;
    snap.gold[b.side]+=WORK_INCOME*Math.min(workers, buildingWorkCap(b));
  }
  for(const side of ['A','B']){
    const alive=Object.values(snap.warriors).filter(w=>w.side===side).length;
    snap.gold[side]=Math.max(0, snap.gold[side]-UPKEEP*alive);
  }
}
function applyLocalTurn(turn){
  const base=cloneState(STATE.snaps[STATE.snaps.length-1]);
  const events=[];
  const human=localSideCommands(HP.side);
  const ai=localSideCommands(HP.side==='A'?'B':'A');
  applyLocalCommands(base, 'A', HP.side==='A'?human:ai, events);
  applyLocalCommands(base, 'B', HP.side==='B'?human:ai, events);
  resolveLocalTurn(base, events);
  base.day=turn;
  base.events=events;
  base.time={lMs:0,lTok:5,rMs:0,rTok:5};
  STATE.snaps.push(base);
  setBoardReady();
  goTo(STATE.snaps.length-1);
  const leftAlive=!!base.buildings[0];
  const rightAlive=!!base.buildings[STATE.map.N-1];
  if(!leftAlive || !rightAlive || turn>=200){
    STATE.result=!leftAlive ? 'RESULT RIGHT_WIN HQ_DESTROYED' :
      !rightAlive ? 'RESULT LEFT_WIN HQ_DESTROYED' : 'RESULT DRAW TURN_LIMIT';
    HP.finished=true; HP.waitingTurn=null;
    document.getElementById('playPanel').classList.remove('show');
    document.getElementById('liveBadge').classList.remove('show');
    render();
    return;
  }
  HP.waitingTurn=turn+1;
  onMyTurn(HP.waitingTurn, null);
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

function updateTrainCap(){
  const snap=STATE.snaps[STATE.idx];
  const hqReg = HP.side==='A'?0:STATE.map.N-1;
  const hq=snap.buildings[hqReg];
  const cap = hq ? HQ_LEVELS[hq.level][4] : 0;
  document.getElementById('ppTrainCap').textContent=`(가능 ${cap})`;
  document.getElementById('ppTrain').max=cap;
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
      if(HP.local && !STATE.map.adj[cur.region].includes(region)){
        document.getElementById('ppHint').textContent='단독 연습 모드에서는 인접한 구역으로만 이동할 수 있습니다.';
        return;
      }
      addMove(HP.selWarrior, region);
      HP.selWarrior=null; render(); return;
    }
    if(cur && region===cur.region){ // 같은 칸 → 선택 해제
      HP.selWarrior=null;
      document.getElementById('ppHint').textContent='선택을 해제했습니다.';
      render(); return;
    }
  }
  if(canUpgradeHere){
    addUpgrade(region);
    render();
    return;
  }
  if(myHere.length){
    HP.selWarrior=myHere.map(([id])=>id)
      .sort((a,b)=>parseInt(a.slice(1))-parseInt(b.slice(1)))[0];
    document.getElementById('ppHint').textContent=
      `${HP.selWarrior} 선택됨 — 목적지 구역을 클릭하세요. (현재 칸을 다시 누르면 해제)`;
    render(); return;
  }
  document.getElementById('ppHint').textContent='건설은 거점·본부에서만 가능합니다.';
}

function onWarriorClick(id, region){
  STATE.selectedRegion=region;
  if(!HP.active || HP.waitingTurn===null){ onBoardClick(region); return; }
  const snap=STATE.snaps[STATE.idx];
  const w=snap.warriors[id];
  if(!w || w.side!==HP.side){ onBoardClick(region); return; }
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
  HP.queueMoves=HP.queueMoves.filter(m=>m.id!==id);
  HP.queueMoves.push({id, region});
  const snap=commandSnap();
  const cost=moveCostForRegion(region, snap);
  document.getElementById('ppHint').textContent=`${id} → ${region} 이동 추가됨. 비용 ${cost}G.`;
  renderQueue();
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

function humanQueuedCost(){
  const snap=commandSnap();
  if(!snap) return 0;
  const upgrades=HP.queueUpgrades.reduce((sum, region)=>sum+upgradeCostForRegion(region, snap), 0);
  const moves=HP.queueMoves.reduce((sum, move)=>sum+moveCostForRegion(move.region, snap), 0);
  const trainInput=document.getElementById('ppTrain');
  const train=trainInput ? Math.max(0, Math.floor(+trainInput.value||0))*TRAIN_COST : 0;
  return upgrades+moves+train;
}

function renderQueue(){
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
        <span class="qmain"><b>TRAIN ${tn}</b><em>전사 훈련 · ${tn*TRAIN_COST}G</em></span>
        <span style="color:var(--ink-dim)">훈련</span>
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
  const tn=Math.max(0, Math.floor(+document.getElementById('ppTrain').value||0));
  if(tn>0) lines.push(`TRAIN ${tn}`);
  const turn=HP.waitingTurn;
  if(HP.local){
    applyLocalTurn(turn);
    HP.queueMoves=[]; HP.queueUpgrades=[]; HP.selWarrior=null;
    document.getElementById('ppTrain').value=0;
    renderQueue();
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
  let inCode=false, listOpen=false;
  const closeList=()=>{ if(listOpen){ out.push('</ul>'); listOpen=false; } };
  for(const line of lines){
    if(line.trim().startsWith('```')){
      if(inCode){ out.push('</code></pre>'); inCode=false; }
      else { closeList(); out.push('<pre><code>'); inCode=true; }
      continue;
    }
    if(inCode){
      out.push(escapeHtml(line)+'\n');
      continue;
    }
    const h=line.match(/^(#{1,3})\s+(.+)$/);
    if(h){
      closeList();
      out.push(`<h${h[1].length}>${inlineMarkdown(h[2])}</h${h[1].length}>`);
      continue;
    }
    const li=line.match(/^\s*[-*]\s+(.+)$/);
    if(li){
      if(!listOpen){ out.push('<ul>'); listOpen=true; }
      out.push(`<li>${inlineMarkdown(li[1])}</li>`);
      continue;
    }
    if(!line.trim()){
      closeList();
      continue;
    }
    closeList();
    out.push(`<p>${inlineMarkdown(line)}</p>`);
  }
  closeList();
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
  body.textContent='규칙 문서를 불러오는 중입니다.';
  try{
    const r=await fetch(staticUrl(RULES_FILE),{cache:'no-store'});
    if(!r.ok) throw new Error('rules missing');
    body.innerHTML=markdownToHtml(await r.text());
  }catch(_){
    body.innerHTML='<p>게임 규칙 파일을 찾을 수 없습니다. 뷰어 폴더에 <code>'+escapeHtml(RULES_FILE)+'</code> 파일을 넣어 주세요.</p>';
  }
}

function closeRules(){
  document.getElementById('rulesModal').classList.remove('show');
}

/* ---------- 이벤트 바인딩 ---------- */
function bind(){
  preloadAssets();
  // 서버 위에서 열렸으면 새 게임 패널 노출
  checkServer().then(ok=>{
    if(ok || isFileMode()) document.getElementById('newgame').style.display='flex';
  });
  // 시드/랜덤 토글
  document.querySelectorAll('input[name="mapmode"]').forEach(r=>{
    r.addEventListener('change', ()=>syncSeedMode(r.value==='random'));
    r.addEventListener('click', ()=>{ if(r.value==='random') syncSeedMode(true); });
  });
  syncSeedMode(true);
  document.getElementById('newBtn').addEventListener('click', newGame);
  document.getElementById('humanBtn').addEventListener('click', humanNewGame);
  document.getElementById('rulesBtn').addEventListener('click', openRules);
  document.getElementById('rulesClose').addEventListener('click', closeRules);
  document.getElementById('rulesModal').addEventListener('click', e=>{ if(e.target.id==='rulesModal') closeRules(); });
  document.getElementById('ppSubmit').addEventListener('click', submitHuman);
  document.getElementById('ppClear').addEventListener('click', clearQueue);
  document.getElementById('ppTrain').addEventListener('input', renderQueue);
  document.getElementById('seedInput').addEventListener('keydown', e=>{ if(e.key==='Enter') newGame(); });

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
