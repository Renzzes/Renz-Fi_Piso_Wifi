import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const syncApi = {
  status: () => api.get<{ pending: number; lastBatch: unknown }>(`${embeddedApi.system}/sync`),
};
