/**
 * Where the repository is, said once.
 *
 * It was said in four places before this file existed — a layout, a code panel,
 * a policy page and a star button — which is three chances to move the project
 * and leave a dead link behind. The star button needs the owner and name apart
 * from the URL anyway, for the API call, so they are the source and the URLs
 * are derived.
 */

/**
 * The GitHub account and repository the site links to.
 *
 * Settable, and it matters: the `policybook` organisation on GitHub was
 * registered by someone else in June 2024, so this project cannot use it
 * without acquiring it. Every link on the site derives from these two strings,
 * so pointing the site at a different account is one environment variable at
 * build time rather than an edit across the tree.
 *
 * The defaults are what the project *wants*; whether it gets them is a launch
 * decision, not a code one.
 */
export const OWNER = import.meta.env.PUBLIC_REPO_OWNER ?? "rch0wdhury";
export const NAME = import.meta.env.PUBLIC_REPO_NAME ?? "policybook";

/** `owner/name`, as the GitHub API and CLI both spell it. */
export const SLUG = `${OWNER}/${NAME}`;

export const REPO_URL = `https://github.com/${SLUG}`;

/** A file, on the default branch. */
export function blobUrl(path: string): string {
  return `${REPO_URL}/blob/main/${path}`;
}

/** A directory, on the default branch. */
export function treeUrl(path: string): string {
  return `${REPO_URL}/tree/main/${path}`;
}

/**
 * The star count as of the last build.
 *
 * A fallback, not a claim: the button prefers a live count and falls back to
 * this when the API is unreachable, rate-limited, or blocked by the reader's
 * browser. Zero means "no build-time figure", and the button then shows no
 * number at all rather than an honest-looking zero.
 */
export const STARS_AT_BUILD = 0;
