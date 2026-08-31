/**
 * `policybook new` — the files a policy needs, with the questions already asked.
 *
 * The scaffold is opinionated on purpose. Every TODO marks something the
 * catalog validator will reject if it is left blank, so a contributor finds out
 * what is missing by running `policybook check` rather than by reading
 *
 */

/** The files a new policy starts with. */
export interface Scaffold {
  path: string;
  contents: string;
}

/** `w-tinylfu` → `WTinyLfu`, a reasonable class name to start from. */
function className(name: string): string {
  return name
    .split(/[-_]/)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join("");
}

export function scaffoldPolicy(domain: string, name: string): Scaffold[] {
  const id = `${domain}/${name}`;
  const klass = className(name);

  const policyJson = {
    id,
    name: klass,
    domain,
    summary: "TODO: one sharp sentence. This is the card text and the CLI row.",
    source: { type: "folklore" },
    complexity: { time: "TODO", space: "TODO" },
    params: [
      {
        name: "capacity",
        type: "number",
        default: 1000,
        description: "Maximum number of entries held.",
      },
    ],
    tags: [],
    recommended: false,
    ports: ["ts"],
    notes: null,
    status: "experimental",
  };

  return [
    {
      path: "policy.json",
      contents: JSON.stringify(policyJson, null, 2) + "\n",
    },
    {
      path: "index.ts",
      contents: `/**
 * ${klass} — TODO: one paragraph in plain language.
 *
 * Say what the policy does and, more importantly, what bet it is making. A
 * reader deciding between this and its neighbour needs the idea, not the code.
 */

export interface ${klass}Params {
  /** Maximum number of entries held. */
  capacity: number;
}

const DEFAULT_CAPACITY = 1000;

export default class ${klass}<K> {
  private readonly capacity: number;

  constructor(params: Partial<${klass}Params> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(\`${klass}: capacity must be a positive integer, received \${capacity}\`);
    }
    this.capacity = capacity;
  }

  onAccess(key: K, hit: boolean): void {
    // TODO
  }

  evict(): K {
    // TODO
    throw new Error("${klass}: not implemented");
  }
}
`,
    },
    {
      path: "vectors.gen.ts",
      contents: `/**
 * Scenario script for ${id}.
 *
 * A step is either \`capture: true\` — record what the implementation does — or
 * \`expect: <value>\`, reasoned from the paper and *verified* against the
 * implementation. The generator refuses to write the file when a hand-authored
 * expectation disagrees, which is what stops an implementation and its vectors
 * sharing the same bug.
 *
 * Regenerate with: pnpm gen:vectors ${id}
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "${id}",
  cases: [
    {
      name: "smoke: TODO",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "evict", expect: "a" },
      ],
    },
    {
      name: "boundary: TODO",
      params: { capacity: 1 },
      seed: 1,
      steps: [{ call: "onAccess", args: ["a", false] }],
    },
    {
      name: "distinguishing: TODO — what separates this from its nearest neighbour",
      params: { capacity: 3 },
      seed: 1,
      steps: [{ call: "onAccess", args: ["a", false] }],
    },
    {
      name: "tiebreak: TODO — what happens when two entries look equal",
      params: { capacity: 3 },
      seed: 1,
      steps: [{ call: "onAccess", args: ["a", false] }],
    },
  ],
};

export default scenarios;
`,
    },
    {
      path: "README.md",
      contents: `# ${klass}

TODO: one paragraph in plain language. What does it do, and what bet is it
making about the workload?

## When to use it

- TODO: three to five bullets, naming the workload shapes where it wins.

## When not to use it

- TODO: three to five bullets. Be specific — failure modes, workloads where it
  loses, operational costs. A reader deciding against a policy is as well served
  as one deciding for it, and the catalog validator rejects an empty section.

## How it works

TODO: short prose plus the core loop in pseudocode. No more than a screen.

**Tie-breaking.** TODO: state the rule, and cover it with a vector.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| \`capacity\` | number | 1000 | Maximum number of entries held. |

## Complexity

TODO: time and space, and any note on lock contention or memory overhead.

## Benchmark

<!-- bench:start -->
Not yet benchmarked. Run \`pnpm bench ${domain}\` and \`pnpm render\`.
<!-- bench:end -->

## Source

TODO: citation, or "folklore". Then: which policy is this closest to, and how
does it differ?

## Notes

TODO: patents, naming confusion, known variants. Say "no patents known" if that
is what you found.
`,
    },
  ];
}
