#!/usr/bin/env node
/**
 * `policybook` — browse, read and verify the registry.
 *
 * Zero runtime dependencies by design: `npx policybook add cache/sieve` should
 * start fast and never pull a tree of packages into a user's project
 *.
 */

import { boolFlag, parseArgs } from "./args";
import { disableColor, bold, cyan, dim, red, yellow } from "./ansi";
import {
  commandAdd,
  commandBench,
  commandCheck,
  commandList,
  commandNew,
  commandRender,
  commandShow,
  commandVerify,
} from "./commands";

const VERSION = "0.1.0";

interface Command {
  summary: string;
  usage: string;
  example: string;
  detail?: string;
}

const COMMANDS: Record<string, Command> = {
  add: {
    summary: "Copy a policy into your project as one self-contained file.",
    usage: "policybook add <id> [--lang ts|python|c] [--out DIR]",
    example: "npx policybook add cache/sieve --out src/cache",
    detail:
      "The copied file has no imports outside the standard library and adds no\n" +
      "  dependency to your project. Shared helpers are inlined, marked, and yours\n" +
      "  to delete. This is the recommended way to use the registry.",
  },
  list: {
    summary: "List policies, with their one-line summaries.",
    usage: "policybook list [domain] [--json]",
    example: "policybook list cache",
    detail: "A ★ marks the recommended default for a domain.",
  },
  show: {
    summary: "Print a policy's page: what it is, when to use it, and when not to.",
    usage: "policybook show <id>",
    example: "policybook show cache/sieve",
  },
  check: {
    summary: "Validate the catalog: metadata, READMEs, vectors and benchmarks.",
    usage: "policybook check",
    example: "policybook check",
  },
  verify: {
    summary: "Replay a policy's test vectors against its implementations.",
    usage: "policybook verify [id|domain|--all] [--lang ts,python,c]",
    example: "policybook verify cache/sieve --lang ts,python",
    detail:
      "Every language runs the same vectors.json, so a port is conformant when it\n" +
      "  reproduces them exactly. --lang c needs cmake and a C compiler; on Windows,\n" +
      "  run it inside WSL.",
  },
  new: {
    summary: "Scaffold a new policy, with every required file and section.",
    usage: "policybook new <domain>/<name>",
    example: "policybook new cache/my-policy",
    detail: "Each TODO marks something `policybook check` will reject if left blank.",
  },
  bench: {
    summary: "Run a domain's policies over its canonical traces.",
    usage: "policybook bench <domain|--all>",
    example: "policybook bench cache",
    detail: "Writes bench.json per policy. Follow with `policybook render`.",
  },
  render: {
    summary: "Inject the benchmark tables into the READMEs.",
    usage: "policybook render",
    example: "policybook render",
  },
};

function usage(): string {
  const width = Math.max(...Object.keys(COMMANDS).map((name) => name.length));
  const lines = [
    bold("policybook") + dim(` ${VERSION}`),
    "",
    "Runnable decision policies for systems and AI infrastructure.",
    "",
    bold("USAGE"),
    "  policybook <command> [options]",
    "",
    bold("COMMANDS"),
  ];

  for (const [name, command] of Object.entries(COMMANDS)) {
    lines.push(`  ${cyan(name.padEnd(width))}  ${command.summary}`);
  }

  lines.push(
    "",
    bold("OPTIONS"),
    "  -h, --help       Show help; `policybook <command> --help` for one command.",
    "  -v, --version    Print the version.",
    "      --no-color   Disable colour (also honours NO_COLOR).",
    "",
    dim("  policybook show cache/sieve"),
  );
  return lines.join("\n");
}

function commandUsage(name: string, command: Command): string {
  const lines = [
    bold(name) + " — " + command.summary,
    "",
    bold("USAGE"),
    `  ${command.usage}`,
  ];
  if (command.detail !== undefined) {
    lines.push("", bold("NOTES"), `  ${command.detail}`);
  }
  lines.push("", bold("EXAMPLE"), `  ${dim(command.example)}`);
  return lines.join("\n");
}

async function main(): Promise<number> {
  let args;
  try {
    args = parseArgs(process.argv.slice(2));
  } catch (error) {
    console.error(red(error instanceof Error ? error.message : String(error)));
    console.error(dim("Run `policybook --help` for usage."));
    return 2;
  }

  if (boolFlag(args, "no-color")) disableColor();

  if (boolFlag(args, "version")) {
    console.log(VERSION);
    return 0;
  }

  if (args.command === undefined || args.command === "help") {
    const topic = args.positionals[0];
    const command = topic === undefined ? undefined : COMMANDS[topic];
    console.log(command === undefined ? usage() : commandUsage(topic!, command));
    return 0;
  }

  const command = COMMANDS[args.command];
  if (command === undefined) {
    console.error(red(`unknown command "${args.command}".`));
    const near = Object.keys(COMMANDS).filter((name) => name.startsWith(args.command![0] ?? ""));
    if (near.length > 0) console.error(dim(`Did you mean: ${near.join(", ")}?`));
    console.error(dim("Run `policybook --help` for usage."));
    return 2;
  }

  if (boolFlag(args, "help")) {
    console.log(commandUsage(args.command, command));
    return 0;
  }

  switch (args.command) {
    case "list":
      return commandList(args);
    case "show":
      return commandShow(args);
    case "check":
      return commandCheck();
    case "verify":
      return await commandVerify(args);
    case "add":
      return commandAdd(args);
    case "new":
      return commandNew(args);
    case "bench":
      return commandBench(args);
    case "render":
      return commandRender(args);
    default:
      return 2;
  }
}

main()
  .then((code) => {
    process.exitCode = code;
  })
  .catch((error: unknown) => {
    console.error(red(error instanceof Error ? error.message : String(error)));
    process.exitCode = 1;
  });

export { usage, VERSION, COMMANDS };
export type { Command };
