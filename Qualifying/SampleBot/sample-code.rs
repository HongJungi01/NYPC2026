use std::fmt;
use std::io::{self, BufRead, BufWriter, Write};
use std::process;

#[allow(dead_code)]
const MAX_TURN: i32 = 200;         // maximum turn (days)
const START_GOLD: i32 = 500;       // initial gold
const START_WARRIORS: i32 = 3;     // initial warriors
const MOVE_COST: i32 = 10;         // move cost
const TRAIN_COST: i32 = 120;       // train cost
const WORK_INCOME: i32 = 15;       // income per warrior
const UPKEEP_PER_WARRIOR: i32 = 2; // upkeep per warrior
const HQ_MAX_LEVEL: usize = 5;     // HQ max level
const BASE_MAX_LEVEL: usize = 3;   // base max level
const HQ_HEAL_COST: i32 = 1000;    // HQ fix cost
const BASE_HEAL_COST: i32 = 500;   // base fix cost

#[allow(dead_code)]
struct HqLevelEntry {
    upgrade_cost: i32,
    warrior_hp: i32,
    hp: i32,
    turret: i32,
    train_cap: i32,
    work_cap: i32,
}

#[allow(dead_code)]
struct BaseLevelEntry {
    cost: i32,
    hp: i32,
    turret: i32,
    work_cap: i32,
}

const HQ_LEVELS: [HqLevelEntry; HQ_MAX_LEVEL + 1] = [
    HqLevelEntry { upgrade_cost: 0,    warrior_hp: 0, hp: 0,  turret: 0, train_cap: 0, work_cap: 0 },
    HqLevelEntry { upgrade_cost: 0,    warrior_hp: 4, hp: 10, turret: 1, train_cap: 1, work_cap: 1 },
    HqLevelEntry { upgrade_cost: 600,  warrior_hp: 5, hp: 15, turret: 2, train_cap: 1, work_cap: 2 },
    HqLevelEntry { upgrade_cost: 1200, warrior_hp: 6, hp: 20, turret: 2, train_cap: 2, work_cap: 3 },
    HqLevelEntry { upgrade_cost: 2400, warrior_hp: 7, hp: 25, turret: 3, train_cap: 2, work_cap: 4 },
    HqLevelEntry { upgrade_cost: 3600, warrior_hp: 8, hp: 30, turret: 3, train_cap: 3, work_cap: 5 },
];

const BASE_LEVELS: [BaseLevelEntry; BASE_MAX_LEVEL + 1] = [
    BaseLevelEntry { cost: 0,    hp: 0,  turret: 0, work_cap: 0 },
    BaseLevelEntry { cost: 300,  hp: 6,  turret: 1, work_cap: 1 },
    BaseLevelEntry { cost: 600,  hp: 12, turret: 1, work_cap: 2 },
    BaseLevelEntry { cost: 1000, hp: 18, turret: 2, work_cap: 3 },
];

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Side {
    Left,
    Right,
}

impl Side {
    fn opposite(self) -> Side {
        match self {
            Side::Left => Side::Right,
            Side::Right => Side::Left,
        }
    }
    fn to_char(self) -> char {
        match self {
            Side::Left => 'A',
            Side::Right => 'B',
        }
    }
    fn from_char(c: char) -> Side {
        match c {
            'A' => Side::Left,
            _ => Side::Right,
        }
    }
    fn from_word(w: &str) -> Side {
        if w == "LEFT" { Side::Left } else { Side::Right }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum BType {
    HQ,
    Base,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum WState {
    Stationary,
    Moving,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct WarriorId {
    side: Side,
    num: i32,
}

impl WarriorId {
    fn parse(tok: &str) -> WarriorId {
        let mut chars = tok.chars();
        let c = chars.next().expect("empty warrior id");
        let num: i32 = tok[1..].parse().expect("invalid warrior num");
        WarriorId { side: Side::from_char(c), num }
    }
}

impl fmt::Display for WarriorId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}{}", self.side.to_char(), self.num)
    }
}

struct Warrior {
    id: WarriorId,
    region: usize,
    hp: i32,
    state: WState,
    target: usize,
}

struct Building {
    region: usize,
    side: Side,
    btype: BType,
    level: usize,
    hp: i32,
}

impl Building {
    fn current_hp(&self) -> i32 {
        match self.btype {
            BType::HQ => HQ_LEVELS[self.level].hp,
            BType::Base => BASE_LEVELS[self.level].hp,
        }
    }
    fn work_cap(&self) -> i32 {
        match self.btype {
            BType::HQ => HQ_LEVELS[self.level].work_cap,
            BType::Base => BASE_LEVELS[self.level].work_cap,
        }
    }
    fn upgrade_cost(&self) -> i32 {
        match self.btype {
            BType::HQ => HQ_LEVELS[self.level + 1].upgrade_cost,
            BType::Base => BASE_LEVELS[self.level + 1].cost,
        }
    }
    fn max_level(&self) -> usize {
        match self.btype {
            BType::HQ => HQ_MAX_LEVEL,
            BType::Base => BASE_MAX_LEVEL,
        }
    }
    fn apply_upgrade(&mut self) {
        self.level += 1;
        self.hp = self.current_hp();
    }
}

fn make_base(region: usize, side: Side) -> Building {
    Building {
        region,
        side,
        btype: BType::Base,
        level: 1,
        hp: BASE_LEVELS[1].hp,
    }
}

struct GameMap {
    n: usize,
    k: usize,
    x: Vec<i64>,
    y: Vec<i64>,
    strongholds: Vec<usize>,
    adj: Vec<Vec<usize>>,
    my_side: Side,
    my_hq: usize,
    opp_hq: usize,
}

impl GameMap {
    fn hq_of(&self, s: Side) -> usize {
        if s == Side::Left { 0 } else { self.n - 1 }
    }
}

struct GameState {
    gold: i32,
    my_countdown: i32,
    opp_countdown: i32,
    warriors: Vec<Warrior>,
    buildings: Vec<Building>,
}

impl GameState {
    fn find_building_idx(&self, region: usize) -> Option<usize> {
        self.buildings.iter().position(|b| b.region == region)
    }
    fn find_warrior_idx(&self, id: WarriorId) -> Option<usize> {
        self.warriors.iter().position(|w| w.id == id)
    }
}

struct Actions {
    train_n: i32,
    moves: Vec<(WarriorId, usize)>,
    upgrades: Vec<usize>,
}

impl Default for Actions {
    fn default() -> Self {
        Actions { train_n: 0, moves: Vec::new(), upgrades: Vec::new() }
    }
}

fn readln(stdin: &mut impl BufRead) -> String {
    let mut s = String::new();
    match stdin.read_line(&mut s) {
        Ok(0) => process::exit(0),
        Ok(_) => {}
        Err(_) => process::exit(0),
    }
    if s.ends_with('\n') {
        s.pop();
        if s.ends_with('\r') {
            s.pop();
        }
    }
    s
}

fn tokens(s: &str) -> Vec<&str> {
    s.split_whitespace().collect()
}

fn euclid_ceil(m: &GameMap, u: usize, v: usize) -> f64 {
    let dx = (m.x[u] - m.x[v]) as f64;
    let dy = (m.y[u] - m.y[v]) as f64;
    (dx * dx + dy * dy).sqrt().ceil()
}

fn parse_init(stdin: &mut impl BufRead, stdout: &mut impl Write) -> (GameMap, GameState) {
    let mut m = GameMap {
        n: 0, k: 0,
        x: Vec::new(), y: Vec::new(),
        strongholds: Vec::new(), adj: Vec::new(),
        my_side: Side::Left, my_hq: 0, opp_hq: 0,
    };

    {
        let line = readln(stdin);
        let t = tokens(&line);
        assert!(t.len() >= 2 && t[0] == "READY");
        m.my_side = Side::from_word(t[1]);
    }
    {
        let line = readln(stdin);
        let t = tokens(&line);
        m.n = t[0].parse().unwrap();
        m.k = t[1].parse().unwrap();
    }
    {
        let line = readln(stdin); // x_0 x_1 ... x_{N-1}
        m.x = tokens(&line).iter().map(|s| s.parse().unwrap()).collect();
    }
    {
        let line = readln(stdin); // y_0 y_1 ... y_{N-1}
        m.y = tokens(&line).iter().map(|s| s.parse().unwrap()).collect();
    }
    {
        let line = readln(stdin); // K strongholds
        let mut sh: Vec<usize> = tokens(&line).iter().map(|s| s.parse().unwrap()).collect();
        sh.sort_unstable();
        m.strongholds = sh;
    }
    m.adj = vec![Vec::new(); m.n];
    for r in 0..m.n {
        let line = readln(stdin); // deg n_1 n_2 ...
        let t = tokens(&line);
        let deg: usize = t[0].parse().unwrap();
        let mut nb: Vec<usize> = t[1..=deg].iter().map(|s| s.parse().unwrap()).collect();
        nb.sort_unstable();
        m.adj[r] = nb;
    }

    m.my_hq = m.hq_of(m.my_side);
    m.opp_hq = m.hq_of(m.my_side.opposite());

    let mut s = GameState {
        gold: START_GOLD,
        my_countdown: 5,
        opp_countdown: 5,
        warriors: Vec::new(),
        buildings: Vec::new(),
    };

    let opp = m.my_side.opposite();
    for sfx in 1..=START_WARRIORS {
        s.warriors.push(Warrior {
            id: WarriorId { side: m.my_side, num: sfx },
            region: m.my_hq,
            hp: HQ_LEVELS[1].warrior_hp,
            state: WState::Stationary,
            target: 0,
        });
        s.warriors.push(Warrior {
            id: WarriorId { side: opp, num: sfx },
            region: m.opp_hq,
            hp: HQ_LEVELS[1].warrior_hp,
            state: WState::Stationary,
            target: 0,
        });
    }
    s.buildings.push(Building {
        region: m.hq_of(Side::Left),
        side: Side::Left,
        btype: BType::HQ,
        level: 1,
        hp: HQ_LEVELS[1].hp,
    });
    s.buildings.push(Building {
        region: m.hq_of(Side::Right),
        side: Side::Right,
        btype: BType::HQ,
        level: 1,
        hp: HQ_LEVELS[1].hp,
    });

    writeln!(stdout, "OK").unwrap();
    stdout.flush().unwrap();

    (m, s)
}

fn read_turn_start(stdin: &mut impl BufRead) -> Option<i32> {
    let line = readln(stdin);
    if line == "FINISH" {
        return None;
    }
    let t = tokens(&line);
    assert!(!t.is_empty() && t[0] == "START");
    let turn: i32 = t[2].parse().unwrap();
    Some(turn)
}

fn read_turn_result(
    s: &mut GameState,
    m: &GameMap,
    submitted: &Actions,
    stdin: &mut impl BufRead,
) {
    for &region in &submitted.upgrades {
        match s.find_building_idx(region) {
            None => {
                s.gold -= BASE_LEVELS[1].cost;
                s.buildings.push(make_base(region, m.my_side));
            }
            Some(idx) => {
                if s.buildings[idx].level >= s.buildings[idx].max_level() {
                    let cost = if s.buildings[idx].btype == BType::HQ { HQ_HEAL_COST } else { BASE_HEAL_COST };
                    s.gold -= cost;
                    s.buildings[idx].hp = s.buildings[idx].current_hp();
                } else {
                    s.gold -= s.buildings[idx].upgrade_cost();
                    s.buildings[idx].apply_upgrade();
                }
            }
        }
    }

    for &(id, target) in &submitted.moves {
        let cost = match s.find_building_idx(target) {
            Some(idx) if s.buildings[idx].side == m.my_side => 0,
            _ => MOVE_COST,
        };
        s.gold -= cost;
        if let Some(idx) = s.find_warrior_idx(id) {
            s.warriors[idx].state = WState::Moving;
            s.warriors[idx].target = target;
        }
    }

    s.gold -= TRAIN_COST * submitted.train_n;

    {
        let line = readln(stdin);
        if line == "FINISH" {
            process::exit(0);
        }
        let t = tokens(&line);
        assert!(!t.is_empty() && t[0] == "TURN");
    }
    {
        let line = readln(stdin);
        let t = tokens(&line);
        s.my_countdown = t[2].parse().unwrap();
        s.opp_countdown = t[4].parse().unwrap();
    }
    // UPGRADE
    {
        let line = readln(stdin); // "UPGRADE N"
        let t = tokens(&line);
        let n: usize = t[1].parse().unwrap();
        for _ in 0..n {
            let r_line = readln(stdin); // "<A|B> <region>"
            let r = tokens(&r_line);
            let side = Side::from_char(r[0].chars().next().unwrap());
            let region: usize = r[1].parse().unwrap();
            match s.find_building_idx(region) {
                None => {
                    s.buildings.push(make_base(region, side));
                }
                Some(idx) if s.buildings[idx].side != m.my_side => {
                    if s.buildings[idx].level >= s.buildings[idx].max_level() {
                        s.buildings[idx].hp = s.buildings[idx].current_hp();
                    } else {
                        s.buildings[idx].apply_upgrade();
                    }
                }
                _ => {}
            }
        }
    }
    // TRAIN
    {
        let line = readln(stdin); // "TRAIN N"
        let t = tokens(&line);
        let n: usize = t[1].parse().unwrap();
        if n > 0 {
            let ids_line = readln(stdin);
            let ids = tokens(&ids_line);
            for i in 0..n {
                let wid = WarriorId::parse(ids[i]);
                let hq_region = m.hq_of(wid.side);
                let hq_level = s.find_building_idx(hq_region)
                    .map(|idx| s.buildings[idx].level)
                    .unwrap_or(1);
                s.warriors.push(Warrior {
                    id: wid,
                    region: hq_region,
                    hp: HQ_LEVELS[hq_level].warrior_hp,
                    state: WState::Stationary,
                    target: 0,
                });
            }
        }
    }
    // MOVE
    {
        let line = readln(stdin); // "MOVE N"
        let t = tokens(&line);
        let n: usize = t[1].parse().unwrap();
        for _ in 0..n {
            let r_line = readln(stdin);
            let r = tokens(&r_line);
            let wid = WarriorId::parse(r[0]);
            let region: usize = r[1].parse().unwrap();
            if let Some(idx) = s.find_warrior_idx(wid) {
                s.warriors[idx].region = region;
                if wid.side == m.my_side
                    && s.warriors[idx].state == WState::Moving
                    && s.warriors[idx].region == s.warriors[idx].target
                {
                    s.warriors[idx].state = WState::Stationary;
                }
            }
        }
    }
    // DAMAGE
    {
        let line = readln(stdin); // "DAMAGE N"
        let t = tokens(&line);
        let n: usize = t[1].parse().unwrap();
        for _ in 0..n {
            let r_line = readln(stdin);
            let r = tokens(&r_line);
            let wid = WarriorId::parse(r[1]);
            let damage: i32 = r[2].parse().unwrap();
            if let Some(idx) = s.find_warrior_idx(wid) {
                s.warriors[idx].hp -= damage;
            }
        }
        s.warriors.retain(|w| w.hp > 0);
    }
    // SIEGE
    {
        let line = readln(stdin); // "SIEGE N"
        let t = tokens(&line);
        let n: usize = t[1].parse().unwrap();
        for _ in 0..n {
            let r_line = readln(stdin);
            let r = tokens(&r_line);
            let region: usize = r[1].parse().unwrap();
            let damage: i32 = r[2].parse().unwrap();
            if let Some(idx) = s.find_building_idx(region) {
                s.buildings[idx].hp -= damage;
            }
        }
        s.buildings.retain(|b| b.hp > 0);
    }
    let _ = readln(stdin); // "END"

    let mut income = 0;
    for b in &s.buildings {
        if b.side != m.my_side {
            continue;
        }
        let count = s.warriors.iter()
            .filter(|w| w.id.side == m.my_side && w.region == b.region)
            .count() as i32;
        income += WORK_INCOME * count.min(b.work_cap());
    }
    s.gold += income;

    let alive = s.warriors.iter().filter(|w| w.id.side == m.my_side).count() as i32;
    s.gold = 0_i32.max(s.gold - UPKEEP_PER_WARRIOR * alive);
}

#[allow(dead_code)]
struct Paths {
    dist: Vec<Vec<f64>>,
    nxt: Vec<Vec<i32>>,
}

fn calculate_paths(m: &GameMap) -> Paths {
    let n = m.n;
    let mut dist = vec![vec![f64::INFINITY; n]; n];
    let mut nxt = vec![vec![-1i32; n]; n];

    for i in 0..n {
        dist[i][i] = 0.0;
        nxt[i][i] = i as i32;
    }
    for u in 0..n {
        for &v in &m.adj[u] {
            let w = euclid_ceil(m, u, v);
            if w < dist[u][v] {
                dist[u][v] = w;
            }
        }
    }

    for k in 0..n {
        for u in 0..n {
            if dist[u][k] == f64::INFINITY {
                continue;
            }
            for v in 0..n {
                let cand = dist[u][k] + dist[k][v];
                if cand < dist[u][v] {
                    dist[u][v] = cand;
                }
            }
        }
    }

    for u in 0..n {
        for v in 0..n {
            if u == v || dist[u][v] == f64::INFINITY {
                continue;
            }
            let mut best_score = f64::INFINITY;
            for &nb in &m.adj[u] {
                if dist[nb][v] == f64::INFINITY {
                    continue;
                }
                let score = euclid_ceil(m, u, nb) + dist[nb][v];
                if score < best_score {
                    best_score = score;
                    nxt[u][v] = nb as i32;
                }
            }
        }
    }

    Paths { dist, nxt }
}

// Returns the next step on the path from u to v.
// If the path is not reachable, returns -1.
#[allow(dead_code)]
fn next_step(p: &Paths, u: usize, v: usize) -> i32 {
    p.nxt[u][v]
}

// Returns the path from u to v.
// If the path is not reachable, returns an empty vector.
#[allow(dead_code)]
fn path(p: &Paths, mut u: usize, v: usize) -> Vec<usize> {
    if p.nxt[u][v] == -1 {
        return Vec::new();
    }
    let mut out = vec![u];
    while u != v {
        u = p.nxt[u][v] as usize;
        out.push(u);
    }
    out
}

fn emit_actions(a: &Actions, stdout: &mut impl Write) {
    writeln!(stdout, "COMMAND").unwrap();
    for &(id, target) in &a.moves {
        writeln!(stdout, "MOVE {} {}", id, target).unwrap();
    }
    for &r in &a.upgrades {
        writeln!(stdout, "UPGRADE {}", r).unwrap();
    }
    if a.train_n > 0 {
        writeln!(stdout, "TRAIN {}", a.train_n).unwrap();
    }
    writeln!(stdout, "END").unwrap();
    stdout.flush().unwrap();
}

//////////////////////////////////
//// WRITE YOUR STRATEGY HERE ////
//////////////////////////////////
fn decide(s: &GameState, m: &GameMap, _p: &Paths, turn: i32) -> Actions {
    let mut a = Actions::default();
    if turn == 1 {
        for w in &s.warriors {
            if w.id.side != m.my_side {
                continue;
            }
            a.moves.push((w.id, m.opp_hq)); // move this warrior to the enemy HQ
        }
    }
    a
}

fn main() {
    let stdin = io::stdin();
    let mut stdin = stdin.lock();
    let stdout = io::stdout();
    let mut stdout = BufWriter::new(stdout.lock());

    let (m, mut s) = parse_init(&mut stdin, &mut stdout); // initialize the game
    let p = calculate_paths(&m);                          // calculate the shortest paths

    while let Some(turn) = read_turn_start(&mut stdin) {
        let a = decide(&s, &m, &p, turn);
        emit_actions(&a, &mut stdout);
        read_turn_result(&mut s, &m, &a, &mut stdin);
    }
}
