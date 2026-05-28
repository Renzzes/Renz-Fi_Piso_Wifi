import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

const portalSettingsPath = `${embeddedApi.settings}/portal`;

export const portalApi = {
  settings: () => api.get<Record<string, string>>(portalSettingsPath),
  save: (settings: Record<string, string>) =>
    api.put<{ ok: boolean }>(portalSettingsPath, settings),
  preview: () =>
    api.get<{
      portalName: string;
      welcomeMessage: string;
      announcement: string;
      primaryColor: string;
      banner: string;
    }>(`${portalSettingsPath}/preview`),
};
