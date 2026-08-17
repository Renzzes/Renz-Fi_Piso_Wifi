import type { RgbColor, RgbSystemStatus } from "@/types/api";

export function getRgbColorComponents(color: RgbColor | undefined | null): RgbColor {
  return {
    red: color?.red ?? 0,
    green: color?.green ?? 0,
    blue: color?.blue ?? 255,
  };
}

export function formatRgbColorValues(color: RgbColor | undefined | null): string {
  const c = getRgbColorComponents(color);
  return `${c.red}, ${c.green}, ${c.blue}`;
}

/** Human-readable label for RGB status rows — prefers firmware colorName. */
export function formatRgbColorLabel(
  rgb: RgbSystemStatus | undefined,
  loading = false,
): string {
  if (loading) return "Loading...";
  if (!rgb) return "—";
  if (rgb.colorName) return rgb.colorName;
  if (rgb.color) return formatRgbColorValues(rgb.color);
  return "—";
}
