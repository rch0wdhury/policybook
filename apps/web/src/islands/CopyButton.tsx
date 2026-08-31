/**
 * A command, and a button that copies it.
 *
 * The only interactive thing on the home page, and it is worth the JavaScript:
 * the command is the shortest path from reading about a policy to having it,
 * and selecting text in a terminal-styled block is fiddly enough that people
 * do not bother.
 *
 * It degrades honestly. `navigator.clipboard` needs a secure context and can be
 * refused outright, so a failure says "select and copy" rather than pretending
 * to have worked — a button that silently does nothing is worse than no button.
 */

import { useState } from "preact/hooks";

interface Props {
  command: string;
}

type State = "idle" | "copied" | "failed";

export default function CopyButton({ command }: Props) {
  const [state, setState] = useState<State>("idle");

  async function copy() {
    try {
      await navigator.clipboard.writeText(command);
      setState("copied");
    } catch {
      setState("failed");
    }
    // Back to idle, so the button does not keep claiming a copy that happened
    // a minute ago.
    setTimeout(() => setState("idle"), 2000);
  }

  const label =
    state === "copied" ? "Copied" : state === "failed" ? "Select and copy" : "Copy";

  return (
    <div class="copy">
      <code>{command}</code>
      <button
        type="button"
        onClick={copy}
        aria-label={`Copy "${command}" to the clipboard`}
        data-state={state}
      >
        {label}
      </button>
    </div>
  );
}
