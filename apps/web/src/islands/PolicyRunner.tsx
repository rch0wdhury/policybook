/**
 * The runner as a policy page mounts it.
 *
 * One island rather than two, because the shell and the visualisation have to
 * hydrate together — passing a render function from Astro into a Preact island
 * across the server boundary is not something that survives serialisation.
 *
 * The visualisation is loaded per domain by `useViz`, which the compare runner
 * shares; see that file for why the split is load-bearing rather than tidy.
 */

import type { Frame } from "../lib/simulation";
import RunnerShell, { type RunnerProps } from "./RunnerShell";
import { useViz } from "./useViz";

type Props = Omit<RunnerProps, "children" | "choices" | "overlay">;

export default function PolicyRunner(props: Props) {
  const Viz = useViz(props.domain);

  return (
    <RunnerShell {...props}>
      {(_slug: string, frame: Frame | undefined, reference: number[] | null) =>
        Viz === null ? null : <Viz frame={frame} reference={reference} />
      }
    </RunnerShell>
  );
}
