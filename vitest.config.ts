import { fileURLToPath } from "node:url";
import { defineConfig } from "vitest/config";

const resolvePath = (relative: string): string =>
  fileURLToPath(new URL(relative, import.meta.url));

export default defineConfig({
  resolve: {
    alias: {
      "@policybook/core": resolvePath("./packages/core/src/index.ts"),
      "@policybook/vectors": resolvePath("./packages/vectors/src/index.ts"),
    },
  },
  test: {
    // Package tests, per-policy tests, and script tests all run in one node
    // environment. A separate browser-ish project is added with the web app
    // (T38/T41), which is the only part of the tree that needs a DOM.
    include: [
      "packages/*/src/**/*.test.ts",
      "packages/*/tests/**/*.test.ts",
      "policies/**/*.test.ts",
      "scripts/**/*.test.ts",
      // The site's runner logic. It needs no DOM: the simulations, the URL
      // codec and the worker's state machine are all plain functions, which is
      // most of why they are separated from the worker that runs them.
      "apps/web/src/**/*.test.ts",
    ],
    exclude: ["**/node_modules/**", "**/dist/**"],
    passWithNoTests: true,
    environment: "node",
  },
});
