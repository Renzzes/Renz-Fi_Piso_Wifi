import { useRef, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import { ChangeAdminPasswordForm } from "@/components/ChangeAdminPasswordForm";
import { ConfirmPhraseDialog } from "@/components/ConfirmPhraseDialog";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { Power, RotateCcw, Download, Upload, HardDrive } from "lucide-react";
import type { SystemStatus } from "@/types/api";
import { settingsApi } from "@/services/settings";
import { systemApi } from "@/services/system";
import { toast } from "sonner";

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

  const statusQuery = useQuery({
    queryKey: ["system-status"],
    queryFn: () => systemApi.status(),
    refetchInterval: 30_000,
  });

  const sd = statusQuery.data?.storage?.sd;
  const sdReady = isSdReadyForBackup(sd);

  const retrySdMutation = useMutation({
    mutationFn: () => systemApi.retrySd(),
    onSuccess: (data) => {
      void queryClient.invalidateQueries({ queryKey: ["system-status"] });
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
    mutationFn: () => systemApi.factoryReset(),
    onSuccess: () => {
      setFactoryOpen(false);
      toast.success("Factory reset complete. Device will reboot.");
    },
    onError: () => toast.error("Factory reset failed"),
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
      <div className="grid md:grid-cols-2 gap-3">
        <div className="rounded-md border bg-card p-3 space-y-3 max-w-xl">
          <div className="text-sm font-medium">Change Admin Password</div>
          <p className="text-xs text-muted-foreground">
            Update the admin login password. You will be signed out after a successful change.
          </p>
          <ChangeAdminPasswordForm onSuccess={onPasswordChanged} />
        </div>

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
          <div className="text-sm font-medium">Storage</div>
          <p className="text-xs text-muted-foreground">
            SD card and SPIFFS fallback status on the ESP32.
          </p>
          <dl className="grid grid-cols-2 gap-x-3 gap-y-1 text-xs">
            <dt className="text-muted-foreground">SD Present</dt>
            <dd>{sd ? (sd.present ? "Yes" : "No") : "—"}</dd>
            <dt className="text-muted-foreground">SD Mounted</dt>
            <dd>{sd ? (sd.mounted ? "Yes" : "No") : "—"}</dd>
            <dt className="text-muted-foreground">Storage Mode</dt>
            <dd>{sd?.mode ?? (sd?.fallback ? "SPIFFS Fallback" : sd?.mounted ? "SD" : "—")}</dd>
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
            <Button size="sm" variant="destructive" onClick={() => setFactoryOpen(true)}>
              <Power className="h-4 w-4" /> Factory Reset
            </Button>
          </div>
          <p className="text-xs text-muted-foreground">
            Factory reset erases all local appliance data and reboots the device.
          </p>
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
        pending={resetMutation.isPending}
        destructive
        onConfirm={() => resetMutation.mutate()}
      />
    </div>
  );
}
