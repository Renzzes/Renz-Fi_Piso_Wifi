import type { ReactNode } from "react";
import { Label } from "@/components/ui/label";
import { cn } from "@/lib/utils";

export function ConfigCard({
  id,
  title,
  description,
  actions,
  children,
  className,
}: {
  id?: string;
  title: string;
  description?: string;
  actions?: ReactNode;
  children: ReactNode;
  className?: string;
}) {
  return (
    <section
      id={id}
      className={cn(
        "w-full scroll-mt-[3.5rem] space-y-4 rounded-[14px] border bg-card p-4",
        className,
      )}
    >
      <div className="flex flex-wrap items-start justify-between gap-2">
        <div className="min-w-0">
          <h3 className="text-[15px] font-semibold leading-tight">{title}</h3>
          {description ? (
            <p className="mt-0.5 text-[12px] text-muted-foreground">{description}</p>
          ) : null}
        </div>
        {actions ? (
          <div className="flex shrink-0 flex-wrap items-center gap-2">{actions}</div>
        ) : null}
      </div>
      {children}
    </section>
  );
}

export function ConfigField({
  label,
  htmlFor,
  hint,
  className,
  children,
}: {
  label: string;
  htmlFor?: string;
  hint?: ReactNode;
  className?: string;
  children: ReactNode;
}) {
  return (
    <div className={cn("min-w-0 space-y-1.5", className)}>
      <Label htmlFor={htmlFor} className="text-[11px] font-medium text-muted-foreground">
        {label}
      </Label>
      {children}
      {hint ? <div className="text-[11px] text-muted-foreground">{hint}</div> : null}
    </div>
  );
}

export function ConfigFormGrid({ children }: { children: ReactNode }) {
  return <div className="grid grid-cols-1 gap-x-4 gap-y-3 sm:grid-cols-2">{children}</div>;
}
