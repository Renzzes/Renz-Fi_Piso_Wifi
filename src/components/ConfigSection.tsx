import type { ReactNode } from "react";

export function ConfigSection({
  title,
  description,
  children,
  className,
}: {
  title: string;
  description?: string;
  children: ReactNode;
  className?: string;
}) {
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
