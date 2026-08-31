/**
 * The runner's state, in the URL.
 *
 * Every configuration is a shareable link: which policies,
 * which trace, which step, which parameters. Someone who finds a surprising
 * moment can send it to somebody else and have them land on exactly that
 * moment rather than on the default.
 *
 * It lives in the fragment rather than the query string, so it never reaches a
 * server and never appears in a log. There is no server here to read it, and
 * the site claims to make no request a reader did not ask for.
 *
 * The encoding is deliberately readable — `#d=cache&p=sieve,lru&t=zipf-1.0-100k`
 * — because a link someone can edit by hand is worth more than a compact one
 * they cannot.
 */

export interface RunnerState {
  domain: string;
  /** Policy slugs, in the order they should be shown. */
  policies: string[];
  trace: string;
  /** Where the runner is paused. */
  step: number;
  /** Overrides on top of each policy's own defaults. */
  params: Record<string, number>;
}

// No seed key: the traces bake their seeds into their ids, so an `s=` was
// decoded and then consumed by nothing — a knob in the link that the runner
// does not have.
const KEYS = {
  domain: "d",
  policies: "p",
  trace: "t",
  step: "n",
} as const;

/** Parameters are prefixed so they cannot collide with the reserved keys. */
const PARAM_PREFIX = "x.";

/**
 * Encode state into a fragment, omitting anything at its default.
 *
 * A link carrying only what differs is shorter, and — more usefully — says what
 * the sender actually changed.
 */
export function encodeState(state: RunnerState, defaults?: Partial<RunnerState>): string {
  const parts: string[] = [];

  parts.push(`${KEYS.domain}=${state.domain}`);
  if (state.policies.length > 0) parts.push(`${KEYS.policies}=${state.policies.join(",")}`);
  if (state.trace !== defaults?.trace) parts.push(`${KEYS.trace}=${state.trace}`);
  if (state.step > 0) parts.push(`${KEYS.step}=${state.step}`);

  for (const [name, value] of Object.entries(state.params).sort()) {
    parts.push(`${PARAM_PREFIX}${name}=${value}`);
  }

  return parts.join("&");
}

/**
 * A percent-escape a browser would reject — `100%`, `%zz` — must not take the
 * island down with it; this runs inside a hydration-time state initializer.
 */
function decodePart(text: string): string | null {
  try {
    return decodeURIComponent(text);
  } catch {
    return null;
  }
}

/**
 * Decode a fragment, falling back to `fallback` for anything absent or unusable.
 *
 * Nothing here throws. A hand-edited link with a typo in it should land the
 * reader on a working page with the parts it understood, not on an error —
 * being able to edit the link is the point of a readable encoding.
 */
export function decodeState(fragment: string, fallback: RunnerState): RunnerState {
  const raw = fragment.replace(/^#/, "");
  const state: RunnerState = {
    ...fallback,
    policies: [...fallback.policies],
    params: { ...fallback.params },
  };
  if (raw === "") return state;

  for (const pair of raw.split("&")) {
    const at = pair.indexOf("=");
    if (at === -1) continue;
    const key = decodePart(pair.slice(0, at));
    const value = decodePart(pair.slice(at + 1));
    if (key === null || value === null || value === "") continue;

    if (key.startsWith(PARAM_PREFIX)) {
      const number = Number(value);
      if (Number.isFinite(number)) state.params[key.slice(PARAM_PREFIX.length)] = number;
      continue;
    }

    switch (key) {
      case KEYS.domain:
        state.domain = value;
        break;
      case KEYS.policies:
        state.policies = value.split(",").filter((slug) => slug !== "");
        break;
      case KEYS.trace:
        state.trace = value;
        break;
      case KEYS.step: {
        const step = Number(value);
        if (Number.isInteger(step) && step >= 0) state.step = step;
        break;
      }
      default:
        // An unknown key is ignored rather than rejected, so a link from a
        // later version of the site still works on this one.
        break;
    }
  }

  return state;
}

/**
 * Replace the fragment without adding a history entry.
 *
 * The runner updates this on discrete moments — pause, seek, a manual step, a
 * configuration change — and pushing a new entry per step would make the back
 * button useless: a reader who pressed it would walk backwards through a
 * thousand steps rather than leaving the page.
 *
 * Guarded, because Safari rate-limits `replaceState` and throws a
 * SecurityError past the limit. A link that briefly lags the runner is a
 * nuisance; an island killed by its own address bar is a broken page.
 */
export function writeFragment(state: RunnerState, defaults?: Partial<RunnerState>): void {
  if (typeof window === "undefined") return;
  const fragment = encodeState(state, defaults);
  const url = `${window.location.pathname}${window.location.search}#${fragment}`;
  try {
    window.history.replaceState(null, "", url);
  } catch {
    // The next discrete moment writes again.
  }
}
