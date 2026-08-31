/**
 * Deep comparison for vector expectations.
 *
 * The rules come from concept.md §8.3: deep equality, with floats compared to
 * an absolute tolerance (1e-9 unless a case overrides it). Typed arrays compare
 * equal to plain JSON arrays, because several interfaces hand back a
 * `Float32Array` while the vector file can only hold a list of numbers.
 *
 * Every mismatch returns a message that names the exact path and both values —
 * a failing vector should tell you what broke without a debugger.
 */

/** Default absolute tolerance for float comparisons (concept.md §8.3). */
export const DEFAULT_TOLERANCE = 1e-9;

/** Renders a value for an error message, including the cases JSON cannot. */
export function describeValue(value: unknown): string {
  if (value === undefined) return "undefined";
  if (typeof value === "string") return JSON.stringify(value);
  if (typeof value === "bigint") return `${value}n`;
  if (typeof value === "function") return "a function";
  if (typeof value === "number") return Object.is(value, -0) ? "-0" : String(value);
  const asArray = arrayLike(value);
  if (asArray !== null && !Array.isArray(value)) {
    const kind = (value as object).constructor.name;
    return `${kind}(${asArray.join(", ")})`;
  }
  try {
    return JSON.stringify(value) ?? String(value);
  } catch {
    return String(value);
  }
}

/**
 * Returns the elements of `value` if it behaves like an array, else null.
 *
 * Covers plain arrays and every typed array; `DataView` is deliberately not
 * array-like.
 */
function arrayLike(value: unknown): unknown[] | null {
  if (Array.isArray(value)) return value;
  if (ArrayBuffer.isView(value) && !(value instanceof DataView)) {
    return Array.from(value as unknown as ArrayLike<number>);
  }
  return null;
}

/**
 * Compares an actual return value against a vector's expectation.
 *
 * @returns `null` when they match, otherwise a message describing the mismatch.
 */
export function compareValues(
  actual: unknown,
  expected: unknown,
  tolerance: number = DEFAULT_TOLERANCE,
  path = "result",
): string | null {
  // Numbers: exact first (which also settles +/-Infinity), then tolerance.
  if (typeof expected === "number") {
    if (typeof actual !== "number") {
      return `${path}: expected the number ${describeValue(expected)}, got ${describeValue(actual)}`;
    }
    if (actual === expected) return null;
    if (Number.isNaN(actual) && Number.isNaN(expected)) return null;
    const difference = Math.abs(actual - expected);
    if (Number.isFinite(difference) && difference <= tolerance) return null;
    return (
      `${path}: expected ${describeValue(expected)}, got ${describeValue(actual)}` +
      (Number.isFinite(difference) ? ` (off by ${difference}, tolerance ${tolerance})` : "")
    );
  }

  const expectedArray = arrayLike(expected);
  if (expectedArray !== null) {
    const actualArray = arrayLike(actual);
    if (actualArray === null) {
      return `${path}: expected an array of ${expectedArray.length}, got ${describeValue(actual)}`;
    }
    if (actualArray.length !== expectedArray.length) {
      return (
        `${path}: expected ${expectedArray.length} element(s), got ${actualArray.length}` +
        ` — expected ${describeValue(expected)}, got ${describeValue(actual)}`
      );
    }
    for (let index = 0; index < expectedArray.length; index += 1) {
      const message = compareValues(
        actualArray[index],
        expectedArray[index],
        tolerance,
        `${path}[${index}]`,
      );
      if (message !== null) return message;
    }
    return null;
  }

  if (expected !== null && typeof expected === "object") {
    if (actual === null || typeof actual !== "object") {
      return `${path}: expected an object, got ${describeValue(actual)}`;
    }
    const expectedKeys = Object.keys(expected as object).sort();
    const actualKeys = Object.keys(actual as object).sort();
    const missing = expectedKeys.filter((key) => !actualKeys.includes(key));
    const extra = actualKeys.filter((key) => !expectedKeys.includes(key));
    if (missing.length > 0 || extra.length > 0) {
      const parts: string[] = [];
      if (missing.length > 0) parts.push(`missing ${missing.join(", ")}`);
      if (extra.length > 0) parts.push(`unexpected ${extra.join(", ")}`);
      return `${path}: object keys differ (${parts.join("; ")})`;
    }
    for (const key of expectedKeys) {
      const message = compareValues(
        (actual as Record<string, unknown>)[key],
        (expected as Record<string, unknown>)[key],
        tolerance,
        `${path}.${key}`,
      );
      if (message !== null) return message;
    }
    return null;
  }

  // Strings, booleans, null, undefined.
  if (actual === expected) return null;
  return `${path}: expected ${describeValue(expected)}, got ${describeValue(actual)}`;
}
