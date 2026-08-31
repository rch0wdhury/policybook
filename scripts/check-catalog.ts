/**
 * Validates every policy in the registry.
 *
 * The rules themselves live in `packages/vectors/src/catalog.ts`, so that this
 * script and the `policybook check` command enforce exactly the same ones
 * rather than drifting apart.
 *
 * Usage: pnpm check
 */

import { checkCatalog, reportCatalog } from "../packages/vectors/src/catalog";

process.exit(reportCatalog(checkCatalog()));
