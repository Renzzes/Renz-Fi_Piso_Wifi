import { useEffect, useRef, useState } from "react";
import { useQuery, useMutation } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Progress } from "@/components/ui/progress";
import { Upload } from "lucide-react";
import { firmwareApi, type FirmwareProgress } from "@/services/firmware";
import { apiUrl, embeddedApi } from "@/services/embeddedApi";
import { toast } from "sonner";

export default function FirmwarePage() {
  const { data: info, refetch } = useQuery({
    queryKey: ["firmware"],
    queryFn: () => firmwareApi.info(),
  });
  const [progress, setProgress] = useState(0);
  const [phase, setPhase] = useState<string>("");
  const [uploading, setUploading] = useState(false);
  const [file, setFile] = useState<File | null>(null);
  const [md5, setMd5] = useState<string | null>(null);
  const eventSourceRef = useRef<EventSource | null>(null);

  useEffect(() => {
    return () => eventSourceRef.current?.close();
  }, []);

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

  return (
    <div>
      <PageHeader
        title="Firmware Update"
        description="Flash a compiled ESP32 .bin over-the-air. Settings and sales data are preserved."
      />
      <div className="grid md:grid-cols-2 gap-3">
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Current Firmware</div>
          <div className="text-xs text-muted-foreground">Version</div>
          <div className="font-mono text-lg">{info?.version ?? "—"}</div>
          <div className="text-xs text-muted-foreground">
            Build: {info?.build ?? "—"} · {info?.sizeMb ?? "—"} MB
            {info?.partition ? ` · ${info.partition}` : ""}
          </div>
        </div>
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Upload New Firmware</div>
          <Input
            type="file"
            accept=".bin,application/octet-stream"
            onChange={(event) => setFile(event.target.files?.[0] ?? null)}
          />
          <p className="text-[11px] text-muted-foreground">
            Accepts .bin only. Rejects .ino. Image is checksum-verified before reboot.
          </p>
          <Button
            size="sm"
            onClick={() => uploadMutation.mutate()}
            disabled={uploading || !file}
          >
            <Upload className="h-4 w-4" /> {uploading ? "Flashing…" : "Flash Firmware"}
          </Button>
          {progress > 0 && (
            <div className="space-y-1">
              <Progress value={progress} className="h-2" />
              <div className="text-xs text-muted-foreground tabular-nums">
                {phase === "upload" && `Uploading… ${progress}%`}
                {phase === "verify" && "Verifying firmware checksum…"}
                {phase === "complete" && "Flash complete"}
                {phase === "rebooting" && "Rebooting device…"}
              </div>
              {md5 && (
                <div className="text-[11px] font-mono text-muted-foreground break-all">
                  MD5: {md5}
                </div>
              )}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
