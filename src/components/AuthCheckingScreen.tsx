import { Loader2 } from "lucide-react";
import { BrandLogo } from "@/components/BrandLogo";
import { ThemeToggle } from "@/components/ThemeToggle";

export function AuthCheckingScreen() {
  return (
    <div className="relative flex min-h-screen flex-col items-center justify-center gap-5 overflow-hidden bg-background px-4">
      <div
        aria-hidden
        className="pointer-events-none absolute inset-0 bg-primary/10 [mask-image:radial-gradient(ellipse_at_50%_30%,black,transparent_55%)]"
      />
      <div className="absolute right-3 top-3 z-10">
        <ThemeToggle />
      </div>
      <div className="relative z-10 flex flex-col items-center gap-4">
        <BrandLogo height={48} />
        <div className="flex items-center gap-2 text-sm text-muted-foreground">
          <Loader2 className="h-4 w-4 animate-spin text-primary" />
          <span>Checking connection…</span>
        </div>
      </div>
    </div>
  );
}
