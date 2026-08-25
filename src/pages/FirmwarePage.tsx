import { useEffect, useRef, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Progress } from "@/components/ui/progress";
import { RefreshCw, Upload } from "lucide-react";
import { firmwareApi, type FirmwareProgress } from "@/services/firmware";
import { apiUrl, embeddedApi } from "@/services/embeddedApi";
import { toast } from "sonner";
import { ConfigCard } from "@/components/system-config/ConfigCard";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import { InfoRow } from "@/components/system-config/InfoRow";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import {
  fetchLatestGithubRelease,
  GITHUB_REPO_URL,
  isGithubOfflineError,
  normalizeVersionLabel,
  type GithubReleaseInfo,
} from "@/lib/githubReleases";

function versionsMatch(latest: string, current: string | null | undefined): boolean {
  const next = normalizeVersionLabel(latest);
  const running = normalizeVersionLabel(current);
  return Boolean(next && running && next === running);
}

export default function FirmwarePage() {
  const queryClient = useQueryClient();
  const {
    data: info,
    refetch,
    isLoading,
  } = useQuery({
    queryKey: ["firmware"],
    queryFn: () => firmwareApi.info(),
  });
  const [progress, setProgress] = useState(0);
  const [phase, setPhase] = useState<string>("");
  const [uploading, setUploading] = useState(false);
  const [file, setFile] = useState<File | null>(null);
  const [md5, setMd5] = useState<string | null>(null);
  const [offlineOpen, setOfflineOpen] = useState(false);
  const [latestRelease, setLatestRelease] = useState<GithubReleaseInfo | null>(null);
  const eventSourceRef = useRef<EventSource | null>(null);

  useEffect(() => {
    return () => eventSourceRef.current?.close();
  }, []);

  const refreshDashboard = async () => {
    await Promise.all([
      queryClient.invalidateQueries({ queryKey: ["firmware"] }),
      queryClient.invalidateQueries({ queryKey: ["appliance", "build"] }),
      queryClient.invalidateQueries({ queryKey: ["system", "status"] }),
      queryClient.invalidateQueries({ queryKey: ["health"] }),
      queryClient.invalidateQueries({ queryKey: ["health", "installationState"] }),
    ]);
  };

  const checkMutation = useMutation({
    mutationFn: fetchLatestGithubRelease,
    onSuccess: async (release) => {
      setLatestRelease(release);
      await refreshDashboard();
      const current = info?.version;
      if (versionsMatch(release.tag, current)) {
        toast.success("Already latest version");
      } else {
        toast.success(`Update available: ${release.tag}`);
      }
    },
    onError: (error) => {
      if (isGithubOfflineError(error)) {
        setOfflineOpen(true);
        return;
      }
      toast.error(error instanceof Error ? error.message : "Unable to check GitHub updates");
    },
  });

  const uploadMutation = useMutation({
    mutationFn: async () => {
      if (!file) throw new Error("Select a .bin firmware file");
      if (file.name.toLowerCase().endsWith(".ino")) {
        throw new Error(".ino source files cannot be flashed — build and upload the .bin");
      }
      if (!file.name.toLowerCase().endsWith(".bin")) {
        throw new Error("Only .bin firmware images are accepted");
      }

      setUploading(true);
      setProgress(0);
      setPhase("upload");
      setMd5(null);

      eventSourceRef.current?.close();
      eventSourceRef.current = new EventSource(apiUrl(embeddedApi.events), {
        withCredentials: true,
      });
      eventSourceRef.current.addEventListener("firmware.progress", (event) => {
        try {
          const detail = JSON.parse((event as MessageEvent).data) as FirmwareProgress;
          if (detail.phase === "verify") {
            setPhase("verify");
            setProgress(100);
            if (detail.md5) setMd5(detail.md5);
          } else if (detail.phase === "complete") {
            setPhase("complete");
            if (detail.md5) setMd5(detail.md5);
          }
        } catch {
          /* ignore */
        }
      });

      return firmwareApi.uploadBin(file, (pct) => {
        setProgress(pct);
        setPhase("upload");
      });
    },
    onSuccess: (result) => {
      toast.success("Firmware verified — device rebooting");
      if (result.md5) setMd5(result.md5);
      setPhase("rebooting");
      setTimeout(() => refetch(), 8000);
    },
    onError: (err: Error) => {
      toast.error(err.message || "Firmware update failed");
    },
    onSettled: () => {
      setUploading(false);
      eventSourceRef.current?.close();
      eventSourceRef.current = null;
    },
  });

  const currentVersion = info?.version ?? null;
  const upToDate = latestRelease ? versionsMatch(latestRelease.tag, currentVersion) : false;

  return (
    <div className="flex w-full max-w-none flex-col gap-4">
      <div>
        <h2 className="text-2xl font-semibold leading-tight">Update</h2>
        <p className="mt-0.5 text-[13px] text-muted-foreground">
          Check GitHub for the latest release tag, then flash an ESP32 firmware image (.bin) if
          needed.
        </p>
      </div>

      <ConfigCard
        title="Check for updates"
        description="Reads the latest release tag from GitHub and refreshes the Admin dashboard version."
        actions={
          latestRelease ? (
            <ConfigStatusBadge
              label={upToDate ? "Already latest version" : "Update available"}
              tone={upToDate ? "ok" : "warn"}
            />
          ) : null
        }
      >
        <div className="rounded-md border bg-muted/20 px-3">
          <InfoRow
            label="Running version"
            value={isLoading ? "Loading…" : (currentVersion ?? "—")}
            mono
          />
          <InfoRow label="GitHub latest" value={latestRelease?.tag ?? "Not checked yet"} mono />
        </div>
        <Button
          size="sm"
          variant="outline"
          disabled={checkMutation.isPending}
          onClick={() => checkMutation.mutate()}
        >
          <RefreshCw className={checkMutation.isPending ? "h-4 w-4 animate-spin" : "h-4 w-4"} />
          {checkMutation.isPending ? "Checking…" : "Check update"}
        </Button>
        {latestRelease ? (
          <div className="space-y-1 text-[12px]">
            <p
              className={
                upToDate
                  ? "font-medium text-emerald-700 dark:text-emerald-400"
                  : "font-medium text-amber-800 dark:text-amber-300"
              }
            >
              {upToDate ? "Already latest version" : `Update available: ${latestRelease.tag}`}
            </p>
            <p className="text-muted-foreground">
              Latest tag{" "}
              <a
                href={latestRelease.htmlUrl}
                target="_blank"
                rel="noopener noreferrer"
                className="font-medium text-primary hover:underline"
              >
                {latestRelease.tag}
              </a>
              {latestRelease.publishedAt
                ? ` · ${new Date(latestRelease.publishedAt).toLocaleDateString()}`
                : ""}
              .{" "}
              <a
                href={GITHUB_REPO_URL}
                target="_blank"
                rel="noopener noreferrer"
                className="text-primary hover:underline"
              >
                Open GitHub
              </a>
            </p>
          </div>
        ) : (
          <p className="text-[12px] text-muted-foreground">
            Internet is required to check GitHub release tags. This does not flash the ESP32.
          </p>
        )}
      </ConfigCard>

      <div className="grid grid-cols-1 items-start gap-4 lg:grid-cols-2">
        <ConfigCard title="Current Firmware">
          <div className="rounded-md border bg-muted/20 px-3">
            <InfoRow label="Version" value={isLoading ? "Loading…" : (info?.version ?? "—")} mono />
            <InfoRow label="Build" value={info?.build ?? "—"} mono />
            <InfoRow label="Size" value={info?.sizeMb != null ? `${info.sizeMb} MB` : "—"} mono />
            {info?.partition ? <InfoRow label="Partition" value={info.partition} mono /> : null}
          </div>
        </ConfigCard>

        <ConfigCard title="Upload New Firmware">
          <Input
            type="file"
            accept=".bin,application/octet-stream"
            onChange={(event) => setFile(event.target.files?.[0] ?? null)}
            className="h-10 w-full min-w-0 text-xs file:mr-3 file:h-8 file:rounded-md file:border-0 file:bg-primary file:px-3 file:text-xs file:font-medium file:text-primary-foreground"
          />
          {file ? (
            <p className="break-all font-mono text-[12px] text-muted-foreground">{file.name}</p>
          ) : null}
          <div className="space-y-1 rounded-lg border bg-muted/20 p-3 text-[12px] text-muted-foreground">
            <p>
              <span className="font-medium text-foreground">Upload here:</span> the compiled ESP32{" "}
              <span className="font-mono">firmware.bin</span> from PlatformIO (for example{" "}
              <span className="font-mono">.pio/build/.../firmware.bin</span>).
            </p>
            <p>
              <span className="font-medium text-foreground">Do not upload:</span> Admin dashboard
              files, SPIFFS image, <span className="font-mono">.ino</span> source, ZIP, or portal
              HTML. Those are not firmware flashes.
            </p>
            <p>
              Admin dashboard updates use <span className="font-mono">npm run deploy:esp32</span> /
              SPIFFS upload — separate from flashing .bin. Settings and sales data are preserved on
              firmware flash.
            </p>
          </div>
          <Button size="sm" onClick={() => uploadMutation.mutate()} disabled={uploading || !file}>
            <Upload className="h-4 w-4" /> {uploading ? "Flashing…" : "Flash Firmware"}
          </Button>
          {progress > 0 ? (
            <div className="space-y-1">
              <Progress value={progress} className="h-2" />
              <div className="text-xs text-muted-foreground tabular-nums">
                {phase === "upload" && `Uploading… ${progress}%`}
                {phase === "verify" && "Verifying firmware checksum…"}
                {phase === "complete" && "Flash complete"}
                {phase === "rebooting" && "Rebooting device…"}
              </div>
              {md5 ? (
                <div className="break-all font-mono text-[11px] text-muted-foreground">
                  MD5: {md5}
                </div>
              ) : null}
            </div>
          ) : null}
        </ConfigCard>
      </div>

      <AlertDialog open={offlineOpen} onOpenChange={setOfflineOpen}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>No internet connection</AlertDialogTitle>
            <AlertDialogDescription>To update, connect to the internet.</AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogAction>OK</AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}
