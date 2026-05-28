import type { PromoRate } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const promosApi = {
  list: () => api.get<PromoRate[]>(embeddedApi.promos),
  create: (promo: Omit<PromoRate, "id">) => api.post<{ id: number }>(embeddedApi.promos, promo),
  update: (promo: PromoRate) =>
    api.put<{ ok: boolean }>(`${embeddedApi.promos}/${promo.id}`, promo),
  delete: (id: number) => api.delete<{ ok: boolean }>(`${embeddedApi.promos}/${id}`),
};
