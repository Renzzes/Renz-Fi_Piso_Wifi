import type { Voucher } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const vouchersApi = {
  list: () => api.get<Voucher[]>(embeddedApi.vouchers),
  generate: (payload: { count: number; amount: number; minutes: number; expires?: string }) =>
    api.post<{ created: string[] }>(embeddedApi.vouchers, payload),
  delete: (code: string) => api.delete<{ ok: boolean }>(`${embeddedApi.vouchers}/${code}`),
  print: (code: string) => api.get<Voucher>(`${embeddedApi.vouchers}/${code}`),
};
