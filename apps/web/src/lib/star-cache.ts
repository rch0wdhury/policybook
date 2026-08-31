/**
 * Remembering the star count for an hour.
 *
 * Split out of the button so it can be tested without a browser, because the
 * interesting behaviour here is entirely about *failure*: `localStorage` throws
 * outright in some privacy modes, holds whatever another tab wrote, and can
 * contain anything at all if a reader edits it. None of that should reach the
 * page, so every path returns null rather than propagating.
 *
 * The TTL exists to be polite. GitHub allows 60 unauthenticated requests an
 * hour per address; a reader clicking through twenty policy pages should cost
 * one of those, not twenty.
 */

export const STAR_CACHE_KEY = "policybook:stars";
export const STAR_TTL_MS = 60 * 60 * 1000;

/** The bit of `localStorage` this needs, so a test can supply its own. */
export interface Storage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

interface Cached {
  count: number;
  at: number;
}

/**
 * The cached count, if it is present, parseable, and still fresh.
 *
 * Returns null for every other case, which includes a storage that throws on
 * access — that is a browser setting, not an error worth surfacing.
 */
export function readStars(storage: Storage | undefined, now = Date.now()): number | null {
  if (storage === undefined) return null;

  try {
    const raw = storage.getItem(STAR_CACHE_KEY);
    if (raw === null) return null;

    const parsed = JSON.parse(raw) as unknown;
    if (typeof parsed !== "object" || parsed === null) return null;

    const { count, at } = parsed as Partial<Cached>;
    if (typeof count !== "number" || !Number.isFinite(count) || count < 0) return null;
    if (typeof at !== "number" || !Number.isFinite(at)) return null;

    // A timestamp in the future means a clock changed under us. Treating it as
    // stale costs one request; trusting it could pin a wrong number for hours.
    if (at > now) return null;
    if (now - at > STAR_TTL_MS) return null;

    return count;
  } catch {
    return null;
  }
}

/** Store the count. Failure is silent: the button works without it. */
export function writeStars(
  storage: Storage | undefined,
  count: number,
  now = Date.now(),
): void {
  if (storage === undefined) return;
  try {
    storage.setItem(STAR_CACHE_KEY, JSON.stringify({ count, at: now } satisfies Cached));
  } catch {
    // Quota exceeded, or writing is disallowed. Not worth a word.
  }
}
