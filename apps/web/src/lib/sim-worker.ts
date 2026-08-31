/**
 * The simulation worker.
 *
 * Policies run here rather than on the main thread, so a runner playing at
 * speed cannot make the page stop responding — scrolling, the theme toggle and
 * the controls all keep working while thousands of events go past.
 *
 * It owns one simulation per policy being compared, all stepped in lockstep
 * over the same trace, which is what makes a side-by-side comparison mean
 * anything: same events, same order, same seed.
 *
 * The state machine the page runs against this is in `worker-protocol.ts` and
 * is tested separately — a Web Worker needs a bundler and an event loop, and
 * the part that can actually be wrong needs neither.
 */

import { CACHE_TRACES, generateCacheTrace } from "../../../../packages/core/src/domains/cache";
import { loadPolicy } from "./policy-modules";
import { createSimulation, reducedLength, type Frame, type Simulation } from "./simulation";
// Types only, so the worker does not pull in the page's reducer.
import type { FromWorker, ToWorker } from "./worker-protocol";

let simulations: { id: string; simulation: Simulation }[] = [];
let playing = false;
let stepsPerTick = 1;
let timer: ReturnType<typeof setTimeout> | null = null;
/**
 * The configuration currently being served, from the latest init.
 *
 * Loading policies is async, so a second init can arrive while the first is
 * still awaiting an import. Without the epoch the resumed first init would
 * push its policies into the second's simulations array. Every message out
 * carries it, so the page can drop anything from a configuration it has
 * abandoned.
 */
let epoch = 0;

function post(message: FromWorker): void {
  (self as unknown as Worker).postMessage(message);
}

function frames(): Record<string, Frame> {
  const result: Record<string, Frame> = {};
  for (const { id, simulation } of simulations) result[id] = simulation.frame();
  return result;
}

function emit(): void {
  post({ type: "frame", epoch, step: simulations[0]?.simulation.step ?? 0, frames: frames() });
}

function stop(): void {
  playing = false;
  if (timer !== null) {
    clearTimeout(timer);
    timer = null;
  }
}

/**
 * One animation tick.
 *
 * The step budget is bounded so a tick cannot run long enough to starve
 * anything else the worker is asked to do — a `pause` message arriving mid-tick
 * should take effect at the next one, not after ten thousand more events.
 */
function tick(): void {
  if (!playing || simulations.length === 0) return;

  const first = simulations[0]!.simulation;
  const target = Math.min(first.step + stepsPerTick, first.totalSteps);

  for (const { simulation } of simulations) simulation.seek(target);
  emit();

  if (target >= first.totalSteps) {
    stop();
    return;
  }
  timer = setTimeout(tick, 16);
}

async function init(message: Extract<ToWorker, { type: "init" }>): Promise<void> {
  stop();
  simulations = [];

  for (const slug of message.policies) {
    // A slug carrying a slash is already a full policy id — the tutorial's
    // examples register as `tutorial/evict-newest`, not under this domain.
    const id = slug.includes("/") ? slug : `${message.domain}/${slug}`;
    const factory = await loadPolicy(id);
    // A newer init took over while the import was in flight; everything from
    // here on would interleave two configurations into one array.
    if (message.epoch !== epoch) return;
    simulations.push({
      id: slug,
      simulation: createSimulation(
        message.domain,
        factory,
        message.params,
        message.trace,
        message.length || reducedLength(message.domain, message.trace),
      ),
    });
  }

  const reference = await referenceCurve(
    message.domain,
    message.policies,
    message.trace,
    message.params,
    message.length || reducedLength(message.domain, message.trace),
  );
  if (message.epoch !== epoch) return;

  post({
    type: "ready",
    epoch,
    totalSteps: simulations[0]?.simulation.totalSteps ?? 0,
    policies: simulations.map((entry) => entry.id),
    reference,
  });
  emit();
}

/**
 * The line every policy on this page is measured against.
 *
 * Each domain has a different one, and the choice matters: a number with no
 * reference is not interpretable. For cache it is Bélády's OPT, the best any
 * policy could do knowing the whole future. For kv-cache it is the sliding
 * window — the simplest thing that works — because the question a reader has
 * about an eviction policy is not "is it good" but "is it better than keeping
 * the most recent N", which is what you get for free.
 */
async function referenceCurve(
  domain: string,
  policies: string[],
  traceId: string,
  params: Record<string, number>,
  length: number,
): Promise<number[] | undefined> {
  if (domain === "cache") return optimumCurve(traceId, params, length);
  if (domain === "kv-cache") {
    // On the sliding window's own page the baseline *is* the policy, and
    // drawing a dashed copy of the solid line underneath it would suggest a
    // comparison where there is none.
    if (policies.length === 1 && policies[0] === "sliding-window") return undefined;
    return slidingWindowCurve(traceId, params, length);
  }
  return undefined;
}

/**
 * What a plain sliding window retains, sampled like the policies' own history.
 *
 * A failure is not fatal, for the same reason as the optimum: the line is worth
 * having and the runner is worth more.
 */
async function slidingWindowCurve(
  traceId: string,
  params: Record<string, number>,
  length: number,
): Promise<number[] | undefined> {
  try {
    const factory = await loadPolicy("kv-cache/sliding-window");
    const baseline = createSimulation("kv-cache", factory, params, traceId, length);
    baseline.seek(baseline.totalSteps);
    const view = baseline.frame().view;
    return view.kind === "kv-cache" ? view.history : undefined;
  } catch {
    return undefined;
  }
}

/**
 * The hit rate Bélády's OPT achieves, sampled on the same schedule as the
 * policies' own histories.
 *
 * Computed once here rather than stepped alongside the others: it is a
 * reference line, not a competitor, and running it as a fourth simulation would
 * invite a reader to compare something uncomparable. It also needs the whole
 * future up front, which is exactly what makes it impossible to deploy.
 *
 * A failure is not fatal. The line is worth having and the runner is worth more,
 * so a missing optimum drops the dashes and keeps the picture.
 */
async function optimumCurve(
  traceId: string,
  params: Record<string, number>,
  length: number,
): Promise<number[] | undefined> {
  try {
    const spec = CACHE_TRACES[traceId];
    if (spec === undefined) return undefined;

    const trace = generateCacheTrace(traceId, length);
    const capacity = params["capacity"] ?? spec.capacity;
    const factory = await loadPolicy("cache/opt");
    const optimum = createSimulation(
      "cache",
      // OPT is the one policy that takes the future, and the harness is the
      // only thing that can hand it over.
      (p) => factory({ ...p, capacity, future: Array.from(trace) }),
      { capacity },
      traceId,
      length,
    );

    optimum.seek(optimum.totalSteps);
    const view = optimum.frame().view;
    return view.kind === "cache" ? view.history : undefined;
  } catch {
    return undefined;
  }
}

self.addEventListener("message", (event: MessageEvent<ToWorker>) => {
  const message = event.data;
  if (message.type === "init") epoch = message.epoch;
  // An error from a superseded init must be reported against its own epoch,
  // where the page will ignore it, not against the configuration that took over.
  const mine = message.type === "init" ? message.epoch : epoch;

  void (async () => {
    try {
      switch (message.type) {
        case "init":
          await init(message);
          break;

        case "seek":
          stop();
          for (const { simulation } of simulations) simulation.seek(message.step);
          emit();
          break;

        case "play":
          if (simulations.length === 0) break;
          stepsPerTick = Math.max(1, message.stepsPerTick);
          playing = true;
          tick();
          break;

        case "pause":
          stop();
          break;

        default:
          break;
      }
    } catch (error) {
      stop();
      post({
        type: "error",
        epoch: mine,
        message: error instanceof Error ? error.message : String(error),
      });
    }
  })();
});
