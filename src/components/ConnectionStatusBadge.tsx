import { cn } from "@/lib/utils";
import {
  connectionToneToVariant,
  type ConnectionStatusDisplay,
} from "@/lib/systemConfigurationStatus";

export function ConnectionStatusList({
  items,
}: {
  items: Array<{ label: string; status: ConnectionStatusDisplay }>;
}) {
  return (
    <div className="rounded-md border bg-muted/30 px-3">
      {items.map((item) => {
        const variant = connectionToneToVariant(item.status.tone);
        const ok = item.status.tone === "connected";
        return (
          <div
            key={item.label}
            className="flex items-center justify-between py-2 border-b last:border-0 text-sm"
          >
            <span className="text-muted-foreground">{item.label}</span>
            <span className="flex items-center gap-2">
              <span
                className={cn(
                  "h-2 w-2 rounded-full",
                  variant === "ok" && "bg-emerald-500",
                  variant === "bad" && "bg-red-500",
                  variant === "unconfigured" && "bg-muted-foreground/50",
                  variant === "unknown" && "bg-amber-500",
                )}
              />
              <span className="font-medium">{item.status.label}</span>
            </span>
          </div>
        );
      })}
    </div>
  );
}
