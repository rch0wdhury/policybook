/**
 * Verifies the palette meets its stated thresholds, by reading the tokens
 * rather than by trusting that someone checked once.
 *
 * A colour choice that looks fine to the person who picked it is not evidence.
 * This computes real contrast ratios from `src/styles/tokens.css` — the light
 * palette and *both* copies of the dark one — and fails the build if any pair
 * a reader actually has to read falls below its threshold.
 *
 * Text thresholds are WCAG 2.1 AA: 4.5 for text, 3.0 for focus rings (the
 * non-text minimum). Borders are held to 1.4, which is *below* WCAG's 3.0 for
 * user-interface component boundaries — deliberately, and said plainly here:
 * these are decorative separators on cards and table rows whose components
 * are identified by their surface change, not the hairline. The palette's
 * borders measure about 1.5:1, and a check claiming 3.0 while testing 1.4
 * was a comment lying about its own code.
 *
 * Usage: pnpm tsx apps/web/scripts/check-contrast.ts
 */

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const TOKENS = join(HERE, "..", "src", "styles", "tokens.css");

/** One channel of sRGB, linearised. The 0.03928 knee is from the spec. */
function channel(value: number): number {
  const c = value / 255;
  return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
}

function luminance(hex: string): number {
  const value = hex.replace("#", "");
  const r = Number.parseInt(value.slice(0, 2), 16);
  const g = Number.parseInt(value.slice(2, 4), 16);
  const b = Number.parseInt(value.slice(4, 6), 16);
  return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

function ratio(foreground: string, background: string): number {
  const a = luminance(foreground);
  const b = luminance(background);
  const [light, dark] = a > b ? [a, b] : [b, a];
  return (light + 0.05) / (dark + 0.05);
}

/**
 * The token values from one CSS block.
 *
 * Deliberately a small regex over the file rather than a CSS parser: the file
 * is ours, its shape is known, and a parser would be more code than the thing
 * it checks. If it ever stops matching, the missing-token check below fails
 * loudly rather than silently passing on an empty set.
 */
function tokensIn(css: string, selector: string): Record<string, string> {
  const start = css.indexOf(selector);
  if (start === -1) throw new Error(`check-contrast: no block for ${selector}`);

  const open = css.indexOf("{", start);
  const close = css.indexOf("}", open);
  const block = css.slice(open + 1, close);

  const found: Record<string, string> = {};
  for (const match of block.matchAll(/--([a-z-]+):\s*(#[0-9a-fA-F]{6})/g)) {
    found[match[1]!] = match[2]!;
  }
  return found;
}

interface Pair {
  foreground: string;
  background: string;
  minimum: number;
  why: string;
}

/** Every pair a reader has to be able to distinguish. */
const PAIRS: Pair[] = [
  { foreground: "text", background: "bg", minimum: 4.5, why: "body text" },
  { foreground: "text", background: "surface", minimum: 4.5, why: "text on a card" },
  { foreground: "muted", background: "bg", minimum: 4.5, why: "secondary text" },
  { foreground: "muted", background: "surface", minimum: 4.5, why: "secondary text on a card" },
  { foreground: "accent", background: "bg", minimum: 4.5, why: "links" },
  { foreground: "accent", background: "surface", minimum: 4.5, why: "links on a card" },
  { foreground: "success", background: "bg", minimum: 4.5, why: "a positive delta" },
  { foreground: "warn", background: "bg", minimum: 4.5, why: "a negative delta" },
  // Not text, and deliberately below WCAG 1.4.11's 3.0 for component
  // boundaries (see the header): a decorative hairline only has to be
  // perceivable, and 1.4 is the floor that keeps "cards" from becoming "a
  // wall of text". State the real number rather than claiming one the palette
  // does not meet.
  { foreground: "border", background: "bg", minimum: 1.4, why: "card and table borders" },
  { foreground: "accent", background: "bg", minimum: 3.0, why: "focus rings" },
];

/**
 * The dark palette exists twice in tokens.css: once under
 * `@media (prefers-color-scheme: dark)` for readers who never touched the
 * toggle, once under `[data-theme="dark"]` for those who did. Both are
 * checked, and asserted identical below — a value edited in one copy would
 * otherwise drift unchecked, and the readers who see the stale copy are
 * exactly the ones no manual toggle-flipping test covers.
 */
const THEMES: { name: string; selector: string }[] = [
  { name: "light", selector: ":root {" },
  { name: "dark (manual toggle)", selector: ':root[data-theme="dark"] {' },
  { name: "dark (system preference)", selector: ':root:not([data-theme="light"]) {' },
];

function main(): void {
  const css = readFileSync(TOKENS, "utf8");
  const failures: string[] = [];
  let checked = 0;

  // The two dark palettes must be the same palette, token for token.
  const manual = tokensIn(css, ':root[data-theme="dark"] {');
  const system = tokensIn(css, ':root:not([data-theme="light"]) {');
  for (const name of new Set([...Object.keys(manual), ...Object.keys(system)])) {
    if (manual[name] !== system[name]) {
      failures.push(
        `dark palettes disagree on --${name}: ` +
          `${manual[name] ?? "missing"} under [data-theme="dark"], ` +
          `${system[name] ?? "missing"} under the media block`,
      );
    }
  }

  for (const theme of THEMES) {
    const tokens = tokensIn(css, theme.selector);

    // A typo in a token name would otherwise skip its pair silently and report
    // a clean run over nothing.
    const needed = new Set(PAIRS.flatMap((pair) => [pair.foreground, pair.background]));
    for (const name of needed) {
      if (tokens[name] === undefined) {
        failures.push(`${theme.name}: --${name} is not defined in this theme`);
      }
    }

    for (const pair of PAIRS) {
      const foreground = tokens[pair.foreground];
      const background = tokens[pair.background];
      if (foreground === undefined || background === undefined) continue;

      const value = ratio(foreground, background);
      checked += 1;
      if (value < pair.minimum) {
        failures.push(
          `${theme.name}: --${pair.foreground} on --${pair.background} is ` +
            `${value.toFixed(2)}:1, needs ${pair.minimum}:1 (${pair.why})`,
        );
      }
    }
  }

  if (failures.length > 0) {
    console.error("check-contrast: the palette fails WCAG AA\n");
    for (const failure of failures) console.error(`  ${failure}`);
    process.exit(1);
  }

  console.log(
    `check-contrast: ${checked} pair(s) across ${THEMES.length} themes, all above threshold.`,
  );
}

main();
