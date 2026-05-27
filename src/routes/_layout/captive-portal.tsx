import { useRef, useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { Upload, X } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Textarea } from "@/components/ui/textarea";
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
  const [announce, setAnnounce] = useState("Insert coin to get internet access.");
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
      <div className="grid md:grid-cols-2 gap-3">
        {/* Settings */}
        <div className="rounded-md border bg-card p-3 space-y-4">
          <div>
            <h3 className="text-sm font-semibold mb-2">Portal Branding</h3>
            <div className="space-y-1">
              <Label className="text-xs">Portal Banner / Logo</Label>
              <div className="flex items-center gap-2">
                <Input
                  ref={fileRef}
                  type="file"
                  accept="image/png,image/jpeg,image/webp"
                  onChange={handleFile}
                  className="h-9 text-xs"
                />
                {banner && (
                  <Button size="icon" variant="outline" onClick={clearBanner} aria-label="Remove banner">
                    <X />
                  </Button>
                )}
              </div>
              <p className="text-[11px] text-muted-foreground">
                Recommended: WEBP format, max 100KB. Hard limit 200KB.
              </p>
              {banner && (
                <p className="text-[11px] text-muted-foreground">
                  Optimized size: {(bannerSize / 1024).toFixed(1)}KB
                </p>
              )}
            </div>
          </div>

          <div className="space-y-1">
            <Label className="text-xs">Web Portal Name (Optional)</Label>
            <Input
              value={portalName}
              onChange={(e) => setPortalName(e.target.value)}
              placeholder="e.g. Renz-Fi Hotspot"
            />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Announcement Message</Label>
            <Textarea value={announce} onChange={(e) => setAnnounce(e.target.value)} rows={2} />
          </div>

          <Button size="sm" onClick={() => toast.success("Settings saved")}>
            <Upload /> Save Settings
          </Button>
        </div>

        {/* Banner Preview */}
        <div className="rounded-md border bg-card p-3">
          <div className="text-xs text-muted-foreground mb-2">Banner Preview</div>
          <div className="flex flex-col items-center justify-center rounded-md border bg-background p-4 min-h-[160px]">
            {banner ? (
              <img
                src={banner}
                alt="Portal banner"
                className="rounded-md object-contain"
                style={{ maxWidth: 220, maxHeight: 120 }}
              />
            ) : (
              <div
                className="rounded-md flex items-center justify-center text-[11px] text-muted-foreground border border-dashed"
                style={{ width: 200, height: 90 }}
              >
                No banner uploaded
              </div>
            )}
            {portalName && (
              <p className="mt-3 text-sm font-semibold text-center">{portalName}</p>
            )}
            {announce && (
              <p className="mt-1 text-[11px] text-muted-foreground text-center max-w-[260px]">
                {announce}
              </p>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
