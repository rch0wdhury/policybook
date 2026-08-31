/**
 * Markdown to terminal text.
 *
 * `policybook show` prints the same README that GitHub and the site render
 * (concept.md §13.6), so this needs to make it readable in a terminal without
 * pretending to be a full renderer. Headings, emphasis, code, lists, tables and
 * links — that is what the policy READMEs use.
 */

import { bold, cyan, dim, green, underline, yellow } from "./ansi";

/** Apply inline markup within a single line. */
function inline(text: string): string {
  return (
    text
      // Code first: its contents must not then be treated as emphasis.
      .replace(/`([^`]+)`/g, (_match, code: string) => cyan(code))
      .replace(/\*\*([^*]+)\*\*/g, (_match, strong: string) => bold(strong))
      .replace(/(^|[^*])\*([^*]+)\*/g, (_match, before: string, emphasis: string) =>
        before + yellow(emphasis),
      )
      // Links become their text; the URL would only be noise at this width.
      .replace(/\[([^\]]+)\]\(([^)]+)\)/g, (_match, label: string) => underline(label))
  );
}

export function renderMarkdown(markdown: string): string {
  const out: string[] = [];
  let inFence = false;

  for (const raw of markdown.split("\n")) {
    const line = raw.replace(/\s+$/, "");

    if (line.startsWith("```")) {
      inFence = !inFence;
      continue;
    }
    if (inFence) {
      out.push(dim("    " + line));
      continue;
    }

    // The generated benchmark markers carry no information for a reader.
    if (line.startsWith("<!--") && line.includes("bench:")) continue;
    if (line.startsWith("<sub>")) {
      out.push(dim("  " + line.replace(/<\/?sub>/g, "")));
      continue;
    }

    const heading = /^(#{1,6})\s+(.*)$/.exec(line);
    if (heading !== null) {
      const level = heading[1]!.length;
      const text = heading[2]!;
      out.push("");
      out.push(level === 1 ? bold(underline(text)) : bold(green(text)));
      continue;
    }

    const bullet = /^(\s*)[-*]\s+(.*)$/.exec(line);
    if (bullet !== null) {
      out.push(`${bullet[1]!}  • ${inline(bullet[2]!)}`);
      continue;
    }

    // Table rules add nothing without box drawing; the rows read fine alone.
    if (/^\|[\s|:-]+\|$/.test(line)) continue;
    if (line.startsWith("|")) {
      out.push("  " + inline(line.replace(/\s*\|\s*/g, "  ").trim()));
      continue;
    }

    out.push(line.length === 0 ? "" : inline(line));
  }

  // Collapse the runs of blank lines that stripping markers leaves behind.
  const collapsed: string[] = [];
  for (const line of out) {
    if (line === "" && collapsed[collapsed.length - 1] === "") continue;
    collapsed.push(line);
  }
  return collapsed.join("\n").trim() + "\n";
}
