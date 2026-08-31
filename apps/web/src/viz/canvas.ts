/**
 * The two things every visualisation needs from a canvas.
 *
 * Shared rather than copied into each domain's renderer: both are small, both
 * are easy to get subtly wrong, and a device-pixel bug fixed in one place
 * should not survive in another.
 */

/** Palette, resolved from the page rather than hard-coded. */
export interface Palette {
  text: string;
  muted: string;
  border: string;
  surface: string;
  accent: string;
  success: string;
  warn: string;
}

/**
 * Read the design tokens off the element itself.
 *
 * At draw time, not at module load: the tokens change when the reader switches
 * theme, and a palette captured once would leave the picture in the old one.
 */
export function palette(canvas: HTMLCanvasElement): Palette {
  const style = getComputedStyle(canvas);
  const read = (name: string, fallback: string): string =>
    style.getPropertyValue(name).trim() || fallback;

  return {
    text: read("--text", "#16181d"),
    muted: read("--muted", "#565d6d"),
    border: read("--border", "#ced4dd"),
    surface: read("--surface", "#f7f8fa"),
    accent: read("--accent", "#4f46e5"),
    success: read("--success", "#0f7a44"),
    warn: read("--warn", "#a04600"),
  };
}

/**
 * Size the backing store to the device's real pixels.
 *
 * Without this the picture is drawn at CSS resolution and then scaled up, which
 * on any modern display turns one-pixel borders into grey smudges — exactly the
 * detail these pictures are made of.
 */
export function fit(
  canvas: HTMLCanvasElement,
  cssHeight: number,
): CanvasRenderingContext2D | null {
  const context = canvas.getContext("2d");
  if (context === null) return null;

  const ratio = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;

  if (
    canvas.width !== Math.round(width * ratio) ||
    canvas.height !== Math.round(cssHeight * ratio)
  ) {
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(cssHeight * ratio);
    canvas.style.height = `${cssHeight}px`;
  }

  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, width, cssHeight);
  return context;
}
