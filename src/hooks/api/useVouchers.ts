import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { vouchersApi } from "@/services/vouchers";

export function useVouchers() {
  return useQuery({
    queryKey: ["vouchers"],
    queryFn: () => vouchersApi.list(),
  });
}

export function useGenerateVouchers() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: vouchersApi.generate,
    onSuccess: () => qc.invalidateQueries({ queryKey: ["vouchers"] }),
  });
}
