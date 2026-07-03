"""PPO update utilities."""

from __future__ import annotations

from typing import Callable


def ppo_update(
    policy,
    optimizer,
    buffer,
    evaluate_fn: Callable,
    *,
    clip_eps: float = 0.2,
    entropy_coef: float = 0.01,
    value_coef: float = 0.5,
    max_grad_norm: float = 0.5,
) -> dict[str, float]:
    """Run one PPO minibatch update over a populated RolloutBuffer.

    evaluate_fn(policy, observations, actions) must return
    (new_logp, values, entropy) tensors in the same item order as the
    buffer. Keeping evaluation model-specific avoids baking one action-space
    layout into this generic trainer utility.
    """
    try:
        import torch
    except ImportError as exc:
        raise ImportError("ppo_update requires torch") from exc

    items = [item for item in buffer.items if item.return_ is not None and item.advantage is not None]
    if not items:
        raise ValueError("buffer has no items with returns and advantages")

    observations = [item.obs for item in items]
    actions = [item.action for item in items]
    old_logp = torch.tensor([item.old_logp for item in items], dtype=torch.float32)
    old_value = torch.tensor([item.old_value for item in items], dtype=torch.float32)
    returns = torch.tensor([float(item.return_) for item in items], dtype=torch.float32)
    advantages = torch.tensor([float(item.advantage) for item in items], dtype=torch.float32)
    advantages = (advantages - advantages.mean()) / advantages.std(unbiased=False).clamp_min(1e-8)

    new_logp, values, entropy = evaluate_fn(policy, observations, actions)
    new_logp = new_logp.reshape_as(old_logp)
    values = values.reshape_as(returns)
    entropy = entropy.reshape_as(old_logp)

    ratio = torch.exp(new_logp - old_logp)
    unclipped = ratio * advantages
    clipped = torch.clamp(ratio, 1.0 - clip_eps, 1.0 + clip_eps) * advantages
    policy_loss = -torch.minimum(unclipped, clipped).mean()
    value_loss = 0.5 * (returns - values).pow(2).mean()
    entropy_loss = -entropy.mean()
    loss = policy_loss + value_coef * value_loss + entropy_coef * entropy_loss

    optimizer.zero_grad(set_to_none=True)
    loss.backward()
    torch.nn.utils.clip_grad_norm_(policy.parameters(), max_grad_norm)
    optimizer.step()

    with torch.no_grad():
        approx_kl = (old_logp - new_logp).mean()
        clip_fraction = ((ratio - 1.0).abs() > clip_eps).float().mean()

    return {
        "loss": float(loss.detach().cpu()),
        "policy_loss": float(policy_loss.detach().cpu()),
        "value_loss": float(value_loss.detach().cpu()),
        "entropy": float(entropy.mean().detach().cpu()),
        "approx_kl": float(approx_kl.detach().cpu()),
        "clip_fraction": float(clip_fraction.detach().cpu()),
        "old_value_mean": float(old_value.mean().detach().cpu()),
    }
