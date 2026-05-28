import { type FormEvent, useEffect, useState } from "react";
import { Navigate, Route, Routes, useNavigate } from "react-router-dom";
import { AdminLayout } from "@/components/AdminLayout";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import AuthPage from "@/pages/AuthPage";
import DashboardPage from "@/pages/DashboardPage";
import PromoRatesPage from "@/pages/PromoRatesPage";
import VouchersPage from "@/pages/VouchersPage";
import ActiveUsersPage from "@/pages/ActiveUsersPage";
import SalesReportsPage from "@/pages/SalesReportsPage";
import CaptivePortalPage from "@/pages/CaptivePortalPage";
import CoinSettingsPage from "@/pages/CoinSettingsPage";
import RouterSettingsPage from "@/pages/RouterSettingsPage";
import LogsPage from "@/pages/LogsPage";
import FirmwarePage from "@/pages/FirmwarePage";
import SystemSettingsPage from "@/pages/SystemSettingsPage";
import { toast } from "sonner";
import { authApi, setRememberedIp } from "@/services/auth";
import { ApiError } from "@/services/api";
import { getEmbeddedHost } from "@/services/embeddedApi";
import { useAdminEventStream } from "@/hooks/useAdminEventStream";
import { RealtimeProvider, type RealtimeContextValue } from "@/contexts/RealtimeContext";

export default function App() {
  const navigate = useNavigate();
  const [adminIp] = useState(() => getEmbeddedHost());
  const [connected, setConnected] = useState<boolean | null>(null);
  const [connecting, setConnecting] = useState(false);
  const [showPasswordChange, setShowPasswordChange] = useState(false);

  const realtime: RealtimeContextValue = useAdminEventStream(connected === true);

  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const json = await authApi.health();
        if (!cancelled) setConnected(Boolean(json?.session?.authenticated));
      } catch {
        if (!cancelled) setConnected(false);
      }
    })();

    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    let wasOnline = navigator.onLine;

    const handleChange = () => {
      const nowOnline = navigator.onLine;
      if (!nowOnline && wasOnline) {
        toast.error("You appear to be offline. Reconnect to refresh data.");
      }
      wasOnline = nowOnline;
    };

    window.addEventListener("offline", handleChange);
    window.addEventListener("online", handleChange);
    return () => {
      window.removeEventListener("offline", handleChange);
      window.removeEventListener("online", handleChange);
    };
  }, []);

  const handleConnect = async (ipAddress: string, password: string, rememberIp: boolean) => {
    setConnecting(true);
    try {
      const health = await authApi.health();
      if (!health.ok) {
        toast.error("Admin server unavailable. Check LAN IP and retry.");
        return;
      }

      const login = await authApi.login(password, rememberIp);
      if (rememberIp) setRememberedIp(ipAddress);

      setConnected(true);
      setShowPasswordChange(Boolean(login.mustChangePassword));
      navigate("/dashboard", { replace: true });
    } catch (err) {
      if (err instanceof ApiError) {
        toast.error(err.message);
        if (err.status === 401) return;
      } else {
        toast.error("Admin server unavailable. Check LAN IP and retry.");
      }
    } finally {
      setConnecting(false);
    }
  };

  const handleDisconnect = async () => {
    try {
      await authApi.logout();
    } catch {
      // Logout is best-effort; UI should still disconnect.
    } finally {
      setConnected(false);
      setShowPasswordChange(false);
      navigate("/login", { replace: true });
    }
  };

  if (connected === null) {
    return (
      <div className="min-h-screen flex items-center justify-center text-sm text-muted-foreground">
        Checking connection…
      </div>
    );
  }

  return (
    <RealtimeProvider value={realtime}>
      <Routes>
        <Route
          path="/login"
          element={
            connected ? (
              <Navigate to="/dashboard" replace />
            ) : (
              <AuthPage onConnect={handleConnect} connecting={connecting} />
            )
          }
        />
        <Route path="/register" element={<Navigate to="/login" replace />} />
        <Route
          path="/"
          element={
            connected ? (
              <AdminLayout adminIp={adminIp} onDisconnect={handleDisconnect} />
            ) : (
              <Navigate to="/login" replace />
            )
          }
        >
          <Route index element={<Navigate to="/dashboard" replace />} />
          <Route path="dashboard" element={<DashboardPage />} />
          <Route path="promo-rates" element={<PromoRatesPage />} />
          <Route path="vouchers" element={<VouchersPage />} />
          <Route path="active-users" element={<ActiveUsersPage />} />
          <Route path="sales-reports" element={<SalesReportsPage />} />
          <Route path="captive-portal" element={<CaptivePortalPage />} />
          <Route path="coin-settings" element={<CoinSettingsPage />} />
          <Route path="router-settings" element={<RouterSettingsPage />} />
          <Route path="logs" element={<LogsPage />} />
          <Route path="firmware" element={<FirmwarePage />} />
          <Route path="system-settings" element={<SystemSettingsPage />} />
          <Route path="*" element={<Navigate to="/dashboard" replace />} />
        </Route>
      </Routes>
      <ChangePasswordDialog
        open={connected === true && showPasswordChange}
        onChanged={() => setShowPasswordChange(false)}
      />
    </RealtimeProvider>
  );
}

function ChangePasswordDialog({ open, onChanged }: { open: boolean; onChanged: () => void }) {
  const [oldPassword, setOldPassword] = useState("admin");
  const [newPassword, setNewPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");
  const [saving, setSaving] = useState(false);

  const handleSubmit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (newPassword.length < 4) {
      toast.error("New password must be at least 4 characters.");
      return;
    }
    if (newPassword !== confirmPassword) {
      toast.error("New passwords do not match.");
      return;
    }

    setSaving(true);
    try {
      await authApi.changePassword({ oldPassword, newPassword });
      toast.success("Admin password changed.");
      setNewPassword("");
      setConfirmPassword("");
      onChanged();
    } catch (err) {
      toast.error(err instanceof ApiError ? err.message : "Failed to change password.");
    } finally {
      setSaving(false);
    }
  };

  return (
    <Dialog open={open} onOpenChange={() => undefined}>
      <DialogContent
        className="sm:max-w-md"
        onEscapeKeyDown={(event) => event.preventDefault()}
        onPointerDownOutside={(event) => event.preventDefault()}
      >
        <DialogHeader>
          <DialogTitle>Change Password</DialogTitle>
          <DialogDescription>
            Change the default admin password before continuing.
          </DialogDescription>
        </DialogHeader>
        <form className="space-y-4" onSubmit={handleSubmit}>
          <div className="space-y-2">
            <Label htmlFor="oldPassword">Old Password</Label>
            <Input
              id="oldPassword"
              type="password"
              value={oldPassword}
              onChange={(event) => setOldPassword(event.target.value)}
              autoComplete="current-password"
              required
            />
          </div>
          <div className="space-y-2">
            <Label htmlFor="newPassword">New Password:</Label>
            <Input
              id="newPassword"
              type="password"
              value={newPassword}
              onChange={(event) => setNewPassword(event.target.value)}
              autoComplete="new-password"
              required
            />
          </div>
          <div className="space-y-2">
            <Label htmlFor="confirmPassword">Confirm New Password:</Label>
            <Input
              id="confirmPassword"
              type="password"
              value={confirmPassword}
              onChange={(event) => setConfirmPassword(event.target.value)}
              autoComplete="new-password"
              required
            />
          </div>
          <DialogFooter>
            <Button type="submit" disabled={saving}>
              {saving ? "Saving..." : "Change Password"}
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  );
}
