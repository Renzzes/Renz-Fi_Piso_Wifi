export const GITHUB_OWNER = "Renzzes";
export const GITHUB_REPO = "Renz-Fi_Piso_Wifi";
export const GITHUB_REPO_URL = `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}`;

export class GithubOfflineError extends Error {
  constructor() {
    super("To update, connect to the internet");
    this.name = "GithubOfflineError";
  }
}

export type GithubReleaseInfo = {
  tag: string;
  name: string | null;
  htmlUrl: string;
  publishedAt: string | null;
};

function githubApi(path: string, signal: AbortSignal) {
  return fetch(`https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}${path}`, {
    headers: { Accept: "application/vnd.github+json" },
    signal,
  });
}

export function isGithubOfflineError(error: unknown): boolean {
  return error instanceof GithubOfflineError;
}

export function normalizeVersionLabel(value: string | null | undefined): string {
  return (value ?? "").trim().replace(/^v/i, "").toLowerCase();
}

export async function fetchLatestGithubRelease(): Promise<GithubReleaseInfo> {
  if (typeof navigator !== "undefined" && navigator.onLine === false) {
    throw new GithubOfflineError();
  }

  const controller = new AbortController();
  const timer = window.setTimeout(() => controller.abort(), 10_000);

  try {
    const latestRes = await githubApi("/releases/latest", controller.signal);
    if (latestRes.ok) {
      const json = (await latestRes.json()) as {
        tag_name?: string;
        name?: string;
        html_url?: string;
        published_at?: string;
      };
      const tag = json.tag_name?.trim();
      if (tag) {
        return {
          tag,
          name: json.name?.trim() || tag,
          htmlUrl: json.html_url || `${GITHUB_REPO_URL}/releases/tag/${encodeURIComponent(tag)}`,
          publishedAt: json.published_at ?? null,
        };
      }
    }

    const tagsRes = await githubApi("/tags?per_page=1", controller.signal);
    if (!tagsRes.ok) {
      throw new Error("Unable to read GitHub release tags");
    }
    const tags = (await tagsRes.json()) as Array<{ name?: string }>;
    const tag = tags[0]?.name?.trim();
    if (!tag) throw new Error("No GitHub tags found");
    return {
      tag,
      name: tag,
      htmlUrl: `${GITHUB_REPO_URL}/releases/tag/${encodeURIComponent(tag)}`,
      publishedAt: null,
    };
  } catch (error) {
    if (error instanceof GithubOfflineError) throw error;
    if (error instanceof DOMException && error.name === "AbortError") {
      throw new GithubOfflineError();
    }
    if (error instanceof TypeError) throw new GithubOfflineError();
    throw error;
  } finally {
    window.clearTimeout(timer);
  }
}
