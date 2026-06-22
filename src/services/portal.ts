import { apiFetch } from "./api";
import { embeddedApi } from "./embeddedApi";

const portalSettingsPath = `${embeddedApi.settings}/portal`;

export type PortalSettings = {
  revision?: number;
  has_banner?: boolean;
  has_music?: boolean;
  hasCustomBanner?: boolean;
  hasCustomMusic?: boolean;
  bannerConfigured?: boolean;
  musicConfigured?: boolean;
  bannerUrl?: string;
  musicUrl?: string;
  banner_path?: string;
  music_path?: string;
};

export const portalApi = {
  settings: () => apiFetch<PortalSettings>(portalSettingsPath),
  uploadBanner: (file: Blob) =>
    apiFetch<PortalSettings>(`${portalSettingsPath}/banner`, {
      method: "POST",
      body: file,
      headers: { "Content-Type": file.type || "image/webp" },
    }),
  uploadMusic: (file: File | Blob) =>
    apiFetch<PortalSettings>(`${portalSettingsPath}/music`, {
      method: "POST",
      body: file,
      headers: { "Content-Type": file.type || "audio/mpeg" },
    }),
  deleteBanner: () => apiFetch<{ ok: boolean }>(`${portalSettingsPath}/banner`, { method: "DELETE" }),
  deleteMusic: () => apiFetch<{ ok: boolean }>(`${portalSettingsPath}/music`, { method: "DELETE" }),
};
