/** Console logging for dashboard API calls (WebView / browser debugging). */
export function logDashboardApi(label: string, err: unknown) {
  console.warn(`[dashboard] ${label} failed`, err);
}
