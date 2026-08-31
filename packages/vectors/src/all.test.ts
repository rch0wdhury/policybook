/**
 * Runs every policy in the registry against its own vectors.
 *
 * This is the suite that makes vectors binding: adding a policy directory adds
 * a test, with nothing to register. It passes trivially while the catalog is
 * empty, and grows a case per policy from T10 onward.
 */

import { existsSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { discoverPolicies, loadFactory, loadVectors } from "./discover";
import { formatFailures, runVectors } from "./run";

const policies = discoverPolicies();

describe("registry", () => {
  it("reads the policy catalog", () => {
    expect(Array.isArray(policies)).toBe(true);
  });

  if (policies.length === 0) {
    it("has no policies yet", () => {
      // The catalog fills in from T10. Until then this suite exists to prove
      // discovery works and to fail loudly the moment a policy is added
      // without vectors.
      expect(policies).toEqual([]);
    });
  }

  for (const policy of policies) {
    if (!policy.meta.ports.includes("ts")) continue;

    describe(policy.id, () => {
      it("has a vectors.json", () => {
        expect(existsSync(policy.vectorsPath)).toBe(true);
      });

      it("passes its vectors", async () => {
        const vectors = loadVectors(policy.vectorsPath);
        expect(vectors.policy).toBe(policy.id);

        const factory = await loadFactory(policy.entry);
        const result = runVectors(factory, vectors);

        if (result.failures.length > 0) throw new Error(formatFailures(result));
        expect(result.assertionsRun).toBeGreaterThan(0);
      });
    });
  }
});
