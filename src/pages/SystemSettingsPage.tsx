import { useRef, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { ChangeAdminPasswordForm } from "@/components/ChangeAdminPasswordForm";
import { ConfirmPhraseDialog } from "@/components/ConfirmPhraseDialog";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { Power, RotateCcw, Download, Upload, HardDrive, TriangleAlert } from "lucide-react";
import type { SystemStatus } from "@/types/api";
import { settingsApi } from "@/services/settings";
import { systemApi, type FactoryResetStatus } from "@/services/system";
import { ApiError, isApiError, isNetworkError } from "@/services/api";
import { setFactoryResetQuiesced } from "@/services/factoryResetQuiesce";
import { toast } from "sonner";
import { OperatorAccountPanel, SetupUnlockPasswordPanel } from "@/components/SetupSecurityPanels";
import { ConfigCard } from "@/components/system-config/ConfigCard";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import { MetricTile } from "@/components/system-config/InfoRow";

type SystemSettingsPageProps = {
  onPasswordChanged: () => void | Promise<void>;
};

const FACTORY_RESET_WARNING = (
  <>
    <p>Factory Reset will erase all local data including:</p>
    <ul className="list-disc pl-5 space-y-1">
      <li>Promos</li>
      <li>Vouchers</li>
      <li>Sales history</li>
      <li>Owner account and Setup Unlock Password</li>
      <li>Router configuration</li>
      <li>Portal branding</li>
      <li>Custom banner</li>
      <li>Custom background music</li>
      <li>Logs</li>
      <li>User sessions</li>
    </ul>
    <p className="font-medium text-destructive">This action cannot be undone.</p>
  </>
);

const SETUP_AP_URL = "http://192.168.4.1";
const RESET_POLL_MS = 400;

function resetReachedReboot(status: FactoryResetStatus | null): boolean {
  if (!status) return false;
  const state = status.status ?? status.state;
  return state === "completed" || status.rebooting === true || status.phase === "Rebooting";
}

async function sleep(ms: number) {
  await new Promise((resolve) => setTimeout(resolve, ms));
}

function isSdReadyForBackup(sd: SystemStatus["storage"]["sd"] | undefined) {
  if (!sd) return false;
  return Boolean(sd.present && sd.mounted && sd.mode === "SD" && !sd.fallback);
}

function formatCapacityMb(mb?: number): string {
  if (mb === undefined || !Number.isFinite(mb)) return "—";
  if (mb >= 1024) return `${(mb / 1024).toFixed(1)} GB`;
  return `${mb.toFixed(1)} MB`;
}

function formatBackupAge(seconds?: number | null): string | null {
  if (seconds === undefined || seconds === null) return null;
  if (seconds < 60) return `${seconds}s ago`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
  return `${Math.floor(seconds / 3600)}h ago`;
}

export default function SystemSettingsPage({ onPasswordChanged }: SystemSettingsPageProps) {
  const restoreInputRef = useRef<HTMLInputElement | null>(null);
  const queryClient = useQueryClient();
  const [restoreFile, setRestoreFile] = useState<File | null>(null);
  const [restoreOpen, setRestoreOpen] = useState(false);
  const [factoryOpen, setFactoryOpen] = useState(false);
  const [rebootOpen, setRebootOpen] = useState(false);
  const [resetStatus, setResetStatus] = useState<FactoryResetStatus | null>(null);
  const [resetRestarting, setResetRestarting] = useState(false);
  const [resetQuiescing, setResetQuiescing] = useState(false);
  const resetting = resetQuiescing || resetRestarting;

  // Shared cache key with useSystemStatus/SystemConfigurationPage so /api/status
  // is polled once and reused, instead of each page opening its own poller.
  // Disabled during factory-reset communication quiesce.
  const statusQuery = useQuery({
    queryKey: ["system", "status"],
    queryFn: () => systemApi.status(),
    refetchInterval: resetting ? false : 30_000,
    enabled: !resetting,
  });

  const sd = statusQuery.data?.storage?.sd;
  const storageStatus = statusQuery.data?.storageStatus;
  const sdReady = isSdReadyForBackup(sd);
  const lastBackupAge =
    formatBackupAge(storageStatus?.lastSuccessfulBackupAgeSeconds) ||
    (storageStatus?.lastSuccessfulBackup ? storageStatus.lastSuccessfulBackup : null);
  const storageMode = storageStatus?.storageMode ?? sd?.mode ?? "—";
  const sdPresent = storageStatus ? storageStatus.sdPresent : Boolean(sd?.present);
  const sdMounted = storageStatus ? storageStatus.sdMounted : Boolean(sd?.mounted);
  const fallbackActive = storageStatus ? storageStatus.fallbackActive : Boolean(sd?.fallback);

  const retrySdMutation = useMutation({
    mutationFn: () => systemApi.retrySd(),
    onSuccess: (data) => {
      void queryClient.invalidateQueries({ queryKey: ["system", "status"] });
      if (data.healthy) {
        toast.success("SD card recovered");
      } else {
        toast.warning("SD card not mounted — still using SPIFFS fallback");
      }
    },
    onError: () => toast.error("SD mount failed"),
  });

  const backupMutation = useMutation({
    mutationFn: () => settingsApi.backup(),
    onSuccess: () => toast.success("Backup downloaded"),
    onError: (error: Error) => {
      if (error.message.toLowerCase().includes("sd card")) {
        toast.error("SD Card is not available");
      } else {
        toast.error(error.message || "Backup failed");
      }
    },
  });

  const rebootMutation = useMutation({
    mutationFn: () => systemApi.reboot(),
    onSuccess: () => {
      setRebootOpen(false);
      toast.success("Reboot scheduled");
    },
  });

  const resetMutation = useMutation({
    mutationFn: async () => {
      // Enter communication quiesce before enqueue so status/rgb/SSE stop
      // before FactoryResetWorker begins storage teardown.
      setResetQuiescing(true);
      setFactoryResetQuiesced(true);
      void queryClient.cancelQueries({ queryKey: ["system", "status"] });
      void queryClient.cancelQueries({ queryKey: ["rgb", "status"] });
      void queryClient.cancelQueries({ queryKey: ["settings", "operator"] });

      let queued: { jobId?: number; status?: string; state?: string };
      try {
        queued = await systemApi.factoryReset();
      } catch (error) {
        if (isApiError(error) && error.code === "FACTORY_RESET_IN_PROGRESS") {
          queued = await systemApi.factoryResetStatus();
        } else {
          throw error;
        }
      }
      const jobId = queued.jobId;
      if (!jobId) {
        throw new ApiError("Factory reset did not return a job id", 500, "FACTORY_RESET_NO_JOB");
      }
      setResetStatus({
        jobId,
        status: queued.status ?? queued.state ?? "queued",
        progress: 0,
        phase: "Preparing",
      });
      let last: FactoryResetStatus = {
        jobId,
        status: queued.status ?? queued.state ?? "queued",
      };
      for (;;) {
        try {
          last = await systemApi.factoryResetStatus(jobId);
          setResetStatus(last);
          const state = last.status ?? last.state;
          if (state === "failed") {
            throw new ApiError(last.error || "Factory reset failed", 500, "FACTORY_RESET_FAILED");
          }
          if (resetReachedReboot(last)) {
            setResetRestarting(true);
            await sleep(RESET_POLL_MS);
            continue;
          }
        } catch (error) {
          if (resetReachedReboot(last) && isNetworkError(error)) {
            setResetRestarting(true);
            return last;
          }
          if (isApiError(error) && error.status === 401) {
            setResetRestarting(true);
            return last;
          }
          throw error;
        }
        await sleep(RESET_POLL_MS);
      }
    },
    onSuccess: () => {
      setResetRestarting(true);
      toast.success("Factory reset completed. The device is restarting.");
      window.setTimeout(() => {
        window.location.href = SETUP_AP_URL;
      }, 1500);
    },
    onError: () => {
      if (resetRestarting || resetReachedReboot(resetStatus)) {
        toast.success("Factory reset completed. The device is restarting.");
        window.location.href = SETUP_AP_URL;
        return;
      }
      setResetQuiescing(false);
      setFactoryResetQuiesced(false);
      setFactoryOpen(true);
      toast.error("Factory reset failed");
    },
  });

  const restoreMutation = useMutation({
    mutationFn: (file: File) => settingsApi.restoreFile(file),
    onSuccess: () => {
      setRestoreOpen(false);
      setRestoreFile(null);
      toast.success("Restore complete. Device will reboot.");
    },
    onError: () => toast.error("Invalid backup file"),
  });

  const pickRestoreFile = (file: File | undefined) => {
    if (!file) return;
    setRestoreFile(file);
    setRestoreOpen(true);
    if (restoreInputRef.current) restoreInputRef.current.value = "";
  };

  return (
    <div className="flex w-full max-w-none flex-col gap-4">
      <div>
        <h2 className="text-2xl font-semibold leading-tight">System Settings</h2>
        <p className="mt-0.5 text-[13px] text-muted-foreground">
          Device maintenance, security, operator access and configuration backup
        </p>
      </div>

      <Alert>
        <TriangleAlert className="h-4 w-4" />
        <AlertTitle>Need to reconfigure the appliance?</AlertTitle>
        <AlertDescription>
          Production Admin no longer opens the Setup Wizard. Use Setup Unlock and the Management AP
          (below) to re-enter installation configuration safely.
        </AlertDescription>
      </Alert>

      <div className="grid grid-cols-1 items-start gap-4 lg:grid-cols-2">
        <ConfigCard
          title="Change Admin Password"
          description="Update the admin login password. You will be signed out after a successful change."
        >
          <ChangeAdminPasswordForm onSuccess={onPasswordChanged} />
        </ConfigCard>
        <SetupUnlockPasswordPanel />
      </div>

      <OperatorAccountPanel />

      <ConfigCard
        title="Backup & Restore"
        description="Export and import appliance configuration, promos, vouchers, sales, and portal assets. Backup requires an SD card."
      >
        <div className="grid grid-cols-1 items-start gap-4 lg:grid-cols-2">
          <div className="space-y-3 rounded-lg border bg-muted/10 p-4">
            <h4 className="text-[13px] font-semibold">Backup Storage</h4>
            <ConfigStatusBadge
              label={sdReady ? "SD Card Connected" : "SD Card Not Available"}
              tone={sdReady ? "ok" : "bad"}
            />
            <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
              <MetricTile label="Storage" value={storageMode} />
              <MetricTile
                label="Status"
                value={sdReady ? "Ready" : "Unavailable"}
                tone={sdReady ? "ok" : "bad"}
              />
              {lastBackupAge ? <MetricTile label="Last Backup" value={lastBackupAge} /> : null}
            </div>
          </div>
          <div className="space-y-3 rounded-lg border bg-muted/10 p-4">
            <h4 className="text-[13px] font-semibold">Backup Actions</h4>
            <div className="flex flex-col gap-2 sm:flex-row sm:flex-wrap">
              <Button
                size="sm"
                variant="outline"
                disabled={!sdReady || backupMutation.isPending}
                onClick={() => backupMutation.mutate()}
              >
                <Download className="h-4 w-4" /> Backup
              </Button>
              <Button size="sm" variant="outline" onClick={() => restoreInputRef.current?.click()}>
                <Upload className="h-4 w-4" /> Restore
              </Button>
              <input
                ref={restoreInputRef}
                type="file"
                accept=".zip,.json,application/zip,application/json"
                className="hidden"
                onChange={(event) => pickRestoreFile(event.target.files?.[0])}
              />
            </div>
            {restoreFile && !restoreOpen ? (
              <p className="break-all text-xs text-muted-foreground">Selected: {restoreFile.name}</p>
            ) : null}
          </div>
        </div>
      </ConfigCard>

      <ConfigCard title="Storage" description="SD card and SPIFFS fallback status on the ESP32.">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 lg:grid-cols-4">
          <MetricTile label="Storage Mode" value={storageMode} />
          <MetricTile
            label="SD Present"
            value={sdPresent ? "Yes" : "No"}
            tone={sdPresent ? "ok" : "bad"}
          />
          <MetricTile
            label="SD Mounted"
            value={sdMounted ? "Yes" : "No"}
            tone={sdMounted ? "ok" : "warn"}
          />
          <MetricTile label="Capacity" value={formatCapacityMb(storageStatus?.capacity)} />
          <MetricTile label="Used" value={formatCapacityMb(storageStatus?.used)} />
          <MetricTile
            label="Fallback Active"
            value={fallbackActive ? "Yes" : "No"}
            tone={fallbackActive ? "warn" : "ok"}
          />
        </div>
        {sd?.pollingDisabled ? (
          <Alert variant="destructive">
            <AlertTitle>SD recovery polling disabled</AlertTitle>
            <AlertDescription>
              SD recovery polling disabled after repeated failures.
            </AlertDescription>
          </Alert>
        ) : null}
        <Button
          size="sm"
          variant="outline"
          disabled={retrySdMutation.isPending}
          onClick={() => retrySdMutation.mutate()}
        >
          <HardDrive className="h-4 w-4" /> Retry SD Card
        </Button>
      </ConfigCard>

      <ConfigCard
        title="Maintenance"
        description="Dangerous operations"
        className="border-destructive/40"
      >
        <div className="flex flex-col gap-2 sm:flex-row sm:flex-wrap">
          <Button
            size="sm"
            variant="outline"
            disabled={rebootMutation.isPending}
            onClick={() => setRebootOpen(true)}
          >
            <RotateCcw className="h-4 w-4" /> Reboot
          </Button>
          <Button
            size="sm"
            variant="destructive"
            disabled={resetMutation.isPending || resetRestarting || resetQuiescing}
            onClick={() => setFactoryOpen(true)}
          >
            <Power className="h-4 w-4" /> Factory Reset
          </Button>
        </div>
        {resetMutation.isPending || resetRestarting || resetQuiescing ? (
          <p className="text-xs text-muted-foreground">
            {resetRestarting
              ? "Factory reset completed. The device is restarting."
              : `Factory Reset in progress${resetStatus?.phase ? ` — ${resetStatus.phase}` : ""}`}
            {resetStatus?.progress != null && !resetRestarting ? ` (${resetStatus.progress}%)` : ""}
          </p>
        ) : (
          <p className="text-xs text-muted-foreground">
            Factory reset erases all local appliance data and reboots the device.
          </p>
        )}
      </ConfigCard>

      <AlertDialog open={rebootOpen} onOpenChange={setRebootOpen}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Reboot this appliance?</AlertDialogTitle>
            <AlertDialogDescription>
              Admin and customer sessions will disconnect until the device comes back online.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel disabled={rebootMutation.isPending}>Cancel</AlertDialogCancel>
            <AlertDialogAction
              disabled={rebootMutation.isPending}
              onClick={(event) => {
                event.preventDefault();
                rebootMutation.mutate();
              }}
            >
              {rebootMutation.isPending ? "Rebooting…" : "Reboot"}
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>

      <ConfirmPhraseDialog
        open={restoreOpen}
        onOpenChange={(open) => {
          setRestoreOpen(open);
          if (!open) setRestoreFile(null);
        }}
        title="Restore Backup"
        description={
          <div className="space-y-2">
            <p>
              Restoring a backup will overwrite the current appliance configuration. The device will
              reboot when restore completes.
            </p>
            {restoreFile ? (
              <p className="font-mono text-xs text-foreground">{restoreFile.name}</p>
            ) : null}
          </div>
        }
        confirmPhrase="RESTORE"
        confirmLabel="Restore Backup"
        pending={restoreMutation.isPending}
        onConfirm={() => {
          if (restoreFile) restoreMutation.mutate(restoreFile);
        }}
      />

      <ConfirmPhraseDialog
        open={factoryOpen || resetMutation.isPending || resetRestarting || resetQuiescing}
        onOpenChange={(open) => {
          if (resetMutation.isPending || resetRestarting || resetQuiescing) return;
          setFactoryOpen(open);
        }}
        title="Factory Reset"
        description={FACTORY_RESET_WARNING}
        confirmPhrase="CONFIRM"
        confirmLabel="Factory Reset"
        pending={resetMutation.isPending || resetRestarting || resetQuiescing}
        progress={
          resetRestarting
            ? 100
            : typeof resetStatus?.progress === "number"
              ? resetStatus.progress
              : resetQuiescing
                ? 5
                : null
        }
        progressLabel={
          resetRestarting
            ? "Factory reset completed. The device is restarting…"
            : resetStatus?.phase
              ? `${resetStatus.phase}${
                  typeof resetStatus.progress === "number" ? ` (${resetStatus.progress}%)` : ""
                }`
              : "Erasing local data… Please wait."
        }
        destructive
        onConfirm={() => resetMutation.mutate()}
      />
    </div>
  );
}
