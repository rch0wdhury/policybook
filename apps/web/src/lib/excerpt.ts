/**
 * Taking part of a real file, without pinning to line numbers.
 *
 * The tutorial imports every sample with `?raw` so it cannot drift from the
 * code. Line numbers would quietly undo that: `lines: [24,
 * 48]` is itself a hand-maintained copy of something about the file, and the
 * day someone adds a comment near the top, the panel shows the wrong code with
 * no error anywhere. That is worse than pasting, because it *looks* generated.
 *
 * So an excerpt is anchored to text that appears in the file. If the anchor is
 * gone, `excerpt` throws — and since the tutorial runs this at build time, the
 * build fails rather than the page lying.
 */

export interface Anchor {
  /** A substring of the first line to include. */
  from: string;
  /**
   * A substring of the last line to include.
   *
   * Omit to take everything from `from` to the end of the file.
   */
  to?: string;
  /** Drop the file's leading block comment. */
  stripHeader?: boolean;
}

/** The lines of `source` between the anchors, inclusive. */
export function excerpt(source: string, anchor: Anchor): string {
  let text = source;

  if (anchor.stripHeader === true && text.trimStart().startsWith("/**")) {
    const end = text.indexOf("*/");
    if (end !== -1) text = text.slice(end + 2).replace(/^\s*\n/, "");
  }

  if (anchor.from === "" && anchor.to === undefined) return text.trimEnd();

  const lines = text.split("\n");
  const start = lines.findIndex((line) => line.includes(anchor.from));
  if (start === -1) {
    throw new Error(
      `excerpt: no line contains ${JSON.stringify(anchor.from)}. ` +
        `The file changed and the tutorial is pointing at something that is no longer there.`,
    );
  }

  if (anchor.to === undefined) {
    return lines.slice(start).join("\n").trimEnd();
  }

  const offset = lines.slice(start).findIndex((line) => line.includes(anchor.to!));
  if (offset === -1) {
    throw new Error(
      `excerpt: found ${JSON.stringify(anchor.from)} but no ${JSON.stringify(anchor.to)} after it.`,
    );
  }

  return lines
    .slice(start, start + offset + 1)
    .join("\n")
    .trimEnd();
}

/**
 * Remove one level of leading indentation from every line.
 *
 * An excerpt taken from inside a class arrives indented by two spaces, which
 * reads as a mistake in a standalone panel.
 */
export function dedent(text: string): string {
  const lines = text.split("\n").filter((line) => line.trim() !== "");
  if (lines.length === 0) return text;

  const smallest = Math.min(
    ...lines.map((line) => line.length - line.trimStart().length),
  );
  if (smallest === 0) return text;

  return text
    .split("\n")
    .map((line) => line.slice(smallest))
    .join("\n");
}
