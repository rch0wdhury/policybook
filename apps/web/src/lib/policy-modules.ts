/**
 * Loading a policy's real implementation into the browser.
 *
 * The runner runs the *same* code the tests and benchmarks run — not a
 * reimplementation, not a simplified version for display. `policies/**` is
 * globbed at build time, so each policy's `index.ts` is bundled and reachable
 * by its id.
 *
 * Lazy, so a page loading one policy does not ship thirty.
 */

/** `../../../policies/<domain>/<slug>/index.ts`, relative to this file. */
const modules = import.meta.glob<{ default: new (params: never) => unknown }>(
  "../../../../policies/*/*/index.ts",
);

/**
 * The tutorial's worked examples, loadable as `tutorial/<name>`.
 *
 * They are not registry policies — no paper, no vectors, and `policybook add`
 * will not fetch them — but the tutorial has to *run* the example it shows, or
 * step five is a code listing rather than a demonstration. Globbing them here
 * means the runner loads the same file the page displays.
 */
const examples = import.meta.glob<{ default: new (params: never) => unknown }>([
  "../tutorial/*.ts",
  // Tests are not examples. Without this the glob matched `*.test.ts` too and
  // shipped it — 85 KB of vitest and assertions, in the site, reachable as a
  // policy called `tutorial/evict-newest.test`. The total-JS budget now fails
  // on that rather than leaving it to be noticed.
  "!../tutorial/*.test.ts",
]);

/** `cache/sieve` from a glob key, or `tutorial/evict-newest`. */
function idOf(path: string): string {
  const policy = /policies\/([^/]+)\/([^/]+)\/index\.ts$/.exec(path);
  if (policy !== null) return `${policy[1]}/${policy[2]}`;

  const example = /tutorial\/([^/]+)\.ts$/.exec(path);
  if (example !== null) return `tutorial/${example[1]}`;

  return path;
}

/** Every policy id the browser can load, for a clear error message. */
export function loadablePolicies(): string[] {
  return [...Object.keys(modules), ...Object.keys(examples)].map(idOf).sort();
}

/**
 * The constructor for one policy, by id.
 *
 * Throws with the list of what *is* available rather than a bare undefined:
 * a URL naming a policy that does not exist is a link someone typed, and the
 * useful response is to say what they might have meant.
 */
export async function loadPolicy(
  id: string,
): Promise<(params: Record<string, unknown>) => unknown> {
  const entry = [...Object.entries(modules), ...Object.entries(examples)].find(
    ([path]) => idOf(path) === id,
  );
  if (entry === undefined) {
    throw new Error(
      `no policy "${id}". Available: ${loadablePolicies().join(", ")}`,
    );
  }

  const module = await entry[1]();
  const Policy = module.default;
  return (params) => new Policy(params as never);
}
