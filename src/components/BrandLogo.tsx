import logoMarkSrc from "../../public/logo-mark.png";
import { cn } from "@/lib/utils";

type BrandLogoProps = {
  className?: string;
  /** Visible logo height in CSS pixels (tight cropped mark). */
  height?: number;
  alt?: string;
};

/**
 * Login / auth brand mark — cropped so padding in logo2.png cannot shrink text.
 * Dark mode matches the Admin sidebar invert treatment.
 */
export function BrandLogo({
  className,
  height = 56,
  alt = "Renz-Fi",
}: BrandLogoProps) {
  const width = Math.round(height * (381 / 103));
  return (
    <img
      src={logoMarkSrc}
      alt={alt}
      width={width}
      height={height}
      decoding="async"
      className={cn(
        "mx-auto max-w-full object-contain dark:brightness-0 dark:invert",
        className,
      )}
      style={{ height, width: "auto" }}
    />
  );
}
