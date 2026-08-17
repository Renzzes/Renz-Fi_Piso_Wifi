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
  MonitorSmartphone,
  Coins,
  SlidersHorizontal,
  Radio,
  ScrollText,
  Download,
  Settings,
  Menu,
  X,
  LogOut,
  Moon,
  Sun,
} from "lucide-react";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { useRealtime } from "@/contexts/RealtimeContext";
import {
  fetchInstallationState,
  isProductionInstallationState,
} from "@/lib/installationState";
import { useThemeMode } from "@/lib/theme";
import {
  DEFAULT_OPERATOR_PERMISSIONS,
  pathPermission,
  type OperatorPermission,
} from "@/lib/operatorPermissions";

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
    icon: MonitorSmartphone,
    permission: "captive-portal",
  },
  { to: "/coin-settings", label: "Coin Settings", icon: Coins, permission: "coin-settings" },
  {
    to: "/system-configuration",
    label: "System Configuration",
    icon: SlidersHorizontal,
    permission: "system-configuration",
  },
  {
    to: "/access-points",
    label: "Access Points",
    icon: Radio,
    ownerOnly: true,
  },
  { to: "/logs", label: "Logs", icon: ScrollText, permission: "logs" },
  { to: "/firmware", label: "Firmware Update", icon: Download, permission: "firmware" },
  { to: "/system-settings", label: "System Settings", icon: Settings, ownerOnly: true },
];

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
      : "Live Updates: Reconnecting";

  return (
    <div
      className={cn(
        "text-[10px] px-2 py-1 rounded-md hidden sm:flex items-center gap-1.5",
        sseConnected
          ? "bg-emerald-500/10 text-emerald-700 dark:text-emerald-400"
          : "bg-amber-500/10 text-amber-700 dark:text-amber-400",
      )}
      title={label}
    >
      <span
        className={cn(
          "h-1.5 w-1.5 rounded-full",
          sseConnected ? "bg-emerald-500" : "bg-amber-500 animate-pulse",
        )}
      />
      {label}
    </div>
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
  const { pathname } = useLocation();
  const displayHost = adminIp;
  const { theme, toggle } = useThemeMode();
  const { data: installationState } = useQuery({
    queryKey: ["health", "installationState"],
    queryFn: fetchInstallationState,
    staleTime: 60_000,
  });
  const productionInstalled = isProductionInstallationState(installationState);
  void productionInstalled;

  const visibleNav = nav.filter((item) => {
    if (item.ownerOnly) return isOwner;
    if (isOwner) return true;
    const key = item.permission ?? pathPermission(item.to);
    if (!key) return false;
    return permissions.includes(key);
  });

  useEffect(() => setOpen(false), [pathname]);

  return (
    <div
      className={cn(
        "min-h-screen flex bg-background text-foreground",
        connectionLost && "pointer-events-none select-none opacity-60",
      )}
      aria-hidden={connectionLost || undefined}
    >
      <aside
        className={cn(
          "fixed inset-y-0 left-0 z-40 w-56 border-r bg-sidebar text-sidebar-foreground flex flex-col transition-transform md:translate-x-0 md:static md:shrink-0",
          open ? "translate-x-0" : "-translate-x-full",
        )}
      >
        <div className="h-14 flex items-center px-4 border-b bg-white dark:bg-sidebar">
          <div className="relative h-10 w-40 overflow-hidden">
            <img
              src={logo2Src}
              alt="Renz-Fi logo"
              className="absolute left-1/2 top-1/2 w-[270px] max-w-none -translate-x-1/2 -translate-y-1/2"
            />
          </div>
        </div>
        <nav className="flex-1 overflow-y-auto py-2">
          {visibleNav.map((item) => {
            const active = pathname === item.to;
            const Icon = item.icon;
            return (
              <Link
                key={item.to}
                to={item.to}
                className={cn(
                  "flex items-center gap-2 px-4 py-2 text-sm hover:bg-sidebar-accent",
                  active &&
                    "bg-sidebar-accent text-sidebar-accent-foreground font-medium border-l-2 border-primary",
                )}
              >
                <Icon className="h-4 w-4 shrink-0" />
                {item.label}
              </Link>
            );
          })}
        </nav>
        <div className="border-t p-3 space-y-2">
          <Button
            type="button"
            variant="outline"
            size="sm"
            className="w-full justify-start gap-2"
            onClick={toggle}
          >
            {theme === "dark" ? <Sun className="h-4 w-4" /> : <Moon className="h-4 w-4" />}
            {theme === "dark" ? "Light mode" : "Dark mode"}
          </Button>
          <div className="text-[10px] text-muted-foreground truncate" title={displayHost}>
            {displayHost}
          </div>
          <Button
            type="button"
            variant="ghost"
            size="sm"
            className="w-full justify-start gap-2"
            onClick={onDisconnect}
          >
            <LogOut className="h-4 w-4" />
            Disconnect
          </Button>
        </div>
      </aside>

      <div className="flex-1 flex flex-col min-w-0">
        <header className="h-14 border-b flex items-center gap-2 px-3 md:px-4 bg-background sticky top-0 z-30">
          <Button
            type="button"
            variant="ghost"
            size="sm"
            className="md:hidden"
            onClick={() => setOpen((v) => !v)}
            aria-label={open ? "Close menu" : "Open menu"}
          >
            {open ? <X className="h-5 w-5" /> : <Menu className="h-5 w-5" />}
          </Button>
          <div className="flex-1" />
          <LiveUpdatesBadge />
        </header>
        <main className="flex-1 p-3 md:p-4 overflow-auto">
          <Outlet />
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
