import type { ReactNode } from "react";
import { SetupIcons, type SetupIconName } from "@/components/setup/SetupIcons";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { cn } from "@/lib/utils";
import type { LucideIcon } from "lucide-react";

export const SETUP_EMPTY_PRESETS = {
  noRouters: { icon: "router" as const, title: "No routers detected" },
  noSdCard: { icon: "sdCard" as const, title: "No SD card found" },
  noInternet: { icon: "internetOff" as const, title: "No internet connection" },
} as const satisfies Record<
  string,
  { icon: SetupIconName; title: string }
>;

export type SetupEmptyStateProps = {
  title: string;
  description?: string;
  icon?: SetupIconName | LucideIcon;
  children?: ReactNode;
  className?: string;
};

function resolveIcon(icon?: SetupIconName | LucideIcon): LucideIcon | undefined {
  if (!icon) return undefined;
  if (typeof icon === "string") return SetupIcons[icon];
  return icon;
}

/** Centered empty / retry state — detection failures, missing hardware, offline. */
export function SetupEmptyState({
  title,
  description,
  icon,
  children,
  className,
}: SetupEmptyStateProps) {
  const Icon = resolveIcon(icon);

  return (
    <div className={cn(wizardTheme.empty.container, className)} role="status">
      {Icon ? (
        <div className={wizardTheme.empty.iconWrap}>
          <Icon className={wizardTheme.empty.icon} aria-hidden />
        </div>
      ) : null}
      <div className="space-y-1">
        <p className={wizardTheme.empty.title}>{title}</p>
        {description ? (
          <p className={wizardTheme.empty.description}>{description}</p>
        ) : null}
      </div>
      {children ? <div className={wizardTheme.empty.actions}>{children}</div> : null}
    </div>
  );
}
