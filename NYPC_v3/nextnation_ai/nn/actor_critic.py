"""Actor-critic model skeleton for canonical graph observations."""

from __future__ import annotations

from .gnn_encoder import _require_torch, build_encoder


def build_actor_critic(
    node_dim: int,
    edge_dim: int,
    global_dim: int,
    hidden_dim: int = 128,
    layers: int = 4,
):
    torch, nn = _require_torch()

    class NextNationActorCritic(nn.Module):
        def __init__(self) -> None:
            super().__init__()
            self.encoder = build_encoder(node_dim, edge_dim, hidden_dim, layers)
            self.global_proj = nn.Sequential(nn.Linear(global_dim, hidden_dim), nn.ReLU())
            self.train_head = nn.Linear(hidden_dim, 4)
            self.upgrade_head = nn.Linear(hidden_dim, 1)
            self.move_target_head = nn.Linear(hidden_dim, 1)
            self.move_count_head = nn.Linear(hidden_dim, 8)
            self.value_head = nn.Sequential(
                nn.Linear(hidden_dim * 2, hidden_dim),
                nn.ReLU(),
                nn.Linear(hidden_dim, 1),
            )

        def forward(self, data):
            node_h = self.encoder(data.x, data.edge_index, data.edge_attr)
            pooled = node_h.mean(dim=0, keepdim=True)
            global_h = self.global_proj(data.global_feat.view(1, -1))
            context = pooled + global_h
            return {
                "node_h": node_h,
                "train_logits": self.train_head(context).squeeze(0),
                "upgrade_logits": self.upgrade_head(node_h).squeeze(-1),
                "move_target_logits": self.move_target_head(node_h).squeeze(-1),
                "move_count_logits": self.move_count_head(context).squeeze(0),
                "value": self.value_head(torch.cat([pooled, global_h], dim=-1)).squeeze(-1),
            }

    return NextNationActorCritic()
