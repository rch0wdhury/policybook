/**
 * The `kv-cache` domain: which tokens to forget during LLM decoding.
 *
 * A transformer's KV cache grows by one entry per token, per layer, per head.
 * At a 4,096-token context that is gigabytes, and the cost is linear in the
 * sequence while the value of any individual token is not — most attention
 * lands on a handful of positions. Dropping the rest is what makes long
 * contexts affordable, and *which* to drop is this domain.
 *
 * The policy sees, at each decode step, how much attention the current token
 * paid to each position it still holds. From that it must name positions to
 * evict when the budget is exceeded. It never sees the tensors themselves, only
 * the weights — a policy that needed the values could not run inside an
 * inference server's memory budget, which is the whole point.
 *
 * **Positions are absolute token indices**, not offsets into the kept set. A
 * policy that renumbered on eviction would be unable to say anything about
 * where a token sits in the sequence, and nearly every policy here cares:
 * attention sinks are the *first* few positions, recency windows are the *last*
 * few.
 */

/** Every kv-cache policy implements this, and nothing more. */
export interface KvCachePolicy {
  /**
   * Called once per decode step, before any eviction.
   *
   * `pos` is the position of the token being generated. `attn` holds the
   * attention weights the policy's kept positions received, in ascending
   * position order — so `attn[i]` belongs to the *i*-th position the policy
   * still holds, and the policy is expected to know its own kept order.
   *
   * **The kept set starts as `{0}`.** Position 0's token exists before the
   * first decode step, so the very first call is `onDecodeStep(1, attn)` with
   * one weight — position 0's — and a policy must initialise its bookkeeping
   * with position 0 already held.
   *
   * `attn` may be null for a policy that does not read attention at all
   * (a sliding window does not). Passing null rather than an ignored array is
   * what lets those policies run without the harness materialising anything.
   *
   * **The weights are not renormalised** after eviction. They are the model's
   * own attention over the full sequence, restricted to what the policy kept,
   * so they sum to less than one by exactly the mass already discarded. That is
   * deliberate: a policy that renormalised would lose the signal that it is
   * dropping mass, and the harness's `retainedAttentionMass` metric measures
   * precisely that loss.
   */
  onDecodeStep(pos: number, attn: Float32Array | null): void;

  /**
   * Called when the kept set exceeds `budget`. Returns positions to drop.
   *
   * The returned positions must currently be held, and removing them must
   * bring the kept set to `budget` or below. The harness treats anything else
   * as a bug in the policy rather than working around it.
   *
   * **The caller consumes the array before calling the policy again**, so a
   * policy may return a buffer it reuses rather than allocating one per
   * eviction. That is what keeps these implementations allocation-free on the
   * decode path, and it mirrors the C vtable, where the caller supplies the
   * buffer outright. Callers that need to keep the positions must copy them.
   */
  evict(budget: number): number[];
}

/** Every kv-cache policy takes at least a budget. */
export interface KvCacheParams {
  /** Maximum positions retained. */
  budget: number;
}

/**
 * The budgets every canonical benchmark runs.
 *
 * A 4,096-token sequence held at 256, 512 and 1,024 — from a sixteenth of the
 * context to a quarter. Below a sixteenth every policy is mostly noise; above a
 * quarter almost nothing is evicted and they all look alike.
 */
export const KV_CACHE_BUDGETS = [256, 512, 1024] as const;
