import { useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Progress } from "@/components/ui/progress";
import { Upload } from "lucide-react";

export const Route = createFileRoute("/_layout/firmware")({
  component: FirmwarePage,
});

function FirmwarePage() {
  const [progress, setProgress] = useState(0);
  const [uploading, setUploading] = useState(false);

  const upload = () => {
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
  };

  return (
    <div>
      <PageHeader title="Firmware Update" description="Upload new ESP32 firmware (.bin)" />
      <div className="grid md:grid-cols-2 gap-3">
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Current Firmware</div>
          <div className="text-xs text-muted-foreground">Version</div>
          <div className="font-mono text-lg">v1.0.0</div>
          <div className="text-xs text-muted-foreground">Build: 2026-05-20 · 1.2 MB</div>
        </div>
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Upload New Firmware</div>
          <Input type="file" accept=".bin" />
          <Button size="sm" onClick={upload} disabled={uploading}>
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
