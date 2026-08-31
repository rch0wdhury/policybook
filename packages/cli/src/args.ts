/**
 * Argument parsing, hand-rolled.
 *
 * The CLI has no runtime dependencies, deliberately: `npx policybook add
 * cache/sieve` should start fast and never pull a tree of packages into
 * someone's project. An argument parser is
 * eighty lines; a dependency is forever.
 */

/** What a command line came out as. */
export interface ParsedArgs {
  /** The first non-flag token, or undefined. */
  command: string | undefined;
  /** Remaining non-flag tokens, in order. */
  positionals: string[];
  /** Flags, with `--key=value` and `--key value` both giving a string. */
  flags: Map<string, string | boolean>;
}

/** Flags that take a value, so `--lang ts` is not read as a positional. */
const VALUE_FLAGS = new Set(["lang", "out", "python", "c-build"]);

export function parseArgs(argv: string[]): ParsedArgs {
  const positionals: string[] = [];
  const flags = new Map<string, string | boolean>();

  for (let index = 0; index < argv.length; index += 1) {
    const token = argv[index]!;

    // A bare `--` is what package managers forward; it separates nothing here.
    if (token === "--") continue;

    if (token.startsWith("--")) {
      const body = token.slice(2);
      const equals = body.indexOf("=");
      if (equals !== -1) {
        flags.set(body.slice(0, equals), body.slice(equals + 1));
        continue;
      }

      const next = argv[index + 1];
      if (VALUE_FLAGS.has(body) && next !== undefined && !next.startsWith("-")) {
        flags.set(body, next);
        index += 1;
      } else {
        flags.set(body, true);
      }
      continue;
    }

    if (token.startsWith("-") && token.length > 1) {
      // Only the conventional short flags; this is not a getopt.
      for (const letter of token.slice(1)) {
        if (letter === "h") flags.set("help", true);
        else if (letter === "v") flags.set("version", true);
        else throw new Error(`unknown option -${letter}`);
      }
      continue;
    }

    positionals.push(token);
  }

  return { command: positionals.shift(), positionals, flags };
}

/** Read a flag as a string, or undefined if absent or valueless. */
export function stringFlag(args: ParsedArgs, name: string): string | undefined {
  const value = args.flags.get(name);
  return typeof value === "string" ? value : undefined;
}

/** Read a flag as a boolean. */
export function boolFlag(args: ParsedArgs, name: string): boolean {
  return args.flags.get(name) === true;
}

/** Split a comma-separated flag such as `--lang ts,python`. */
export function listFlag(args: ParsedArgs, name: string): string[] | undefined {
  const value = stringFlag(args, name);
  if (value === undefined) return undefined;
  return value
    .split(",")
    .map((entry) => entry.trim())
    .filter((entry) => entry.length > 0);
}
