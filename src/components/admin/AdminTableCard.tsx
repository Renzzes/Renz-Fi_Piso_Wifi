import type { ReactNode } from "react";
import { cn } from "@/lib/utils";

export function AdminTableCard({
  children,
  footer,
  className,
}: {
  children: ReactNode;
  footer?: ReactNode;
  className?: string;
}) {
  return (
    <div className={cn("overflow-hidden rounded-[14px] border bg-card", className)}>
      <div className="overflow-x-auto">{children}</div>
      {footer ? <div className="border-t">{footer}</div> : null}
    </div>
  );
}
