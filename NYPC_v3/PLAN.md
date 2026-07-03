아래 설계는 **“처음부터 코드를 작성할 수 있는 기준 문서”**처럼 쓰는 것을 목표로 정리했습니다. 핵심은 다음입니다.

> **정확한 시뮬레이터 + 합법 행동 마스크 + GNN 상태 인코더 + 계층형 policy head + value head + self-play PPO**

이 게임은 매일 `건설/업그레이드`, `이동`, `훈련` 명령을 여러 개 조합해서 제출해야 하고, 유효하지 않은 명령을 내면 즉시 패배하므로, 신경망이 직접 문자열 명령을 만들게 하면 안 됩니다. 신경망은 **확률적 의사결정**만 하고, 최종 명령은 반드시 **rule mask + validator**를 통과해서 생성해야 합니다. 게임 규칙상 전장은 51~109개의 구역으로 구성된 가변 크기 그래프이며, 중심 기준 점대칭 구조를 갖고, 플레이어는 건설·이동·훈련 명령을 매일 원하는 만큼 낼 수 있습니다.  또한 `START` 이후 100ms 안에 `END`까지 출력해야 하고, 유효하지 않은 명령은 즉시 패배로 이어집니다. 

---

# 1. 전체 구조

전체 시스템은 다음 7개 블록으로 나누는 것이 좋습니다.

```text
[1] Game Simulator
    규칙 그대로 하루 진행: 건설 → 이동 → 훈련 → 전투 → 노동 → 보급 → 종료 판정

[2] Legal Action Mask / Validator
    현재 상태에서 가능한 TRAIN / UPGRADE / MOVE 후보 생성
    신경망 출력이 불법 명령이 되지 않도록 강제

[3] Observation Builder
    현재 게임 상태를 "내 관점 canonical graph"로 변환

[4] GNN Actor-Critic Model
    Graph Encoder
    + TRAIN policy head
    + UPGRADE policy head
    + MOVE policy head
    + Value head

[5] Action Sampler / Command Decoder
    policy distribution에서 action sample
    sample된 action을 실제 COMMAND 문자열로 변환

[6] Self-Play Rollout System
    현재 모델 vs 현재 모델 또는 과거 snapshot 모델로 게임 진행

[7] PPO Trainer
    rollout 데이터로 advantage 계산
    PPO loss로 policy/value 업데이트
```

PPO를 선택하는 이유는 명확합니다. PPO는 환경과 상호작용해서 데이터를 모은 뒤 surrogate objective를 여러 epoch 동안 최적화하는 방식이고, 구현 단순성·범용성·sample complexity·wall-time 사이의 균형이 좋아 actor-critic 계열의 기본 선택지로 널리 쓰입니다. ([arXiv][1]) GNN을 쓰는 이유는 전장이 고정 격자가 아니라 노드와 엣지로 이루어진 가변 그래프이기 때문입니다. GNN은 노드 간 관계를 message passing으로 모델링하는 구조라서 이런 전장 표현에 잘 맞습니다. ([arXiv][2])

---

# 2. 코드 작성 순서

처음부터 신경망을 만들면 실패할 가능성이 높습니다. 반드시 아래 순서로 작성하세요.

```text
1. constants.py
2. map.py
3. state.py
4. simulator.py
5. legal.py
6. observation.py
7. random_agent.py
8. env.py
9. model.py
10. action_dist.py
11. rollout_buffer.py
12. ppo.py
13. self_play.py
14. train.py
15. evaluate.py
16. interactive_bot.py
```

추천 디렉터리 구조는 다음과 같습니다.

```text
nextnation_ai/
  game/
    constants.py
    map.py
    state.py
    simulator.py
    legal.py
    observation.py
    reward.py
    env.py

  agents/
    random_agent.py
    neural_agent.py
    snapshot_agent.py

  nn/
    gnn_encoder.py
    actor_critic.py
    action_dist.py

  rl/
    rollout_buffer.py
    gae.py
    ppo.py
    self_play_runner.py
    league.py

  scripts/
    train.py
    evaluate.py
    export_model.py

  submit/
    interactive_bot.py
    fast_inference.py

  tests/
    test_movement.py
    test_combat.py
    test_economy.py
    test_legal_actions.py
    test_symmetry.py
```

---

# 3. 1단계: 시뮬레이터부터 만든다

## 3.1 왜 시뮬레이터가 먼저인가

Self-Play PPO는 수십만~수백만 턴을 스스로 플레이하면서 배웁니다. 따라서 **정확한 로컬 시뮬레이터**가 없으면 학습 자체가 불가능합니다.

게임 하루는 다음 순서로 진행됩니다.

```text
아침:
  1. 건설 단계
  2. 이동 단계
  3. 훈련 단계

낮:
  4. 전투 단계

저녁:
  5. 노동 단계
  6. 보급 단계
```

이 순서는 규칙에 명시되어 있고, 노동은 건물마다 `15 × min(해당 구역 아군 전사 수, 건물 노동 가능 전사 수)`만큼 금화를 얻으며, 보급에서는 전사마다 금화 2개를 소모하고 부족하면 체력이 감소합니다.  

## 3.2 state.py

상태는 최소한 다음 구조로 잡으세요.

```python
@dataclass
class Unit:
    uid: int
    owner: int          # 0: LEFT, 1: RIGHT
    pos: int
    hp: int
    max_hp: int
    moving: bool
    target: int | None  # 자기 유닛은 알고 있음. 상대 유닛은 실전 관측에서는 모를 수 있음.
    alive: bool = True


@dataclass
class Building:
    owner: int
    kind: str           # "HQ" or "BASE"
    level: int
    hp: int
    max_hp: int


@dataclass
class PlayerState:
    gold: int
    hq_pos: int
    next_unit_id: int
    remaining_time_tokens: int = 5


@dataclass
class GameState:
    day: int
    map: GameMap
    players: list[PlayerState]
    units: dict[int, Unit]
    buildings: dict[int, Building]  # key: node id
    done: bool = False
    winner: int | None = None
```

처음 상태는 다음과 같습니다.

```text
LEFT:
  HQ = 0
  gold = 500
  units = A1, A2, A3 at 0
  HQ level = 1
  HQ hp = 10

RIGHT:
  HQ = N - 1
  gold = 500
  units = B1, B2, B3 at N - 1
  HQ level = 1
  HQ hp = 10
```

본부가 파괴된 플레이어는 패배하고, 200일 동안 양쪽 본부가 파괴되지 않으면 본부 체력 비교로 승패를 정합니다. 

---

# 4. 2단계: 맵과 이동 전처리

## 4.1 GameMap

맵은 그래프입니다.

```python
@dataclass
class GameMap:
    N: int
    K: int
    x: list[int]
    y: list[int]
    points: set[int]
    adj: list[list[int]]

    edge_index: torch.Tensor
    edge_attr: torch.Tensor

    dist: list[list[int]]
    next_hop: list[list[int]]
```

전장은 매 게임마다 달라지고, `N`은 51~109, 거점 수 `K`는 대략 `0.15N~0.2N` 범위입니다.  그래서 CNN이 아니라 graph encoder가 맞습니다.

## 4.2 이동용 shortest path

이동 규칙은 단순 BFS가 아닙니다. 인접한 두 구역 중심 사이의 거리는 다음입니다.

```text
ceil(sqrt((x1 - x2)^2 + (y1 - y2)^2))
```

전사는 현재 위치에서 목적지까지 이 거리 합이 최소가 되는 경로를 따라 하루에 한 칸 이동하고, 최단 경로가 여러 개면 구역 번호가 더 작은 다음 구역을 선택합니다. 

따라서 모든 `(현재 위치, 목적지)`에 대해 다음을 미리 계산하세요.

```python
dist[u][target]
next_hop[u][target]
```

구현 방식은 다음입니다.

```python
def precompute_shortest_paths(game_map):
    # target마다 Dijkstra
    # dist_to_target[v] = v에서 target까지 최단 거리
    # next_hop[u][target] = u에서 target으로 갈 때 다음 노드
```

`N ≤ 109`이므로 모든 목적지에 대해 Dijkstra를 돌려도 충분히 작습니다.

다음 노드 선택은 이렇게 합니다.

```python
best_v = argmin over v in adj[u]:
    edge_len(u, v) + dist[v][target]

tie-break:
    smaller v
```

이 부분은 반드시 `test_movement.py`로 검증해야 합니다.

---

# 5. 3단계: Legal Action Mask / Validator

이 프로젝트에서 가장 중요한 파일은 `legal.py`입니다.

신경망은 “하고 싶은 것”을 확률로 고르고, `legal.py`는 “실제로 가능한 것만” 남깁니다.

## 5.1 명령 종류

하루 명령은 다음 3종류입니다.

```text
TRAIN n
UPGRADE d
MOVE a d
```

훈련은 하루에 1번 이하이고, `UPGRADE d`는 기존 아군 건물을 업그레이드/수리하거나 건물 없는 거점에 기지를 건설합니다. `MOVE a d`는 이동 중이 아닌 전사 하나에게 목적지를 지정합니다. 

## 5.2 예산 처리 방식

명령 문자열의 순서는 자유지만, 실제 지불 순서는 다음입니다.

```text
건설 비용 → 이동 명령 비용 → 훈련 비용
```

학습용 action sampler에서는 불법 행동을 피하기 위해 **훈련 비용을 먼저 reserve**하는 방식을 추천합니다.

```text
1. TRAIN n을 먼저 sample
2. train_cost = 120 * n 을 reserve
3. upgrade/build는 gold - train_cost 안에서만 선택
4. move는 남은 budget 안에서만 선택
5. 최종 COMMAND에는 TRAIN n, UPGRADE들, MOVE들을 출력
```

이 방식은 실제 규칙을 깨지 않습니다. 어차피 하루 중 노동 수입은 저녁에 들어오므로, 그날 아침/낮에 쓰는 총비용은 현재 gold 이하여야 합니다.

## 5.3 TRAIN mask

```python
def legal_train_mask(state, player):
    hq = state.buildings[player.hq_pos]
    max_train = HQ_TABLE[hq.level].train_cap
    mask = [False] * 4  # 0,1,2,3
    for n in range(max_train + 1):
        if 120 * n <= player.gold:
            mask[n] = True
    return mask
```

본부 레벨에 따라 훈련 가능 전사 수가 달라집니다. 본부 레벨 1은 1명, 레벨 3은 2명, 레벨 5는 3명까지 훈련할 수 있습니다. 

## 5.4 UPGRADE / BUILD mask

`UPGRADE d`가 가능한 조건은 다음입니다.

```text
공통 조건:
  - 해당 구역에 내 전사가 1명 이상 있어야 한다.
  - 해당 구역에 적 전사가 없어야 한다.
  - 그 구역에 오늘 이미 UPGRADE를 선택하지 않았어야 한다.
  - 비용을 낼 수 있어야 한다.

건물이 있는 경우:
  - 내 건물이어야 한다.
  - HQ면 HQ 업그레이드/수리 비용 적용
  - BASE면 BASE 업그레이드/수리 비용 적용

건물이 없는 경우:
  - 해당 구역이 거점이어야 한다.
  - 비용 300으로 BASE level 1 건설
```

기지는 레벨 1 건설 비용 300, 레벨 2 업그레이드 비용 600, 레벨 3 업그레이드 비용 1000, 레벨 3 이후 수리 비용 500입니다. 

## 5.5 MOVE mask

처음 버전에서는 **전사 개별 이동**보다 **구역별 그룹 이동**을 추천합니다.

개별 이동:

```text
unit A13 → target 37
unit A14 → target 42
unit A15 → target 37
...
```

그룹 이동:

```text
origin 12에 있는 idle 전사 5명 중 3명을 target 37로 이동
```

실제 명령은 여전히 개별 `MOVE a d`로 출력하지만, policy는 그룹 단위로 선택하게 합니다.

```python
@dataclass
class MoveGroupAction:
    origin: int
    target: int | None  # None이면 NOOP
    count: int
```

`origin`에 있는 idle 전사들을 ID 오름차순으로 정렬한 뒤, 앞에서 `count`명에게 같은 target을 부여하면 됩니다.

```python
def decode_move_group(state, player, action):
    units = idle_units_at_origin_sorted_by_id(state, player, action.origin)
    selected = units[:action.count]
    commands = []
    for u in selected:
        commands.append(f"MOVE {unit_name(u)} {action.target}")
    return commands
```

이렇게 하면 행동 공간이 크게 줄고, PPO가 훨씬 안정됩니다.

---

# 6. 4단계: Observation Builder

## 6.1 항상 “내가 LEFT”인 관점으로 정규화

게임은 중심 기준 점대칭입니다. LEFT의 본부는 0번, RIGHT의 본부는 `N-1`번입니다. 

따라서 RIGHT 플레이어 관측을 만들 때는 노드 번호를 뒤집으세요.

```python
def canonical_node(i, player, N):
    if player == LEFT:
        return i
    else:
        return N - 1 - i
```

좌표도 뒤집습니다.

```python
if player == RIGHT:
    x' = -x
    y' = -y
```

소유권도 바꿉니다.

```text
현재 player의 것 → my
상대 player의 것 → enemy
```

그러면 모델은 항상 다음 관점으로 학습합니다.

```text
내 본부: 0번 근처
적 본부: N-1번 근처
공격 방향: 왼쪽 → 오른쪽
```

이 정규화는 데이터 효율을 크게 높입니다.

## 6.2 Node feature

각 구역마다 다음 feature를 만듭니다.

```text
기하/맵:
  x_norm
  y_norm
  is_point
  is_my_hq
  is_enemy_hq
  degree_norm
  dist_to_my_hq_norm
  dist_to_enemy_hq_norm
  dist_to_center_norm

내 건물:
  my_building_exists
  my_building_is_hq
  my_building_is_base
  my_building_level_norm
  my_building_hp_ratio
  my_turret_power_norm
  my_worker_cap_norm

적 건물:
  enemy_building_exists
  enemy_building_is_hq
  enemy_building_is_base
  enemy_building_level_norm
  enemy_building_hp_ratio
  enemy_turret_power_norm
  enemy_worker_cap_norm

내 전사:
  my_unit_count_norm
  my_idle_count_norm
  my_moving_count_norm
  my_hp_sum_norm
  my_hp_mean_norm
  my_min_hp_norm

적 전사:
  enemy_unit_count_norm
  enemy_hp_sum_norm
  enemy_hp_mean_norm
  enemy_min_hp_norm

전술/경제:
  my_income_here_norm
  enemy_income_here_norm
  local_hp_balance_norm
  adjacent_my_units_norm
  adjacent_enemy_units_norm
  can_build_or_upgrade
```

처음에는 feature를 너무 줄이지 않는 편이 좋습니다. GNN은 불필요한 feature를 어느 정도 무시할 수 있지만, 없는 정보는 학습할 수 없습니다.

## 6.3 Edge feature

각 인접 엣지마다 다음을 넣습니다.

```text
edge_length_norm
dx_norm
dy_norm
is_forward_edge
is_backward_edge
```

`is_forward_edge`는 canonical 관점에서 `x`가 적 본부 방향으로 증가하는지 여부입니다.

## 6.4 Global feature

```text
day / 200
my_gold_norm
enemy_gold_estimate_norm
my_hq_level_norm
enemy_hq_level_norm
my_hq_hp_ratio
enemy_hq_hp_ratio
my_total_units_norm
enemy_total_units_norm
my_total_buildings_norm
enemy_total_buildings_norm
remaining_days_norm
```

상대 금화는 실전에서 직접 주어지지 않더라도, 훈련/건설/이동/노동/보급 이벤트를 추적하면 어느 정도 계산할 수 있습니다. 단, 상대 이동 목적지처럼 완전히 공개되지 않는 정보는 관측에 넣지 않는 편이 안전합니다.

---

# 7. 5단계: 모델 구조

추천 모델 구조는 다음입니다.

```text
Observation Graph
  node features x_i
  edge_index
  edge_attr
  global features g

        ↓

GNN Encoder
  4~6 message passing layers
  residual connection
  layer normalization
  global pooling

        ↓

Shared representation:
  node embeddings h_i
  global embedding z

        ↓

Policy heads:
  TRAIN head
  UPGRADE pointer head
  MOVE group head

Value head:
  V(s)
```

PyTorch Geometric을 쓰면 가변 크기 그래프 배치를 쉽게 처리할 수 있습니다. PyG는 PyTorch 기반 GNN 라이브러리이고, 여러 그래프를 하나의 큰 그래프로 이어 붙이는 방식으로 batch를 처리합니다. ([PyG Documentation][3])

---

## 7.1 GNN Encoder

처음 구현은 복잡한 Graph Transformer보다 **edge-aware message passing GNN**을 추천합니다.

```python
class GraphEncoder(nn.Module):
    def __init__(self, node_dim, edge_dim, global_dim, hidden_dim=128, num_layers=4):
        super().__init__()

        self.node_in = nn.Linear(node_dim, hidden_dim)
        self.edge_in = nn.Linear(edge_dim, hidden_dim)
        self.global_in = nn.Linear(global_dim, hidden_dim)

        self.layers = nn.ModuleList([
            GraphBlock(hidden_dim)
            for _ in range(num_layers)
        ])

        self.global_out = nn.Sequential(
            nn.Linear(hidden_dim * 3, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, hidden_dim),
        )

    def forward(self, data):
        # data.x: [total_nodes, node_dim]
        # data.edge_index: [2, total_edges]
        # data.edge_attr: [total_edges, edge_dim]
        # data.global_feat: [batch_size, global_dim]
        # data.batch: [total_nodes], 각 node가 어느 graph에 속하는지

        h = self.node_in(data.x)
        e = self.edge_in(data.edge_attr)
        g = self.global_in(data.global_feat)

        for layer in self.layers:
            h = layer(h, data.edge_index, e, g, data.batch)

        mean_pool = global_mean_pool(h, data.batch)
        max_pool = global_max_pool(h, data.batch)

        z = self.global_out(torch.cat([g, mean_pool, max_pool], dim=-1))
        return h, z
```

`GraphBlock`은 다음 형태면 충분합니다.

```python
class GraphBlock(nn.Module):
    def __init__(self, hidden_dim):
        super().__init__()

        self.msg_mlp = nn.Sequential(
            nn.Linear(hidden_dim * 3, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, hidden_dim),
        )

        self.node_mlp = nn.Sequential(
            nn.Linear(hidden_dim * 2, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, hidden_dim),
        )

        self.norm = nn.LayerNorm(hidden_dim)

    def forward(self, h, edge_index, e, g, batch):
        src, dst = edge_index

        msg_input = torch.cat([h[src], h[dst], e], dim=-1)
        msg = self.msg_mlp(msg_input)

        agg = scatter_mean(msg, dst, dim=0, dim_size=h.size(0))

        h_new = self.node_mlp(torch.cat([h, agg], dim=-1))
        h = self.norm(h + h_new)
        return h
```

처음에는 hidden dimension 128, layer 4개로 시작하세요. 성능이 부족하면 256, layer 6개로 올립니다.

---

# 8. 6단계: Policy head 설계

이 게임에서 가장 중요한 설계는 policy head입니다.

하루 전체 action을 하나의 categorical로 만들면 안 됩니다. 행동 공간이 폭발합니다.

대신 다음처럼 factorization합니다.

```text
π(a | s)
= π_train(n | s)
  × π_upgrade(seq | s, n)
  × π_move(seq | s, n, upgrades)
```

즉, 하루 명령 묶음을 여러 개의 작은 선택으로 나눕니다.

---

## 8.1 TRAIN head

```python
class TrainHead(nn.Module):
    def __init__(self, hidden_dim):
        super().__init__()
        self.net = nn.Linear(hidden_dim, 4)  # n = 0,1,2,3

    def forward(self, z, train_mask):
        logits = self.net(z)
        logits = logits.masked_fill(~train_mask, -1e9)
        return Categorical(logits=logits)
```

훈련 가능 최대치는 본부 레벨에 따라 1~3입니다. 비용은 전사 1명당 120입니다. 

---

## 8.2 UPGRADE pointer head

하루에 여러 구역을 업그레이드할 수 있지만, 한 구역은 하루에 한 번만 가능합니다. 

따라서 다음처럼 pointer 방식으로 반복 선택합니다.

```text
step 1:
  STOP or node d 선택

step 2:
  STOP or 아직 선택하지 않은 legal node d 선택

step 3:
  STOP or 아직 선택하지 않은 legal node d 선택

...
```

구현:

```python
class UpgradeHead(nn.Module):
    def __init__(self, hidden_dim):
        super().__init__()
        self.node_score = nn.Sequential(
            nn.Linear(hidden_dim * 2, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, 1),
        )
        self.stop_score = nn.Linear(hidden_dim, 1)

    def logits(self, node_h, z, legal_mask):
        # node_h: [N, H]
        # z: [H]
        z_expand = z.unsqueeze(0).expand(node_h.size(0), -1)

        node_logits = self.node_score(
            torch.cat([node_h, z_expand], dim=-1)
        ).squeeze(-1)

        node_logits = node_logits.masked_fill(~legal_mask, -1e9)

        stop_logit = self.stop_score(z).view(1)

        # index 0 = STOP
        # index 1..N = node 0..N-1
        return torch.cat([stop_logit, node_logits], dim=0)
```

sampling logic:

```python
def sample_upgrades(model, state, node_h, z, budget):
    selected = []
    logps = []
    entropies = []

    used_nodes = set()

    for step in range(MAX_UPGRADES_PER_TURN):
        legal_mask, costs = compute_upgrade_mask_and_costs(
            state,
            budget=budget,
            used_nodes=used_nodes,
        )

        logits = model.upgrade_head.logits(node_h, z, legal_mask)
        dist = Categorical(logits=logits)
        choice = dist.sample()

        logps.append(dist.log_prob(choice))
        entropies.append(dist.entropy())

        if choice.item() == 0:
            break

        node = choice.item() - 1
        cost = costs[node]

        selected.append(node)
        used_nodes.add(node)
        budget -= cost

        if budget <= 0:
            break

    return selected, budget, sum(logps), sum(entropies)
```

처음에는 `MAX_UPGRADES_PER_TURN = 3` 정도로 제한하세요. 이후 5, 8, 12로 늘리면 됩니다.

---

## 8.3 MOVE group head

개별 전사마다 목적지를 고르면 action이 너무 깁니다. 처음에는 **origin node 단위의 group move**를 쓰세요.

```text
origin node 12:
  target = 35
  count = 3

=> 12번 구역의 idle 전사 중 ID가 작은 3명에게 MOVE target 35 명령
```

### Target head

```python
class MoveTargetHead(nn.Module):
    def __init__(self, hidden_dim):
        super().__init__()

        self.origin_proj = nn.Linear(hidden_dim, hidden_dim)
        self.target_proj = nn.Linear(hidden_dim, hidden_dim)
        self.noop_score = nn.Linear(hidden_dim * 2, 1)

    def logits(self, origin_h, node_h, z, target_mask):
        # origin_h: [H]
        # node_h: [N, H]

        q = self.origin_proj(origin_h)          # [H]
        k = self.target_proj(node_h)            # [N, H]

        target_logits = torch.matmul(k, q) / math.sqrt(q.size(-1))
        target_logits = target_logits.masked_fill(~target_mask, -1e9)

        noop_logit = self.noop_score(torch.cat([origin_h, z], dim=-1)).view(1)

        # index 0 = NOOP
        # index 1..N = target node 0..N-1
        return torch.cat([noop_logit, target_logits], dim=0)
```

### Count head

```python
class MoveCountHead(nn.Module):
    def __init__(self, hidden_dim, max_count=32):
        super().__init__()
        self.max_count = max_count
        self.net = nn.Sequential(
            nn.Linear(hidden_dim * 3 + 3, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, max_count),
        )

    def forward(self, origin_h, target_h, z, count_features, count_mask):
        inp = torch.cat([origin_h, target_h, z, count_features], dim=-1)
        logits = self.net(inp)
        logits = logits.masked_fill(~count_mask, -1e9)
        return Categorical(logits=logits)
```

`count_mask`는 다음 조건으로 만듭니다.

```text
count ≤ origin의 idle 전사 수
target이 내 건물이면 이동 비용 0
target이 내 건물이 아니면 count × 10 ≤ 남은 budget
```

이동 명령을 새로 받은 전사는 기본적으로 금화 10을 소모하지만, 목적지가 아군 건물이면 소모하지 않습니다. 

---

# 9. Actor-Critic 전체 모델

전체 모델은 다음처럼 구성합니다.

```python
class NextNationActorCritic(nn.Module):
    def __init__(
        self,
        node_dim,
        edge_dim,
        global_dim,
        hidden_dim=128,
        gnn_layers=4,
    ):
        super().__init__()

        self.encoder = GraphEncoder(
            node_dim=node_dim,
            edge_dim=edge_dim,
            global_dim=global_dim,
            hidden_dim=hidden_dim,
            num_layers=gnn_layers,
        )

        self.train_head = TrainHead(hidden_dim)
        self.upgrade_head = UpgradeHead(hidden_dim)
        self.move_target_head = MoveTargetHead(hidden_dim)
        self.move_count_head = MoveCountHead(hidden_dim)

        self.value_head = nn.Sequential(
            nn.Linear(hidden_dim, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, 1),
        )

    def encode(self, obs):
        node_h, z = self.encoder(obs.graph)
        value = self.value_head(z).squeeze(-1)
        return node_h, z, value
```

`value`는 현재 관점 플레이어가 이길 기대값입니다.

```text
V(s) ≈ 현재 player 관점 expected return
```

즉, 같은 실제 상태라도 LEFT 관점 observation과 RIGHT 관점 observation의 value는 부호가 반대에 가까워야 합니다.

---

# 10. Action 객체 설계

PPO에서는 나중에 같은 action의 log probability를 다시 계산해야 합니다. 따라서 action은 문자열이 아니라 구조체로 저장해야 합니다.

```python
@dataclass
class NNAction:
    train_n: int
    upgrades: list[int]
    moves: list[MoveGroupAction]

    # PPO용
    old_logp: float
    old_value: float
    entropy: float
```

더 안정적으로 하려면 head별 logp도 저장하세요.

```python
@dataclass
class ActionLogProbs:
    train_logp: torch.Tensor
    upgrade_logps: list[torch.Tensor]
    move_target_logps: list[torch.Tensor]
    move_count_logps: list[torch.Tensor]
```

최종 명령은 다음처럼 만듭니다.

```python
def action_to_commands(action, state, player):
    commands = ["COMMAND"]

    for d in action.upgrades:
        commands.append(f"UPGRADE {to_absolute_node(d, player, state.map.N)}")

    for move in action.moves:
        commands.extend(decode_move_group(state, player, move))

    commands.append(f"TRAIN {action.train_n}")
    commands.append("END")

    return commands
```

명령 순서는 자유지만, 디버깅을 위해 항상 같은 순서로 출력하는 것이 좋습니다.

```text
COMMAND
UPGRADE ...
MOVE ...
TRAIN ...
END
```

---

# 11. Reward 설계

승패 reward만 주면 학습이 매우 느릴 가능성이 큽니다. 하지만 shaping을 너무 강하게 주면 실제 승리보다 “점수 벌기”에 과적합합니다.

추천은 **potential-based shaping**입니다.

```text
reward_t =
    terminal_reward
    + beta * (Phi(s_{t+1}) - Phi(s_t))
```

## 11.1 Terminal reward

```python
if win:
    terminal_reward = +1.0
elif lose:
    terminal_reward = -1.0
else:
    terminal_reward = 0.0
```

## 11.2 Potential score

```python
Phi(s, player) =
    0.35 * my_hq_hp_ratio
  - 0.35 * enemy_hq_hp_ratio
  + 0.10 * my_income_norm
  - 0.10 * enemy_income_norm
  + 0.08 * my_building_value_norm
  - 0.08 * enemy_building_value_norm
  + 0.05 * my_unit_hp_norm
  - 0.05 * enemy_unit_hp_norm
  + 0.03 * my_map_control_norm
  - 0.03 * enemy_map_control_norm
```

`beta`는 처음에는 크게, 나중에는 작게 줄입니다.

```text
초기: beta = 0.20
중기: beta = 0.10
후기: beta = 0.03
최종 fine-tuning: beta = 0.00 ~ 0.02
```

이렇게 하면 초반에는 학습 신호가 많고, 후반에는 실제 승패에 더 맞춰집니다.

---

# 12. PPO 학습 루프

PPO는 다음 과정을 반복합니다.

```text
1. 현재 policy로 self-play rollout 수집
2. 각 transition에 대해 reward, value, logp 저장
3. GAE로 advantage 계산
4. PPO objective로 policy/value 업데이트
5. 일정 주기마다 snapshot 저장
6. 평가 pool을 상대로 승률 측정
```

GAE는 value function을 이용해 policy gradient의 variance를 줄이는 advantage 추정 방법입니다. ([arXiv][4])

## 12.1 GAE 공식

```python
delta_t = r_t + gamma * V(s_{t+1}) * (1 - done_t) - V(s_t)

A_t = delta_t + gamma * lambda * (1 - done_t) * A_{t+1}

return_t = A_t + V(s_t)
```

추천값:

```text
gamma = 0.997
gae_lambda = 0.95
```

게임 최대 길이가 200일이므로 `gamma`는 너무 낮게 잡지 않는 편이 좋습니다.

---

## 12.2 PPO loss

```python
ratio = exp(new_logp - old_logp)

policy_loss = -mean(
    min(
        ratio * advantage,
        clip(ratio, 1 - eps, 1 + eps) * advantage
    )
)

value_loss = mean((new_value - return_)^2)

entropy_loss = -mean(entropy)

loss =
    policy_loss
  + value_coef * value_loss
  + entropy_coef * entropy_loss
```

추천 초기 hyperparameter:

```text
learning_rate = 3e-4
clip_eps = 0.2
value_coef = 0.5
entropy_coef = 0.01
max_grad_norm = 0.5

rollout_len = 64
num_envs = 64
ppo_epochs = 4
minibatch_size = 2048 ~ 8192
target_kl = 0.03
```

학습이 불안정하면 다음 순서로 조정하세요.

```text
1. learning_rate 3e-4 → 1e-4
2. clip_eps 0.2 → 0.1
3. entropy_coef 0.01 → 0.005
4. rollout_len 64 → 128
5. reward shaping scale beta 감소
```

---

# 13. Self-Play 구성

## 13.1 처음에는 current vs current

가장 단순한 self-play는 다음입니다.

```text
LEFT: current_model
RIGHT: current_model
```

둘 다 같은 파라미터를 공유하지만, observation은 각자 canonical 관점으로 변환됩니다.

```python
obs_left = build_observation(state, player=LEFT)
obs_right = build_observation(state, player=RIGHT)

action_left = policy.sample(obs_left)
action_right = policy.sample(obs_right)

next_state = simulator.step(state, action_left, action_right)
```

이때 LEFT transition과 RIGHT transition을 모두 buffer에 넣습니다.

```python
buffer.add(obs_left, action_left, reward_left, value_left, logp_left)
buffer.add(obs_right, action_right, reward_right, value_right, logp_right)
```

## 13.2 이후에는 snapshot opponent 추가

current vs current만 하면 특정 전략에 과적합하거나 이전에 배운 대응을 잊기 쉽습니다.

따라서 일정 업데이트마다 snapshot을 저장하세요.

```text
policy_000100.pt
policy_000200.pt
policy_000300.pt
...
```

상대 선택은 이렇게 합니다.

```text
초기:
  100% current vs current

중기:
  70% current vs random past snapshot
  20% current vs current
  10% current vs random legal agent

후기:
  50% current vs strong snapshot
  30% current vs random past snapshot
  10% current vs current
  10% current vs exploiter
```

주의할 점은 PPO가 on-policy 알고리즘이라는 것입니다. snapshot이 선택한 행동을 current policy 학습 데이터로 쓰면 안 됩니다. current policy가 조종한 쪽의 transition만 PPO buffer에 넣으세요.

```python
if left_agent_is_current:
    buffer.add(left_transition)

if right_agent_is_current:
    buffer.add(right_transition)
```

---

# 14. 학습 curriculum

처음부터 전체 행동 공간을 열면 거의 학습이 안 될 가능성이 큽니다.

아래 순서로 난이도를 늘리세요.

## Phase 0: simulator 검증

목표:

```text
랜덤 legal agent끼리 10,000게임을 돌려도 invalid action이 없어야 함.
시뮬레이터 state가 음수 gold, 음수 hp, 중복 unit id를 만들면 안 됨.
```

테스트:

```text
test_movement.py
test_combat.py
test_economy.py
test_build_upgrade.py
test_training.py
test_terminal.py
test_symmetry.py
test_random_games.py
```

## Phase 1: 제한된 action PPO

제한:

```text
MAX_UPGRADES_PER_TURN = 1
MAX_MOVE_ORIGINS_PER_TURN = 3
MOVE target 후보 = 내 HQ, 적 HQ, 모든 거점, 현재 적 건물, 현재 내 건물
horizon = 50일
beta = 0.20
```

목표:

```text
무작위보다 높은 승률
굶주림으로 대량 사망하지 않기
아무것도 안 하는 정책으로 붕괴하지 않기
```

## Phase 2: 일반 action PPO

제한 완화:

```text
MAX_UPGRADES_PER_TURN = 3
MAX_MOVE_ORIGINS_PER_TURN = 6
MOVE target 후보 = 전체 node 0..N-1
horizon = 100일
beta = 0.10
```

목표:

```text
확장, 훈련, 공격 타이밍이 생기기 시작
과거 snapshot 상대로 승률 상승
```

## Phase 3: full 200-day self-play

```text
MAX_UPGRADES_PER_TURN = 6~10
MAX_MOVE_ORIGINS_PER_TURN = 10~20
horizon = 200일
beta = 0.03
snapshot league 사용
held-out map set 평가
```

## Phase 4: fine-tuning

```text
beta = 0.00 ~ 0.02
entropy_coef 감소
상위 snapshot/exploiter 위주 대전
실제 제출 시간 100ms 기준으로 inference 제한
```

---

# 15. Rollout Runner 의사코드

```python
def collect_rollouts(policy, opponent_pool, num_envs, rollout_len):
    envs = [NextNationEnv(seed=i) for i in range(num_envs)]
    buffer = RolloutBuffer()

    for t in range(rollout_len):
        for env in envs:
            state = env.state

            # current가 어느 쪽을 조종할지 결정
            controlled_side = env.controlled_side

            left_agent = env.left_agent
            right_agent = env.right_agent

            obs_left = build_observation(state, LEFT)
            obs_right = build_observation(state, RIGHT)

            if left_agent == "current":
                action_left, logp_left, value_left, entropy_left = policy.act(obs_left)
            else:
                action_left = left_agent.act(obs_left)

            if right_agent == "current":
                action_right, logp_right, value_right, entropy_right = policy.act(obs_right)
            else:
                action_right = right_agent.act(obs_right)

            prev_state = state.clone()

            next_state, reward_left, reward_right, done, info = env.step(
                action_left,
                action_right,
            )

            if left_agent == "current":
                buffer.add(
                    obs=obs_left,
                    action=action_left,
                    reward=reward_left,
                    done=done,
                    old_logp=logp_left,
                    old_value=value_left,
                    entropy=entropy_left,
                )

            if right_agent == "current":
                buffer.add(
                    obs=obs_right,
                    action=action_right,
                    reward=reward_right,
                    done=done,
                    old_logp=logp_right,
                    old_value=value_right,
                    entropy=entropy_right,
                )

            if done:
                env.reset(opponent_pool.sample_matchup())

    return buffer
```

---

# 16. PPO Trainer 의사코드

```python
def train_ppo(policy, optimizer, buffer):
    # 1. bootstrap value 계산
    buffer.compute_gae(
        gamma=0.997,
        gae_lambda=0.95,
    )

    # 2. advantage normalization
    buffer.advantages = (
        buffer.advantages - buffer.advantages.mean()
    ) / (buffer.advantages.std() + 1e-8)

    for epoch in range(PPO_EPOCHS):
        for batch in buffer.iter_minibatches(MINIBATCH_SIZE):
            new_logp, entropy, value = policy.evaluate_actions(
                batch.obs,
                batch.actions,
            )

            ratio = torch.exp(new_logp - batch.old_logp)

            unclipped = ratio * batch.advantages
            clipped = torch.clamp(
                ratio,
                1.0 - CLIP_EPS,
                1.0 + CLIP_EPS,
            ) * batch.advantages

            policy_loss = -torch.mean(torch.min(unclipped, clipped))

            value_loss = F.mse_loss(value, batch.returns)

            entropy_loss = -entropy.mean()

            loss = (
                policy_loss
                + VALUE_COEF * value_loss
                + ENTROPY_COEF * entropy_loss
            )

            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(policy.parameters(), 0.5)
            optimizer.step()
```

---

# 17. 큰 action log probability 문제

이 게임에서는 하루 action이 다음처럼 길 수 있습니다.

```text
TRAIN 2
UPGRADE 13
UPGRADE 27
MOVE A4 35
MOVE A5 35
MOVE A8 41
...
```

이 모든 선택의 log probability를 단순 합산하면 `new_logp - old_logp`가 너무 커져 PPO ratio가 불안정해질 수 있습니다.

처음에는 joint log probability를 써도 됩니다.

```python
total_logp = (
    train_logp
    + sum(upgrade_logps)
    + sum(move_target_logps)
    + sum(move_count_logps)
)
```

불안정하면 다음 방식으로 바꾸세요.

```text
state 하나에 대해 policy loss를 한 번 계산하지 말고,
TRAIN / UPGRADE step / MOVE target / MOVE count별 PPO loss를 따로 계산한 뒤 평균낸다.
```

실용적으로는 다음이 더 안정적인 경우가 많습니다.

```python
policy_loss = 0
num_terms = 0

for component in action_components:
    ratio = exp(component.new_logp - component.old_logp)

    loss_k = -min(
        ratio * advantage,
        clip(ratio, 1 - eps, 1 + eps) * advantage,
    )

    policy_loss += loss_k
    num_terms += 1

policy_loss = policy_loss / max(1, num_terms)
```

이 방식은 엄밀한 joint PPO objective는 아니지만, action이 매우 긴 factorized policy에서는 안정성이 좋아지는 경우가 많습니다.

---

# 18. Auxiliary loss 추가

초기 학습 안정화를 위해 value/policy loss 외에 보조 예측을 붙이면 좋습니다.

```text
auxiliary heads:
  next_my_gold_delta
  next_enemy_hq_hp_delta
  next_my_hq_hp_delta
  next_combat_damage
  next_income
  terminal_within_10_days
```

예:

```python
aux_loss =
    mse(pred_gold_delta, actual_gold_delta)
  + mse(pred_hq_hp_delta, actual_hq_hp_delta)
  + bce(pred_terminal_soon, terminal_soon)
```

최종 loss:

```python
loss =
    policy_loss
  + 0.5 * value_loss
  - 0.01 * entropy
  + 0.05 * aux_loss
```

auxiliary loss는 모델이 “이 상태가 경제적으로 좋은지”, “이 전투가 위험한지”를 더 빨리 배우게 해줍니다.

---

# 19. 평가 방식

학습 중에는 loss보다 승률을 더 믿어야 합니다.

평가 pool을 따로 만드세요.

```text
eval opponents:
  random legal agent
  old snapshot 5개
  best snapshot 5개
  economy-biased weak agent
  attack-biased weak agent
```

여기서 economy-biased, attack-biased는 강한 휴리스틱이 아니라 아주 단순한 구조적 baseline이면 됩니다.

예:

```text
economy-biased:
  가능한 경우 거점에 기지 건설
  금화가 남으면 훈련

attack-biased:
  가능한 경우 훈련
  병력을 적 본부 방향으로 이동
```

측정 지표:

```text
invalid action rate
win rate
draw rate
average game length
average HQ hp difference
average income
average unit count
average building count
policy entropy
approx KL
clip fraction
value explained variance
```

가장 중요한 것은 다음입니다.

```text
invalid action rate = 0.000%
```

불법 명령이 한 번이라도 나오면 모델 문제가 아니라 `legal.py` 또는 decoder 문제입니다.

---

# 20. 실전 제출용 추론 구조

실전에서는 100ms 제한이 있으므로 학습 때 쓰던 무거운 sampling을 그대로 쓰면 안 됩니다. 

실전용은 다음처럼 단순화하세요.

```text
1. 현재 상태를 observation으로 변환
2. GNN forward 1회
3. TRAIN은 argmax
4. UPGRADE는 top-k greedy
5. MOVE group도 top-k greedy
6. validator 통과
7. COMMAND 출력
```

학습 때는 sampling을 사용합니다.

```python
action = policy.sample(obs)
```

실전에서는 deterministic하게 씁니다.

```python
action = policy.greedy(obs)
```

추론 중 MCTS나 rollout search를 붙이고 싶더라도, 처음 버전에서는 하지 마세요. 먼저 GNN policy만으로 100ms 안에 안정적으로 명령을 만드는 것이 우선입니다.

---

# 21. 실제 코드 작성 순서 상세

## Step 1. constants.py

```python
HQ_TABLE = {
    1: dict(upgrade_cost=None, max_unit_hp=4, max_hp=10, turret=1, train_cap=1, worker_cap=1),
    2: dict(upgrade_cost=600,  max_unit_hp=5, max_hp=15, turret=2, train_cap=1, worker_cap=2),
    3: dict(upgrade_cost=1200, max_unit_hp=6, max_hp=20, turret=2, train_cap=2, worker_cap=3),
    4: dict(upgrade_cost=2400, max_unit_hp=7, max_hp=25, turret=3, train_cap=2, worker_cap=4),
    5: dict(upgrade_cost=3600, max_unit_hp=8, max_hp=30, turret=3, train_cap=3, worker_cap=5),
}

HQ_REPAIR_COST = 1000

BASE_TABLE = {
    1: dict(cost=300,  max_hp=6,  turret=1, worker_cap=1),
    2: dict(cost=600,  max_hp=12, turret=1, worker_cap=2),
    3: dict(cost=1000, max_hp=18, turret=2, worker_cap=3),
}

BASE_REPAIR_COST = 500

TRAIN_COST = 120
MOVE_COST = 10
FOOD_COST = 2
INCOME_PER_WORKER = 15
MAX_DAYS = 200
```

## Step 2. map.py

구현:

```text
- 입력 맵 parsing
- edge length 계산
- all-pairs shortest path
- next_hop 계산
- canonical node 변환
```

테스트:

```text
- next_hop[u][u]는 None
- next_hop[u][v]는 u의 인접 노드
- RIGHT canonical 변환을 두 번 하면 원래 노드로 돌아옴
```

## Step 3. state.py

구현:

```text
- Unit
- Building
- PlayerState
- GameState
- clone()
- get_units_at(node, owner)
- get_idle_units_at(node, owner)
- get_hq(owner)
```

## Step 4. simulator.py

구현 순서:

```text
1. apply_upgrades()
2. apply_moves()
3. apply_training()
4. apply_combat()
5. apply_labor()
6. apply_supply()
7. check_terminal()
8. step(action_left, action_right)
```

전투는 포탑 공격 후 전사 간 공격, 그리고 체력 0인 전사/건물 제거를 정확히 처리해야 합니다. 전투 공격 대상은 체력이 0이 아닌 상대 전사 중 체력이 가장 낮은 전사, 동률이면 번호가 앞선 전사입니다. 전사가 없으면 상대 건물을 공격합니다. 

## Step 5. legal.py

구현:

```text
- legal_train_mask()
- legal_upgrade_mask_and_cost()
- legal_move_origins()
- legal_move_target_mask()
- legal_move_count_mask()
- validate_action()
```

`validate_action()`은 학습 중 항상 켜두세요.

```python
assert validate_action(state, player, action)
```

속도가 필요할 때만 제출용에서 일부 assert를 끕니다.

## Step 6. observation.py

구현:

```text
- build_node_features()
- build_edge_features()
- build_global_features()
- canonicalize_state()
- make_pyg_data()
```

처음에는 PyG `Data` 객체로 만드는 것을 추천합니다.

```python
data = Data(
    x=node_features,
    edge_index=edge_index,
    edge_attr=edge_features,
)
data.global_feat = global_features
```

## Step 7. random_agent.py

무조건 필요합니다.

```python
class RandomLegalAgent:
    def act(self, obs, state, player):
        train_n = random legal train
        upgrades = random legal upgrades under budget
        moves = random legal move groups under budget
        return action
```

이 agent의 목적은 강하게 두는 것이 아니라, simulator와 validator를 검증하는 것입니다.

## Step 8. env.py

```python
class NextNationEnv:
    def reset(self, map_seed=None):
        ...
        return obs_left, obs_right

    def step(self, action_left, action_right):
        assert validate_action(...)
        next_state = simulator.step(...)
        reward_left, reward_right = compute_rewards(...)
        done = next_state.done
        return obs_left, obs_right, reward_left, reward_right, done, info
```

## Step 9. model.py

```text
- GraphEncoder
- TrainHead
- UpgradeHead
- MoveTargetHead
- MoveCountHead
- ValueHead
```

## Step 10. action_dist.py

여기서 제일 많은 버그가 납니다.

구현해야 하는 함수:

```python
def sample_action(model, obs, state_context):
    ...

def evaluate_action(model, obs, action, state_context):
    ...
```

`sample_action()`과 `evaluate_action()`은 반드시 같은 순서로 log probability를 계산해야 합니다.

## Step 11. rollout_buffer.py

저장 항목:

```text
obs
action
reward
done
old_logp
old_value
return
advantage
```

## Step 12. ppo.py

구현:

```text
- compute_gae()
- ppo_update()
- advantage normalization
- gradient clipping
- KL monitoring
```

## Step 13. self_play.py

구현:

```text
- current vs current
- current vs snapshot
- current vs random legal
- opponent sampling
```

## Step 14. train.py

전체 학습 실행:

```python
policy = NextNationActorCritic(...)
optimizer = Adam(policy.parameters(), lr=3e-4)

league = League()
runner = SelfPlayRunner(policy, league)

for update in range(NUM_UPDATES):
    buffer = runner.collect_rollouts(
        num_envs=64,
        rollout_len=64,
    )

    stats = ppo_update(policy, optimizer, buffer)

    if update % 20 == 0:
        eval_stats = evaluate(policy, league.eval_opponents)

    if update % 50 == 0:
        league.add_snapshot(policy)
        save_checkpoint(policy, update)
```

---

# 22. 추천 초기 설정

처음 실험은 이렇게 시작하세요.

```text
Model:
  hidden_dim = 128
  gnn_layers = 4
  use_layer_norm = True
  use_residual = True

Action:
  MAX_UPGRADES_PER_TURN = 1
  MAX_MOVE_ORIGINS_PER_TURN = 3
  move_target_candidates = points + HQs + existing buildings

PPO:
  gamma = 0.997
  gae_lambda = 0.95
  lr = 3e-4
  clip_eps = 0.2
  entropy_coef = 0.01
  value_coef = 0.5
  ppo_epochs = 4
  rollout_len = 64
  num_envs = 64
  minibatch_size = 4096

Reward:
  terminal = ±1
  shaping_beta = 0.20

Self-play:
  first 100 updates: current vs current
  after that: current vs snapshots
```

성능이 좋아지면 다음처럼 늘립니다.

```text
hidden_dim = 256
gnn_layers = 6
MAX_UPGRADES_PER_TURN = 6
MAX_MOVE_ORIGINS_PER_TURN = 10
move_target_candidates = all nodes
shaping_beta = 0.03
horizon = 200
```

---

# 23. 구현할 때 가장 조심해야 할 부분

## 23.1 불법 행동 방지

신경망 출력은 절대 그대로 명령으로 내보내지 마세요.

```text
policy output
  ↓
legal mask
  ↓
sample / greedy
  ↓
validator
  ↓
COMMAND 출력
```

## 23.2 simulator와 실제 인터랙션 차이

학습 시뮬레이터는 모든 내부 상태를 압니다. 하지만 실제 경기에서는 상대의 장기 이동 목적지 같은 정보가 완전히 공개되지 않을 수 있습니다. 처음에는 full-state self-play로 학습해도 되지만, 최종 제출 전에는 observation builder가 실제로 알 수 있는 정보만 쓰는지 점검해야 합니다.

## 23.3 snapshot 데이터는 PPO 학습에 넣지 않기

PPO는 on-policy에 가깝습니다. 과거 snapshot이 고른 action을 current policy 업데이트에 사용하면 학습이 망가질 수 있습니다.

## 23.4 reward shaping 과다 사용 금지

모델이 “수입을 늘리는 것”만 배우고 공격을 안 할 수 있습니다. shaping은 점점 줄이고, 최종적으로는 승패 중심으로 fine-tuning하세요.

## 23.5 action space를 한 번에 열지 않기

처음부터 모든 node target, 모든 upgrade, 모든 unit move를 열면 탐색 공간이 너무 큽니다. 반드시 curriculum으로 늘리세요.

---

# 24. 가장 추천하는 최종 학습 플로우

최종적으로는 이 순서가 가장 안전합니다.

```text
[1] 규칙 시뮬레이터 구현
    ↓
[2] legal action mask / validator 구현
    ↓
[3] random legal self-play 10,000게임으로 검증
    ↓
[4] observation canonicalization 구현
    ↓
[5] GNN Actor-Critic 구현
    ↓
[6] 제한된 action space로 PPO 시작
    ↓
[7] reward shaping으로 초반 학습 안정화
    ↓
[8] current vs current self-play
    ↓
[9] snapshot league 추가
    ↓
[10] action space 확장
    ↓
[11] horizon 200일로 확장
    ↓
[12] shaping 줄이고 승패 중심 fine-tuning
    ↓
[13] greedy inference로 제출용 bot 제작
```

이 게임에서 제일 중요한 구현 원칙은 다음 한 문장입니다.

> **신경망은 전략적 선택만 하고, 규칙 준수는 100% 코드가 책임져야 한다.**

그 구조만 지키면, GNN Actor-Critic + Self-Play PPO는 NEXT NATION처럼 가변 그래프 전장, 장기 경제 운영, 동시 명령 제출, 복합 action을 가진 게임에 가장 현실적인 학습 방식입니다.

[1]: https://arxiv.org/abs/1707.06347?utm_source=chatgpt.com "Proximal Policy Optimization Algorithms"
[2]: https://arxiv.org/abs/1812.08434?utm_source=chatgpt.com "Graph Neural Networks: A Review of Methods and Applications"
[3]: https://pytorch-geometric.readthedocs.io/?utm_source=chatgpt.com "PyTorch Geometric - Read the Docs"
[4]: https://arxiv.org/abs/1506.02438?utm_source=chatgpt.com "High-Dimensional Continuous Control Using Generalized Advantage Estimation"
