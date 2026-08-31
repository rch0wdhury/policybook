/**
 * When a visualisation must redraw, decided once for all of them.
 *
 * Every viz answers the same four prompts: its data changed; its canvas was
 * resized; the theme changed (colours are read from the page at draw time);
 * the device pixel ratio changed (the backing store is sized in real pixels).
 * Shared rather than copied into each island, because the theme prompt alone
 * has three sources — the toggle stamps `data-theme`, but in system mode an OS
 * light↔dark flip changes nothing in the DOM, and a copy that watched only the
 * attribute left paused canvases in the old palette.
 *
 * The observers and listeners are created once per mount, with the latest
 * paint closure read through a ref. Keying them on the frame instead — as each
 * island originally did — tears down and rebuilds a ResizeObserver and a
 * MutationObserver per canvas per animation frame, hundreds a second on a
 * four-policy comparison, for pictures that are identical either way.
 */

import { useEffect, useRef } from "preact/hooks";
import type { RefObject } from "preact";

export function useRepaint(
  paint: () => void,
  canvases: RefObject<HTMLCanvasElement | null>[],
  /** What the picture is drawn from; a change here is prompt number one. */
  deps: unknown[],
): void {
  const draw = useRef(paint);
  draw.current = paint;

  // Drawing is its own effect, keyed on the data, so a new frame costs one
  // paint and nothing else.
  useEffect(() => {
    draw.current();
  }, deps);

  useEffect(() => {
    const repaint = () => draw.current();

    // Resize: the layouts derive column counts and mark spacing from width.
    // The refs are stable; a canvas that mounts later (the keys strip on a
    // multi-key trace) still repaints with its siblings, it is just not its
    // own resize trigger.
    const observer = new ResizeObserver(repaint);
    for (const canvas of canvases) {
      if (canvas.current) observer.observe(canvas.current);
    }

    // The theme toggle stamps the attribute before first paint.
    const theme = new MutationObserver(repaint);
    theme.observe(document.documentElement, {
      attributes: true,
      attributeFilter: ["data-theme"],
    });

    // In system mode nothing is stamped, so an OS flip is only visible here.
    const scheme = window.matchMedia("(prefers-color-scheme: dark)");
    scheme.addEventListener("change", repaint);

    // Dragging the window to a different-DPI monitor fires no resize while
    // paused. A query pinned to the current ratio fires once when it stops
    // matching, so it is re-armed after every change.
    let pixelRatio: MediaQueryList | null = null;
    const onRatioChange = (): void => {
      repaint();
      armRatio();
    };
    const armRatio = (): void => {
      pixelRatio?.removeEventListener("change", onRatioChange);
      pixelRatio = window.matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`);
      pixelRatio.addEventListener("change", onRatioChange);
    };
    armRatio();

    return () => {
      observer.disconnect();
      theme.disconnect();
      scheme.removeEventListener("change", repaint);
      pixelRatio?.removeEventListener("change", onRatioChange);
    };
    // Mount-once by design: everything current is read through `draw`.
  }, []);
}
