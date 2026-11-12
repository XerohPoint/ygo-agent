"""Edison policy/value network with real card-identity embeddings.

A pure function of one player's public observation: no recurrent state, no hidden
information and no opponent deck. The value path never touches the action path, so a
search leaf can be evaluated without encoding actions.

Rows: identity embedding + categorical columns + binned ATK/DEF + type bits +
selection_ flags. Context token: global_ + decision_ + own action history. A small
transformer runs over context and rows. An action is scored from its fields, its
card's identity, the transformed row it points at, and the state.
"""
from dataclasses import asdict, dataclass

import flax.linen as nn
import jax
import jax.numpy as jnp
import numpy as np

from obs import HINT_MIN, N_HINTS


@dataclass(frozen=True)
class NetConfig:
    n_codes: int = 509
    id_dim: int = 48
    channels: int = 96
    layers: int = 2
    heads: int = 4
    critic_hidden: int = 128
    # Code list v2: one more row after the code list, read by every card the network does not know by identity.
    # It also widens three action tables to every value the env can emit: message 17, and any byte in the
    # number and place columns, which declarations use. Values no training list raises keep their initial rows.
    unknown_row: bool = False

    def dict(self):
        return asdict(self)


BIN_POINTS = np.concatenate([np.linspace(0, 8000, 25, dtype=np.float32)[1:],
                             np.linspace(8000, 12000, 9, dtype=np.float32)[1:]])
BIN_WIDTHS = np.concatenate([BIN_POINTS[:1], BIN_POINTS[1:] - BIN_POINTS[:-1]])


def thermometer(high, low):
    """Encode a two-byte value. The top of the range (unknown "?" ATK/DEF) has its own bit."""
    value = high.astype(jnp.float32) * 256.0 + low.astype(jnp.float32)
    unknown = (value > 60000.0).astype(jnp.float32)[..., None]
    known = jnp.where(value > 60000.0, 0.0, value)[..., None]
    bins = jnp.clip((known - BIN_POINTS + BIN_WIDTHS) / BIN_WIDTHS, 0.0, 1.0)
    return jnp.concatenate([bins, unknown], axis=-1)


class Attention(nn.Module):
    channels: int
    heads: int

    @nn.compact
    def __call__(self, x, mask):
        batch, length, _ = x.shape
        size = self.channels // self.heads
        qkv = nn.Dense(self.channels * 3, use_bias=False)(x)
        qkv = qkv.reshape(batch, length, 3, self.heads, size).transpose(2, 0, 3, 1, 4)
        q, k, v = qkv[0], qkv[1], qkv[2]
        score = jnp.einsum("bhmd,bhnd->bhmn", q, k) / jnp.sqrt(float(size))
        score = jnp.where(mask[:, None, None, :], score, -1e9)
        out = jnp.einsum("bhmn,bhnd->bhmd", jax.nn.softmax(score, axis=-1), v)
        return nn.Dense(self.channels, use_bias=False)(out.transpose(0, 2, 1, 3).reshape(batch, length, -1))


class Block(nn.Module):
    channels: int
    heads: int

    @nn.compact
    def __call__(self, x, mask):
        x = x + Attention(self.channels, self.heads)(nn.LayerNorm()(x), mask)
        y = nn.Dense(self.channels * 2)(nn.LayerNorm()(x))
        return x + nn.Dense(self.channels)(jax.nn.gelu(y))


WIDE_ACTION_TABLES = {"msg": (17, 18), "number": (14, 256), "place": (32, 256)}  # name: (code list v1, v2)


def action_fields(a, prefix, wide=False):
    """Embed the categorical fields shared by offered and historical actions."""
    embed = lambda n, d, x, name: nn.Embed(n, d, name=f"{prefix}_{name}", embedding_init=nn.initializers.normal(0.1))(x.astype(jnp.int32))
    size = lambda name: WIDE_ACTION_TABLES[name][int(wide)]
    return jnp.concatenate([
        embed(size("msg"), 8, a[..., 3], "msg"), embed(10, 8, a[..., 4], "act"), embed(2, 2, a[..., 5], "finish"),
        embed(256, 16, a[..., 6], "effect"), embed(4, 4, a[..., 7], "phase"), embed(9, 4, a[..., 8], "position"),
        embed(size("number"), 4, a[..., 9], "number"), embed(size("place"), 8, a[..., 10], "place"), embed(10, 4, a[..., 11], "attrib"),
    ], axis=-1)


class Encoder(nn.Module):
    config: NetConfig

    @nn.compact
    def __call__(self, batch, id_table):
        cfg = self.config
        c = cfg.channels
        cards, row_mask, hist, decision, glob = (batch["cards"], batch["row_mask"], batch["hist"],
                                                 batch["decision"], batch["global"])
        embed = lambda n, d, x, name: nn.Embed(n, d, name=name, embedding_init=nn.initializers.normal(0.1))(x.astype(jnp.int32))

        # Card rows.
        ids = (cards[..., 0].astype(jnp.int32) << 8) + cards[..., 1]
        row = jnp.concatenate([
            id_table[ids], embed(9, 6, cards[..., 2], "loc"), embed(80, 6, cards[..., 3], "seq"),
            embed(2, 2, cards[..., 4], "owner"), embed(9, 4, cards[..., 5], "pos"), embed(2, 2, cards[..., 6], "overlay"),
            embed(8, 4, cards[..., 7], "attr"), embed(27, 4, cards[..., 8], "race"), embed(14, 4, cards[..., 9], "level"),
            embed(16, 4, cards[..., 10], "counter"), embed(2, 2, cards[..., 11], "negated"),
            thermometer(cards[..., 12], cards[..., 13]), thermometer(cards[..., 14], cards[..., 15]),
            cards[..., 16:41].astype(jnp.float32), cards[..., 41:45].astype(jnp.float32)], axis=-1)
        row = nn.Dense(c)(jax.nn.gelu(nn.Dense(c)(row)))

        # Global and decision context.
        hint = decision[:, 1]
        hint_index = jnp.where(hint == 0, 0, hint - HINT_MIN + 1)
        scaled = jnp.stack([decision[:, 4] / 5.0, decision[:, 5] / 5.0, decision[:, 6] / 5.0, decision[:, 7] / 5.0,
                            decision[:, 8].astype(jnp.float32), decision[:, 9].astype(jnp.float32),
                            decision[:, 10] / 12.0, decision[:, 11].astype(jnp.float32),
                            (decision[:, 3] > 0).astype(jnp.float32)], axis=-1)
        counts = glob[:, 8:22].astype(jnp.float32) / 40.0
        context = jnp.concatenate([
            thermometer(glob[:, 0], glob[:, 1]), thermometer(glob[:, 2], glob[:, 3]),
            embed(17, 6, glob[:, 4], "turn"), embed(11, 6, glob[:, 5], "phase"),
            embed(2, 2, glob[:, 6], "first"), embed(2, 2, glob[:, 7], "my_turn"), counts,
            embed(N_HINTS, 12, hint_index, "hint"), embed(4, 4, decision[:, 2], "stage"), scaled], axis=-1)

        # Own recent actions. A pooled view: order is given by the turns-ago field.
        h_ids = (hist[..., 1].astype(jnp.int32) << 8) + hist[..., 2]
        h_valid = (hist[..., 3] != 0)[..., None].astype(jnp.float32)
        h = jnp.concatenate([action_fields(hist, "hist", cfg.unknown_row), id_table[h_ids],
                             embed(17, 4, hist[..., 12], "h_turns"), embed(11, 4, hist[..., 13], "h_phase")], axis=-1)
        h = jax.nn.gelu(nn.Dense(c)(h)) * h_valid
        h_pool = h.sum(axis=1) / jnp.maximum(h_valid.sum(axis=1), 1.0)

        token = nn.Dense(c)(jax.nn.gelu(nn.Dense(c)(jnp.concatenate([context, h_pool], axis=-1))))
        x = jnp.concatenate([token[:, None, :], row], axis=1)
        mask = jnp.concatenate([jnp.ones_like(row_mask[:, :1]), row_mask], axis=1)
        for _ in range(cfg.layers):
            x = Block(c, cfg.heads)(x, mask)
        x = nn.LayerNorm()(x)
        none_row = self.param("no_row", nn.initializers.normal(0.02), (c,))
        # Index 0 is "no card". The context token sits there, so replace it for pointer use.
        rows = jnp.concatenate([jnp.broadcast_to(none_row, (x.shape[0], 1, c)), x[:, 1:]], axis=1)
        source = rows[jnp.arange(x.shape[0]), decision[:, 3]]
        pooled = (x[:, 1:] * row_mask[..., None]).sum(axis=1) / jnp.maximum(row_mask.sum(axis=1, keepdims=True), 1)
        state = nn.Dense(c)(jax.nn.gelu(nn.Dense(c * 2)(jnp.concatenate([x[:, 0], source, pooled], axis=-1))))
        return state, rows


class EdisonNet(nn.Module):
    config: NetConfig

    @nn.compact
    def __call__(self, batch, value_only=False):
        cfg = self.config
        c = cfg.channels
        id_table = self.param("card_id", nn.initializers.normal(0.1), (cfg.n_codes + 1 + int(cfg.unknown_row), cfg.id_dim))
        state, rows = Encoder(cfg)(batch, id_table)
        # The critic starts at zero and the actor uniform, as the NumPy baseline does.
        value = nn.Dense(1, kernel_init=nn.initializers.zeros, name="value_out")(
            jax.nn.gelu(nn.Dense(cfg.critic_hidden, name="value_hidden")(state)))
        value = jnp.tanh(value)[:, 0]
        if value_only:
            return value
        actions, segments = batch["actions"], batch["segments"]
        size = state.shape[0]
        valid = segments < size
        seg = jnp.minimum(segments, size - 1)
        ids = (actions[:, 1].astype(jnp.int32) << 8) + actions[:, 2]
        fields = action_fields(actions, "action", cfg.unknown_row)
        pointed = rows[seg, actions[:, 0].astype(jnp.int32)]
        a = jax.nn.gelu(nn.Dense(c, name="action_in")(jnp.concatenate([fields, id_table[ids], pointed], axis=-1)))
        # Compare options through the mean offered action in the same decision.
        weights = valid[..., None].astype(a.dtype)
        total = jax.ops.segment_sum(a * weights, seg, num_segments=size)
        count = jax.ops.segment_sum(weights, seg, num_segments=size)
        pool = nn.Dense(c, name="action_pool")(total / jnp.maximum(count, 1.0))
        s = nn.Dense(c, name="action_state")(state)[seg]
        hidden = jax.nn.gelu(nn.Dense(c, name="logit_hidden")(jnp.concatenate([a, s, a * s, pool[seg]], axis=-1)))
        logits = nn.Dense(1, kernel_init=nn.initializers.zeros, name="logit_out")(hidden)[:, 0]
        return logits, value


def dummy_batch(config, batch=1, rows=48, actions=16):
    cards = np.zeros((batch, rows, 45), dtype=np.uint8)
    return {"cards": cards, "row_mask": np.ones((batch, rows), dtype=bool),
            "actions": np.zeros((actions, 12), dtype=np.uint8), "segments": np.zeros(actions, dtype=np.int32),
            "hist": np.zeros((batch, 16, 14), dtype=np.uint8), "decision": np.zeros((batch, 12), dtype=np.int32),
            "global": np.zeros((batch, 23), dtype=np.uint8)}


def init_params(config, seed):
    model = EdisonNet(config)
    return model, model.init(jax.random.PRNGKey(seed), dummy_batch(config))["params"]


def param_count(params):
    return sum(int(np.prod(x.shape)) for x in jax.tree_util.tree_leaves(params))


def segment_log_softmax(logits, segments, size):
    """Log-probabilities over offered actions, with padded entries in segment `size`."""
    top = jax.ops.segment_max(jax.lax.stop_gradient(logits), segments, num_segments=size + 1)
    shifted = logits - top[segments]
    total = jax.ops.segment_sum(jnp.exp(shifted), segments, num_segments=size + 1)
    return shifted - jnp.log(total)[segments]
