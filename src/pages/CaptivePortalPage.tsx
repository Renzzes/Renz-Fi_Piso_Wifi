import { useRef, useState, useEffect } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { Trash2, Upload, X } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { toast } from "sonner";
import { isApiError } from "@/services/api";
import { portalApi } from "@/services/portal";
import { resolvePortalAssetUrl } from "@/services/embeddedApi";
import { CaptivePortalLivePreview } from "@/components/captive-portal/CaptivePortalLivePreview";
import {
  isLivePreviewBannerVideo,
  resolveLivePreviewBannerSrc,
  resolvePreviewMusicUrl,
} from "@/lib/portalPreview";

const BANNER_MAX_BYTES = 4 * 1024 * 1024;
const MUSIC_MAX_BYTES = 4 * 1024 * 1024;
const BANNER_ALLOWED = ["image/png", "image/jpeg", "image/jpg", "video/mp4"];

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

  const livePreviewBannerSrc = resolveLivePreviewBannerSrc(bannerPreview, settings);
  const livePreviewIsVideo = isLivePreviewBannerVideo(settings, bannerBlob, bannerName);
  const previewMusicSrc = resolvePreviewMusicUrl(settings);

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
        !hadMusic || Boolean(latest.musicConfigured ?? latest.has_music ?? latest.hasCustomMusic);
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
      } else if (!latest.bannerConfigured && !latest.has_banner) {
        setBannerPreview(null);
      }
    },
    onError: (err) => {
      toast.error(isApiError(err) ? err.message : "Failed to save portal assets");
    },
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
    const typeOk = BANNER_ALLOWED.includes(file.type) || /\.(png|jpe?g|mp4)$/i.test(file.name);
    if (!typeOk) {
      toast.error("Only PNG, JPEG, or MP4 allowed");
      return;
    }
    if (file.size > BANNER_MAX_BYTES) {
      toast.error("Banner exceeds 4 MB limit");
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
      toast.error("Music exceeds 4 MB limit");
      return;
    }
    setMusicFile(file);
  };

  const fileInputClass =
    "h-10 w-full max-w-full text-xs file:mr-3 file:h-8 file:rounded-md file:border-0 file:bg-primary file:px-3 file:text-xs file:font-medium file:text-primary-foreground";

  return (
    <div className="w-full max-w-none space-y-3">
      <div>
        <h2 className="text-2xl font-semibold leading-tight">Captive Portal</h2>
        <p className="mt-0.5 text-[13px] text-muted-foreground">
          Configure banner and background music for the captive portal.
        </p>
      </div>
      <div className="w-full rounded-[14px] border bg-card p-4 sm:p-5">
        <div className="grid grid-cols-1 items-start gap-6 xl:grid-cols-2">
          <div className="min-w-0 space-y-5">
            <div>
              <h3 className="text-sm font-semibold text-foreground">Portal Configuration</h3>
              <p className="mt-0.5 text-[11px] text-muted-foreground">
                Upload assets, then save. Live Preview updates immediately when you choose a banner
                file.
              </p>
            </div>

            <div className="space-y-1.5">
              <Label className="text-xs">Portal Banner / Logo</Label>
              <Input
                ref={bannerRef}
                type="file"
                accept="image/png,image/jpeg,.png,.jpg,.jpeg,video/mp4,.mp4"
                onChange={handleBanner}
                className={fileInputClass}
              />
              <p className="text-[11px] text-muted-foreground">
                PNG, JPEG, or MP4 (short video), maximum 4 MB. Prefer under ~1.5 MB for reliable
                uploads on the appliance. Falls back to Default-Banner.png when none is uploaded.
              </p>
              <p className="text-[11px]">
                {bannerConfigured ? (
                  <span className="text-emerald-600">Custom banner active</span>
                ) : (
                  <span className="text-muted-foreground">
                    Using default banner (Default-Banner.png)
                  </span>
                )}
              </p>
              {bannerPreview && (
                <div className="relative mt-2 w-full">
                  {bannerBlob?.type === "video/mp4" ||
                  /\.mp4$/i.test(bannerName) ||
                  settings?.bannerIsVideo ? (
                    <video
                      src={bannerPreview}
                      className="max-h-64 w-full rounded-md border bg-muted object-cover"
                      controls
                      muted
                      playsInline
                    />
                  ) : (
                    <img
                      src={bannerPreview}
                      alt="Portal banner preview"
                      className="max-h-64 w-full rounded-md border bg-muted object-cover"
                    />
                  )}
                  <button
                    type="button"
                    onClick={clearBanner}
                    aria-label="Clear pending banner"
                    className="absolute -top-2 -right-2 flex h-6 w-6 items-center justify-center rounded-full bg-destructive text-destructive-foreground shadow"
                  >
                    <X className="h-3.5 w-3.5" />
                  </button>
                  {bannerBlob ? (
                    <p className="mt-1 text-[11px] text-muted-foreground">
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

            <div className="space-y-1.5">
              <Label className="text-xs">Background Music</Label>
              <Input
                ref={musicRef}
                type="file"
                accept="audio/mpeg,.mp3"
                onChange={handleMusic}
                className={fileInputClass}
              />
              <p className="text-[11px] text-muted-foreground">
                Background music during Insert Coin. MP3 only, maximum 4 MB. Falls back to
                bg_music.mp3 when none is uploaded.
              </p>
              <p className="text-[11px]">
                {musicConfigured ? (
                  <span className="text-emerald-600">Custom music active</span>
                ) : (
                  <span className="text-muted-foreground">Using default music (bg_music.mp3)</span>
                )}
              </p>
              {musicConfigured && settings?.musicUrl && !musicFile ? (
                <audio
                  controls
                  className="mt-2 w-full"
                  src={resolvePortalAssetUrl(settings.musicUrl)}
                />
              ) : null}
              {musicFile && (
                <p className="break-all text-[11px] text-muted-foreground">
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
              className="h-9 w-full px-4 sm:w-auto"
              disabled={saveMutation.isPending || (!bannerBlob && !musicFile)}
              onClick={() => saveMutation.mutate()}
            >
              <Upload className="h-3.5 w-3.5" />
              {saveMutation.isPending ? "Saving…" : "Save branding"}
            </Button>
          </div>

          <div className="flex min-w-0 flex-col space-y-2 xl:min-h-[420px]">
            <div>
              <h3 className="text-sm font-semibold text-foreground">Live Preview</h3>
              <p className="mt-0.5 text-[11px] text-muted-foreground">
                Complete customer portal scaled to fit. Insert Coin simulates preview-only payment
                UI and may play configured background music. No production session is created.
              </p>
            </div>
            <CaptivePortalLivePreview
              bannerSrc={livePreviewBannerSrc}
              isVideo={livePreviewIsVideo}
              musicSrc={previewMusicSrc}
            />
          </div>
        </div>
      </div>
    </div>
  );
}
