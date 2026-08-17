import type { ReactNode } from "react";
import { getSetupIcon, type SetupIconName } from "@/components/setup/SetupIcons";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { cn } from "@/lib/utils";

export const SETUP_INFO_BANNER_VARIANTS = [
  "info",
  "success",
  "warning",
  "error",
  "tip",
] as const;

export type SetupInfoBannerVariant = (typeof SETUP_INFO_BANNER_VARIANTS)[number];

const VARIANT_ICONS: Record<SetupInfoBannerVariant, SetupIconName> = {
  info: "info",
  success: "check",
  warning: "warning",
  error: "error",
  tip: "tip",
};

export type SetupInfoBannerProps = {
  variant: SetupInfoBannerVariant;
  title: string;
  description?: string;
  /** Override default variant icon */
  icon?: SetupIconName;
  children?: ReactNode;
  className?: string;
};

/** Contextual callout — compatibility hints, defaults applied, calibration, next steps. */
export function SetupInfoBanner({
  variant,
  title,
  description,
  icon,
  children,
  className,
}: SetupInfoBannerProps) {
  const tokens = wizardTheme.banner[variant];
  const Icon = getSetupIcon(icon ?? VARIANT_ICONS[variant]);
  const isAlert = variant === "warning" || variant === "error";

  return (
    <div
      className={cn(wizardTheme.banner.base, tokens.surface, className)}
      role={isAlert ? "alert" : "note"}
      aria-label={title}
    >
      <Icon className={cn(wizardTheme.banner.icon, tokens.icon)} aria-hidden />
      <div className={wizardTheme.banner.body}>
        <p className={wizardTheme.banner.title}>{title}</p>
        {description ? <p className={wizardTheme.banner.description}>{description}</p> : null}
        {children ? <div className={wizardTheme.banner.actions}>{children}</div> : null}
      </div>
    </div>
  );
}

export type SetupInfoBannerListProps = {
  children: ReactNode;
  className?: string;
};

/** Stack multiple banners with consistent spacing. */
export function SetupInfoBannerList({ children, className }: SetupInfoBannerListProps) {
  return <div className={cn(wizardTheme.banner.list, className)}>{children}</div>;
}
