import type { ReactNode } from "react";
import { CollapsiblePanel } from "@/components/CollapsiblePanel";

export function ConfigSection({
  title,
  description,
  children,
  className,
  panelId,
  summary,
}: {
  title: string;
  description?: string;
  children: ReactNode;
  className?: string;
  /** When set, section collapses; details hidden until expanded. */
  panelId?: string;
  summary?: ReactNode;
}) {
  if (panelId) {
    return (
      <CollapsiblePanel
        id={panelId}
        title={title}
        summary={summary ?? description ?? "Tap to show complete details"}
        className={className}
      >
        {description ? (
          <p className="text-xs text-muted-foreground">{description}</p>
        ) : null}
        <div className="space-y-3">{children}</div>
      </CollapsiblePanel>
    );
  }

  return (
    <section className={`rounded-md border bg-card p-3 space-y-3 ${className ?? ""}`}>
      <div>
        <h2 className="text-sm font-medium">{title}</h2>
        {description ? (
          <p className="text-xs text-muted-foreground mt-0.5">{description}</p>
        ) : null}
      </div>
      {children}
    </section>
  );
}
