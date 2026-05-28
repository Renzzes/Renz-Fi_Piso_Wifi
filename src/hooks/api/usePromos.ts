import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import type { PromoRate } from "@/types/api";
import { promosApi } from "@/services/promos";

export function usePromos() {
  return useQuery({
    queryKey: ["promos"],
    queryFn: () => promosApi.list(),
  });
}

export function useSavePromo() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (promo: PromoRate) =>
      promo.id ? promosApi.update(promo) : promosApi.create(promo),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["promos"] }),
  });
}

export function useDeletePromo() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (id: number) => promosApi.delete(id),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["promos"] }),
  });
}
