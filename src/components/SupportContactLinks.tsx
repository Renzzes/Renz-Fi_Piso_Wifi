import { Facebook, Phone } from "lucide-react";
import { cn } from "@/lib/utils";

const SUPPORT_FB_NAME = "Rence Bersamora";
const SUPPORT_FB_URL = "https://www.facebook.com/rence.bersamora";
const SUPPORT_PHONE = "09624816474";
const SUPPORT_PHONE_HREF = `tel:+63${SUPPORT_PHONE.replace(/^0/, "")}`;

type SupportContactLinksProps = {
  className?: string;
  compact?: boolean;
  variant?: "default" | "sidebar";
};

export function SupportContactLinks({
  className,
  compact,
  variant = "default",
}: SupportContactLinksProps) {
  if (variant === "sidebar") {
    return (
      <div
        className={cn("space-y-2 text-[11px] text-muted-foreground", compact && "text-[10px]", className)}
      >
        <p className="leading-snug text-sidebar-foreground">Need help or want to report a problem?</p>
        <div className="space-y-2">
          <a
            href={SUPPORT_FB_URL}
            target="_blank"
            rel="noopener noreferrer"
            className="block rounded-sm hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <div className="text-[10px] uppercase tracking-wide text-muted-foreground">Facebook</div>
            <div className="text-sidebar-foreground">{SUPPORT_FB_NAME}</div>
          </a>
          <a
            href={SUPPORT_PHONE_HREF}
            className="block rounded-sm hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <div className="text-[10px] uppercase tracking-wide text-muted-foreground">Phone</div>
            <div className="text-sidebar-foreground">{SUPPORT_PHONE}</div>
          </a>
        </div>
      </div>
    );
  }

  return (
    <div
      className={cn(
        "rounded-md border bg-muted/30 px-3 py-2 text-xs text-muted-foreground space-y-1.5 text-center",
        className,
      )}
    >
      <p className={cn("font-medium text-foreground", compact && "text-[11px]")}>
        Need help or want to report a problem?
      </p>
      <div className="flex flex-col items-center gap-1">
        <a
          href={SUPPORT_FB_URL}
          target="_blank"
          rel="noopener noreferrer"
          className="inline-flex items-center justify-center gap-1.5 hover:text-foreground underline-offset-2 hover:underline"
        >
          <Facebook className="h-3.5 w-3.5 shrink-0" />
          FB: {SUPPORT_FB_NAME}
        </a>
        <a
          href={SUPPORT_PHONE_HREF}
          className="inline-flex items-center justify-center gap-1.5 hover:text-foreground underline-offset-2 hover:underline"
        >
          <Phone className="h-3.5 w-3.5 shrink-0" />
          {SUPPORT_PHONE}
        </a>
      </div>
    </div>
  );
}
