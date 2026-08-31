/**
 * A link to the repository, with the star count on it.
 *
 * **It is a plain anchor.** Not a form, not a script that stars anything, not
 * an iframe from github.com — a link, styled as a button, that goes where it
 * says it goes. The count is decoration on top; if the fetch fails the link is
 * exactly as useful as before.
 *
 * ## What it does to a reader's browser
 *
 * One request, to `api.github.com`, unauthenticated and anonymous, cached in
 * `localStorage` for an hour. That is the only network call the site makes
 * after the page loads, and it exists so the button can say something true
 * rather than a number frozen at build time.
 *
 * Every part of it is allowed to fail. `localStorage` throws outright in some
 * privacy modes; the API is rate-limited to 60 requests an hour per address and
 * will refuse; a content blocker may reject the request entirely. None of that
 * is exceptional enough to warrant an error state, so each is caught and the
 * button falls back to the build-time figure — and if that is zero, to no
 * number at all, since a confident "0" is worse than silence.
 */

import { useEffect, useState } from "preact/hooks";
import { REPO_URL, SLUG, STARS_AT_BUILD } from "../lib/repo";
import { readStars, writeStars, type Storage } from "../lib/star-cache";

interface Props {
  /** The count at build time, used until a live one arrives. */
  fallback?: number;
  /** A smaller variant, for sitting beside a copy command. */
  compact?: boolean;
}

/**
 * `localStorage`, if this browser will admit to having one.
 *
 * The *property access* throws in some privacy modes, before any method is
 * called, so it is reached for inside a try rather than passed around.
 */
function storage(): Storage | undefined {
  try {
    return window.localStorage;
  } catch {
    return undefined;
  }
}

export default function StarButton({ fallback = STARS_AT_BUILD, compact = false }: Props) {
  const [count, setCount] = useState<number | null>(fallback > 0 ? fallback : null);

  useEffect(() => {
    const cached = readStars(storage());
    if (cached !== null) {
      setCount(cached);
      return;
    }

    let live = true;
    const controller = new AbortController();

    void (async () => {
      try {
        const response = await fetch(`https://api.github.com/repos/${SLUG}`, {
          signal: controller.signal,
          headers: { Accept: "application/vnd.github+json" },
        });
        if (!response.ok) return;

        const body = (await response.json()) as { stargazers_count?: unknown };
        const stars = body.stargazers_count;
        if (typeof stars !== "number" || !Number.isFinite(stars)) return;

        writeStars(storage(), stars);
        if (live) setCount(stars);
      } catch {
        // Offline, blocked, rate-limited, or the reader navigated away. The
        // build-time figure stands.
      }
    })();

    return () => {
      live = false;
      controller.abort();
    };
  }, []);

  return (
    <a
      class={compact ? "star-button compact" : "star-button"}
      href={REPO_URL}
      rel="noopener noreferrer"
    >
      <span aria-hidden="true" class="star-icon">★</span>
      <span>Star on GitHub</span>
      {count !== null && (
        <span class="star-count tabular">{count.toLocaleString("en-US")}</span>
      )}
    </a>
  );
}
