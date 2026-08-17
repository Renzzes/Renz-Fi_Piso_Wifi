import { useRef, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import { ChangeAdminPasswordForm } from "@/components/ChangeAdminPasswordForm";
import { ConfirmPhraseDialog } from "@/components/ConfirmPhraseDialog";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Power, RotateCcw, Download, Upload, HardDrive, Lightbulb } from "lucide-react";
import type { RgbMode, SystemStatus } from "@/types/api";
import { getRgbColorComponents } from "@/lib/rgbDisplay";
import { settingsApi } from "@/services/settings";
import { rgbApi } from "@/services/rgb";
import { systemApi, type FactoryResetStatus } from "@/services/system";
import { ApiError, isApiError, isNetworkError } from "@/services/api";
import { toast } from "sonner";
import {
  OperatorAccountPanel,
  SetupUnlockPasswordPanel,
} from "@/components/SetupSecurityPanels";

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

export default function SystemSettingsPage({ onPasswordChanged }: SystemSettingsPageProps) {
  const restoreInputRef = useRef<HTMLInputElement | null>(null);
  const queryClient = useQueryClient();
  const [restoreFile, setRestoreFile] = useState<File | null>(null);
  const [restoreOpen, setRestoreOpen] = useState(false);
  const [factoryOpen, setFactoryOpen] = useState(false);
  const [resetStatus, setResetStatus] = useState<FactoryResetStatus | null>(null);
  const [resetRestarting, setResetRestarting] = useState(false);

  // Shared cache key with useSystemStatus/SystemConfigurationPage so /api/status
  // is polled once and reused, instead of each page opening its own poller.
  const statusQuery = useQuery({
    queryKey: ["system", "status"],
    queryFn: () => systemApi.status(),
    refetchInterval: 30_000,
  });
  const rgbQuery = useQuery({
    queryKey: ["rgb", "status"],
    queryFn: () => rgbApi.status(),
    refetchInterval: 30_000,
  });

  const sd = statusQuery.data?.storage?.sd;
  const storageStatus = statusQuery.data?.storageStatus;
  const sdReady = isSdReadyForBackup(sd);
  const rgb = rgbQuery.data;
  const rgbColor = getRgbColorComponents(rgb?.color);

  if (rgb) {
    console.log("[RGB]", rgb);
  }

  const rgbModeMutation = useMutation({
    mutationFn: (mode: RgbMode) => rgbApi.setMode(mode),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ["rgb", "status"] });
      toast.success("RGB mode updated");
    },
    onError: () => toast.error("Failed to update RGB mode"),
  });

  const rgbBrightnessMutation = useMutation({
    mutationFn: (brightness: number) => rgbApi.setBrightness(brightness),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ["rgb", "status"] });
      toast.success("RGB brightness updated");
    },
    onError: () => toast.error("Failed to update RGB brightness"),
  });

  const rgbColorMutation = useMutation({
    mutationFn: (color: { red: number; green: number; blue: number }) => rgbApi.setColor(color),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ["rgb", "status"] });
      toast.success("RGB color updated");
    },
    onError: () => toast.error("Failed to update RGB color"),
  });

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
    onSuccess: () => toast.success("Reboot scheduled"),
  });

  const resetMutation = useMutation({
    mutationFn: async () => {
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
            throw new ApiError(
              last.error || "Factory reset failed",
              500,
              "FACTORY_RESET_FAILED",
            );
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
      setFactoryOpen(false);
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
    <div>
      <PageHeader
        title="System Settings"
        description="Device maintenance and configuration backup"
      />
      <Alert className="mb-3 max-w-3xl">
        <Lightbulb className="h-4 w-4" />
        <AlertTitle>Need to reconfigure the appliance?</AlertTitle>
        <AlertDescription>
          Production Admin no longer opens the Setup Wizard. Use Setup Unlock and the
          Management AP (below) to re-enter installation configuration safely.
        </AlertDescription>
      </Alert>
      <div className="grid md:grid-cols-2 gap-3">
        <div className="rounded-md border bg-card p-3 space-y-3 max-w-xl">
          <div className="text-sm font-medium">Change Admin Password</div>
          <p className="text-xs text-muted-foreground">
            Update the admin login password. You will be signed out after a successful change.
          </p>
          <ChangeAdminPasswordForm onSuccess={onPasswordChanged} />
        </div>

        <SetupUnlockPasswordPanel />

        <OperatorAccountPanel />

        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Backup & Restore</div>
          <p className="text-xs text-muted-foreground">
            Export and import appliance configuration, promos, vouchers, sales, and portal assets.
            Backup requires an SD card.
          </p>
          <div className="flex items-center gap-2 text-xs">
            <span
              className={`inline-block h-2 w-2 rounded-full ${sdReady ? "bg-emerald-500" : "bg-destructive"}`}
            />
            <span>{sdReady ? "SD Card Connected" : "SD Card Not Available"}</span>
          </div>
          <div className="flex flex-wrap gap-2">
            <Button
              size="sm"
              variant="outline"
              disabled={!sdReady || backupMutation.isPending}
              onClick={() => backupMutation.mutate()}
            >
              <Download className="h-4 w-4" /> Backup
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={() => restoreInputRef.current?.click()}
            >
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
            <p className="text-xs text-muted-foreground">Selected: {restoreFile.name}</p>
          ) : null}
        </div>

        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium flex items-center gap-2">
            <Lightbulb className="h-4 w-4" /> RGB Controller
          </div>
          <p className="text-xs text-muted-foreground">
            Control the appliance status LED. SYSTEM_STATUS reflects network, storage, and session
            health automatically.
          </p>
          <div className="space-y-2">
            <Label className="text-xs">Mode</Label>
            <Select
              value={rgb?.mode ?? "SYSTEM_STATUS"}
              onValueChange={(value) => rgbModeMutation.mutate(value as RgbMode)}
              disabled={rgbModeMutation.isPending}
            >
              <SelectTrigger className="h-8">
                <SelectValue placeholder="Select mode" />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="OFF">Off</SelectItem>
                <SelectItem value="SOLID">Solid</SelectItem>
                <SelectItem value="BREATHING">Breathing</SelectItem>
                <SelectItem value="RAINBOW">Rainbow</SelectItem>
                <SelectItem value="SYSTEM_STATUS">System Status</SelectItem>
              </SelectContent>
            </Select>
          </div>
          <div className="space-y-2">
            <Label className="text-xs">Brightness ({rgb?.brightness ?? 80}%)</Label>
            <Input
              type="range"
              min={0}
              max={100}
              value={rgb?.brightness ?? 80}
              onChange={(event) =>
                rgbBrightnessMutation.mutate(Number(event.target.value))
              }
            />
          </div>
          <div className="grid grid-cols-3 gap-2">
            <div className="space-y-1">
              <Label className="text-xs">Red</Label>
              <Input
                type="number"
                min={0}
                max={255}
                defaultValue={rgbColor.red}
                onBlur={(event) =>
                  rgbColorMutation.mutate({
                    red: Number(event.target.value),
                    green: rgbColor.green,
                    blue: rgbColor.blue,
                  })
                }
              />
            </div>
            <div className="space-y-1">
              <Label className="text-xs">Green</Label>
              <Input
                type="number"
                min={0}
                max={255}
                defaultValue={rgbColor.green}
                onBlur={(event) =>
                  rgbColorMutation.mutate({
                    red: rgbColor.red,
                    green: Number(event.target.value),
                    blue: rgbColor.blue,
                  })
                }
              />
            </div>
            <div className="space-y-1">
              <Label className="text-xs">Blue</Label>
              <Input
                type="number"
                min={0}
                max={255}
                defaultValue={rgbColor.blue}
                onBlur={(event) =>
                  rgbColorMutation.mutate({
                    red: rgbColor.red,
                    green: rgbColor.green,
                    blue: Number(event.target.value),
                  })
                }
              />
            </div>
          </div>
        </div>

        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Storage</div>
          <p className="text-xs text-muted-foreground">
            SD card and SPIFFS fallback status on the ESP32.
          </p>
          <dl className="grid grid-cols-2 gap-x-3 gap-y-1 text-xs">
            <dt className="text-muted-foreground">Storage Mode</dt>
            <dd>{storageStatus?.storageMode ?? sd?.mode ?? "—"}</dd>
            <dt className="text-muted-foreground">SD Present</dt>
            <dd>{storageStatus ? (storageStatus.sdPresent ? "Yes" : "No") : sd?.present ? "Yes" : "No"}</dd>
            <dt className="text-muted-foreground">SD Mounted</dt>
            <dd>{storageStatus ? (storageStatus.sdMounted ? "Yes" : "No") : sd?.mounted ? "Yes" : "No"}</dd>
            <dt className="text-muted-foreground">Capacity</dt>
            <dd>{storageStatus?.capacity !== undefined ? `${storageStatus.capacity.toFixed(1)} MB` : "—"}</dd>
            <dt className="text-muted-foreground">Used</dt>
            <dd>{storageStatus?.used !== undefined ? `${storageStatus.used.toFixed(1)} MB` : "—"}</dd>
            <dt className="text-muted-foreground">Fallback Active</dt>
            <dd>{storageStatus ? (storageStatus.fallbackActive ? "Yes" : "No") : sd?.fallback ? "Yes" : "No"}</dd>
          </dl>
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
        </div>

        <div className="rounded-md border border-destructive/40 bg-card p-3 space-y-3">
          <div className="text-sm font-medium text-destructive">Maintenance</div>
          <div className="flex flex-wrap gap-2">
            <Button size="sm" variant="outline" onClick={() => rebootMutation.mutate()}>
              <RotateCcw className="h-4 w-4" /> Reboot
            </Button>
            <Button
              size="sm"
              variant="destructive"
              disabled={resetMutation.isPending || resetRestarting}
              onClick={() => setFactoryOpen(true)}
            >
              <Power className="h-4 w-4" /> Factory Reset
            </Button>
          </div>
          {resetMutation.isPending || resetRestarting ? (
            <p className="text-xs text-muted-foreground">
              {resetRestarting
                ? "Factory reset completed. The device is restarting."
                : `Factory Reset in progress${resetStatus?.phase ? ` — ${resetStatus.phase}` : ""}`}
              {resetStatus?.progress != null && !resetRestarting
                ? ` (${resetStatus.progress}%)`
                : ""}
            </p>
          ) : (
            <p className="text-xs text-muted-foreground">
              Factory reset erases all local appliance data and reboots the device.
            </p>
          )}
        </div>
      </div>

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
        open={factoryOpen}
        onOpenChange={setFactoryOpen}
        title="Factory Reset"
        description={FACTORY_RESET_WARNING}
        confirmPhrase="CONFIRM"
        confirmLabel="Factory Reset"
        pending={resetMutation.isPending || resetRestarting}
        destructive
        onConfirm={() => resetMutation.mutate()}
      />
    </div>
  );
}
