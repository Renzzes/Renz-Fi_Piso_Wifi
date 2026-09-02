import { useCallback, useEffect, useState, type ReactNode } from "react";
import { Navigate, Route, Routes, useLocation, useNavigate } from "react-router-dom";
import { useQueryClient } from "@tanstack/react-query";
import { AdminLayout } from "@/components/AdminLayout";
import { AdminSyncScreen } from "@/components/AdminSyncScreen";
import { AuthCheckingScreen } from "@/components/AuthCheckingScreen";
import { ConnectionLostOverlay } from "@/components/ConnectionLostOverlay";
import { SessionExpiryWarningDialog } from "@/components/SessionExpiryWarningDialog";
import AuthPage from "@/pages/AuthPage";
import ChangePasswordPage from "@/pages/ChangePasswordPage";
import DashboardPage from "@/pages/DashboardPage";
import { ErrorBoundary } from "@/components/ErrorBoundary";
import PromoRatesPage from "@/pages/PromoRatesPage";
import VouchersPage from "@/pages/VouchersPage";
import ActiveUsersPage from "@/pages/ActiveUsersPage";
import SalesReportsPage from "@/pages/SalesReportsPage";
import CaptivePortalPage from "@/pages/CaptivePortalPage";
import CoinSettingsPage from "@/pages/CoinSettingsPage";
import RouterSettingsPage from "@/pages/RouterSettingsPage";
import SystemConfigurationLegacyRedirect from "@/pages/SystemConfigurationLegacyRedirect";
import NetworkPage from "@/pages/NetworkPage";
import RouterStatusPage from "@/pages/RouterStatusPage";
import BandwidthPage from "@/pages/BandwidthPage";
import StoragePage from "@/pages/StoragePage";
import WirelessPage from "@/pages/WirelessPage";
import AccessPointsPage from "@/pages/AccessPointsPage";
import ContentFilteringPage from "@/pages/ContentFilteringPage";
import GamingPriorityPage from "@/pages/GamingPriorityPage";
import LogsPage from "@/pages/LogsPage";
import FirmwarePage from "@/pages/FirmwarePage";
import SystemSettingsPage from "@/pages/SystemSettingsPage";
import { toast } from "sonner";
import { authApi, setRememberedIp, type AuthRole } from "@/services/auth";
import { ApiError } from "@/services/api";
import { setUnauthorizedHandler } from "@/services/authSession";
import {
  DEFAULT_OPERATOR_PERMISSIONS,
  normalizeOperatorPermissions,
  type OperatorPermission,
} from "@/lib/operatorPermissions";
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
import { synchronizeAdminClient, type AdminSyncPhase } from "@/services/adminSync";
import { useDashboardEvents } from "@/hooks/useDashboardEvents";
import { useAdminApiMonitor } from "@/hooks/useAdminApiMonitor";
import { useSessionIdleTimeout } from "@/hooks/useSessionIdleTimeout";
import { RealtimeProvider, type RealtimeContextValue } from "@/contexts/RealtimeContext";
import { DeviceRegistryProvider } from "@/contexts/DeviceRegistryContext";
import type { RegisteredDevice } from "@/types/deviceProfile";
import {
  isFactoryResetQuiesced,
  onFactoryResetQuiesce,
} from "@/services/factoryResetQuiesce";

function RequireOwner({ isOwner, children }: { isOwner: boolean; children: ReactNode }) {
  if (!isOwner) return <Navigate to="/dashboard" replace />;
  return <>{children}</>;
}

function RequirePermission({
  isOwner,
  permissions,
  permission,
  children,
}: {
  isOwner: boolean;
  permissions: OperatorPermission[];
  permission: OperatorPermission;
  children: ReactNode;
}) {
  if (isOwner) return <>{children}</>;
  if (!permissions.includes(permission)) {
    return <Navigate to="/dashboard" replace />;
  }
  return <>{children}</>;
}

export default function App() {
  const navigate = useNavigate();
  const location = useLocation();
  const queryClient = useQueryClient();
  const [adminIp] = useState(() => getEmbeddedHost());
  const [authenticated, setAuthenticated] = useState<boolean | null>(null);
  const [role, setRole] = useState<AuthRole>("none");
  const [permissions, setPermissions] = useState<OperatorPermission[]>([
    ...DEFAULT_OPERATOR_PERMISSIONS,
  ]);
  const [connecting, setConnecting] = useState(false);
  const [passwordChangeRequired, setPasswordChangeRequired] = useState(false);
  const [connectionRetrying, setConnectionRetrying] = useState(false);
  const [coreSyncDone, setCoreSyncDone] = useState(false);
  const [syncPhase, setSyncPhase] = useState<AdminSyncPhase>("device");
  const [syncError, setSyncError] = useState<string | null>(null);

  const sessionChecked = authenticated !== null;
  const isLoggedIn = authenticated === true;
  const isOwner = role === "owner";

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
      setPasswordChangeRequired(false);
      setRole("none");
      setPermissions([...DEFAULT_OPERATOR_PERMISSIONS]);
      setCoreSyncDone(false);
      setLiveUpdatesEnabled(false);
      navigate("/login", { replace: true });
    }
  }, [navigate]);

  const runCoreSync = useCallback(async () => {
    setSyncError(null);
    setSyncPhase("device");
    try {
      const result = await synchronizeAdminClient((phase) => setSyncPhase(phase));
      queryClient.setQueryData(["system", "status"], result.status);
      setCoreSyncDone(true);
      return true;
    } catch (err) {
      setSyncError(
        err instanceof ApiError
          ? err.message
          : "Unable to synchronize with the appliance.",
      );
      setCoreSyncDone(false);
      return false;
    }
  }, [queryClient]);

  const handlePasswordChangeComplete = useCallback(async () => {
    setPasswordChangeRequired(false);
    setCoreSyncDone(false);
    const synced = await runCoreSync();
    if (!synced) return;
    await queryClient.invalidateQueries();
    navigate("/dashboard", { replace: true });
  }, [navigate, queryClient, runCoreSync]);

  const handleReconnectRequireLogin = useCallback(async () => {
    markReloginRequired("reconnect");
    queryClient.clear();
    setAuthenticated(false);
    setRole("none");
    setPasswordChangeRequired(false);
    setCoreSyncDone(false);
    navigate("/login", { replace: true });
    toast.message("Connection restored. Please sign in again.");

    const cleared = await logoutBestEffort();
    if (!cleared) {
      startLogoutRetryUntilSuccess();
    }
  }, [navigate, queryClient]);

  const [factoryResetQuiesced, setFactoryResetQuiescedState] = useState(
    isFactoryResetQuiesced,
  );
  useEffect(() => onFactoryResetQuiesce(setFactoryResetQuiescedState), []);

  const [liveUpdatesEnabled, setLiveUpdatesEnabled] = useState(false);

  const dashboardEvents = useDashboardEvents(
    isLoggedIn &&
      !passwordChangeRequired &&
      !factoryResetQuiesced &&
      liveUpdatesEnabled,
  );
  const { connectionLost, adminApiReachable, retryConnection } = useAdminApiMonitor({
    enabled: isLoggedIn && !passwordChangeRequired && !factoryResetQuiesced,
    sseConnected: dashboardEvents.sseConnected,
    standbyIdle: !liveUpdatesEnabled,
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

  const handleDeviceSwitch = useCallback(
    async (_device: RegisteredDevice) => {
      queryClient.clear();
      stopLogoutRetry();
      setPasswordChangeRequired(false);
      try {
        const json = await authApi.health();
        const serverAuthenticated = Boolean(json?.session?.authenticated);
        const authed = resolveBootstrapAuth(serverAuthenticated);
        setAuthenticated(authed);
        setRole(authed ? json?.session?.role ?? "none" : "none");
        setPermissions(
          normalizeOperatorPermissions(json?.session?.permissions),
        );
        if (!authed) {
          navigate("/login", { replace: true, state: { from: location.pathname } });
        }
      } catch {
        setAuthenticated(false);
        setRole("none");
        setPermissions([...DEFAULT_OPERATOR_PERMISSIONS]);
        navigate("/login", { replace: true, state: { from: location.pathname } });
      }
    },
    [location.pathname, navigate, queryClient],
  );

  const realtime: RealtimeContextValue = {
    ...dashboardEvents,
    connectionLost,
    adminApiReachable,
    liveUpdatesEnabled,
    setLiveUpdatesEnabled,
  };

  const { showWarning, stayLoggedIn } = useSessionIdleTimeout({
    enabled: isLoggedIn && !connectionLost && !passwordChangeRequired,
    onExpire: () => {
      toast.message("Session expired due to inactivity.");
      void handleDisconnect();
    },
  });

  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        let json = await authApi.health();
        for (let attempt = 0; json.transientLoad && attempt < 3 && !cancelled; attempt++) {
          await new Promise((resolve) => window.setTimeout(resolve, 2000));
          json = await authApi.health();
        }
        if (json.transientLoad) {
          if (!cancelled) {
            setAuthenticated(false);
            setRole("none");
            setPermissions([...DEFAULT_OPERATOR_PERMISSIONS]);
            setCoreSyncDone(true);
          }
          return;
        }
        const serverAuthenticated = Boolean(json?.session?.authenticated);
        if (isReloginRequired() && serverAuthenticated) {
          void logoutBestEffort().then((cleared) => {
            if (!cleared) startLogoutRetryUntilSuccess();
          });
        }
        if (!cancelled) {
          const authed = resolveBootstrapAuth(serverAuthenticated);
          setAuthenticated(authed);
          setRole(authed ? json?.session?.role ?? "none" : "none");
          setPermissions(
            normalizeOperatorPermissions(json?.session?.permissions),
          );
          const needsPasswordChange = authed && Boolean(json?.session?.mustChangePassword);
          setPasswordChangeRequired(needsPasswordChange);
          if (needsPasswordChange) {
            setCoreSyncDone(true);
            navigate("/change-password", { replace: true });
          } else if (authed) {
            void runCoreSync();
          } else {
            setCoreSyncDone(true);
          }
        }
      } catch {
        if (!cancelled) {
          setAuthenticated(false);
          setRole("none");
          setPermissions([...DEFAULT_OPERATOR_PERMISSIONS]);
          setCoreSyncDone(true);
        }
      }
    })();

    return () => {
      cancelled = true;
      stopLogoutRetry();
    };
  }, [navigate, runCoreSync]);

  useEffect(() => {
    setUnauthorizedHandler(() => {
      toast.message(
        "Device was factory reset or your session ended. Reconnect through Setup if needed.",
      );
      void handleDisconnect();
    });

    return () => {
      setUnauthorizedHandler(null);
    };
  }, [handleDisconnect]);

  const handleConnect = async (ipAddress: string, password: string, rememberIpAddress: boolean) => {
    setConnecting(true);
    setCoreSyncDone(false);
    setSyncError(null);
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
      setRole(login.role ?? "owner");
      setPermissions(normalizeOperatorPermissions(login.permissions));
      const needsPasswordChange = Boolean(login.mustChangePassword);
      setPasswordChangeRequired(needsPasswordChange);
      if (needsPasswordChange) {
        setCoreSyncDone(true);
        navigate("/change-password", { replace: true });
        return;
      }
      const synced = await runCoreSync();
      if (synced) navigate("/dashboard", { replace: true });
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

  if (isLoggedIn && !passwordChangeRequired && !coreSyncDone) {
    return (
      <AdminSyncScreen
        phase={syncPhase}
        error={syncError}
        onRetry={() => void runCoreSync()}
      />
    );
  }

  const postLoginTarget = passwordChangeRequired ? "/change-password" : "/dashboard";

  const adminEntry = isLoggedIn ? (
    <Navigate to={postLoginTarget} replace />
  ) : (
    <Navigate to="/login" replace />
  );

  return (
    <DeviceRegistryProvider onDeviceSwitch={(device) => void handleDeviceSwitch(device)}>
    <RealtimeProvider value={realtime}>
      <ConnectionLostOverlay
        open={isLoggedIn && !passwordChangeRequired && connectionLost}
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
              <Navigate to={postLoginTarget} replace />
            ) : (
              <AuthPage onConnect={handleConnect} connecting={connecting} />
            )
          }
        />
        <Route
          path="/change-password"
          element={
            isLoggedIn ? (
              passwordChangeRequired ? (
                <ChangePasswordPage
                  onComplete={handlePasswordChangeComplete}
                  onLogout={handleDisconnect}
                />
              ) : (
                <Navigate to="/dashboard" replace />
              )
            ) : (
              <Navigate to="/login" replace />
            )
          }
        />
        <Route
          path="/setup"
          element={
            isLoggedIn ? (
              passwordChangeRequired ? (
                <Navigate to="/change-password" replace />
              ) : (
                <Navigate to="/system-settings" replace />
              )
            ) : (
              <Navigate to="/login" replace />
            )
          }
        />
        <Route path="/register" element={<Navigate to="/login" replace />} />
        <Route
          path="/"
          element={
            isLoggedIn ? (
              passwordChangeRequired ? (
                <Navigate to="/change-password" replace />
              ) : (
                <AdminLayout
                  adminIp={adminIp}
                  onDisconnect={handleDisconnect}
                  connectionLost={connectionLost}
                  isOwner={isOwner}
                  permissions={permissions}
                />
              )
            ) : (
              <Navigate to="/login" replace />
            )
          }
        >
          <Route index element={<Navigate to="/dashboard" replace />} />
          <Route
            path="dashboard"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="dashboard"
              >
                <ErrorBoundary>
                  <DashboardPage />
                </ErrorBoundary>
              </RequirePermission>
            }
          />
          <Route
            path="promo-rates"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="promo-rates"
              >
                <PromoRatesPage />
              </RequirePermission>
            }
          />
          <Route
            path="vouchers"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="vouchers"
              >
                <VouchersPage />
              </RequirePermission>
            }
          />
          <Route
            path="active-users"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="active-users"
              >
                <ActiveUsersPage />
              </RequirePermission>
            }
          />
          <Route
            path="sales-reports"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="sales-reports"
              >
                <SalesReportsPage />
              </RequirePermission>
            }
          />
          <Route
            path="captive-portal"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="captive-portal"
              >
                <CaptivePortalPage />
              </RequirePermission>
            }
          />
          <Route
            path="coin-settings"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="coin-settings"
              >
                <CoinSettingsPage />
              </RequirePermission>
            }
          />
          <Route
            path="system-configuration"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="system-configuration"
              >
                <SystemConfigurationLegacyRedirect />
              </RequirePermission>
            }
          />
          <Route
            path="network"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="system-configuration"
              >
                <NetworkPage />
              </RequirePermission>
            }
          />
          <Route
            path="router-status"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="system-configuration"
              >
                <RouterStatusPage />
              </RequirePermission>
            }
          />
          <Route
            path="bandwidth"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="system-configuration"
              >
                <BandwidthPage />
              </RequirePermission>
            }
          />
          <Route
            path="storage"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="system-configuration"
              >
                <StoragePage />
              </RequirePermission>
            }
          />
          <Route
            path="wireless"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="system-configuration"
              >
                <WirelessPage />
              </RequirePermission>
            }
          />
          <Route
            path="access-points"
            element={
              <RequireOwner isOwner={isOwner}>
                <AccessPointsPage />
              </RequireOwner>
            }
          />
          <Route
            path="content-filtering"
            element={
              <RequireOwner isOwner={isOwner}>
                <ContentFilteringPage />
              </RequireOwner>
            }
          />
          <Route
            path="gaming-priority"
            element={
              <RequireOwner isOwner={isOwner}>
                <GamingPriorityPage />
              </RequireOwner>
            }
          />
          {/* Compatibility bookmark: former Router nav → System Configuration */}
          <Route
            path="router-settings"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="system-configuration"
              >
                <RouterSettingsPage />
              </RequirePermission>
            }
          />
          <Route
            path="logs"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="logs"
              >
                <LogsPage />
              </RequirePermission>
            }
          />
          <Route
            path="firmware"
            element={
              <RequirePermission
                isOwner={isOwner}
                permissions={permissions}
                permission="firmware"
              >
                <FirmwarePage />
              </RequirePermission>
            }
          />
          <Route
            path="system-settings"
            element={
              <RequireOwner isOwner={isOwner}>
                <SystemSettingsPage onPasswordChanged={handleDisconnect} />
              </RequireOwner>
            }
          />
          {/* Obsolete fleet/devices bookmarks → Dashboard (standalone appliance) */}
          <Route path="devices" element={<Navigate to="/dashboard" replace />} />
          <Route path="fleet" element={<Navigate to="/dashboard" replace />} />
          <Route path="*" element={<Navigate to="/dashboard" replace />} />
        </Route>
      </Routes>
      <SessionExpiryWarningDialog
        open={isLoggedIn && !passwordChangeRequired && showWarning}
        onStayLoggedIn={stayLoggedIn}
        onLogout={() => void handleDisconnect()}
      />
    </RealtimeProvider>
    </DeviceRegistryProvider>
  );
}
