import { useRef } from "react";
import { useMutation } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Power, RotateCcw, Download, Upload } from "lucide-react";
import { settingsApi } from "@/services/settings";
import { systemApi } from "@/services/system";
import { toast } from "sonner";

export default function SystemSettingsPage() {
  const restoreInputRef = useRef<HTMLInputElement | null>(null);

  const rebootMutation = useMutation({
    mutationFn: () => systemApi.reboot(),
    onSuccess: () => toast.success("Reboot scheduled"),
  });

  const resetMutation = useMutation({
    mutationFn: () => systemApi.factoryReset(),
    onSuccess: () => toast.warning("Factory reset prepared"),
  });

  const restoreMutation = useMutation({
    mutationFn: (backup: unknown) => settingsApi.restore(backup),
    onSuccess: () => toast.success("Backup restored"),
    onError: () => toast.error("Invalid backup file"),
  });

  const restoreBackup = async (file: File | undefined) => {
    if (!file) return;
    try {
      const text = await file.text();
      restoreMutation.mutate(JSON.parse(text));
    } catch {
      toast.error("Invalid backup file");
    }
  };

  return (
    <div>
      <PageHeader
        title="System Settings"
        description="Device maintenance and configuration backup"
      />
      <div className="grid md:grid-cols-2 gap-3">
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Backup & Restore</div>
          <p className="text-xs text-muted-foreground">
            Export and import all configuration including promos, vouchers and settings.
          </p>
          <div className="flex gap-2">
            <Button
              size="sm"
              variant="outline"
              onClick={() => window.open(settingsApi.backupUrl(), "_blank")}
            >
              <Download className="h-4 w-4" /> Backup
            </Button>
            <Button size="sm" variant="outline" onClick={() => restoreInputRef.current?.click()}>
              <Upload className="h-4 w-4" /> Restore
            </Button>
            <input
              ref={restoreInputRef}
              type="file"
              accept="application/json,.json"
              className="hidden"
              onChange={(event) => void restoreBackup(event.target.files?.[0])}
            />
          </div>
        </div>

        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Maintenance</div>
          <div className="flex flex-wrap gap-2">
            <Button size="sm" variant="outline" onClick={() => rebootMutation.mutate()}>
              <RotateCcw className="h-4 w-4" /> Reboot
            </Button>
            <Button size="sm" variant="destructive" onClick={() => resetMutation.mutate()}>
              <Power className="h-4 w-4" /> Factory Reset
            </Button>
          </div>
          <p className="text-xs text-muted-foreground">
            Factory reset will erase all promos, vouchers, and connection settings.
          </p>
        </div>
      </div>
    </div>
  );
}
