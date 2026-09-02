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
  bannerIsVideo?: boolean;
  bannerMime?: string;
  banner_path?: string;
  music_path?: string;
};

export const portalApi = {
  settings: () => apiFetch<PortalSettings>(portalSettingsPath),
  uploadBanner: (file: Blob | File) => {
    const form = new FormData();
    const name =
      file instanceof File && file.name
        ? file.name
        : file.type.includes("png")
          ? "banner.png"
          : file.type.includes("jpeg") || file.type.includes("jpg")
            ? "banner.jpg"
            : "banner.png";
    form.append("file", file, name);
    return apiFetch<PortalSettings>(`${portalSettingsPath}/banner`, {
      method: "POST",
      body: form,
    });
  },
  uploadMusic: (file: File | Blob) => {
    const form = new FormData();
    const name = file instanceof File && file.name ? file.name : "bg_music.mp3";
    form.append("file", file, name);
    return apiFetch<PortalSettings>(`${portalSettingsPath}/music`, {
      method: "POST",
      body: form,
    });
  },
  deleteBanner: () => apiFetch<{ ok: boolean }>(`${portalSettingsPath}/banner`, { method: "DELETE" }),
  deleteMusic: () => apiFetch<{ ok: boolean }>(`${portalSettingsPath}/music`, { method: "DELETE" }),
};
