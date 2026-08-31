/**
 * Rendering the repository's own markdown, and pulling structure out of it.
 *
 * The domain and policy pages are the READMEs. Not a copy, not a summary — the
 * same file, rendered. That is what stops a page and a
 * repository from disagreeing, and it means the effort that went into a
 * README's "when not to use it" section reaches a reader rather than sitting in
 * a directory.
 *
 * The processor is Astro's own, so a fenced block on a policy page gets the
 * same build-time Shiki highlighting, wired to the same tokens, as one written
 * in a `.astro` file.
 */

import { createMarkdownProcessor } from "@astrojs/markdown-remark";

let processor: Awaited<ReturnType<typeof createMarkdownProcessor>> | null = null;

async function getProcessor() {
  processor ??= await createMarkdownProcessor({
    shikiConfig: { theme: "css-variables", wrap: false },
    gfm: true,
  });
  return processor;
}

/** Render a markdown string to HTML. */
export async function renderMarkdown(markdown: string): Promise<string> {
  const { code } = await (await getProcessor()).render(markdown);
  return code;
}

/**
 * Rewrites the relative links a README uses into site URLs.
 *
 * A README links to its neighbours the way a file does — `../streaming-llm/` —
 * which is right on GitHub and wrong on a site whose policy pages live under
 * `/p/<domain>/<policy>/`. Rewriting rather than editing the READMEs keeps
 * both correct from one source.
 */
export function rewriteLinks(markdown: string, domain: string, base: string): string {
  return (
    markdown
      // `../streaming-llm/` and `streaming-llm/` -> the policy page.
      .replace(/\]\(\.\.\/([a-z0-9-]+)\/\)/g, `](${base}p/${domain}/$1/)`)
      .replace(/\]\((?!https?:|\/|#|\.\.)([a-z0-9-]+)\/\)/g, `](${base}p/${domain}/$1/)`)
      // A domain README pointing at its own directory.
      .replace(/\]\(\.\.\/\.\.\/policies\/([a-z0-9-]+)\/\)/g, `](${base}d/$1/)`)
  );
}

/** One markdown table, as headers plus rows of cell text. */
export interface MarkdownTable {
  headers: string[];
  rows: string[][];
}

function splitRow(line: string): string[] {
  return line
    .trim()
    .replace(/^\|/, "")
    .replace(/\|$/, "")
    .split("|")
    .map((cell) => cell.trim());
}

/**
 * The first table under a heading matching `headingPattern`.
 *
 * Used for the decision tables: those are written once, in the domain README,
 * where they are reviewed alongside the policies they describe. Parsing rather
 * than duplicating means the site cannot recommend something the README does
 * not.
 *
 * Returns null when the section or its table is absent, which is a normal state
 * for a domain whose README has not been written yet rather than an error.
 */
export function tableUnderHeading(
  markdown: string,
  headingPattern: RegExp,
): MarkdownTable | null {
  const lines = markdown.split("\n");

  let index = lines.findIndex((line) => /^#{2,3}\s/.test(line) && headingPattern.test(line));
  if (index === -1) return null;

  // Walk to the first table row, stopping at the next heading so a section
  // without a table does not borrow the following section's.
  while (index < lines.length) {
    index += 1;
    const line = lines[index];
    if (line === undefined) return null;
    if (/^#{1,3}\s/.test(line)) return null;
    if (line.trim().startsWith("|")) break;
  }

  const headerLine = lines[index];
  const separator = lines[index + 1];
  if (headerLine === undefined || separator === undefined) return null;
  if (!/^\s*\|[\s:|-]+\|\s*$/.test(separator)) return null;

  const headers = splitRow(headerLine);
  const rows: string[][] = [];

  for (let i = index + 2; i < lines.length; i += 1) {
    const line = lines[i];
    if (line === undefined || !line.trim().startsWith("|")) break;
    rows.push(splitRow(line));
  }

  return { headers, rows };
}

/**
 * The prose between the title and the first `##`, as markdown.
 *
 * A README's opening is written to be the first thing a reader sees, which is
 * exactly what a domain page needs at the top. Taking it from there rather than
 * writing a second version means there is one to keep good.
 */
export function intro(markdown: string): string {
  const withoutTitle = markdown.replace(/^#[^\n]*\n+/, "");
  const nextHeading = withoutTitle.search(/^##\s/m);
  return (nextHeading === -1 ? withoutTitle : withoutTitle.slice(0, nextHeading)).trim();
}

/**
 * Removes whole `## sections` from a README, heading included.
 *
 * Only safe where the section is *entirely* generated — the Benchmark section
 * is written between `bench:start` and `bench:end` markers and contains nothing
 * a person wrote — because it takes any `###` subsections with it.
 */
export function dropSections(markdown: string, headingPatterns: RegExp[]): string {
  const lines = markdown.split("\n");
  const kept: string[] = [];
  let skipping = false;

  for (const line of lines) {
    if (/^##\s/.test(line)) {
      skipping = headingPatterns.some((pattern) => pattern.test(line));
    }
    if (!skipping) kept.push(line);
  }

  return kept.join("\n");
}

/**
 * Removes the first table under a heading, and nothing else.
 *
 * For the Parameters section, where the page renders a better table from
 * `policy.json` — typed, and guaranteed in sync — but the prose *around* the
 * table is often the most valuable thing on the page. H2O's `recentWindow`
 * sweep is a `###` subsection under Parameters and is the entire argument of
 * that policy's page.
 *
 * Two earlier attempts took it out: hiding every table in the rendered README
 * with CSS, then dropping the whole Parameters section. Both were reasonable
 * rules that removed something they were never aimed at, which is the argument
 * for cutting exactly what is duplicated and nothing more.
 */
export function dropFirstTableUnder(markdown: string, headingPattern: RegExp): string {
  const lines = markdown.split("\n");
  const start = lines.findIndex((line) => /^##\s/.test(line) && headingPattern.test(line));
  if (start === -1) return markdown;

  // Find the table, stopping at the next heading of any level so a section
  // without one does not consume the following section's.
  let tableStart = -1;
  for (let i = start + 1; i < lines.length; i += 1) {
    const line = lines[i]!;
    if (/^#{1,6}\s/.test(line)) break;
    if (line.trim().startsWith("|")) {
      tableStart = i;
      break;
    }
  }
  if (tableStart === -1) return markdown;

  let tableEnd = tableStart;
  while (tableEnd < lines.length && lines[tableEnd]!.trim().startsWith("|")) {
    tableEnd += 1;
  }

  return [...lines.slice(0, tableStart), ...lines.slice(tableEnd)].join("\n");
}

/** A named `## section` of a README, as markdown, without its heading. */
export function section(markdown: string, headingPattern: RegExp): string | null {
  const lines = markdown.split("\n");
  const start = lines.findIndex((line) => /^##\s/.test(line) && headingPattern.test(line));
  if (start === -1) return null;

  const body: string[] = [];
  for (let i = start + 1; i < lines.length; i += 1) {
    const line = lines[i]!;
    if (/^##\s/.test(line)) break;
    body.push(line);
  }
  return body.join("\n").trim();
}
