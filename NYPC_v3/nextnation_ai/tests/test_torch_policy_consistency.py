"""PPO invariant: sample-time logp must equal re-evaluated logp exactly.

Skipped silently when torch is unavailable (the game/simulator tests do not
require it).
"""

from __future__ import annotations

try:
    import torch
    _TORCH_AVAILABLE = True
except ImportError:  # pragma: no cover
    _TORCH_AVAILABLE = False


def test_sampled_logp_matches_evaluated_logp():
    if not _TORCH_AVAILABLE:
        return

    import random

    from nextnation_ai.agents.random_agent import RandomLegalAgent
    from nextnation_ai.game.env import NextNationEnv
    from nextnation_ai.game.map import make_line_map
    from nextnation_ai.game.state import Side
    from nextnation_ai.nn import torch_policy
    from nextnation_ai.nn.torch_policy import (
        NextNationGNNPolicy,
        evaluate_components,
        fallback_count,
        select_action,
    )

    torch.manual_seed(0)
    random.seed(0)
    device = torch.device("cpu")
    model = NextNationGNNPolicy()
    env = NextNationEnv(make_line_map(51))
    env.reset()
    opponent = RandomLegalAgent(seed=3)

    fallbacks_before = fallback_count()
    for _ in range(20):
        state = env.state
        decision = select_action(model, state, Side.LEFT, device, deterministic=False)
        evaluated = evaluate_components(model, state, Side.LEFT, decision.components, device)
        sampled_logp = float(decision.logp.detach().cpu())
        evaluated_logp = float(evaluated.logp.detach().cpu())
        assert abs(sampled_logp - evaluated_logp) < 1e-6, (
            f"logp mismatch: sampled={sampled_logp} evaluated={evaluated_logp} "
            f"components={decision.components}"
        )
        assert not decision.fallback
        step = env.step(decision.action, opponent.act(state, Side.RIGHT))
        if step.done:
            env.reset()

    # With correct legal masks the degradation path must never trigger.
    assert fallback_count() == fallbacks_before


def test_batched_evaluation_matches_sequential():
    """The hoisted/batched PPO path must reproduce evaluate_components exactly
    (fp32 batching noise aside), including the imitation CE, across mixed map
    sizes in one padded batch."""
    if not _TORCH_AVAILABLE:
        return

    import random

    from nextnation_ai.agents.random_agent import RandomLegalAgent
    from nextnation_ai.game.env import NextNationEnv
    from nextnation_ai.game.map import make_line_map
    from nextnation_ai.game.state import Side
    from nextnation_ai.nn.torch_policy import (
        NextNationGNNPolicy,
        action_components_from_action,
        evaluate_components,
        evaluate_prepared_batch,
        prepare_evaluation_batch,
        select_action,
    )

    torch.manual_seed(1)
    random.seed(1)
    device = torch.device("cpu")
    model = NextNationGNNPolicy()
    manual_source = RandomLegalAgent(seed=7, upgrade_probability=0.5, move_probability=0.6)

    items = []
    for n in (51, 61):  # two map sizes in one batch exercises the padding
        env = NextNationEnv(make_line_map(n))
        env.reset()
        opponent = RandomLegalAgent(seed=n)
        for step_index in range(8):
            state = env.state
            decision = select_action(model, state, Side.LEFT, device, deterministic=False)
            manual_comp = None
            if step_index % 2 == 0:  # mix present/absent imitation targets
                manual_action = manual_source.act(state, Side.LEFT)
                manual_comp = action_components_from_action(state, Side.LEFT, manual_action)
            items.append((state.clone(), Side.LEFT, decision.components, manual_comp))
            step = env.step(decision.action, opponent.act(state, Side.RIGHT))
            if step.done:
                env.reset()

    batch = prepare_evaluation_batch(items, device)
    with torch.no_grad():
        batched = evaluate_prepared_batch(model, batch)

    for i, (state, player, comp, manual_comp) in enumerate(items):
        with torch.no_grad():
            seq = evaluate_components(model, state, player, comp, device, manual_comp)
        assert abs(float(seq.logp) - float(batched.logp[i])) < 1e-4, f"logp mismatch at {i}"
        assert abs(float(seq.entropy) - float(batched.entropy[i])) < 1e-4, f"entropy mismatch at {i}"
        assert abs(float(seq.value) - float(batched.value[i])) < 1e-4, f"value mismatch at {i}"
        if seq.imitation_loss is None:
            assert float(batched.imitation_present[i]) == 0.0
            assert float(batched.imitation_per_item[i]) == 0.0
        else:
            assert float(batched.imitation_present[i]) == 1.0
            assert abs(float(seq.imitation_loss) - float(batched.imitation_per_item[i])) < 1e-4, f"imitation mismatch at {i}"
