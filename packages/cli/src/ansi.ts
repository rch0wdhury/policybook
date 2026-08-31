/**
 * Terminal colour, kept to what a terminal will actually honour.
 *
 * Colour is off when output is piped, when `NO_COLOR` is set, when `TERM` is
 * `dumb`, or when `--no-color` was passed — so `policybook list > file` gives a
 * clean file and a CI log stays readable.
 */

let enabled =
  process.stdout.isTTY === true &&
  process.env["NO_COLOR"] === undefined &&
  process.env["TERM"] !== "dumb";

/** Turn colour off, for `--no-color`. */
export function disableColor(): void {
  enabled = false;
}

export function colorEnabled(): boolean {
  return enabled;
}

function wrap(open: number, close: number) {
  return (text: string): string => (enabled ? `[${open}m${text}[${close}m` : text);
}

export const bold = wrap(1, 22);
export const dim = wrap(2, 22);
export const italic = wrap(3, 23);
export const underline = wrap(4, 24);

export const red = wrap(31, 39);
export const green = wrap(32, 39);
export const yellow = wrap(33, 39);
export const blue = wrap(34, 39);
export const magenta = wrap(35, 39);
export const cyan = wrap(36, 39);

/** Visible width, ignoring escape sequences, for column alignment. */
export function visibleWidth(text: string): number {
  // eslint-disable-next-line no-control-regex
  return text.replace(/\[[0-9;]*m/g, "").length;
}

/** Pad to a width using the visible length, so colour does not skew columns. */
export function padEnd(text: string, width: number): string {
  const padding = width - visibleWidth(text);
  return padding > 0 ? text + " ".repeat(padding) : text;
}

/** Cut to a width, adding an ellipsis, on the visible length. */
export function truncate(text: string, width: number): string {
  if (visibleWidth(text) <= width) return text;
  if (width <= 1) return "…";
  return text.slice(0, width - 1) + "…";
}
