/**
 * The runner's controls and readout.
 *
 * Everything shared between domains lives here: transport, trace selection,
 * the metrics readout, the URL sync, and the explainer every runner is
 * required to carry. The picture itself is a `children`
 * slot, which each domain's visualisation fills — T42 onward.
 *
 * The simulation runs in a worker. This component never touches a policy.
 */

import { useCallback, useEffect, useMemo, useRef, useState } from "preact/hooks";
import type { Frame } from "../lib/simulation";
import { decodeState, writeFragment, type RunnerState } from "../lib/url-state";
import {
  initialView,
  reduce,
  reduceLocal,
  type FromWorker,
  type RunnerView,
} from "../lib/worker-protocol";

export interface RunnerProps {
  domain: string;
  /** Policy slugs to compare, stepped in lockstep over one trace. */
  policies: string[];
  /** Trace ids the reader can choose between. */
  traces: string[];
  /** Names for the readout, by slug. */
  names: Record<string, string>;
  /** The metrics to show, in order, with their labels. */
  metrics: { key: string; label: string }[];
  /** Two or three sentences on what the picture means. */
  explainer: string;
  /**
   * Whether this runner owns the page's URL fragment. Default true.
   *
   * A page has one fragment, so only one runner can treat it as state. An
   * embedded runner — the tutorial mounts two — passes false: it neither reads
   * nor writes the fragment, and its keyboard shortcuts listen on the element
   * rather than the window, so pressing space cannot drive both at once.
   */
  urlState?: boolean;
  /**
   * Rendered per policy, given its latest frame and the domain's reference
   * curve where one exists.
   */
  children?: (
    slug: string,
    frame: Frame | undefined,
    reference: number[] | null,
  ) => unknown;
  /**
   * Every policy the reader may add, for the compare page's picker.
   *
   * Absent on a policy page, which runs the one policy it is about.
   */
  choices?: { slug: string; name: string }[];
  /** How many policies the picker allows at once. */
  maxPolicies?: number;
  /**
   * Rendered once above the per-policy rows, given every current frame.
   *
   * This is where a comparison lives: one chart with every policy on it, which
   * is a different question from the per-policy pictures below.
   */
  overlay?: (
    frames: Record<string, Frame>,
    policies: string[],
    reference: number[] | null,
  ) => unknown;
}

/** Speeds, in events per animation tick. */
const SPEEDS = [1, 8, 64, 512];

export default function RunnerShell(props: RunnerProps) {
  const { domain, traces, names, metrics, explainer, choices } = props;
  const maxPolicies = props.maxPolicies ?? 4;
  const urlState = props.urlState !== false;

  const initial: RunnerState = useMemo(
    () => ({
      domain,
      policies: props.policies,
      trace: traces[0] ?? "",
      step: 0,
      params: {},
    }),
    [domain, props.policies, traces],
  );

  /**
   * A link is the state, and it has to be read *before* the first render.
   *
   * This used to happen in an effect, which ran after the worker had already
   * been started on the default configuration — so following a shared link
   * built one set of simulations, threw them away, and built the linked set.
   * On the kv-cache compare page that is about 400 ms of wasted work and a
   * visible flash of the wrong policies. The browser smoke test found it by
   * reporting a different policy count on two consecutive runs.
   *
   * Guarded for the server, where there is no location and the island renders
   * its default markup. An embedded runner never reads the fragment: it is
   * not this runner's state to adopt.
   */
  const [state, setState] = useState<RunnerState>(() =>
    typeof window === "undefined" || !urlState
      ? initial
      : decodeState(window.location.hash, initial),
  );
  const [view, setView] = useState<RunnerView>(initialView);
  const [speed, setSpeed] = useState(SPEEDS[1]!);
  const [explaining, setExplaining] = useState(false);
  const worker = useRef<Worker | null>(null);
  /**
   * Which configuration the page is on, counted up per init.
   *
   * The worker tags everything it sends with the epoch of the init it answers,
   * and the reducer drops the rest — so work still in flight for an abandoned
   * configuration cannot leak frames, errors or a stale ready into this one.
   */
  const epoch = useRef(0);
  /**
   * A shared link's `n=` step, until the worker has been walked to it.
   *
   * Decoding alone does not restore it — the worker starts at step zero — so
   * the runner seeks once the worker is ready, and the fragment is left alone
   * until that seek's frame arrives so the linked step is not erased first.
   */
  const restoreStep = useRef(state.step > 0 ? state.step : null);

  // Start the worker and load the policies whenever the configuration changes.
  useEffect(() => {
    const thisEpoch = ++epoch.current;
    setView(reduceLocal(initialView, { type: "loading", epoch: thisEpoch }));

    const instance = new Worker(new URL("../lib/sim-worker.ts", import.meta.url), {
      type: "module",
    });
    worker.current = instance;

    instance.addEventListener("message", (event: MessageEvent<FromWorker>) => {
      setView((current) => reduce(current, event.data));
    });
    instance.addEventListener("error", (event) => {
      setView((current) =>
        reduce(current, {
          type: "error",
          epoch: thisEpoch,
          message: event.message || "the worker failed",
        }),
      );
    });

    instance.postMessage({
      type: "init",
      epoch: thisEpoch,
      domain,
      policies: state.policies,
      trace: state.trace,
      params: state.params,
      length: 0,
    });

    return () => {
      instance.terminate();
      worker.current = null;
    };
  }, [domain, state.policies, state.trace, state.params]);

  const send = useCallback((message: unknown) => {
    worker.current?.postMessage(message);
  }, []);

  const seek = useCallback(
    (step: number) => {
      setView((current) => reduceLocal(current, { type: "pause" }));
      send({ type: "seek", step });
    },
    [send],
  );

  const togglePlay = useCallback(() => {
    setView((current) => {
      const next = reduceLocal(current, { type: current.playing ? "pause" : "play" });
      if (next.playing) {
        if (next.step !== current.step) send({ type: "seek", step: next.step });
        send({ type: "play", stepsPerTick: speed });
      } else {
        send({ type: "pause" });
      }
      return next;
    });
  }, [send, speed]);

  // Walk the worker to a shared link's step once it is ready.
  useEffect(() => {
    if (view.status !== "ready" || restoreStep.current === null) return;
    send({ type: "seek", step: Math.min(restoreStep.current, view.totalSteps) });
  }, [view.status, view.totalSteps, send]);

  /**
   * Keep the fragment current, but never push history: a reader who pressed
   * back after playing should leave the page, not walk back a thousand steps.
   *
   * Written only on the discrete moments — pause, seek, a manual step, the end
   * of a run, a configuration change — never per animation frame: Safari
   * rate-limits `replaceState` to about a hundred calls per thirty seconds and
   * throws past that, which killed the island seconds into playback. While
   * playing, the URL holds the last paused step, and that is intended: a
   * moment worth sharing is one the reader has stopped on.
   */
  useEffect(() => {
    if (!urlState || view.status !== "ready") return;
    if (restoreStep.current !== null) {
      // The linked step has not been reached yet; writing now would erase it.
      if (view.step < Math.min(restoreStep.current, view.totalSteps)) return;
      restoreStep.current = null;
    }
    if (view.playing && view.step < view.totalSteps) return;
    writeFragment({ ...state, step: view.step }, { trace: traces[0] });
  }, [urlState, state, view.step, view.playing, view.status, view.totalSteps, traces]);

  /**
   * Space plays, arrows step. Ignored while a form control has focus, so
   * typing into a select does not start the runner, and ignored until the
   * worker is ready — a play flagged locally while the worker still drops the
   * message would leave "Pause" showing over a stopped runner.
   *
   * The handler reads through a ref so it can be attached once per mount
   * rather than re-attached on every frame of playback.
   */
  const keyState = useRef({ togglePlay, seek, view });
  keyState.current = { togglePlay, seek, view };

  const onKey = useCallback((event: KeyboardEvent) => {
    const current = keyState.current;
    if (current.view.status !== "ready") return;
    const target = event.target as HTMLElement | null;
    if (target && /^(INPUT|SELECT|TEXTAREA)$/.test(target.tagName)) return;

    if (event.key === " ") {
      event.preventDefault();
      current.togglePlay();
    } else if (event.key === "ArrowRight") {
      event.preventDefault();
      current.seek(
        Math.min(current.view.step + (event.shiftKey ? 100 : 1), current.view.totalSteps),
      );
    } else if (event.key === "ArrowLeft") {
      event.preventDefault();
      current.seek(Math.max(current.view.step - (event.shiftKey ? 100 : 1), 0));
    }
  }, []);

  // A page's single runner listens on the window. An embedded one would fight
  // its siblings for every keypress, so it takes keys from its own element
  // instead — the root carries tabIndex for that — and only while focus is in
  // it.
  useEffect(() => {
    if (!urlState) return;
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [urlState, onKey]);

  /**
   * Add or remove a policy from the comparison.
   *
   * Refuses to drop the last one — an empty comparison is not a state worth
   * being able to reach — and refuses to exceed the maximum, because past four
   * lines a chart stops being readable and starts being decorative.
   */
  const togglePolicy = useCallback(
    (slug: string) => {
      setState((current) => {
        const chosen = current.policies.includes(slug);
        if (chosen && current.policies.length === 1) return current;
        if (!chosen && current.policies.length >= maxPolicies) return current;
        return {
          ...current,
          policies: chosen
            ? current.policies.filter((entry) => entry !== slug)
            : [...current.policies, slug],
        };
      });
    },
    [maxPolicies],
  );

  if (view.status === "error") {
    return (
      <div class="runner runner-error" role="alert">
        <p><strong>The runner could not start.</strong></p>
        <p class="muted">{view.error}</p>
        <p class="muted">
          The benchmark numbers on this page come from the same policies and do
          not depend on this working.
        </p>
      </div>
    );
  }

  return (
    <div
      class="runner"
      tabIndex={urlState ? undefined : 0}
      onKeyDown={urlState ? undefined : onKey}
    >
      {choices !== undefined && (
        <fieldset class="runner-picker">
          <legend>
            Policies{" "}
            <span class="muted">
              ({state.policies.length} of {maxPolicies})
            </span>
          </legend>
          {choices.map((choice) => {
            const chosen = state.policies.includes(choice.slug);
            return (
              <label class={chosen ? "chosen" : undefined} key={choice.slug}>
                <input
                  type="checkbox"
                  checked={chosen}
                  disabled={!chosen && state.policies.length >= maxPolicies}
                  onChange={() => togglePolicy(choice.slug)}
                />
                {choice.name}
              </label>
            );
          })}
        </fieldset>
      )}

      <div class="runner-controls">
        <button
          type="button"
          onClick={togglePlay}
          disabled={view.status !== "ready"}
          class="primary"
        >
          {view.playing ? "Pause" : view.step >= view.totalSteps && view.totalSteps > 0 ? "Replay" : "Play"}
        </button>

        <button type="button" onClick={() => seek(view.step - 1)} disabled={view.step === 0}>
          ‹ Step
        </button>
        <button
          type="button"
          onClick={() => seek(view.step + 1)}
          disabled={view.step >= view.totalSteps}
        >
          Step ›
        </button>

        <label>
          <span class="control-label">Speed</span>
          <select
            value={String(speed)}
            onChange={(event) => setSpeed(Number((event.target as HTMLSelectElement).value))}
          >
            {SPEEDS.map((value) => (
              <option value={String(value)}>{value}×</option>
            ))}
          </select>
        </label>

        {traces.length > 1 && (
          <label>
            <span class="control-label">Trace</span>
            <select
              value={state.trace}
              onChange={(event) =>
                setState((current) => ({
                  ...current,
                  trace: (event.target as HTMLSelectElement).value,
                }))
              }
            >
              {traces.map((trace) => (
                <option value={trace}>{trace}</option>
              ))}
            </select>
          </label>
        )}

        <button type="button" onClick={() => setExplaining((on) => !on)} aria-expanded={explaining}>
          What am I looking at?
        </button>
      </div>

      {explaining && (
        <p class="runner-explainer">
          {explainer}{" "}
          <span class="muted">
            Space plays and pauses; the arrow keys step, and hold shift to move a
            hundred at a time.{" "}
            {urlState
              ? "The address bar carries the whole configuration, so any moment here is a link you can send to someone."
              : "Click the runner first. Its keys work while it has focus, so two runners on one page cannot both grab your spacebar."}
          </span>
        </p>
      )}

      <input
        type="range"
        class="scrubber"
        min={0}
        max={Math.max(view.totalSteps, 1)}
        value={view.step}
        disabled={view.status !== "ready"}
        aria-label="Step"
        onInput={(event) => seek(Number((event.target as HTMLInputElement).value))}
      />

      <p class="runner-position muted">
        {view.status === "loading"
          ? "Loading the policies…"
          : `Step ${view.step.toLocaleString()} of ${view.totalSteps.toLocaleString()}`}
      </p>

      {props.overlay?.(view.frames, view.policies, view.reference)}

      <div class="runner-rows">
        {view.policies.map((slug) => {
          const frame = view.frames[slug];
          return (
            <div class="runner-row" key={slug}>
              <div class="runner-row-head">
                <strong>{names[slug] ?? slug}</strong>
                <span class="runner-metrics">
                  {metrics.map((metric) => (
                    <span>
                      <span class="muted">{metric.label}</span>{" "}
                      <span class="tabular">
                        {frame === undefined
                          ? "-"
                          : formatMetric(frame.metrics[metric.key])}
                      </span>
                    </span>
                  ))}
                </span>
              </div>
              {props.children?.(slug, frame, view.reference)}
            </div>
          );
        })}
      </div>
    </div>
  );
}

function formatMetric(value: number | undefined): string {
  if (value === undefined) return "-";
  return Number.isInteger(value) ? value.toLocaleString("en-US") : value.toFixed(4);
}
