import { cn } from "@/lib/utils";

export const SYSTEM_CONFIG_SECTIONS = [
  { id: "syscfg-overview", label: "Overview" },
  { id: "syscfg-network", label: "Network" },
  { id: "syscfg-wireless", label: "Wireless" },
  { id: "syscfg-hotspot", label: "Bandwidth" },
  { id: "syscfg-storage", label: "Storage & Firmware" },
  { id: "syscfg-router", label: "Router Status" },
] as const;

export type SystemConfigSectionId = (typeof SYSTEM_CONFIG_SECTIONS)[number]["id"];

export function isSystemConfigSectionId(value: string): value is SystemConfigSectionId {
  return SYSTEM_CONFIG_SECTIONS.some((section) => section.id === value);
}

export function readSystemConfigSection(): SystemConfigSectionId {
  if (typeof window === "undefined") return "syscfg-overview";
  const hash = window.location.hash.replace(/^#/, "");
  return isSystemConfigSectionId(hash) ? hash : "syscfg-overview";
}

export function ConfigSectionNav({
  active,
  onChange,
}: {
  active: SystemConfigSectionId;
  onChange: (id: SystemConfigSectionId) => void;
}) {
  return (
    <nav
      aria-label="System configuration sections"
      className="sticky top-0 z-20 -mx-3 border-b bg-background/95 px-3 backdrop-blur md:-mx-4 md:px-4 supports-[backdrop-filter]:bg-background/80"
    >
      <div className="flex h-11 items-center gap-1 overflow-x-auto">
        {SYSTEM_CONFIG_SECTIONS.map((section) => {
          const isActive = active === section.id;
          return (
            <button
              key={section.id}
              type="button"
              onClick={() => onChange(section.id)}
              className={cn(
                "h-8 shrink-0 rounded-md px-3 text-[12px] font-medium whitespace-nowrap transition-colors",
                isActive
                  ? "bg-primary text-primary-foreground"
                  : "text-muted-foreground hover:bg-muted hover:text-foreground",
              )}
            >
              {section.label}
            </button>
          );
        })}
      </div>
    </nav>
  );
}
