"""Small dependency-light GNN encoder."""

from __future__ import annotations


def _require_torch():
    try:
        import torch
        from torch import nn
    except ImportError as exc:
        raise ImportError("nextnation_ai.nn requires torch") from exc
    return torch, nn


def build_encoder(node_dim: int, edge_dim: int, hidden_dim: int = 128, layers: int = 4):
    torch, nn = _require_torch()

    class GraphEncoder(nn.Module):
        def __init__(self) -> None:
            super().__init__()
            self.node_proj = nn.Linear(node_dim, hidden_dim)
            self.edge_proj = nn.Linear(edge_dim, hidden_dim)
            self.layers = nn.ModuleList(
                nn.Sequential(
                    nn.LayerNorm(hidden_dim),
                    nn.Linear(hidden_dim, hidden_dim),
                    nn.ReLU(),
                    nn.Linear(hidden_dim, hidden_dim),
                )
                for _ in range(layers)
            )

        def forward(self, x, edge_index, edge_attr):
            h = self.node_proj(x)
            e = self.edge_proj(edge_attr)
            src, dst = edge_index
            for layer in self.layers:
                msg = h[src] + e
                agg = torch.zeros_like(h)
                agg.index_add_(0, dst, msg)
                deg = torch.zeros(h.size(0), device=h.device, dtype=h.dtype)
                deg.index_add_(0, dst, torch.ones(dst.numel(), device=h.device, dtype=h.dtype))
                agg = agg / deg.clamp_min(1.0).unsqueeze(-1)
                h = h + layer(agg)
            return h

    return GraphEncoder()
