import { useEffect, useState, type ReactNode } from "react";
import { ChevronDown, ChevronRight } from "lucide-react";
import { cn } from "@/lib/utils";
import {
  Collapsible,
  CollapsibleContent,
  CollapsibleTrigger,
} from "@/components/ui/collapsible";

const STORAGE_PREFIX = "renzfi.panel.";

type CollapsiblePanelProps = {
  id: string;
  title: string;
  /** Shown when collapsed — keep brief. */
  summary: ReactNode;
  children: ReactNode;
  className?: string;
  /** Default collapsed unless localStorage says otherwise. */
  defaultOpen?: boolean;
};

export function CollapsiblePanel({
  id,
  title,
  summary,
  children,
  className,
  defaultOpen = false,
}: CollapsiblePanelProps) {
  const storageKey = `${STORAGE_PREFIX}${id}`;
  const [open, setOpen] = useState(() => {
    try {
      const raw = localStorage.getItem(storageKey);
      if (raw === "1") return true;
      if (raw === "0") return false;
    } catch {
      /* ignore */
    }
    return defaultOpen;
  });

  useEffect(() => {
    try {
      localStorage.setItem(storageKey, open ? "1" : "0");
    } catch {
      /* ignore */
    }
  }, [open, storageKey]);

  return (
    <Collapsible open={open} onOpenChange={setOpen} className={cn("rounded-md border bg-card", className)}>
      <div className="p-3 space-y-2">
        <CollapsibleTrigger asChild>
          <button
            type="button"
            className="flex w-full items-start justify-between gap-2 text-left"
          >
            <div className="min-w-0 space-y-1">
              <div className="text-sm font-medium">{title}</div>
              {!open ? (
                <div className="text-xs text-muted-foreground">{summary}</div>
              ) : null}
            </div>
            <span className="shrink-0 text-xs text-muted-foreground flex items-center gap-1 pt-0.5">
              {open ? "Hide details" : "Show Complete Details"}
              {open ? (
                <ChevronDown className="h-3.5 w-3.5" />
              ) : (
                <ChevronRight className="h-3.5 w-3.5" />
              )}
            </span>
          </button>
        </CollapsibleTrigger>
        <CollapsibleContent className="space-y-2 pt-1">
          {children}
        </CollapsibleContent>
      </div>
    </Collapsible>
  );
}
