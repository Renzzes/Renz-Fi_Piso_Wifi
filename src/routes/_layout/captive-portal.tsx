import { useRef, useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { Upload, Wifi, X } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Textarea } from "@/components/ui/textarea";
import { toast } from "sonner";

export const Route = createFileRoute("/_layout/captive-portal")({
  component: PortalPage,
});

const MAX_BYTES = 200 * 1024; // hard limit 200KB
const PREFERRED_BYTES = 100 * 1024;
const ALLOWED = ["image/png", "image/jpeg", "image/webp"];
const MAX_W = 440; // 2x display for retina, keeps file small

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
  // Try webp first for smallest size; fall back to jpeg
  let quality = 0.85;
  let dataUrl = canvas.toDataURL("image/webp", quality);
  while (dataUrl.length * 0.75 > PREFERRED_BYTES && quality > 0.4) {
    quality -= 0.1;
    dataUrl = canvas.toDataURL("image/webp", quality);
  }
  return dataUrl;
}

function PortalPage() {
  const [welcome, setWelcome] = useState("Welcome to Renz-Fi");
  const [announce, setAnnounce] = useState("Insert coin to get internet access.");
  const [accent, setAccent] = useState("#3b82f6");
  const [btnColor, setBtnColor] = useState("#10b981");
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
        description="Configure the landing page shown to users"
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
            <Label className="text-xs">Welcome Message</Label>
            <Input value={welcome} onChange={(e) => setWelcome(e.target.value)} />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Announcement Message</Label>
            <Textarea value={announce} onChange={(e) => setAnnounce(e.target.value)} rows={2} />
          </div>
          <div className="grid grid-cols-2 gap-3">
            <div className="space-y-1">
              <Label className="text-xs">Accent Color</Label>
              <Input type="color" value={accent} onChange={(e) => setAccent(e.target.value)} className="h-9 p-1" />
            </div>
            <div className="space-y-1">
              <Label className="text-xs">Button Color</Label>
              <Input type="color" value={btnColor} onChange={(e) => setBtnColor(e.target.value)} className="h-9 p-1" />
            </div>
          </div>
          <Button size="sm" onClick={() => toast.success("Settings saved")}>
            <Upload /> Save Settings
          </Button>
        </div>

        {/* Live Preview */}
        <div className="rounded-md border bg-card p-3">
          <div className="text-xs text-muted-foreground mb-2">Live Preview</div>
          <div className="mx-auto max-w-[320px] rounded-md border bg-background p-4 space-y-3">
            <div className="flex justify-center">
              {banner ? (
                <img
                  src={banner}
                  alt="Portal banner"
                  className="rounded-md object-contain"
                  style={{ maxWidth: 200, maxHeight: 100 }}
                />
              ) : (
                <div
                  className="rounded-md flex items-center justify-center text-[10px] text-muted-foreground border border-dashed"
                  style={{ width: 200, height: 80 }}
                >
                  Banner / Logo
                </div>
              )}
            </div>
            <div className="text-center">
              <h3 className="text-base font-semibold" style={{ color: accent }}>
                {welcome}
              </h3>
              <p className="text-[11px] text-muted-foreground mt-1">{announce}</p>
            </div>
            <div className="flex items-center justify-center gap-1 text-[11px]">
              <Wifi className="h-3 w-3 text-green-500" />
              <span className="text-muted-foreground">Status: Online</span>
            </div>
            <div className="grid grid-cols-2 gap-2">
              <button
                className="px-2 py-1.5 rounded-md text-xs font-medium text-white"
                style={{ background: btnColor }}
              >
                Insert Coin
              </button>
              <button
                className="px-2 py-1.5 rounded-md text-xs font-medium border"
                style={{ borderColor: accent, color: accent }}
              >
                Promo Rates
              </button>
            </div>
            <div className="space-y-1.5">
              <input
                placeholder="Enter voucher code"
                className="w-full h-8 px-2 rounded-md border bg-background text-xs"
              />
              <button
                className="w-full px-2 py-1.5 rounded-md text-xs font-medium text-white"
                style={{ background: accent }}
              >
                Connect
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
