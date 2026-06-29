const SVG_NS = "http://www.w3.org/2000/svg";

const SIDES = ["A", "B"];
const MAX_DAY = 200;
const TRAIN_COST = 120;
const MOVE_COST = 10;
const FOOD_COST = 2;
const LABOR_VALUE = 15;

const SIDE_LABEL = {
  A: "A 진영",
  B: "B 진영",
};

const HQ_LEVELS = {
  1: { maxHp: 10, turret: 1, train: 1, labor: 1, warriorHp: 4, upgradeCost: 600 },
  2: { maxHp: 15, turret: 2, train: 1, labor: 2, warriorHp: 5, upgradeCost: 1200 },
  3: { maxHp: 20, turret: 2, train: 2, labor: 3, warriorHp: 6, upgradeCost: 2400 },
  4: { maxHp: 25, turret: 3, train: 2, labor: 4, warriorHp: 7, upgradeCost: 3600 },
  5: { maxHp: 30, turret: 3, train: 3, labor: 5, warriorHp: 8, repairCost: 1000 },
};

const BASE_LEVELS = {
  1: { maxHp: 6, turret: 1, labor: 1, upgradeCost: 600 },
  2: { maxHp: 12, turret: 1, labor: 2, upgradeCost: 1000 },
  3: { maxHp: 18, turret: 2, labor: 3, repairCost: 500 },
};

const MIN_MAP_SIZE = 51;
const MAX_MAP_SIZE = 109;
const BOARD_CENTER = { x: 500, y: 280 };
const BOARD_RADIUS = 246;
const WORLD_RADIUS = 10000;
const CIRCLE_SEGMENTS = 144;
const EPSILON = 0.00001;

let state;

document.addEventListener("DOMContentLoaded", () => {
  document.getElementById("sideAButton").addEventListener("click", () => setActiveSide("A"));
  document.getElementById("sideBButton").addEventListener("click", () => setActiveSide("B"));
  document.getElementById("resolveButton").addEventListener("click", resolveDay);
  document.getElementById("newGameButton").addEventListener("click", () => {
    const n = Number(document.getElementById("mapSizeInput").value);
    startNewGame(n);
  });

  document.getElementById("commandPanel").addEventListener("click", handleCommandPanelClick);
  document.getElementById("commandPanel").addEventListener("change", handleCommandPanelChange);
  document.getElementById("queuePanel").addEventListener("click", handleQueuePanelClick);

  startNewGame(51);
});

function startNewGame(n) {
  n = normalizeMapSize(n);
  document.getElementById("mapSizeInput").value = n;
  const generated = generateMap(n, `${Date.now()}-${Math.random()}`);
  state = {
    day: 1,
    activeSide: "A",
    selectedZoneId: 0,
    selectedWarriorId: null,
    finished: false,
    winner: null,
    finishText: "",
    zones: generated.zones,
    warriors: [],
    sides: {
      A: { gold: 500, nextId: 1, hqZone: 0, hqDestroyed: false, command: emptyCommand() },
      B: { gold: 500, nextId: 1, hqZone: n - 1, hqDestroyed: false, command: emptyCommand() },
    },
    log: [],
  };

  const leftHq = state.zones[0];
  leftHq.building = { owner: "A", type: "hq", level: 1, hp: HQ_LEVELS[1].maxHp };
  const rightHq = state.zones[n - 1];
  rightHq.building = { owner: "B", type: "hq", level: 1, hp: HQ_LEVELS[1].maxHp };

  for (let i = 0; i < 3; i += 1) {
    createWarrior("A", 0, HQ_LEVELS[1].warriorHp);
    createWarrior("B", n - 1, HQ_LEVELS[1].warriorHp);
  }

  addLog(`새 전장 생성: 구역 ${n}개, 거점 ${generated.outpostCount}개.`);
  render();
}

function emptyCommand() {
  return { builds: [], moves: [], train: 0 };
}

function createWarrior(owner, zoneId, hp) {
  const id = `${owner}${state.sides[owner].nextId}`;
  state.sides[owner].nextId += 1;
  const warrior = {
    id,
    owner,
    zone: zoneId,
    hp,
    maxHp: hp,
    dest: null,
    alive: true,
  };
  state.warriors.push(warrior);
  return warrior;
}

function normalizeMapSize(value) {
  let n = Math.round(Number(value));
  if (!Number.isFinite(n)) n = MIN_MAP_SIZE;
  n = Math.max(MIN_MAP_SIZE, Math.min(MAX_MAP_SIZE, n));
  if (n % 2 === 0) n += n < MAX_MAP_SIZE ? 1 : -1;
  return n;
}

function generateMap(n, seedText) {
  n = normalizeMapSize(n);
  const rng = createRng(seedText);
  const points = generateSymmetricPoints(n, rng);
  const cells = computeVoronoiCells(points);
  const neighbors = neighborsFromCells(cells);
  ensureConnectedNeighbors(points, neighbors);
  const outposts = chooseOutposts(n, neighbors, rng);

  const zones = points.map((point, id) => ({
    id,
    x: point.x,
    y: point.y,
    worldX: point.worldX,
    worldY: point.worldY,
    polygon: cells[id],
    neighbors: [...neighbors[id]].sort((a, b) => a - b),
    outpost: outposts.has(id),
    building: null,
  }));

  return { zones, outpostCount: outposts.size };
}

function generateSymmetricPoints(n, rng) {
  const half = Math.floor(n / 2);
  const pairs = [];
  const hqY = Math.round(1200 + rng() * 1500);
  pairs.push({ worldX: 9500, worldY: hqY });

  const minGap = Math.max(880, 1360 - (n - MIN_MAP_SIZE) * 8);
  let guard = 0;
  while (pairs.length < half && guard < 30000) {
    guard += 1;
    const radius = Math.sqrt(rng()) * 8800;
    const angle = (rng() - 0.5) * Math.PI;
    const worldX = Math.round(Math.cos(angle) * radius);
    const worldY = Math.round(Math.sin(angle) * radius);

    if (worldX < 650 || worldX > 8750) continue;
    if (tooCloseToExistingPair({ worldX, worldY }, pairs, minGap)) continue;
    pairs.push({ worldX, worldY });
  }

  while (pairs.length < half) {
    const t = pairs.length / half;
    const ring = 2500 + (pairs.length % 4) * 1500;
    const angle = -Math.PI / 2 + t * Math.PI;
    pairs.push({
      worldX: Math.round(Math.max(800, Math.cos(angle) * ring)),
      worldY: Math.round(Math.sin(angle) * ring),
    });
  }

  const raw = [{ worldX: 0, worldY: 0 }];
  for (const point of pairs) {
    raw.push(point);
    raw.push({ worldX: -point.worldX, worldY: -point.worldY });
  }
  raw.sort((a, b) => a.worldX - b.worldX || a.worldY - b.worldY);

  return raw.map((point) => ({
    ...point,
    x: BOARD_CENTER.x + (point.worldX / WORLD_RADIUS) * BOARD_RADIUS,
    y: BOARD_CENTER.y - (point.worldY / WORLD_RADIUS) * BOARD_RADIUS,
  }));
}

function tooCloseToExistingPair(candidate, pairs, minGap) {
  const candidates = [
    { worldX: candidate.worldX, worldY: candidate.worldY },
    { worldX: -candidate.worldX, worldY: -candidate.worldY },
  ];
  const existing = [{ worldX: 0, worldY: 0 }];
  for (const point of pairs) {
    existing.push(point, { worldX: -point.worldX, worldY: -point.worldY });
  }

  return candidates.some((candidatePoint) =>
    existing.some((point) => {
      const dx = candidatePoint.worldX - point.worldX;
      const dy = candidatePoint.worldY - point.worldY;
      return Math.sqrt(dx * dx + dy * dy) < minGap;
    }),
  );
}

function computeVoronoiCells(points) {
  const boundary = circlePolygon();
  return points.map((point, index) => {
    let cell = boundary.map((item) => ({ ...item }));
    for (let other = 0; other < points.length; other += 1) {
      if (other === index) continue;
      cell = clipToNearestHalfPlane(cell, point, points[other]);
      if (cell.length === 0) break;
    }
    return cell;
  });
}

function circlePolygon() {
  const polygon = [];
  for (let i = 0; i < CIRCLE_SEGMENTS; i += 1) {
    const angle = (Math.PI * 2 * i) / CIRCLE_SEGMENTS;
    polygon.push({
      x: BOARD_CENTER.x + Math.cos(angle) * BOARD_RADIUS,
      y: BOARD_CENTER.y + Math.sin(angle) * BOARD_RADIUS,
    });
  }
  return polygon;
}

function clipToNearestHalfPlane(polygon, point, other) {
  const a = other.x - point.x;
  const b = other.y - point.y;
  const c = (other.x * other.x + other.y * other.y - point.x * point.x - point.y * point.y) / 2;
  const clipped = [];

  for (let i = 0; i < polygon.length; i += 1) {
    const current = polygon[i];
    const next = polygon[(i + 1) % polygon.length];
    const currentValue = a * current.x + b * current.y - c;
    const nextValue = a * next.x + b * next.y - c;
    const currentInside = currentValue <= EPSILON;
    const nextInside = nextValue <= EPSILON;

    if (currentInside && nextInside) {
      clipped.push(next);
    } else if (currentInside && !nextInside) {
      clipped.push(intersectLineSegment(current, next, currentValue, nextValue));
    } else if (!currentInside && nextInside) {
      clipped.push(intersectLineSegment(current, next, currentValue, nextValue));
      clipped.push(next);
    }
  }

  return clipped;
}

function intersectLineSegment(start, end, startValue, endValue) {
  const t = startValue / (startValue - endValue);
  return {
    x: start.x + (end.x - start.x) * t,
    y: start.y + (end.y - start.y) * t,
  };
}

function neighborsFromCells(cells) {
  const neighbors = Array.from({ length: cells.length }, () => new Set());
  const edgeOwners = new Map();

  cells.forEach((cell, id) => {
    for (let i = 0; i < cell.length; i += 1) {
      const a = cell[i];
      const b = cell[(i + 1) % cell.length];
      if (distance(a, b) < 1.5) continue;
      const key = cellEdgeKey(a, b);
      if (!edgeOwners.has(key)) edgeOwners.set(key, []);
      edgeOwners.get(key).push(id);
    }
  });

  for (const owners of edgeOwners.values()) {
    if (owners.length < 2) continue;
    for (let i = 0; i < owners.length; i += 1) {
      for (let j = i + 1; j < owners.length; j += 1) {
        const a = owners[i];
        const b = owners[j];
        neighbors[a].add(b);
        neighbors[b].add(a);
      }
    }
  }

  return neighbors;
}

function cellEdgeKey(a, b) {
  const pa = roundedPointKey(a);
  const pb = roundedPointKey(b);
  return pa < pb ? `${pa}|${pb}` : `${pb}|${pa}`;
}

function roundedPointKey(point) {
  return `${Math.round(point.x * 10)},${Math.round(point.y * 10)}`;
}

function ensureConnectedNeighbors(points, neighbors) {
  let components = connectedComponents(neighbors);
  while (components.length > 1) {
    const base = components[0];
    let best = null;
    for (let c = 1; c < components.length; c += 1) {
      for (const a of base) {
        for (const b of components[c]) {
          const d = distance(points[a], points[b]);
          if (!best || d < best.d) best = { a, b, d };
        }
      }
    }
    if (!best) break;
    neighbors[best.a].add(best.b);
    neighbors[best.b].add(best.a);
    components = connectedComponents(neighbors);
  }
}

function connectedComponents(neighbors) {
  const seen = new Set();
  const components = [];

  for (let start = 0; start < neighbors.length; start += 1) {
    if (seen.has(start)) continue;
    const stack = [start];
    const component = [];
    seen.add(start);

    while (stack.length > 0) {
      const current = stack.pop();
      component.push(current);
      for (const next of neighbors[current]) {
        if (seen.has(next)) continue;
        seen.add(next);
        stack.push(next);
      }
    }

    components.push(component);
  }

  return components;
}

function chooseOutposts(n, neighbors, rng) {
  const minK = nextOdd(Math.ceil(n * 0.15));
  const maxK = previousOdd(Math.floor(n * 0.2));
  const oddChoices = [];
  for (let k = minK; k <= maxK; k += 2) oddChoices.push(k);
  const target = oddChoices[Math.floor(rng() * oddChoices.length)] ?? minK;

  const mid = Math.floor(n / 2);
  const outposts = new Set([mid]);
  const pairCount = Math.floor((target - 1) / 2);
  const candidates = [];
  for (let id = 1; id < mid; id += 1) candidates.push(id);
  shuffle(candidates, rng);

  const conflicts = (id) => {
    const mirror = n - 1 - id;
    if (id <= 0 || mirror >= n - 1 || id === mirror) return true;
    if (outposts.has(id) || outposts.has(mirror)) return true;
    if (neighbors[id].has(mirror)) return true;
    for (const outpost of outposts) {
      if (neighbors[id].has(outpost) || neighbors[mirror].has(outpost)) return true;
    }
    return false;
  };

  for (const id of candidates) {
    if ((outposts.size - 1) / 2 >= pairCount) break;
    if (conflicts(id)) continue;
    outposts.add(id);
    outposts.add(n - 1 - id);
  }

  return outposts;
}

function nextOdd(value) {
  return value % 2 === 1 ? value : value + 1;
}

function previousOdd(value) {
  return value % 2 === 1 ? value : value - 1;
}

function shuffle(items, rng) {
  for (let i = items.length - 1; i > 0; i -= 1) {
    const j = Math.floor(rng() * (i + 1));
    [items[i], items[j]] = [items[j], items[i]];
  }
}

function createRng(seedText) {
  let seed = 2166136261;
  for (let i = 0; i < seedText.length; i += 1) {
    seed ^= seedText.charCodeAt(i);
    seed = Math.imul(seed, 16777619);
  }
  return () => {
    seed = Math.imul(seed + 0x6d2b79f5, 1);
    let t = seed;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function render() {
  renderHeader();
  renderScores();
  renderMap();
  renderSelectedZone();
  renderCommandPanel();
  renderQueuePanel();
  renderLog();
}

function renderHeader() {
  const status = document.getElementById("turnStatus");
  status.textContent = state.finished ? state.finishText : `${state.day}일차 명령 단계`;
  document.getElementById("resolveButton").disabled = state.finished;
  document.getElementById("sideAButton").classList.toggle("is-active", state.activeSide === "A");
  document.getElementById("sideBButton").classList.toggle("is-active", state.activeSide === "B");
}

function renderScores() {
  document.getElementById("scoreA").innerHTML = sideScoreHtml("A");
  document.getElementById("scoreB").innerHTML = sideScoreHtml("B");
}

function sideScoreHtml(side) {
  const sideState = state.sides[side];
  const hq = getHqBuilding(side);
  const hqText = hq ? `Lv.${hq.level} ${hq.hp}/${buildingMaxHp(hq)}` : "파괴됨";
  const warriorCount = aliveWarriors(side).length;
  const pending = queuedCost(side);
  return `
    <div class="score-title">
      <span>${SIDE_LABEL[side]}</span>
      <span>${sideState.gold - pending}G</span>
    </div>
    <div class="score-metrics">
      <div>본부 ${hqText}</div>
      <div>전사 ${warriorCount}명 · 예약 ${pending}G</div>
    </div>
  `;
}

function renderMap() {
  const svg = document.getElementById("battlefield");
  svg.setAttribute("viewBox", "0 0 1000 560");
  svg.innerHTML = "";

  svg.appendChild(svgEl("circle", { class: "board-disc", cx: BOARD_CENTER.x, cy: BOARD_CENTER.y, r: BOARD_RADIUS }));

  const cellLayer = svgEl("g", { class: "cell-layer" });
  for (const zone of state.zones) {
    cellLayer.appendChild(renderZoneCell(zone));
  }
  svg.appendChild(cellLayer);
  svg.appendChild(svgEl("circle", { class: "board-outline", cx: BOARD_CENTER.x, cy: BOARD_CENTER.y, r: BOARD_RADIUS }));

  const markerLayer = svgEl("g", { class: "marker-layer" });
  for (const zone of state.zones) {
    markerLayer.appendChild(renderZoneNode(zone));
  }
  svg.appendChild(markerLayer);
}

function renderZoneCell(zone) {
  const building = zone.building;
  const classes = ["zone-cell"];
  if (zone.outpost) classes.push("is-outpost");
  if (building) classes.push(`owner-${building.owner.toLowerCase()}`);
  if (building?.type === "hq") classes.push("is-hq");
  if (zone.id === state.selectedZoneId) classes.push("is-selected");
  if (zone.neighbors.includes(state.selectedZoneId)) classes.push("is-neighbor");

  const group = svgEl("g", {
    class: classes.join(" "),
    tabindex: "0",
    role: "button",
    "aria-label": `${zone.id}번 구역`,
  });
  group.addEventListener("click", () => selectZone(zone.id));
  group.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      selectZone(zone.id);
    }
  });

  const title = svgEl("title");
  title.textContent = zoneTitle(zone);
  group.appendChild(title);
  group.appendChild(svgEl("polygon", { class: "zone-area", points: polygonPoints(zone.polygon) }));

  return group;
}

function renderZoneNode(zone) {
  const building = zone.building;
  const classes = ["zone-node"];
  if (zone.outpost) classes.push("is-outpost");
  if (building) classes.push(`owner-${building.owner.toLowerCase()}`);
  if (building?.type === "hq") classes.push("is-hq");
  if (zone.id === state.selectedZoneId) classes.push("is-selected");

  const group = svgEl("g", {
    class: classes.join(" "),
    transform: `translate(${zone.x} ${zone.y})`,
  });

  if (building?.type === "hq") {
    group.appendChild(svgEl("path", { class: "hq-star", d: starPath(0, 0, 14, 6, 5) }));
    group.appendChild(svgEl("rect", { class: "hq-marker", x: -5, y: -18, width: 10, height: 8, rx: 2 }));
  } else if (building?.type === "base") {
    group.appendChild(svgEl("polygon", { class: "base-marker", points: "-7,-17 7,-17 10,-8 -10,-8" }));
  }

  group.appendChild(svgEl("text", { class: "zone-label", x: 0, y: building ? -28 : 1 }, String(zone.id)));

  const aCount = warriorsAt(zone.id, "A").length;
  const bCount = warriorsAt(zone.id, "B").length;
  if (aCount > 0) group.appendChild(renderBadge("A", aCount, -12, 16));
  if (bCount > 0) group.appendChild(renderBadge("B", bCount, 12, 16));

  return group;
}

function renderBadge(side, count, x, y) {
  const group = svgEl("g", { class: `badge badge-${side.toLowerCase()}`, transform: `translate(${x} ${y})` });
  group.appendChild(svgEl("circle", { r: 8, cx: 0, cy: 0 }));
  group.appendChild(svgEl("text", { x: 0, y: 1 }, String(count)));
  return group;
}

function renderSelectedZone() {
  const zone = selectedZone();
  const building = zone.building;
  const panel = document.getElementById("selectedZonePanel");
  const aUnits = warriorsAt(zone.id, "A");
  const bUnits = warriorsAt(zone.id, "B");

  panel.innerHTML = `
    <div class="section-title">
      <span>${zone.id}번 구역</span>
      <span class="subtle">${zone.outpost ? "거점" : "일반"} · 인접 ${zone.neighbors.length}</span>
    </div>
    <div class="stat-grid">
      <div class="stat">
        <div class="stat-label">건물</div>
        <div class="stat-value">${buildingSummary(building)}</div>
      </div>
      <div class="stat">
        <div class="stat-label">좌표</div>
        <div class="stat-value">${zone.worldX}, ${zone.worldY}</div>
      </div>
    </div>
    <div class="unit-list">
      ${unitRows("A", aUnits)}
      ${unitRows("B", bUnits)}
    </div>
  `;
}

function unitRows(side, units) {
  if (units.length === 0) {
    return `<div class="unit-row"><div class="unit-name">${SIDE_LABEL[side]}</div><div class="unit-meta">전사 없음</div></div>`;
  }
  return units
    .map((unit) => {
      const moving = unit.dest === null ? "" : ` · 이동 ${unit.dest}`;
      return `
        <div class="unit-row">
          <div class="unit-name">${unit.id}</div>
          <div class="unit-meta">HP ${unit.hp}/${unit.maxHp}${moving}</div>
        </div>
      `;
    })
    .join("");
}

function renderCommandPanel() {
  const side = state.activeSide;
  const sideState = state.sides[side];
  const zone = selectedZone();

  if (state.finished) {
    document.getElementById("commandPanel").innerHTML = `<div class="winner-banner">${state.finishText}</div>`;
    return;
  }

  const buildOption = getBuildOption(side, zone.id);
  const available = availableMoveWarriors(side);
  if (!available.some((unit) => unit.id === state.selectedWarriorId)) {
    state.selectedWarriorId = available[0]?.id ?? null;
  }
  const moveWarrior = available.find((unit) => unit.id === state.selectedWarriorId) ?? null;
  const moveOption = getMoveOption(side, moveWarrior, zone.id);
  const trainCap = projectedTrainCap(side);
  const trainNow = sideState.command.train;
  const committedWithoutTrain = queuedCost(side, { excludeTrain: true });
  const maxAffordableTrain = Math.max(0, Math.floor((sideState.gold - committedWithoutTrain) / TRAIN_COST));
  const trainMax = Math.min(trainCap, maxAffordableTrain);

  document.getElementById("commandPanel").innerHTML = `
    <div class="section-title">
      <span>${SIDE_LABEL[side]} 명령</span>
      <span class="subtle">사용 가능 ${sideState.gold - queuedCost(side)}G</span>
    </div>
    <div class="command-stack">
      <button id="queueBuildButton" class="wide-action" type="button" ${buildOption.disabled ? "disabled" : ""} title="${buildOption.reason}">
        ${buildOption.label}
      </button>

      <div class="form-row">
        <select id="warriorSelect" aria-label="이동할 전사">
          ${available.length === 0 ? `<option value="">이동 가능 전사 없음</option>` : ""}
          ${available.map((unit) => `<option value="${unit.id}" ${unit.id === state.selectedWarriorId ? "selected" : ""}>${unit.id} · ${unit.zone}번</option>`).join("")}
        </select>
        <button id="queueMoveButton" type="button" ${moveOption.disabled ? "disabled" : ""} title="${moveOption.reason}">
          ${moveOption.label}
        </button>
      </div>

      <div class="form-row">
        <input id="trainInput" type="number" min="0" max="${trainCap}" value="${trainNow}" aria-label="훈련 수">
        <button id="setTrainButton" type="button" title="최대 ${trainCap}명, 지불 가능 ${trainMax}명">
          훈련 예약
        </button>
      </div>
    </div>
  `;
}

function renderQueuePanel() {
  document.getElementById("queuePanel").innerHTML = `
    <div class="section-title">
      <span>예약 명령</span>
      <span class="subtle">건설 · 이동 · 훈련</span>
    </div>
    ${SIDES.map((side) => queueGroupHtml(side)).join("")}
  `;
}

function queueGroupHtml(side) {
  const command = state.sides[side].command;
  const rows = [];

  command.builds.forEach((item, index) => {
    rows.push(commandRowHtml(side, "build", index, `건설 ${item.zoneId}번`, `${item.label} · ${item.cost}G`));
  });
  command.moves.forEach((item, index) => {
    rows.push(commandRowHtml(side, "move", index, `이동 ${item.warriorId}`, `${item.dest}번 · ${item.cost}G`));
  });
  if (command.train > 0) {
    rows.push(commandRowHtml(side, "train", 0, `훈련 ${command.train}명`, `${command.train * TRAIN_COST}G`));
  }

  return `
    <div class="queue-group">
      <div class="queue-heading">
        <span>${SIDE_LABEL[side]}</span>
        <span>${queuedCost(side)}G</span>
      </div>
      ${rows.length === 0 ? `<div class="empty-state">예약 없음</div>` : rows.join("")}
    </div>
  `;
}

function commandRowHtml(side, type, index, text, meta) {
  return `
    <div class="command-row">
      <div>
        <div class="command-text">${text}</div>
        <div class="command-meta">${meta}</div>
      </div>
      <button class="danger-action" type="button" data-cancel-side="${side}" data-cancel-type="${type}" data-cancel-index="${index}">취소</button>
    </div>
  `;
}

function renderLog() {
  const log = document.getElementById("eventLog");
  log.innerHTML = state.log
    .slice(0, 60)
    .map((line) => `<li>${line}</li>`)
    .join("");
}

function handleCommandPanelClick(event) {
  if (state.finished) return;
  if (event.target.id === "queueBuildButton") queueBuildCommand();
  if (event.target.id === "queueMoveButton") queueMoveCommand();
  if (event.target.id === "setTrainButton") setTrainCommand();
}

function handleCommandPanelChange(event) {
  if (event.target.id === "warriorSelect") {
    state.selectedWarriorId = event.target.value || null;
    render();
  }
}

function handleQueuePanelClick(event) {
  const button = event.target.closest("[data-cancel-side]");
  if (!button) return;
  const side = button.dataset.cancelSide;
  const type = button.dataset.cancelType;
  const index = Number(button.dataset.cancelIndex);
  const command = state.sides[side].command;

  if (type === "build") command.builds.splice(index, 1);
  if (type === "move") command.moves.splice(index, 1);
  if (type === "train") command.train = 0;
  render();
}

function setActiveSide(side) {
  state.activeSide = side;
  render();
}

function selectZone(zoneId) {
  state.selectedZoneId = zoneId;
  render();
}

function queueBuildCommand() {
  const side = state.activeSide;
  const zoneId = state.selectedZoneId;
  const option = getBuildOption(side, zoneId);
  if (option.disabled) return;
  state.sides[side].command.builds.push({
    zoneId,
    cost: option.cost,
    label: option.shortLabel,
  });
  addLog(`${SIDE_LABEL[side]}: ${zoneId}번 ${option.shortLabel} 예약.`);
  render();
}

function queueMoveCommand() {
  const side = state.activeSide;
  const warrior = getWarrior(state.selectedWarriorId);
  const option = getMoveOption(side, warrior, state.selectedZoneId);
  if (option.disabled) return;
  state.sides[side].command.moves.push({
    warriorId: warrior.id,
    dest: state.selectedZoneId,
    cost: option.cost,
  });
  addLog(`${SIDE_LABEL[side]}: ${warrior.id}을 ${state.selectedZoneId}번으로 이동 예약.`);
  render();
}

function setTrainCommand() {
  const side = state.activeSide;
  const input = document.getElementById("trainInput");
  const raw = Number(input.value);
  const wanted = Number.isFinite(raw) ? Math.max(0, Math.floor(raw)) : 0;
  const cap = projectedTrainCap(side);
  const withoutTrain = queuedCost(side, { excludeTrain: true });
  const affordable = Math.max(0, Math.floor((state.sides[side].gold - withoutTrain) / TRAIN_COST));
  const amount = Math.min(wanted, cap, affordable);
  state.sides[side].command.train = amount;
  addLog(`${SIDE_LABEL[side]}: 훈련 ${amount}명 예약.`);
  render();
}

function getBuildOption(side, zoneId) {
  const sideState = state.sides[side];
  const zone = state.zones[zoneId];
  const base = describeBuild(side, zoneId);
  const disabled = (reason, label = "건설 불가") => ({
    disabled: true,
    reason,
    label,
    shortLabel: label,
    cost: 0,
  });

  if (!base) return disabled("건설, 업그레이드 또는 수리할 대상이 없습니다.");
  if (sideState.command.builds.some((item) => item.zoneId === zoneId)) {
    return disabled("같은 구역에는 하루 한 번만 건설 명령을 예약할 수 있습니다.", `${base.shortLabel} · 예약됨`);
  }
  if (!hasFriendlyWarrior(zoneId, side)) {
    return disabled("아군 전사가 있는 구역에서만 건설할 수 있습니다.", `${base.shortLabel} · ${base.cost}G`);
  }
  if (hasEnemyWarrior(zoneId, side)) {
    return disabled("적 전사가 있는 구역에서는 건설할 수 없습니다.", `${base.shortLabel} · ${base.cost}G`);
  }
  if (queuedCost(side) + base.cost > sideState.gold) {
    return disabled("예약 비용이 보유 금화를 초과합니다.", `${base.shortLabel} · ${base.cost}G`);
  }
  if (zone.building && zone.building.owner !== side) {
    return disabled("적 건물은 업그레이드할 수 없습니다.");
  }

  return {
    disabled: false,
    reason: "",
    label: `${base.shortLabel} · ${base.cost}G`,
    shortLabel: base.shortLabel,
    cost: base.cost,
  };
}

function describeBuild(side, zoneId) {
  const zone = state.zones[zoneId];
  const building = zone.building;
  if (!building) {
    if (!zone.outpost) return null;
    return { kind: "build-base", cost: 300, shortLabel: "기지 건설" };
  }
  if (building.owner !== side) return null;
  if (building.type === "hq") {
    const level = HQ_LEVELS[building.level];
    if (building.level < 5) {
      return { kind: "upgrade-hq", cost: level.upgradeCost, shortLabel: `본부 Lv.${building.level + 1}` };
    }
    if (building.hp < level.maxHp) return { kind: "repair-hq", cost: level.repairCost, shortLabel: "본부 수리" };
    return null;
  }
  const level = BASE_LEVELS[building.level];
  if (building.level < 3) {
    return { kind: "upgrade-base", cost: level.upgradeCost, shortLabel: `기지 Lv.${building.level + 1}` };
  }
  if (building.hp < level.maxHp) return { kind: "repair-base", cost: level.repairCost, shortLabel: "기지 수리" };
  return null;
}

function getMoveOption(side, warrior, dest) {
  const disabled = (reason, label = "이동 예약") => ({ disabled: true, reason, label, cost: 0 });
  if (!warrior) return disabled("이동 가능한 전사가 없습니다.");
  if (!warrior.alive || warrior.owner !== side) return disabled("아군 전사만 이동시킬 수 있습니다.");
  if (warrior.dest !== null) return disabled("이미 이동 중인 전사입니다.");
  if (state.sides[side].command.moves.some((item) => item.warriorId === warrior.id)) {
    return disabled("이미 오늘 이동 명령을 예약한 전사입니다.");
  }
  if (warrior.zone === dest) return disabled("현재 위치와 목적지가 같습니다.");
  const cost = hasAlliedBuilding(dest, side) ? 0 : MOVE_COST;
  if (queuedCost(side) + cost > state.sides[side].gold) {
    return disabled("예약 비용이 보유 금화를 초과합니다.", `이동 · ${cost}G`);
  }
  return { disabled: false, reason: "", label: `이동 · ${cost}G`, cost };
}

function resolveDay() {
  if (state.finished) return;
  const turn = state.day;
  const summary = createSummary();

  applyBuildStage(summary);
  applyMoveStage(summary);
  applyTrainStage(summary);
  applyCombatStage(summary);

  const destructionFinished = evaluateDestroyedHqs();
  if (!destructionFinished) {
    applyLaborStage(summary);
    applySupplyStage(summary);
    if (turn >= MAX_DAY) evaluateFinalScore();
  }

  addTurnSummary(turn, summary);
  for (const side of SIDES) state.sides[side].command = emptyCommand();

  if (!state.finished) state.day += 1;
  render();
}

function createSummary() {
  return {
    builds: 0,
    moves: 0,
    trained: 0,
    warriorDamage: 0,
    siegeDamage: 0,
    retreats: 0,
    destroyedBuildings: 0,
    labor: { A: 0, B: 0 },
    hunger: 0,
    skipped: 0,
  };
}

function applyBuildStage(summary) {
  for (const side of SIDES) {
    for (const item of state.sides[side].command.builds) {
      const action = describeBuild(side, item.zoneId);
      if (!action || !hasFriendlyWarrior(item.zoneId, side) || hasEnemyWarrior(item.zoneId, side)) {
        summary.skipped += 1;
        continue;
      }
      if (state.sides[side].gold < action.cost) {
        summary.skipped += 1;
        continue;
      }
      state.sides[side].gold -= action.cost;
      applyBuildAction(side, item.zoneId, action);
      summary.builds += 1;
    }
  }
}

function applyBuildAction(side, zoneId, action) {
  const zone = state.zones[zoneId];
  if (action.kind === "build-base") {
    zone.building = { owner: side, type: "base", level: 1, hp: BASE_LEVELS[1].maxHp };
    return;
  }

  const building = zone.building;
  if (action.kind === "upgrade-hq") {
    building.level += 1;
    building.hp = HQ_LEVELS[building.level].maxHp;
    return;
  }
  if (action.kind === "repair-hq") {
    building.hp = HQ_LEVELS[building.level].maxHp;
    return;
  }
  if (action.kind === "upgrade-base") {
    building.level += 1;
    building.hp = BASE_LEVELS[building.level].maxHp;
    return;
  }
  if (action.kind === "repair-base") {
    building.hp = BASE_LEVELS[building.level].maxHp;
  }
}

function applyMoveStage(summary) {
  for (const side of SIDES) {
    for (const item of state.sides[side].command.moves) {
      const warrior = getWarrior(item.warriorId);
      if (!warrior || !warrior.alive || warrior.owner !== side || warrior.dest !== null) {
        summary.skipped += 1;
        continue;
      }
      if (state.sides[side].gold < item.cost) {
        summary.skipped += 1;
        continue;
      }
      state.sides[side].gold -= item.cost;
      warrior.dest = item.dest;
    }
  }

  const canMove = new Map();
  for (const warrior of state.warriors.filter((unit) => unit.alive && unit.dest !== null)) {
    canMove.set(warrior.id, !hasEnemyWarrior(warrior.zone, warrior.owner));
  }

  for (const warrior of state.warriors.filter((unit) => unit.alive && unit.dest !== null)) {
    if (!canMove.get(warrior.id)) continue;
    const next = nextStep(warrior.zone, warrior.dest);
    if (next === null) continue;
    warrior.zone = next;
    summary.moves += 1;
    if (warrior.zone === warrior.dest) warrior.dest = null;
  }
}

function applyTrainStage(summary) {
  for (const side of SIDES) {
    const amount = state.sides[side].command.train;
    if (amount <= 0) continue;
    const hq = getHqBuilding(side);
    if (!hq) {
      summary.skipped += 1;
      continue;
    }
    const cap = HQ_LEVELS[hq.level].train;
    const actual = Math.min(amount, cap, Math.floor(state.sides[side].gold / TRAIN_COST));
    if (actual <= 0) {
      summary.skipped += 1;
      continue;
    }
    state.sides[side].gold -= actual * TRAIN_COST;
    for (let i = 0; i < actual; i += 1) {
      createWarrior(side, state.sides[side].hqZone, HQ_LEVELS[hq.level].warriorHp);
    }
    summary.trained += actual;
    if (actual < amount) summary.skipped += amount - actual;
  }
}

function applyCombatStage(summary) {
  for (const zone of state.zones) {
    const building = zone.building;
    if (!building || building.hp <= 0) continue;
    const power = buildingTurret(building);
    for (let i = 0; i < power; i += 1) {
      performAttack(building.owner, zone.id, "TURRET", summary);
    }
  }

  const combatCounts = state.zones.map((zone) => ({
    id: zone.id,
    A: state.warriors.filter((unit) => unit.alive && unit.owner === "A" && unit.zone === zone.id).length,
    B: state.warriors.filter((unit) => unit.alive && unit.owner === "B" && unit.zone === zone.id).length,
  }));

  for (const count of combatCounts) {
    for (let i = 0; i < count.A; i += 1) performAttack("A", count.id, "COMBAT", summary);
    for (let i = 0; i < count.B; i += 1) performAttack("B", count.id, "COMBAT", summary);
  }

  for (const warrior of state.warriors) {
    if (warrior.alive && warrior.hp <= 0) {
      warrior.alive = false;
      warrior.dest = null;
      summary.retreats += 1;
    }
  }

  for (const zone of state.zones) {
    const building = zone.building;
    if (building && building.hp <= 0) {
      if (building.type === "hq") state.sides[building.owner].hqDestroyed = true;
      zone.building = null;
      summary.destroyedBuildings += 1;
    }
  }
}

function performAttack(attacker, zoneId, cause, summary) {
  const defender = otherSide(attacker);
  const target = warriorsAt(zoneId, defender)
    .filter((unit) => unit.hp > 0)
    .sort((a, b) => a.hp - b.hp || warriorNumber(a.id) - warriorNumber(b.id))[0];

  if (target) {
    target.hp -= 1;
    summary.warriorDamage += 1;
    return;
  }

  const building = state.zones[zoneId].building;
  if (building && building.owner === defender && building.hp > 0) {
    building.hp -= 1;
    summary.siegeDamage += 1;
  }
}

function applyLaborStage(summary) {
  for (const zone of state.zones) {
    const building = zone.building;
    if (!building) continue;
    const workers = warriorsAt(zone.id, building.owner).filter((unit) => unit.hp > 0).length;
    const income = LABOR_VALUE * Math.min(workers, buildingLabor(building));
    state.sides[building.owner].gold += income;
    summary.labor[building.owner] += income;
  }
}

function applySupplyStage(summary) {
  for (const side of SIDES) {
    const units = aliveWarriors(side).sort((a, b) => warriorNumber(a.id) - warriorNumber(b.id));
    for (const unit of units) {
      if (state.sides[side].gold >= FOOD_COST) {
        state.sides[side].gold -= FOOD_COST;
      } else {
        unit.hp -= 1;
        summary.hunger += 1;
        if (unit.hp <= 0) {
          unit.alive = false;
          unit.dest = null;
          summary.retreats += 1;
        }
      }
    }
  }
}

function evaluateDestroyedHqs() {
  const aDead = state.sides.A.hqDestroyed;
  const bDead = state.sides.B.hqDestroyed;
  if (!aDead && !bDead) return false;
  if (aDead && bDead) finishGame(null, "양쪽 본부가 동시에 파괴되어 무승부입니다.");
  else if (aDead) finishGame("B", "A 본부가 파괴되어 B 진영 승리입니다.");
  else finishGame("A", "B 본부가 파괴되어 A 진영 승리입니다.");
  return true;
}

function evaluateFinalScore() {
  const aHp = getHqBuilding("A")?.hp ?? 0;
  const bHp = getHqBuilding("B")?.hp ?? 0;
  if (aHp > bHp) finishGame("A", "200일 종료: A 본부 체력이 더 높아 A 진영 승리입니다.");
  else if (bHp > aHp) finishGame("B", "200일 종료: B 본부 체력이 더 높아 B 진영 승리입니다.");
  else finishGame(null, "200일 종료: 본부 체력이 같아 무승부입니다.");
}

function finishGame(winner, text) {
  state.finished = true;
  state.winner = winner;
  state.finishText = text;
  addLog(text);
}

function addTurnSummary(turn, summary) {
  const pieces = [
    `건설 ${summary.builds}`,
    `이동 ${summary.moves}`,
    `훈련 ${summary.trained}`,
    `전사 피해 ${summary.warriorDamage}`,
    `건물 피해 ${summary.siegeDamage}`,
    `퇴각 ${summary.retreats}`,
    `수입 A ${summary.labor.A}G/B ${summary.labor.B}G`,
  ];
  if (summary.hunger > 0) pieces.push(`식량 피해 ${summary.hunger}`);
  if (summary.destroyedBuildings > 0) pieces.push(`건물 파괴 ${summary.destroyedBuildings}`);
  if (summary.skipped > 0) pieces.push(`무효 ${summary.skipped}`);
  addLog(`${turn}일차: ${pieces.join(" · ")}.`);
}

function addLog(text) {
  if (!state) return;
  state.log.unshift(text);
  state.log = state.log.slice(0, 80);
}

function queuedCost(side, options = {}) {
  const command = state.sides[side].command;
  const buildCost = command.builds.reduce((sum, item) => sum + item.cost, 0);
  const moveCost = command.moves.reduce((sum, item) => sum + item.cost, 0);
  const trainCost = options.excludeTrain ? 0 : command.train * TRAIN_COST;
  return buildCost + moveCost + trainCost;
}

function projectedTrainCap(side) {
  const hq = getHqBuilding(side);
  if (!hq) return 0;
  const hqZone = state.sides[side].hqZone;
  const upgrading = state.sides[side].command.builds.some((item) => item.zoneId === hqZone && hq.level < 5);
  const level = Math.min(5, hq.level + (upgrading ? 1 : 0));
  return HQ_LEVELS[level].train;
}

function nextStep(from, to) {
  if (from === to) return null;
  const n = state.zones.length;
  const dist = Array(n).fill(Infinity);
  const visited = Array(n).fill(false);
  dist[to] = 0;

  for (let step = 0; step < n; step += 1) {
    let current = -1;
    for (let i = 0; i < n; i += 1) {
      if (!visited[i] && (current === -1 || dist[i] < dist[current])) current = i;
    }
    if (current === -1 || dist[current] === Infinity) break;
    visited[current] = true;
    for (const neighbor of state.zones[current].neighbors) {
      const cost = edgeCost(current, neighbor);
      if (dist[current] + cost < dist[neighbor]) {
        dist[neighbor] = dist[current] + cost;
      }
    }
  }

  return state.zones[from].neighbors
    .map((neighbor) => ({ id: neighbor, total: edgeCost(from, neighbor) + dist[neighbor] }))
    .sort((a, b) => a.total - b.total || a.id - b.id)[0]?.id ?? null;
}

function edgeCost(a, b) {
  return Math.ceil(distance(state.zones[a], state.zones[b]));
}

function distance(a, b) {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  return Math.sqrt(dx * dx + dy * dy);
}

function selectedZone() {
  return state.zones[state.selectedZoneId];
}

function getWarrior(id) {
  return state.warriors.find((unit) => unit.id === id && unit.alive) ?? null;
}

function aliveWarriors(side) {
  return state.warriors.filter((unit) => unit.alive && unit.owner === side);
}

function warriorsAt(zoneId, side) {
  return state.warriors.filter((unit) => unit.alive && unit.zone === zoneId && (!side || unit.owner === side));
}

function availableMoveWarriors(side) {
  const queued = new Set(state.sides[side].command.moves.map((item) => item.warriorId));
  return aliveWarriors(side)
    .filter((unit) => unit.dest === null && !queued.has(unit.id))
    .sort((a, b) => warriorNumber(a.id) - warriorNumber(b.id));
}

function hasFriendlyWarrior(zoneId, side) {
  return warriorsAt(zoneId, side).some((unit) => unit.hp > 0);
}

function hasEnemyWarrior(zoneId, side) {
  return warriorsAt(zoneId, otherSide(side)).some((unit) => unit.hp > 0);
}

function hasAlliedBuilding(zoneId, side) {
  return state.zones[zoneId].building?.owner === side;
}

function getHqBuilding(side) {
  const hqZone = state.sides[side].hqZone;
  const building = state.zones[hqZone]?.building;
  return building?.owner === side && building.type === "hq" ? building : null;
}

function buildingMaxHp(building) {
  return building.type === "hq" ? HQ_LEVELS[building.level].maxHp : BASE_LEVELS[building.level].maxHp;
}

function buildingTurret(building) {
  return building.type === "hq" ? HQ_LEVELS[building.level].turret : BASE_LEVELS[building.level].turret;
}

function buildingLabor(building) {
  return building.type === "hq" ? HQ_LEVELS[building.level].labor : BASE_LEVELS[building.level].labor;
}

function buildingSummary(building) {
  if (!building) return "없음";
  const type = building.type === "hq" ? "본부" : "기지";
  return `${SIDE_LABEL[building.owner]} ${type} Lv.${building.level} · HP ${building.hp}/${buildingMaxHp(building)}`;
}

function zoneTitle(zone) {
  const building = zone.building ? buildingSummary(zone.building) : zone.outpost ? "건설 가능 거점" : "일반 구역";
  const a = warriorsAt(zone.id, "A").length;
  const b = warriorsAt(zone.id, "B").length;
  return `${zone.id}번 구역 · ${building} · A ${a}명 · B ${b}명`;
}

function otherSide(side) {
  return side === "A" ? "B" : "A";
}

function warriorNumber(id) {
  return Number(id.slice(1));
}

function polygonPoints(points) {
  return points.map((point) => `${point.x.toFixed(1)},${point.y.toFixed(1)}`).join(" ");
}

function starPath(cx, cy, outerRadius, innerRadius, points) {
  const commands = [];
  for (let i = 0; i < points * 2; i += 1) {
    const radius = i % 2 === 0 ? outerRadius : innerRadius;
    const angle = -Math.PI / 2 + (Math.PI * i) / points;
    const x = cx + Math.cos(angle) * radius;
    const y = cy + Math.sin(angle) * radius;
    commands.push(`${i === 0 ? "M" : "L"} ${x.toFixed(1)} ${y.toFixed(1)}`);
  }
  commands.push("Z");
  return commands.join(" ");
}

function svgEl(tag, attrs = {}, text = "") {
  const element = document.createElementNS(SVG_NS, tag);
  for (const [key, value] of Object.entries(attrs)) {
    element.setAttribute(key, value);
  }
  if (text) element.textContent = text;
  return element;
}
