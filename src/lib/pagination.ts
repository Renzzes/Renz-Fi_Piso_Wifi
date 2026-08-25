export const PAGE_SIZES = [12, 24, 48] as const;
export const PAGE_SIZE_DEFAULT = 12;

export function clampPage(page: number, totalItems: number, pageSize: number): number {
  const totalPages = Math.max(1, Math.ceil(totalItems / pageSize) || 1);
  return Math.min(Math.max(1, page), totalPages);
}

export function pageSlice<T>(items: T[], page: number, pageSize: number): T[] {
  const safe = clampPage(page, items.length, pageSize);
  const start = (safe - 1) * pageSize;
  return items.slice(start, start + pageSize);
}
