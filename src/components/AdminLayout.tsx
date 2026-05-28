import { useState, useEffect } from "react";
import { Link, Outlet, useLocation } from "react-router-dom";
import {
  LayoutDashboard,
  Tag,
  Ticket,
  Users,
  BarChart3,
  MonitorSmartphone,
  Coins,
  Router as RouterIcon,
  ScrollText,
  Download,
  Settings,
  Menu,
  X,
  LogOut,
} from "lucide-react";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";

const nav = [
  { to: "/dashboard", label: "Dashboard", icon: LayoutDashboard },
  { to: "/promo-rates", label: "Promo Rates", icon: Tag },
  { to: "/vouchers", label: "Vouchers", icon: Ticket },
  { to: "/active-users", label: "Active Users", icon: Users },
  { to: "/sales-reports", label: "Sales Reports", icon: BarChart3 },
  { to: "/captive-portal", label: "Captive Portal", icon: MonitorSmartphone },
  { to: "/coin-settings", label: "Coin Settings", icon: Coins },
  { to: "/router-settings", label: "Router Settings", icon: RouterIcon },
  { to: "/logs", label: "Logs", icon: ScrollText },
  { to: "/firmware", label: "Firmware Update", icon: Download },
  { to: "/system-settings", label: "System Settings", icon: Settings },
];

type AdminLayoutProps = {
  adminIp: string;
  onDisconnect: () => void;
};

export function AdminLayout({ adminIp, onDisconnect }: AdminLayoutProps) {
  const [open, setOpen] = useState(false);
  const { pathname } = useLocation();

  useEffect(() => setOpen(false), [pathname]);

  return (
    <div className="min-h-screen flex bg-background text-foreground">
      <aside
        className={cn(
          "fixed inset-y-0 left-0 z-40 w-56 border-r bg-sidebar text-sidebar-foreground flex flex-col transition-transform md:translate-x-0 md:static md:shrink-0",
          open ? "translate-x-0" : "-translate-x-full",
        )}
      >
        <div className="h-14 flex items-center px-4 border-b bg-white">
          <div className="relative h-10 w-40 overflow-hidden">
            <img
              src="/logo2.png"
              alt="Renz-Fi logo"
              className="absolute left-1/2 top-1/2 w-[270px] max-w-none -translate-x-1/2 -translate-y-1/2"
            />
          </div>
        </div>
        <nav className="flex-1 overflow-y-auto py-2">
          {nav.map((item) => {
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
                <Icon className="h-4 w-4" />
                {item.label}
              </Link>
            );
          })}
        </nav>
        <div className="px-4 py-2 text-[10px] text-muted-foreground border-t">
          v1.0.0 · {adminIp}
        </div>
      </aside>

      {open && (
        <div onClick={() => setOpen(false)} className="fixed inset-0 z-30 bg-black/40 md:hidden" />
      )}

      <div className="flex-1 flex flex-col min-w-0">
        <header className="h-12 border-b flex items-center justify-between px-3 sticky top-0 bg-background/95 backdrop-blur z-20">
          <div className="flex items-center gap-2">
            <Button
              variant="ghost"
              size="icon"
              className="md:hidden h-8 w-8"
              onClick={() => setOpen(!open)}
            >
              {open ? <X className="h-4 w-4" /> : <Menu className="h-4 w-4" />}
            </Button>
            <h1 className="text-sm font-medium">
              {nav.find((n) => n.to === pathname)?.label ?? "Admin"}
            </h1>
          </div>
          <div className="flex items-center gap-1">
            <div className="text-xs px-2 py-1 rounded-md bg-muted hidden sm:block">{adminIp}</div>
            <Button variant="ghost" size="icon" className="h-8 w-8" onClick={onDisconnect}>
              <LogOut className="h-4 w-4" />
            </Button>
          </div>
        </header>
        <main className="flex-1 p-3 sm:p-4 overflow-x-hidden">
          <Outlet />
        </main>
      </div>
    </div>
  );
}
