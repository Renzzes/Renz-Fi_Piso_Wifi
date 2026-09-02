import type { PortalSettings } from "@/services/portal";
import { resolvePortalAssetUrl } from "@/services/embeddedApi";

/** Saved server banner, or bundled Default-Banner.png when none is configured. */
export function resolveSavedPortalBannerUrl(settings: PortalSettings | undefined): string {
  const configured = Boolean(
    settings?.bannerConfigured ?? settings?.has_banner ?? settings?.hasCustomBanner,
  );
  if (configured && settings?.bannerUrl) {
    return resolvePortalAssetUrl(settings.bannerUrl);
  }
  return resolvePortalAssetUrl("/Default-Banner.png");
}

/** Live Preview source: local blob URL or saved custom banner; null → upload placeholder. */
export function resolveLivePreviewBannerSrc(
  localPreview: string | null,
  settings: PortalSettings | undefined,
): string | null {
  if (localPreview) return localPreview;
  const configured = Boolean(
    settings?.bannerConfigured ?? settings?.has_banner ?? settings?.hasCustomBanner,
  );
  if (configured && settings?.bannerUrl) {
    return resolvePortalAssetUrl(settings.bannerUrl);
  }
  return null;
}

/** Preview-only background music (settings custom or bundled default). */
export function resolvePreviewMusicUrl(settings: PortalSettings | undefined): string {
  const configured = Boolean(
    settings?.musicConfigured ?? settings?.has_music ?? settings?.hasCustomMusic,
  );
  if (configured && settings?.musicUrl) {
    return resolvePortalAssetUrl(settings.musicUrl);
  }
  return resolvePortalAssetUrl("/bg_music.mp3");
}

export function isLivePreviewBannerVideo(
  settings: PortalSettings | undefined,
  bannerBlob: Blob | null,
  bannerName: string,
): boolean {
  if (bannerBlob?.type === "video/mp4") return true;
  if (/\.mp4$/i.test(bannerName)) return true;
  if (!bannerBlob && settings?.bannerIsVideo) return true;
  return false;
}
