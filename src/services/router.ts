import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export type RouterConfig = {
  host: string;
  username: string;
  password: string;
  profile: string;
  ssid?: string;
};

export const routerApi = {
  settings: () => api.get<RouterConfig>(`${embeddedApi.router}/settings`),
  save: (config: Partial<RouterConfig>) =>
    api.put<{ ok: boolean }>(`${embeddedApi.router}/settings`, config),
  test: (config?: Partial<RouterConfig>) =>
    api.post<{ ok: boolean }>(`${embeddedApi.router}/test`, config),
};
