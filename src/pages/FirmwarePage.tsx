import { useState } from "react";
import { useQuery, useMutation } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Progress } from "@/components/ui/progress";
import { Upload } from "lucide-react";
import { firmwareApi } from "@/services/firmware";
import { toast } from "sonner";

export default function FirmwarePage() {
  const { data: info } = useQuery({
    queryKey: ["firmware"],
    queryFn: () => firmwareApi.info(),
  });
  const [progress, setProgress] = useState(0);
  const [uploading, setUploading] = useState(false);
  const [file, setFile] = useState<File | null>(null);

  const uploadMutation = useMutation({
    mutationFn: () =>
      firmwareApi.upload(
        file
          ? {
              filename: file.name,
              sizeBytes: file.size,
              mimeType: file.type || "application/octet-stream",
            }
          : undefined,
      ),
    onMutate: () => {
      setUploading(true);
      setProgress(0);
      const id = setInterval(() => {
        setProgress((p) => {
          if (p >= 100) {
            clearInterval(id);
            setUploading(false);
            return 100;
          }
          return p + 5;
        });
      }, 120);
    },
    onSuccess: () => toast.success("Firmware upload prepared"),
  });

  return (
    <div>
      <PageHeader title="Firmware Update" description="Upload new ESP32 firmware (.bin)" />
      <div className="grid md:grid-cols-2 gap-3">
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Current Firmware</div>
          <div className="text-xs text-muted-foreground">Version</div>
          <div className="font-mono text-lg">{info?.version ?? "v1.0.0"}</div>
          <div className="text-xs text-muted-foreground">
            Build: {info?.build ?? "2026-05-20"} · {info?.sizeMb ?? 1.2} MB
          </div>
        </div>
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Upload New Firmware</div>
          <Input
            type="file"
            accept=".bin"
            onChange={(event) => setFile(event.target.files?.[0] ?? null)}
          />
          <Button size="sm" onClick={() => uploadMutation.mutate()} disabled={uploading}>
            <Upload className="h-4 w-4" /> {uploading ? "Uploading..." : "Flash"}
          </Button>
          {progress > 0 && (
            <div className="space-y-1">
              <Progress value={progress} className="h-2" />
              <div className="text-xs text-muted-foreground tabular-nums">{progress}%</div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
