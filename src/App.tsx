import { useCallback, useEffect, useState } from "react";
import { Navigate, Route, Routes, useNavigate } from "react-router-dom";
import { useQueryClient } from "@tanstack/react-query";
import { AdminLayout } from "@/components/AdminLayout";
import { AuthCheckingScreen } from "@/components/AuthCheckingScreen";
import { ConnectionLostOverlay } from "@/components/ConnectionLostOverlay";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { ChangeAdminPasswordForm } from "@/components/ChangeAdminPasswordForm";
import { SessionExpiryWarningDialog } from "@/components/SessionExpiryWarningDialog";
import AuthPage from "@/pages/AuthPage";
import DashboardPage from "@/pages/DashboardPage";
import PromoRatesPage from "@/pages/PromoRatesPage";
import VouchersPage from "@/pages/VouchersPage";
import ActiveUsersPage from "@/pages/ActiveUsersPage";
import SalesReportsPage from "@/pages/SalesReportsPage";
import CaptivePortalPage from "@/pages/CaptivePortalPage";
import CoinSettingsPage from "@/pages/CoinSettingsPage";
import RouterSettingsPage from "@/pages/RouterSettingsPage";
import SystemConfigurationPage from "@/pages/SystemConfigurationPage";
import LogsPage from "@/pages/LogsPage";
import FirmwarePage from "@/pages/FirmwarePage";
import SystemSettingsPage from "@/pages/SystemSettingsPage";
import { toast } from "sonner";
import { authApi, setRememberedIp } from "@/services/auth";
import { ApiError } from "@/services/api";
import { setUnauthorizedHandler } from "@/services/authSession";
import {
  clearReloginRequired,
  isReloginRequired,
  logoutBestEffort,
  markReloginRequired,
  resolveBootstrapAuth,
  startLogoutRetryUntilSuccess,
  stopLogoutRetry,
} from "@/services/sessionGate";
import { getEmbeddedHost } from "@/services/embeddedApi";
import { useDashboardEvents } from "@/hooks/useDashboardEvents";
import { useAdminApiMonitor } from "@/hooks/useAdminApiMonitor";
import { useSessionIdleTimeout } from "@/hooks/useSessionIdleTimeout";
import { RealtimeProvider, type RealtimeContextValue } from "@/contexts/RealtimeContext";

export default function App() {
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const [adminIp] = useState(() => getEmbeddedHost());
  const [authenticated, setAuthenticated] = useState<boolean | null>(null);
  const [connecting, setConnecting] = useState(false);
  const [showPasswordChange, setShowPasswordChange] = useState(false);
  const [connectionRetrying, setConnectionRetrying] = useState(false);

  const sessionChecked = authenticated !== null;
  const isLoggedIn = authenticated === true;

  const handleDisconnect = useCallback(async () => {
    try {
      const cleared = await logoutBestEffort();
      if (cleared) {
        clearReloginRequired();
      } else {
        markReloginRequired("disconnect");
        startLogoutRetryUntilSuccess();
      }
    } catch {
      markReloginRequired("disconnect");
      startLogoutRetryUntilSuccess();
    } finally {
      setAuthenticated(false);
      setShowPasswordChange(false);
      navigate("/login", { replace: true });
    }
  }, [navigate]);

  const handleReconnectRequireLogin = useCallback(async () => {
    markReloginRequired("reconnect");
    queryClient.clear();
    setAuthenticated(false);
    setShowPasswordChange(false);
    navigate("/login", { replace: true });
    toast.message("Connection restored. Please sign in again.");

    const cleared = await logoutBestEffort();
    if (!cleared) {
      startLogoutRetryUntilSuccess();
    }
  }, [navigate, queryClient]);

  const { connectionLost, adminApiReachable, retryConnection } = useAdminApiMonitor({
    enabled: isLoggedIn,
    onReconnectRequireLogin: handleReconnectRequireLogin,
  });

  const handleConnectionRetry = useCallback(async () => {
    setConnectionRetrying(true);
    try {
      await retryConnection();
    } finally {
      setConnectionRetrying(false);
    }
  }, [retryConnection]);

  const dashboardEvents = useDashboardEvents(isLoggedIn);
  const realtime: RealtimeContextValue = {
    ...dashboardEvents,
    connectionLost,
    adminApiReachable,
  };

  const { showWarning, stayLoggedIn } = useSessionIdleTimeout({
    enabled: isLoggedIn && !connectionLost,
    onExpire: () => {
      toast.message("Session expired due to inactivity.");
      void handleDisconnect();
    },
  });

  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const json = await authApi.health();
        const serverAuthenticated = Boolean(json?.session?.authenticated);
        if (isReloginRequired() && serverAuthenticated) {
          void logoutBestEffort().then((cleared) => {
            if (!cleared) startLogoutRetryUntilSuccess();
          });
        }
        if (!cancelled) {
          setAuthenticated(resolveBootstrapAuth(serverAuthenticated));
        }
      } catch {
        if (!cancelled) setAuthenticated(false);
      }
    })();

    return () => {
      cancelled = true;
      stopLogoutRetry();
    };
  }, []);

  useEffect(() => {
    setUnauthorizedHandler(() => {
      toast.message("Your session has ended. Please sign in again.");
      void handleDisconnect();
    });

    return () => {
      setUnauthorizedHandler(null);
    };
  }, [handleDisconnect]);

  const handleConnect = async (ipAddress: string, password: string, rememberIpAddress: boolean) => {
    setConnecting(true);
    try {
      const health = await authApi.health();
      if (!health.ok) {
        toast.error("Admin server unavailable. Check LAN IP and retry.");
        return;
      }

      const login = await authApi.login(password);
      if (rememberIpAddress) setRememberedIp(ipAddress);

      clearReloginRequired();
      stopLogoutRetry();
      setAuthenticated(true);
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

  if (!sessionChecked) {
    return <AuthCheckingScreen />;
  }

  const adminEntry = isLoggedIn ? (
    <Navigate to="/dashboard" replace />
  ) : (
    <Navigate to="/login" replace />
  );

  return (
    <RealtimeProvider value={realtime}>
      <ConnectionLostOverlay
        open={isLoggedIn && connectionLost}
        onRetry={() => void handleConnectionRetry()}
        retrying={connectionRetrying}
      />
      <Routes>
        <Route path="/admin" element={adminEntry} />
        <Route path="/admin/*" element={adminEntry} />
        <Route
          path="/login"
          element={
            isLoggedIn ? (
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
            isLoggedIn ? (
              <AdminLayout
                adminIp={adminIp}
                onDisconnect={handleDisconnect}
                connectionLost={connectionLost}
              />
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
          <Route path="system-configuration" element={<SystemConfigurationPage />} />
          <Route path="router-settings" element={<RouterSettingsPage />} />
          <Route path="logs" element={<LogsPage />} />
          <Route path="firmware" element={<FirmwarePage />} />
          <Route
            path="system-settings"
            element={<SystemSettingsPage onPasswordChanged={handleDisconnect} />}
          />
          <Route path="*" element={<Navigate to="/dashboard" replace />} />
        </Route>
      </Routes>
      <ChangePasswordDialog open={isLoggedIn && showPasswordChange} onChanged={handleDisconnect} />
      <SessionExpiryWarningDialog
        open={isLoggedIn && showWarning}
        onStayLoggedIn={stayLoggedIn}
        onLogout={() => void handleDisconnect()}
      />
    </RealtimeProvider>
  );
}

function ChangePasswordDialog({
  open,
  onChanged,
}: {
  open: boolean;
  onChanged: () => void | Promise<void>;
}) {
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
        <ChangeAdminPasswordForm defaultOldPassword="admin" onSuccess={onChanged} />
      </DialogContent>
    </Dialog>
  );
}
