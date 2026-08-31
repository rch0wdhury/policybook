/**
 * Stable JSON serialisation for every generated file in the repository.
 *
 * Generated artefacts (`vectors.json`, `bench.json`, trace prefixes) are
 * committed, so their bytes must be reproducible: regenerating without a
 * behaviour change has to produce a zero diff. That means one indent style, key
 * order fixed by insertion, and a trailing newline.
 */

/** Serialise `value` as the repository's canonical JSON: 2-space indent, trailing newline. */
export function stableJson(value: unknown): string {
  return `${JSON.stringify(value, null, 2)}\n`;
}
