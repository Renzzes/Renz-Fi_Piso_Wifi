/** Format ISO build timestamp as YYYY-MM-DD for installer-facing UI. */
export function formatBuildDate(iso: string | undefined): string {
  if (!iso) return "—";
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return iso.slice(0, 10);
  return d.toISOString().slice(0, 10);
}

export function formatBuildNumber(value: number | undefined): string {
  if (value === undefined || value === null) return "—";
  return String(value);
}
