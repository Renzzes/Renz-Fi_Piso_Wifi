import { useRef, useState, useEffect } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { Music, Trash2, Upload, X } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { toast } from "sonner";
import { portalApi } from "@/services/portal";
import { resolvePortalAssetUrl } from "@/services/embeddedApi";

const BANNER_MAX_BYTES = 200 * 1024;
const BANNER_PREFERRED_BYTES = 100 * 1024;
const MUSIC_MAX_BYTES = 1000 * 1024;
const MUSIC_PREFERRED_BYTES = 950 * 1024;
const BANNER_ALLOWED = ["image/png", "image/jpeg", "image/webp"];
const MAX_W = 440;

async function optimizeBanner(file: File): Promise<Blob> {
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
  let blob = await new Promise<Blob>((resolve, reject) => {
    canvas.toBlob((b) => (b ? resolve(b) : reject(new Error("encode failed"))), "image/webp", quality);
  });
  while (blob.size > BANNER_PREFERRED_BYTES && quality > 0.4) {
    quality -= 0.1;
    blob = await new Promise<Blob>((resolve, reject) => {
      canvas.toBlob((b) => (b ? resolve(b) : reject(new Error("encode failed"))), "image/webp", quality);
    });
  }
  return blob;
}

export default function CaptivePortalPage() {
  const qc = useQueryClient();
  const { data: settings } = useQuery({
    queryKey: ["portal", "settings"],
    queryFn: () => portalApi.settings(),
  });
  const [bannerPreview, setBannerPreview] = useState<string | null>(null);
  const [bannerBlob, setBannerBlob] = useState<Blob | null>(null);
  const [bannerSize, setBannerSize] = useState(0);
  const [musicFile, setMusicFile] = useState<File | null>(null);
  const bannerRef = useRef<HTMLInputElement>(null);
  const musicRef = useRef<HTMLInputElement>(null);

  const bannerConfigured = Boolean(
    settings?.bannerConfigured ?? settings?.has_banner ?? settings?.hasCustomBanner,
  );
  const musicConfigured = Boolean(
    settings?.musicConfigured ?? settings?.has_music ?? settings?.hasCustomMusic,
  );

  useEffect(() => {
    if (!settings?.musicUrl || !musicConfigured) return;
    console.log("Music URL", settings.musicUrl, "→", resolvePortalAssetUrl(settings.musicUrl));
  }, [settings?.musicUrl, musicConfigured]);

  useEffect(() => {
    if (!settings?.bannerUrl || bannerBlob) return;
    if (bannerConfigured) {
      const resolved = resolvePortalAssetUrl(settings.bannerUrl);
      console.log("Banner URL", settings.bannerUrl, "→", resolved);
      setBannerPreview(resolved);
    } else {
      setBannerPreview(null);
    }
  }, [
    settings?.bannerUrl,
    settings?.bannerConfigured,
    settings?.has_banner,
    settings?.hasCustomBanner,
    bannerBlob,
    bannerConfigured,
  ]);

  useEffect(() => {
    return () => {
      if (bannerPreview && bannerPreview.startsWith("blob:")) {
        URL.revokeObjectURL(bannerPreview);
      }
    };
  }, [bannerPreview]);

  const saveMutation = useMutation({
    mutationFn: async () => {
      const hadBanner = Boolean(bannerBlob);
      const hadMusic = Boolean(musicFile);
      if (bannerBlob) await portalApi.uploadBanner(bannerBlob);
      if (musicFile) await portalApi.uploadMusic(musicFile);
      const latest = await portalApi.settings();
      return { latest, hadBanner, hadMusic };
    },
    onSuccess: ({ latest, hadBanner, hadMusic }) => {
      qc.setQueryData(["portal", "settings"], latest);

      const bannerOk =
        !hadBanner ||
        Boolean(latest.bannerConfigured ?? latest.has_banner ?? latest.hasCustomBanner);
      const musicOk =
        !hadMusic ||
        Boolean(latest.musicConfigured ?? latest.has_music ?? latest.hasCustomMusic);

      if (!bannerOk || !musicOk) {
        toast.error(
          !bannerOk && !musicOk
            ? "Upload did not persist — check firmware logs for [portal-upload]"
            : !bannerOk
              ? "Banner upload did not persist on device"
              : "Music upload did not persist on device",
        );
        return;
      }

      toast.success("Portal branding saved — captive portal will refresh automatically");
      setBannerBlob(null);
      setMusicFile(null);
      if (bannerRef.current) bannerRef.current.value = "";
      if (musicRef.current) musicRef.current.value = "";
      if (latest.bannerUrl && (latest.bannerConfigured ?? latest.has_banner)) {
        const resolved = resolvePortalAssetUrl(latest.bannerUrl);
        console.log("Banner URL", latest.bannerUrl, "→", resolved);
        setBannerPreview(resolved);
      }
    },
    onError: () => toast.error("Failed to save portal branding"),
  });

  const deleteBannerMutation = useMutation({
    mutationFn: () => portalApi.deleteBanner(),
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["portal"] });
      clearBanner();
      toast.success("Custom banner removed — default banner will be used");
    },
    onError: () => toast.error("Failed to remove banner"),
  });

  const deleteMusicMutation = useMutation({
    mutationFn: () => portalApi.deleteMusic(),
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["portal"] });
      clearMusic();
      toast.success("Custom music removed — default bg_music.mp3 will be used");
    },
    onError: () => toast.error("Failed to remove music"),
  });

  const handleBanner = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    if (!BANNER_ALLOWED.includes(file.type)) {
      toast.error("Only PNG, JPG, or WEBP allowed");
      return;
    }
    if (file.size > BANNER_MAX_BYTES) {
      toast.error("Banner exceeds 200 KiB hard limit");
      return;
    }
    try {
      const blob = await optimizeBanner(file);
      if (blob.size > BANNER_MAX_BYTES) {
        toast.error("Optimized banner still exceeds 200 KiB");
        return;
      }
      if (bannerPreview?.startsWith("blob:")) URL.revokeObjectURL(bannerPreview);
      setBannerBlob(blob);
      setBannerSize(blob.size);
      setBannerPreview(URL.createObjectURL(blob));
      toast.success(`Banner ready (${(blob.size / 1024).toFixed(1)} KiB)`);
    } catch {
      toast.error("Failed to process banner");
    }
  };

  const handleMusic = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    if (!file.name.toLowerCase().endsWith(".mp3") && file.type !== "audio/mpeg") {
      toast.error("Only MP3 files are allowed");
      return;
    }
    if (file.size > MUSIC_MAX_BYTES) {
      toast.error("Music exceeds 1000 KiB hard limit");
      return;
    }
    if (file.size > MUSIC_PREFERRED_BYTES) {
      toast.warning(`Music is ${(file.size / 1024).toFixed(0)} KiB — recommended max is 950 KiB`);
    }
    setMusicFile(file);
  };

  const clearBanner = () => {
    if (bannerPreview?.startsWith("blob:")) URL.revokeObjectURL(bannerPreview);
    setBannerPreview(null);
    setBannerBlob(null);
    setBannerSize(0);
    if (bannerRef.current) bannerRef.current.value = "";
  };

  const clearMusic = () => {
    setMusicFile(null);
    if (musicRef.current) musicRef.current.value = "";
  };

  return (
    <div>
      <PageHeader
        title="Captive Portal"
        description="Configure banner and background music shown on the captive portal login page"
      />
      <div className="max-w-xl space-y-4 rounded-md border bg-card p-3">
        <div className="space-y-1">
          <Label className="text-xs">Portal Banner / Logo</Label>
          <Input
            ref={bannerRef}
            type="file"
            accept="image/png,image/jpeg,image/webp"
            onChange={handleBanner}
            className="h-9 text-xs"
          />
          <p className="text-[11px] text-muted-foreground">
            Recommended: WEBP, max 100 KiB. Hard limit 200 KiB. Falls back to Default-Banner.png when
            none is uploaded.
          </p>
          <p className="text-[11px]">
            {bannerConfigured ? (
              <span className="text-emerald-600">Custom banner active</span>
            ) : (
              <span className="text-muted-foreground">Using default banner (Default-Banner.png)</span>
            )}
          </p>

          {bannerPreview && (
            <div className="relative inline-block mt-2">
              <img
                src={bannerPreview}
                alt="Portal banner preview"
                className="rounded-md object-contain"
                style={{ maxWidth: 220, maxHeight: 120 }}
              />
              <button
                type="button"
                onClick={clearBanner}
                aria-label="Clear pending banner"
                className="absolute -top-2 -right-2 h-6 w-6 rounded-full bg-destructive text-destructive-foreground flex items-center justify-center shadow"
              >
                <X className="h-3.5 w-3.5" />
              </button>
              {bannerBlob ? (
                <p className="text-[11px] text-muted-foreground mt-1">
                  Ready to upload: {(bannerSize / 1024).toFixed(1)} KiB
                </p>
              ) : null}
            </div>
          )}

          {bannerConfigured && !bannerBlob ? (
            <Button
              size="sm"
              variant="outline"
              className="mt-2"
              onClick={() => deleteBannerMutation.mutate()}
              disabled={deleteBannerMutation.isPending}
            >
              <Trash2 className="h-3.5 w-3.5" /> Remove custom banner
            </Button>
          ) : null}
        </div>

        <div className="space-y-1">
          <Label className="text-xs">Background Music Upload</Label>
          <Input
            ref={musicRef}
            type="file"
            accept="audio/mpeg,.mp3"
            onChange={handleMusic}
            className="h-9 text-xs"
          />
          <p className="text-[11px] text-muted-foreground">
            MP3 only. Recommended 950 KiB, hard limit 1000 KiB. Falls back to bg_music.mp3 when none
            is uploaded.
          </p>
          <p className="text-[11px]">
            {musicConfigured ? (
              <span className="text-emerald-600">Custom music active</span>
            ) : (
              <span className="text-muted-foreground">Using default music (bg_music.mp3)</span>
            )}
          </p>
          {musicConfigured && settings?.musicUrl && !musicFile ? (
            <audio controls preload="none" className="w-full mt-2 h-8">
              <source
                src={resolvePortalAssetUrl(settings.musicUrl)}
                type="audio/mpeg"
              />
            </audio>
          ) : null}
          {musicFile && (
            <div className="flex items-center gap-2 mt-1 text-xs">
              <Music className="h-3.5 w-3.5 text-muted-foreground" />
              <span>{musicFile.name}</span>
              <span className="text-muted-foreground">
                ({(musicFile.size / 1024).toFixed(1)} KiB)
              </span>
              <button type="button" onClick={clearMusic} className="text-destructive hover:underline">
                Remove
              </button>
            </div>
          )}
          {musicConfigured && !musicFile ? (
            <Button
              size="sm"
              variant="outline"
              className="mt-2"
              onClick={() => deleteMusicMutation.mutate()}
              disabled={deleteMusicMutation.isPending}
            >
              <Trash2 className="h-3.5 w-3.5" /> Remove custom music
            </Button>
          ) : null}
        </div>

        <Button
          size="sm"
          onClick={() => saveMutation.mutate()}
          disabled={saveMutation.isPending || (!bannerBlob && !musicFile)}
        >
          <Upload /> Save Settings
        </Button>
      </div>
    </div>
  );
}
