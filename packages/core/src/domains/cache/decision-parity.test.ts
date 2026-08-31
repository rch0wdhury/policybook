/**
 * The TypeScript ports must still produce the committed decision streams.
 *
 * A drift here means a policy's decisions changed: either intentionally (rerun
 * `pnpm tsx scripts/gen-decision-parity.ts`, and expect bench.json and the
 * Python/C suites to move with it) or not (a bug the vectors did not reach).
 */

import { readFileSync } from "node:fs";
import { join } from "node:path";

import { describe, expect, it } from "vitest";

import { discoverPolicies, findRepoRoot, loadFactory } from "../../../../vectors/src/discover";
import {
  CHURN_STREAMS,
  driveCachePolicy,
  generateChurnStream,
} from "./decision-parity";
import type { DecisionRecord } from "./decision-parity";
import { CACHE_TRACES, generateCacheTrace } from "./traces";

const repoRoot = findRepoRoot();
const artifact = JSON.parse(
  readFileSync(join(repoRoot, "packages/core/src/domains/cache/decision-parity.json"), "utf8"),
) as { policies: Record<string, Record<string, DecisionRecord>> };

const policies = discoverPolicies(repoRoot).filter((policy) => policy.domain === "cache");

describe("cache decision parity", () => {
  it("records every cache policy", () => {
    expect(Object.keys(artifact.policies).sort()).toEqual(
      policies.map((policy) => policy.id).sort(),
    );
  });

  for (const policy of policies) {
    it(`${policy.id} reproduces its committed decision stream`, async () => {
      const factory = await loadFactory(policy.entry);
      const wantsFuture = policy.meta.params.some((param) => param.name === "future");
      const expected = artifact.policies[policy.id];
      expect(expected).toBeDefined();

      const run = (trace: Uint32Array, capacity: number, keyUniverse: number) => {
        const params: Record<string, unknown> = { capacity };
        if (wantsFuture) params["future"] = Array.from(trace);
        const driven = factory(params as never, undefined as never);
        return driveCachePolicy(driven as never, trace, capacity, keyUniverse);
      };

      for (const traceId of Object.keys(CACHE_TRACES)) {
        const spec = CACHE_TRACES[traceId]!;
        expect(
          run(generateCacheTrace(traceId), spec.capacity, spec.keyUniverse),
          `${policy.id} on ${traceId}`,
        ).toEqual(expected![traceId]);
      }
      for (const stream of CHURN_STREAMS) {
        expect(
          run(generateChurnStream(stream), stream.capacity, stream.keyUniverse),
          `${policy.id} on ${stream.id}`,
        ).toEqual(expected![stream.id]);
      }
    });
  }
});
