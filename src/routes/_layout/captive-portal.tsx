import { useRef, useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { Upload, X } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { toast } from "sonner";

export const Route = createFileRoute("/_layout/captive-portal")({
  component: PortalPage,
});

const MAX_BYTES = 200 * 1024;
const PREFERRED_BYTES = 100 * 1024;
const ALLOWED = ["image/png", "image/jpeg", "image/webp"];
const MAX_W = 440;

async function optimizeImage(file: File): Promise<string> {
  const bitmap = await createImageBitmap(file);
  const ratio = bitmap.width > MAX_W ? MAX_W / bitmap.width : 1;
  const w = Math.round(bitmap.width * ratio);
  const h = Math.round(bitmap.height * ratio);
  const canvas = document.createElement("canvas");
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext("2d")!;
  ctx.drawImage(bitmap, 0, 0, w, h);
  let quality = 0.85;
  let dataUrl = canvas.toDataURL("image/webp", quality);
  while (dataUrl.length * 0.75 > PREFERRED_BYTES && quality > 0.4) {
    quality -= 0.1;
    dataUrl = canvas.toDataURL("image/webp", quality);
  }
  return dataUrl;
}

function PortalPage() {
  const [portalName, setPortalName] = useState("");
  const [banner, setBanner] = useState<string | null>(null);
  const [bannerSize, setBannerSize] = useState<number>(0);
  const fileRef = useRef<HTMLInputElement>(null);

  const handleFile = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    if (!ALLOWED.includes(file.type)) {
      toast.error("Only PNG, JPG, or WEBP allowed");
      return;
    }
    if (file.size > MAX_BYTES) {
      toast.error("File exceeds 200KB hard limit");
      return;
    }
    try {
      const optimized = await optimizeImage(file);
      const approxBytes = Math.round(optimized.length * 0.75);
      setBanner(optimized);
      setBannerSize(approxBytes);
      toast.success(`Banner optimized (${(approxBytes / 1024).toFixed(1)}KB)`);
    } catch {
      toast.error("Failed to process image");
    }
  };

  const clearBanner = () => {
    setBanner(null);
    setBannerSize(0);
    if (fileRef.current) fileRef.current.value = "";
  };

  return (
    <div>
      <PageHeader
        title="Captive Portal"
        description="Configure the branding shown on your captive portal"
      />
      <div className="max-w-xl space-y-4 rounded-md border bg-card p-3">
        <div className="space-y-1">
          <Label className="text-xs">Portal Banner / Logo</Label>
          <Input
            ref={fileRef}
            type="file"
            accept="image/png,image/jpeg,image/webp"
            onChange={handleFile}
            className="h-9 text-xs"
          />
          <p className="text-[11px] text-muted-foreground">
            Recommended: WEBP format, max 100KB. Hard limit 200KB.
          </p>

          {banner && (
            <div className="relative inline-block mt-2">
              <img
                src={banner}
                alt="Portal banner"
                className="rounded-md object-contain"
                style={{ maxWidth: 220, maxHeight: 120 }}
              />
              <button
                type="button"
                onClick={clearBanner}
                aria-label="Remove banner"
                className="absolute -top-2 -right-2 h-6 w-6 rounded-full bg-destructive text-destructive-foreground flex items-center justify-center shadow"
              >
                <X className="h-3.5 w-3.5" />
              </button>
              <p className="text-[11px] text-muted-foreground mt-1">
                Optimized: {(bannerSize / 1024).toFixed(1)}KB
              </p>
            </div>
          )}
        </div>

        <div className="space-y-1">
          <Label className="text-xs">Web Portal Name (Optional)</Label>
          <Input
            value={portalName}
            onChange={(e) => setPortalName(e.target.value)}
            placeholder="e.g. Renz-Fi Hotspot"
          />
        </div>

        <Button size="sm" onClick={() => toast.success("Settings saved")}>
          <Upload /> Save Settings
        </Button>
      </div>
    </div>
  );
}
