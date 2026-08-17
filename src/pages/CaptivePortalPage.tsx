import { useRef, useState, useEffect } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { Trash2, Upload, X } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { toast } from "sonner";
import { portalApi } from "@/services/portal";
import { resolvePortalAssetUrl } from "@/services/embeddedApi";

const BANNER_MAX_BYTES = 2 * 1024 * 1024;
const MUSIC_MAX_BYTES = 2 * 1024 * 1024;
const BANNER_ALLOWED = ["image/png", "image/jpeg", "image/jpg"];

export default function CaptivePortalPage() {
  const qc = useQueryClient();
  const { data: settings } = useQuery({
    queryKey: ["portal", "settings"],
    queryFn: () => portalApi.settings(),
  });
  const [bannerPreview, setBannerPreview] = useState<string | null>(null);
  const [bannerBlob, setBannerBlob] = useState<Blob | null>(null);
  const [bannerSize, setBannerSize] = useState(0);
  const [bannerName, setBannerName] = useState("banner.png");
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
    if (!settings?.bannerUrl || bannerBlob) return;
    if (bannerConfigured) {
      setBannerPreview(resolvePortalAssetUrl(settings.bannerUrl));
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
      if (bannerBlob) {
        const file = new File([bannerBlob], bannerName, {
          type: bannerBlob.type || "image/png",
        });
        await portalApi.uploadBanner(file);
      }
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
        toast.error("Upload did not persist — check device storage");
        return;
      }
      toast.success("Portal branding saved");
      setBannerBlob(null);
      setBannerSize(0);
      setMusicFile(null);
      if (bannerRef.current) bannerRef.current.value = "";
      if (musicRef.current) musicRef.current.value = "";
      if (latest.bannerUrl && (latest.bannerConfigured ?? latest.has_banner)) {
        setBannerPreview(resolvePortalAssetUrl(latest.bannerUrl));
      }
    },
    onError: () => toast.error("Failed to save portal assets"),
  });

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

  const removeBannerMutation = useMutation({
    mutationFn: () => portalApi.deleteBanner(),
    onSuccess: async () => {
      await qc.invalidateQueries({ queryKey: ["portal", "settings"] });
      clearBanner();
      toast.success("Custom banner removed — default banner will be used");
    },
    onError: () => toast.error("Failed to remove banner"),
  });

  const removeMusicMutation = useMutation({
    mutationFn: () => portalApi.deleteMusic(),
    onSuccess: async () => {
      await qc.invalidateQueries({ queryKey: ["portal", "settings"] });
      clearMusic();
      toast.success("Custom music removed — default bg_music.mp3 will be used");
    },
    onError: () => toast.error("Failed to remove music"),
  });

  const handleBanner = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const typeOk =
      BANNER_ALLOWED.includes(file.type) || /\.(png|jpe?g)$/i.test(file.name);
    if (!typeOk) {
      toast.error("Only PNG or JPEG allowed");
      return;
    }
    if (file.size > BANNER_MAX_BYTES) {
      toast.error("Banner exceeds 2 MB limit");
      return;
    }
    if (bannerPreview?.startsWith("blob:")) URL.revokeObjectURL(bannerPreview);
    setBannerBlob(file);
    setBannerSize(file.size);
    setBannerName(file.name);
    setBannerPreview(URL.createObjectURL(file));
    toast.success(`Banner ready (${(file.size / 1024).toFixed(1)} KiB)`);
  };

  const handleMusic = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    if (!file.name.toLowerCase().endsWith(".mp3") && file.type !== "audio/mpeg") {
      toast.error("Only MP3 files are allowed");
      return;
    }
    if (file.size > MUSIC_MAX_BYTES) {
      toast.error("Music exceeds 2 MB limit");
      return;
    }
    setMusicFile(file);
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
            accept="image/png,image/jpeg,.png,.jpg,.jpeg"
            onChange={handleBanner}
            className="h-9 text-xs"
          />
          <p className="text-[11px] text-muted-foreground">
            PNG or JPEG, maximum 2 MB. Falls back to Default-Banner.png when none is uploaded.
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
              disabled={removeBannerMutation.isPending}
              onClick={() => removeBannerMutation.mutate()}
            >
              <Trash2 className="h-3.5 w-3.5" /> Remove custom banner
            </Button>
          ) : null}
        </div>

        <div className="space-y-1">
          <Label className="text-xs">Background Music</Label>
          <Input
            ref={musicRef}
            type="file"
            accept="audio/mpeg,.mp3"
            onChange={handleMusic}
            className="h-9 text-xs"
          />
          <p className="text-[11px] text-muted-foreground">
            MP3 only, maximum 2 MB. Falls back to bg_music.mp3 when none is uploaded.
          </p>
          <p className="text-[11px]">
            {musicConfigured ? (
              <span className="text-emerald-600">Custom music active</span>
            ) : (
              <span className="text-muted-foreground">Using default music (bg_music.mp3)</span>
            )}
          </p>
          {musicConfigured && settings?.musicUrl && !musicFile ? (
            <audio controls className="w-full mt-2" src={resolvePortalAssetUrl(settings.musicUrl)} />
          ) : null}
          {musicFile && (
            <p className="text-[11px] text-muted-foreground">
              {musicFile.name} ({(musicFile.size / 1024).toFixed(1)} KiB)
            </p>
          )}
          {musicConfigured && !musicFile ? (
            <Button
              size="sm"
              variant="outline"
              className="mt-2"
              disabled={removeMusicMutation.isPending}
              onClick={() => removeMusicMutation.mutate()}
            >
              <Trash2 className="h-3.5 w-3.5" /> Remove custom music
            </Button>
          ) : null}
        </div>

        <Button
          size="sm"
          disabled={saveMutation.isPending || (!bannerBlob && !musicFile)}
          onClick={() => saveMutation.mutate()}
        >
          <Upload className="h-3.5 w-3.5" />
          {saveMutation.isPending ? "Saving…" : "Save branding"}
        </Button>
      </div>
    </div>
  );
}
