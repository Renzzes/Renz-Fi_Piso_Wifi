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
    onSuccess: (result) => {
      void qc.invalidateQueries({ queryKey: ["vouchers"] });
      if (import.meta.env.DEV) {
        const n = result.count ?? result.created?.length ?? 0;
        console.debug(`[voucher-ui] refreshed vouchers count=${n}`);
      }
    },
  });
}

export function useVoucherAction() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: ({
      code,
      action,
    }: {
      code: string;
      action: "terminate" | "expire" | "disable" | "archive";
    }) => vouchersApi[action](code),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["vouchers"] }),
  });
}

export function useDeleteVoucher() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (code: string) => vouchersApi.delete(code),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["vouchers"] }),
  });
}

export function useBulkDeleteVouchers() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (codes: string[]) => vouchersApi.bulkDelete(codes),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["vouchers"] }),
  });
}
