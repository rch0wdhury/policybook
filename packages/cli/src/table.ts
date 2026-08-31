/**
 * A table that fits the terminal.
 *
 * Columns take the width their content needs, and the widest flexible column
 * gives up whatever is left over, so a long summary is truncated rather than
 * wrapping into an unreadable mess on a narrow terminal.
 */

import { bold, dim, padEnd, truncate, visibleWidth } from "./ansi";

export interface Column {
  header: string;
  /** Cell text for a row, already coloured if it should be. */
  value: (row: number) => string;
  /** This column absorbs the leftover width when the terminal is narrow. */
  flexible?: boolean;
  /** Never shrink below this. */
  minWidth?: number;
}

const GAP = 2;

export function renderTable(columns: Column[], rowCount: number): string {
  if (rowCount === 0) return dim("  (nothing to show)");

  const cells = columns.map((column) =>
    Array.from({ length: rowCount }, (_unused, row) => column.value(row)),
  );

  const natural = columns.map((column, index) =>
    Math.max(visibleWidth(column.header), ...cells[index]!.map(visibleWidth)),
  );

  // Shrink the flexible column to whatever the terminal has left.
  const terminal = process.stdout.columns ?? 100;
  const widths = [...natural];
  const flexible = columns.findIndex((column) => column.flexible === true);
  if (flexible !== -1) {
    const others = widths.reduce(
      (total, width, index) => (index === flexible ? total : total + width + GAP),
      0,
    );
    const available = terminal - others - GAP;
    const minimum = columns[flexible]?.minWidth ?? 20;
    widths[flexible] = Math.max(minimum, Math.min(natural[flexible]!, available));
  }

  const lines: string[] = [];
  lines.push(
    "  " +
      columns.map((column, index) => bold(padEnd(column.header, widths[index]!))).join("  ").trimEnd(),
  );

  for (let row = 0; row < rowCount; row += 1) {
    lines.push(
      "  " +
        columns
          .map((_column, index) => padEnd(truncate(cells[index]![row]!, widths[index]!), widths[index]!))
          .join("  ")
          .trimEnd(),
    );
  }

  return lines.join("\n");
}
