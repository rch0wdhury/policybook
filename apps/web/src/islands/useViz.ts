/**
 * Loading a domain's visualisation, once, on demand.
 *
 * **The import is dynamic and per domain, and that matters for size.** A static
 * import puts every domain's drawing code into one island chunk, so a cache
 * page would ship the kv-cache renderer it can never use — and the per-viz
 * budget would have nothing to measure, because the chunks would not exist
 * separately. It passed vacuously until this was split.
 *
 * Shared by the policy runner and the compare runner so there is one list of
 * domains and one loading rule, rather than two that can disagree.
 */

import { useEffect, useState } from "preact/hooks";
import type { Frame } from "../lib/simulation";

/** A visualisation: given a frame and a reference curve, draws the picture. */
export type VizComponent = (props: {
  frame: Frame | undefined;
  reference: number[] | null;
}) => unknown;

const LOADERS: Record<string, () => Promise<{ default: VizComponent }>> = {
  cache: () => import("./CacheViz") as Promise<{ default: VizComponent }>,
  "rate-limiter": () => import("./RateLimiterViz") as Promise<{ default: VizComponent }>,
  "kv-cache": () => import("./KvCacheViz") as Promise<{ default: VizComponent }>,
};

/** The domain's renderer once it has arrived, or null. */
export function useViz(domain: string): VizComponent | null {
  const [viz, setViz] = useState<{ component: VizComponent } | null>(null);

  useEffect(() => {
    const load = LOADERS[domain];
    if (load === undefined) {
      setViz(null);
      return;
    }

    let live = true;
    void load().then((module) => {
      // The component can outlive the import if a reader navigates away mid
      // flight, and setting state on an unmounted tree is a warning at best.
      if (live) setViz({ component: module.default });
    });

    return () => {
      live = false;
    };
  }, [domain]);

  return viz?.component ?? null;
}
