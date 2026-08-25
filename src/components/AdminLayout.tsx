import { useState, useEffect } from "react";
import logo2Src from "../../public/logo2.png";
import { Link, Outlet, useLocation } from "react-router-dom";
import { useQuery } from "@tanstack/react-query";
import {
  LayoutDashboard,
  Tag,
  Ticket,
  Users,
  BarChart3,
  Globe,
  Coins,
  SlidersHorizontal,
  Wifi,
  ScrollText,
  Upload,
  Settings,
  Menu,
  X,
  LogOut,
} from "lucide-react";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { ThemeToggle } from "@/components/ThemeToggle";
import { useRealtime } from "@/contexts/RealtimeContext";
import { fetchInstallationState, isProductionInstallationState } from "@/lib/installationState";
import { useIsMobile } from "@/hooks/use-mobile";
import {
  DEFAULT_OPERATOR_PERMISSIONS,
  pathPermission,
  type OperatorPermission,
} from "@/lib/operatorPermissions";
import { SupportContactLinks } from "@/components/SupportContactLinks";
import { fetchApplianceBuildSnapshot } from "@/services/applianceBuild";

const nav: {
  to: string;
  label: string;
  icon: typeof LayoutDashboard;
  permission?: OperatorPermission | null;
  ownerOnly?: boolean;
}[] = [
  { to: "/dashboard", label: "Dashboard", icon: LayoutDashboard, permission: "dashboard" },
  { to: "/promo-rates", label: "Promo Rates", icon: Tag, permission: "promo-rates" },
  { to: "/vouchers", label: "Vouchers", icon: Ticket, permission: "vouchers" },
  { to: "/active-users", label: "Active Users", icon: Users, permission: "active-users" },
  { to: "/sales-reports", label: "Sales Reports", icon: BarChart3, permission: "sales-reports" },
  {
    to: "/captive-portal",
    label: "Captive Portal",
    icon: Globe,
    permission: "captive-portal",
  },
  {
    to: "/access-points",
    label: "Access Points",
    icon: Wifi,
    ownerOnly: true,
  },
  { to: "/coin-settings", label: "Coin Settings", icon: Coins, permission: "coin-settings" },
  {
    to: "/system-configuration",
    label: "System Configuration",
    icon: SlidersHorizontal,
    permission: "system-configuration",
  },
  { to: "/logs", label: "Logs", icon: ScrollText, permission: "logs" },
  { to: "/firmware", label: "Update", icon: Upload, permission: "firmware" },
  { to: "/system-settings", label: "System Settings", icon: Settings, ownerOnly: true },
];

export type AdminOutletContext = {
  isOwner: boolean;
  permissions: OperatorPermission[];
};

type AdminLayoutProps = {
  adminIp: string;
  onDisconnect: () => void;
  connectionLost?: boolean;
  isOwner: boolean;
  permissions?: OperatorPermission[];
};

function LiveUpdatesBadge() {
  const { sseConnected, sseReconnecting } = useRealtime();
  const label = sseConnected
    ? "Live Updates: Connected"
    : sseReconnecting
      ? "Live Updates: Reconnecting"
      : "Live Updates: Disconnected";

  const tone = sseConnected
    ? "bg-emerald-500/15 text-emerald-700 dark:text-emerald-400"
    : "bg-amber-500/15 text-amber-800 dark:text-amber-300";
  const dot = (
    <span
      className={cn(
        "h-1.5 w-1.5 rounded-full",
        sseConnected ? "bg-emerald-500" : "bg-amber-500 animate-pulse",
      )}
    />
  );

  return (
    <>
      <div
        className={cn(
          "flex h-8 w-8 items-center justify-center rounded-md sm:hidden",
          tone,
        )}
        title={label}
        aria-label={label}
      >
        {dot}
      </div>
      <div
        className={cn(
          "hidden max-w-[min(100%,14rem)] items-center gap-1.5 truncate rounded-md px-2.5 py-1 text-[11px] font-medium sm:flex",
          tone,
        )}
        title={label}
      >
        {dot}
        <span className="truncate">{label}</span>
      </div>
    </>
  );
}

export function AdminLayout({
  adminIp,
  onDisconnect,
  connectionLost = false,
  isOwner,
  permissions = DEFAULT_OPERATOR_PERMISSIONS,
}: AdminLayoutProps) {
  const [open, setOpen] = useState(false);
  const [collapsed, setCollapsed] = useState(false);
  const { pathname } = useLocation();
  const displayHost = adminIp;
  const isMobile = useIsMobile();
  const { data: installationState } = useQuery({
    queryKey: ["health", "installationState"],
    queryFn: fetchInstallationState,
    staleTime: 60_000,
  });
  const { data: buildSnapshot } = useQuery({
    queryKey: ["appliance", "build"],
    queryFn: fetchApplianceBuildSnapshot,
    staleTime: 60_000,
  });
  const productionInstalled = isProductionInstallationState(installationState);
  void productionInstalled;

  const versionLabel = buildSnapshot?.runningFirmwareVersion
    ? buildSnapshot.runningFirmwareVersion.startsWith("v")
      ? buildSnapshot.runningFirmwareVersion
      : `v${buildSnapshot.runningFirmwareVersion}`
    : null;

  const visibleNav = nav.filter((item) => {
    if (item.ownerOnly) return isOwner;
    if (isOwner) return true;
    const key = item.permission ?? pathPermission(item.to);
    if (!key) return false;
    return permissions.includes(key);
  });

  useEffect(() => setOpen(false), [pathname]);

  const compact = collapsed && !isMobile;

  const onMenuClick = () => {
    if (typeof window !== "undefined" && window.innerWidth < 768) setOpen((v) => !v);
    else setCollapsed((v) => !v);
  };

  return (
    <div
      className={cn(
        "flex min-h-screen min-w-0 bg-background text-foreground",
        connectionLost && "pointer-events-none select-none opacity-60",
      )}
      aria-hidden={connectionLost || undefined}
    >
      <aside
        className={cn(
          "fixed inset-y-0 left-0 z-40 flex w-[min(220px,85vw)] flex-col border-r border-sidebar-border bg-sidebar text-sidebar-foreground transition-all duration-300 md:static md:translate-x-0 md:shrink-0",
          compact ? "md:w-16" : "md:w-[220px]",
          open ? "translate-x-0" : "-translate-x-full",
        )}
      >
        <div className="flex h-14 items-center gap-2 border-b border-sidebar-border px-3">
          <div className={cn("relative h-9 overflow-hidden", compact ? "w-9" : "w-[148px]")}>
            <img
              src={logo2Src}
              alt="Renz-Fi logo"
              className="absolute left-1/2 top-1/2 max-w-none -translate-x-1/2 -translate-y-1/2 dark:brightness-0 dark:invert"
              style={{ width: compact ? 86 : 200 }}
            />
          </div>
        </div>
        <nav className="min-h-0 flex-1 space-y-0.5 overflow-y-auto px-2 py-3">
          {visibleNav.map((item) => {
            const active = pathname === item.to;
            const Icon = item.icon;
            return (
              <Link
                key={item.to}
                to={item.to}
                title={item.label}
                className={cn(
                  "flex items-center gap-2.5 rounded-md px-2.5 py-2 text-[14px] transition-colors hover:bg-sidebar-accent hover:text-sidebar-accent-foreground",
                  compact && "justify-center px-0",
                  active &&
                    "bg-primary text-primary-foreground shadow-none hover:bg-primary hover:text-primary-foreground dark:shadow-[0_0_16px_rgba(37,99,235,0.45)]",
                )}
              >
                <Icon className="h-4 w-4 shrink-0" />
                {compact ? <span className="sr-only">{item.label}</span> : item.label}
              </Link>
            );
          })}
        </nav>
        <div className="shrink-0 border-t border-sidebar-border px-3 py-3">
          {compact ? null : (
            <>
              <SupportContactLinks compact variant="sidebar" />
              <div className="mt-2 truncate text-[10px] text-muted-foreground" title={displayHost}>
                {displayHost}
              </div>
              <div className="my-3 border-t border-sidebar-border" />
            </>
          )}
          <Button
            type="button"
            variant="ghost"
            size="sm"
            className={cn(
              "w-full justify-start gap-2 text-sidebar-foreground hover:bg-sidebar-accent hover:text-sidebar-accent-foreground",
              compact && "justify-center px-0",
            )}
            onClick={onDisconnect}
          >
            <LogOut className="h-4 w-4" />
            {compact ? <span className="sr-only">Log out</span> : "Log out"}
          </Button>
          <div className="mt-3 text-center text-[11px] text-muted-foreground">
            {versionLabel ?? "—"}
          </div>
        </div>
      </aside>

      <div className="flex min-w-0 flex-1 flex-col">
        <header className="sticky top-0 z-30 flex h-14 min-w-0 items-center gap-1.5 border-b bg-background/90 px-2 backdrop-blur sm:gap-2 sm:px-3 md:px-4">
          <Button
            type="button"
            variant="ghost"
            size="icon"
            onClick={onMenuClick}
            aria-label={
              isMobile
                ? open
                  ? "Close menu"
                  : "Open menu"
                : collapsed
                  ? "Expand sidebar"
                  : "Collapse sidebar"
            }
            className="shrink-0 text-muted-foreground hover:bg-accent hover:text-foreground"
          >
            {open && isMobile ? <X className="h-5 w-5" /> : <Menu className="h-5 w-5" />}
          </Button>
          <div className="min-w-0 flex-1" />
          <LiveUpdatesBadge />
          <ThemeToggle />
          <div className="flex min-w-0 items-center gap-2 pl-0.5 sm:pl-1">
            <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-full bg-primary text-[11px] font-semibold text-primary-foreground">
              {isOwner ? "AD" : "OP"}
            </div>
            <span className="hidden text-[13px] font-medium sm:inline">
              {isOwner ? "Admin" : "Operator"}
            </span>
          </div>
        </header>
        <main className="min-w-0 flex-1 overflow-x-hidden overflow-y-auto p-3 md:p-4">
          <Outlet context={{ isOwner, permissions } satisfies AdminOutletContext} />
        </main>
      </div>

      {open ? (
        <button
          type="button"
          className="fixed inset-0 z-30 bg-black/40 md:hidden"
          aria-label="Close menu overlay"
          onClick={() => setOpen(false)}
        />
      ) : null}
    </div>
  );
}
