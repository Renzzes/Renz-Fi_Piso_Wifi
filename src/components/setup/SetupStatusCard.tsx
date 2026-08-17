import type { ReactNode } from "react";
import { CircleDashed, type LucideIcon } from "lucide-react";
import { getSetupIcon, type SetupIconName } from "@/components/setup/SetupIcons";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { cn } from "@/lib/utils";

export const SETUP_STATUS_VALUES = ["success", "warning", "error", "pending"] as const;

export type SetupStatus = (typeof SETUP_STATUS_VALUES)[number];

const STATUS_ICON_NAMES: Record<SetupStatus, SetupIconName | "pending"> = {
  success: "check",
  warning: "warning",
  error: "error",
  pending: "pending",
};

const PENDING_ICON = CircleDashed;

export type SetupStatusCardProps = {
  status: SetupStatus;
  title: string;
  description?: string;
  details?: string;
  className?: string;
};

/** Single check / diagnostic row — validation, summary, health. */
export function SetupStatusCard({
  status,
  title,
  description,
  details,
  className,
}: SetupStatusCardProps) {
  const tokens = wizardTheme.status[status];
  const iconName = STATUS_ICON_NAMES[status];
  const Icon: LucideIcon =
    iconName === "pending" ? PENDING_ICON : getSetupIcon(iconName);

  return (
    <div
      className={cn(wizardTheme.status.card, tokens.card, className)}
      role="status"
      aria-label={`${title}: ${status}`}
    >
      <Icon className={cn(wizardTheme.status.icon, tokens.icon)} aria-hidden />
      <div className={wizardTheme.status.body}>
        <p className={wizardTheme.status.title}>{title}</p>
        {description ? (
          <p className={wizardTheme.status.description}>{description}</p>
        ) : null}
        {details ? <p className={wizardTheme.status.details}>{details}</p> : null}
      </div>
    </div>
  );
}

export type SetupStatusListProps = {
  children: ReactNode;
  className?: string;
  "aria-label"?: string;
};

/** Vertical stack of SetupStatusCard items. */
export function SetupStatusList({
  children,
  className,
  "aria-label": ariaLabel = "Status checks",
}: SetupStatusListProps) {
  return (
    <div className={cn(wizardTheme.status.list, className)} role="list" aria-label={ariaLabel}>
      {children}
    </div>
  );
}
