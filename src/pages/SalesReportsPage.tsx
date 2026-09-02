import { useEffect, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { FileSpreadsheet } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { salesApi } from "@/services/sales";
import { formatPeso } from "@/lib/currency";
import { exportSalesExcel } from "@/lib/salesExport";
import { toast } from "sonner";
import type { ChartData } from "@/types/api";
import { AdminTableCard } from "@/components/admin/AdminTableCard";
import { DataPagination } from "@/components/admin/DataPagination";
import { EmptyState } from "@/components/admin/EmptyState";
import { Skeleton } from "@/components/ui/skeleton";
import { clampPage, PAGE_SIZE_DEFAULT, pageSlice } from "@/lib/pagination";

function hasChartData(chart: ChartData | undefined): chart is ChartData {
  return Boolean(chart?.data?.length);
}

function MiniBars({
  data,
  labels,
  loading,
}: {
  data: number[];
  labels: string[];
  loading: boolean;
}) {
  if (loading) {
    return (
      <div className="flex h-[180px] items-center justify-center text-sm text-muted-foreground">
        Loading...
      </div>
    );
  }

  if (!data.length) {
    return (
      <div className="flex h-[180px] items-center justify-center text-sm text-muted-foreground">
        No sales data available
      </div>
    );
  }

  const max = Math.max(...data, 1);
  return (
    <div className="overflow-x-auto">
      <div className="flex h-[180px] min-w-[240px] items-end gap-1 px-1">
        {data.map((v, i) => (
          <div key={i} className="flex min-w-0 flex-1 flex-col items-center gap-1">
            <div
              className="w-full rounded-sm bg-primary transition-all"
              style={{ height: `${(v / max) * 100}%`, minHeight: 2 }}
              title={`${labels[i]}: ${formatPeso(v)}`}
            />
            <span className="max-w-full truncate text-[10px] text-muted-foreground">
              {labels[i]}
            </span>
          </div>
        ))}
      </div>
    </div>
  );
}

function displaySalesAmount(
  summary:
    | {
        amount?: number;
        undatedAmount?: number;
        clockReady?: boolean;
      }
    | undefined,
  loading: boolean,
) {
  if (loading) return formatPeso(undefined, true);
  const dated = Number(summary?.amount || 0);
  const undated = Number(summary?.undatedAmount || 0);
  const shown = summary?.clockReady === false ? dated + undated : dated;
  return formatPeso(shown);
}

function SummaryCard({ label, value }: { label: string; value: string }) {
  return (
    <div className="rounded-[14px] border bg-card p-4">
      <div className="text-[11px] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
        {label}
      </div>
      <div className="mt-1 text-2xl font-semibold tabular-nums">{value}</div>
    </div>
  );
}

export default function SalesReportsPage() {
  const queryClient = useQueryClient();
  const [loadPhase, setLoadPhase] = useState(0);
  const [resetStep, setResetStep] = useState<0 | 1 | 2>(0);
  const [txPage, setTxPage] = useState(1);
  const [txPageSize, setTxPageSize] = useState(PAGE_SIZE_DEFAULT);
  const [historyPage, setHistoryPage] = useState(1);
  const [historyPageSize, setHistoryPageSize] = useState(PAGE_SIZE_DEFAULT);

  useEffect(() => {
    const t1 = window.setTimeout(() => setLoadPhase(1), 150);
    const t2 = window.setTimeout(() => setLoadPhase(2), 450);
    const t3 = window.setTimeout(() => setLoadPhase(3), 900);
    return () => {
      window.clearTimeout(t1);
      window.clearTimeout(t2);
      window.clearTimeout(t3);
    };
  }, []);

  const { data: today, isLoading: todayLoading } = useQuery({
    queryKey: ["sales", "today"],
    queryFn: () => salesApi.today(),
    enabled: loadPhase >= 0,
  });
  const { data: weekly, isLoading: weeklyLoading } = useQuery({
    queryKey: ["sales", "weekly"],
    queryFn: () => salesApi.weekly(),
    enabled: loadPhase >= 1,
  });
  const { data: dailyChart, isLoading: dailyChartLoading } = useQuery({
    queryKey: ["sales", "chart", "daily"],
    queryFn: () => salesApi.chartDaily(),
    enabled: loadPhase >= 2,
  });
  const { data: weeklyChart, isLoading: weeklyChartLoading } = useQuery({
    queryKey: ["sales", "chart", "weekly"],
    queryFn: () => salesApi.chartWeekly(),
    enabled: loadPhase >= 2,
  });
  const { data: monthlyChart, isLoading: monthlyChartLoading } = useQuery({
    queryKey: ["sales", "chart", "monthly"],
    queryFn: () => salesApi.chartMonthly(),
    enabled: loadPhase >= 3,
  });
  const { data: history = [], isLoading: historyLoading } = useQuery({
    queryKey: ["sales", "history"],
    queryFn: () => salesApi.history(),
    enabled: loadPhase >= 3,
  });
  const { data: records = [], isLoading: recordsLoading } = useQuery({
    queryKey: ["sales", "records"],
    queryFn: () => salesApi.records(),
    enabled: loadPhase >= 3,
  });

  const exportExcelMutation = useMutation({
    mutationFn: async () => {
      const rows = records.length > 0 ? records : await salesApi.records(500);
      await exportSalesExcel(rows);
    },
    onSuccess: () => toast.success("Excel report downloaded"),
    onError: (error: Error) => toast.error(error.message || "Excel export failed"),
  });

  const resetMutation = useMutation({
    mutationFn: () => salesApi.reset(),
    onSuccess: async () => {
      toast.success("Sales reset. A new audit cycle has started.");
      await queryClient.invalidateQueries({ queryKey: ["sales"] });
      setResetStep(0);
    },
    onError: (error: Error) => toast.error(error.message || "Reset failed"),
  });

  const daily = hasChartData(dailyChart) ? dailyChart.data : [];
  const dailyLabels = hasChartData(dailyChart) ? dailyChart.labels : [];
  const dailyTotal = daily.reduce((a, b) => a + b, 0);
  const avgPerDay = daily.length > 0 ? Math.round(dailyTotal / daily.length) : undefined;

  const historySafePage = clampPage(historyPage, history.length, historyPageSize);
  const historyRows = pageSlice(history, historySafePage, historyPageSize);
  const txSafePage = clampPage(txPage, records.length, txPageSize);
  const txRows = pageSlice(records, txSafePage, txPageSize);

  useEffect(() => {
    setHistoryPage(1);
  }, [historyPageSize]);

  useEffect(() => {
    setTxPage(1);
  }, [txPageSize]);

  return (
    <div className="space-y-3">
      <div className="flex flex-col gap-3 lg:flex-row lg:items-start lg:justify-between">
        <div>
          <h2 className="text-2xl font-semibold leading-tight">Sales Reports</h2>
          <p className="mt-0.5 text-[13px] text-muted-foreground">Revenue overview</p>
        </div>
        <div className="flex flex-nowrap items-center gap-2.5 overflow-x-auto">
          <Button
            size="sm"
            variant="secondary"
            className="h-9 shrink-0 px-4"
            onClick={() => {
              void queryClient.invalidateQueries({ queryKey: ["sales"] });
              void queryClient.invalidateQueries({ queryKey: ["system", "status"] });
            }}
          >
            Reload Sales
          </Button>
          <Button
            size="sm"
            variant="outline"
            className="h-9 shrink-0 px-4 border-red-500/40 bg-red-500/10 text-red-600 hover:bg-red-500/20 dark:text-red-400"
            disabled={resetMutation.isPending}
            onClick={() => setResetStep(1)}
          >
            {resetMutation.isPending ? "Resetting…" : "Reset Sales"}
          </Button>
          <Button
            size="sm"
            variant="outline"
            className="h-9 shrink-0 px-4"
            disabled={exportExcelMutation.isPending || recordsLoading}
            onClick={() => exportExcelMutation.mutate()}
          >
            <FileSpreadsheet className="h-4 w-4" /> Export Excel
          </Button>
        </div>
      </div>

      <div className="grid grid-cols-1 gap-3 sm:grid-cols-3">
        <SummaryCard label="Today" value={displaySalesAmount(today, todayLoading)} />
        <SummaryCard label="This Week" value={displaySalesAmount(weekly, weeklyLoading)} />
        <SummaryCard label="Avg / Day" value={formatPeso(avgPerDay, dailyChartLoading)} />
      </div>
      {(today?.undatedAmount || 0) > 0 || (weekly?.undatedAmount || 0) > 0 ? (
        <p className="text-xs text-muted-foreground">
          Some COIN sales were recorded before the wall clock was ready and are listed as undated
          until a session timestamp can be resolved — they are not added to Today automatically.
        </p>
      ) : null}

      <div className="rounded-[14px] border bg-card p-3">
        <Tabs defaultValue="daily">
          <TabsList className="h-8 w-full max-w-full justify-start overflow-x-auto">
            <TabsTrigger value="daily" className="h-7 text-[12px]">
              Daily
            </TabsTrigger>
            <TabsTrigger value="weekly" className="h-7 text-[12px]">
              Weekly
            </TabsTrigger>
            <TabsTrigger value="monthly" className="h-7 text-[12px]">
              Monthly
            </TabsTrigger>
          </TabsList>
          <TabsContent value="daily" className="mt-3">
            <MiniBars data={daily} labels={dailyLabels} loading={dailyChartLoading} />
          </TabsContent>
          <TabsContent value="weekly" className="mt-3">
            <MiniBars
              data={hasChartData(weeklyChart) ? weeklyChart.data : []}
              labels={hasChartData(weeklyChart) ? weeklyChart.labels : []}
              loading={weeklyChartLoading}
            />
          </TabsContent>
          <TabsContent value="monthly" className="mt-3">
            <MiniBars
              data={hasChartData(monthlyChart) ? monthlyChart.data : []}
              labels={hasChartData(monthlyChart) ? monthlyChart.labels : []}
              loading={monthlyChartLoading}
            />
          </TabsContent>
        </Tabs>
      </div>

      <AdminTableCard
        footer={
          !historyLoading && history.length > 0 ? (
            <DataPagination
              page={historySafePage}
              pageSize={historyPageSize}
              total={history.length}
              onPageChange={setHistoryPage}
              onPageSizeChange={setHistoryPageSize}
              itemLabel="entries"
            />
          ) : null
        }
      >
        <Table>
          <TableHeader>
            <TableRow className="hover:bg-transparent">
              <TableHead className="h-10 bg-muted/40 text-[12px]">Date</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Sessions</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Transactions</TableHead>
              <TableHead className="h-10 bg-muted/40 text-right text-[12px]">Revenue</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {historyLoading
              ? Array.from({ length: 4 }).map((_, i) => (
                  <TableRow key={`hsk-${i}`} className="h-12">
                    {Array.from({ length: 4 }).map((__, cell) => (
                      <TableCell key={cell}>
                        <Skeleton className="h-3.5 w-full max-w-[6rem]" />
                      </TableCell>
                    ))}
                  </TableRow>
                ))
              : historyRows.map((r) => (
                  <TableRow key={r.date} className="h-12">
                    <TableCell className="text-[12px]">{r.date}</TableCell>
                    <TableCell className="text-[13px]">{r.sessions}</TableCell>
                    <TableCell className="text-[13px]">{r.sessions}</TableCell>
                    <TableCell className="text-right text-[13px] tabular-nums">
                      {formatPeso(r.revenue)}
                    </TableCell>
                  </TableRow>
                ))}
          </TableBody>
        </Table>
        {!historyLoading && history.length === 0 ? (
          <EmptyState title="No sales data available" />
        ) : null}
      </AdminTableCard>

      <AdminTableCard
        footer={
          !recordsLoading && records.length > 0 ? (
            <DataPagination
              page={txSafePage}
              pageSize={txPageSize}
              total={records.length}
              onPageChange={setTxPage}
              onPageSizeChange={setTxPageSize}
              itemLabel="transactions"
            />
          ) : null
        }
      >
        <Table>
          <TableHeader>
            <TableRow className="hover:bg-transparent">
              <TableHead className="h-10 bg-muted/40 text-[12px]">Date / Time</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Transaction</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Minutes</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Status</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Payment</TableHead>
              <TableHead className="h-10 bg-muted/40 text-right text-[12px]">Amount</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {recordsLoading
              ? Array.from({ length: 6 }).map((_, i) => (
                  <TableRow key={`tsk-${i}`} className="h-12">
                    {Array.from({ length: 6 }).map((__, cell) => (
                      <TableCell key={cell}>
                        <Skeleton className="h-3.5 w-full max-w-[7rem]" />
                      </TableCell>
                    ))}
                  </TableRow>
                ))
              : txRows.map((record) => (
                  <TableRow key={record.id} className="h-12">
                    <TableCell className="text-[12px]">
                      {record.recordedAt || record.recorded_at || "—"}
                    </TableCell>
                    <TableCell className="text-[12px]">
                      {record.id || record.sessionId || "—"}
                    </TableCell>
                    <TableCell className="text-[13px]">{record.durationMinutes ?? 0}</TableCell>
                    <TableCell className="text-[12px]">
                      {record.status || "completed"}
                      {record.terminationReason ? (
                        <div className="text-muted-foreground">{record.terminationReason}</div>
                      ) : null}
                    </TableCell>
                    <TableCell className="text-[12px] uppercase">
                      {record.paymentType || "—"}
                    </TableCell>
                    <TableCell className="text-right text-[13px] tabular-nums">
                      {formatPeso(record.amount)}
                    </TableCell>
                  </TableRow>
                ))}
          </TableBody>
        </Table>
        {!recordsLoading && records.length === 0 ? (
          <EmptyState title="No transactions found for this period." />
        ) : null}
      </AdminTableCard>

      <AlertDialog open={resetStep > 0} onOpenChange={(open) => !open && setResetStep(0)}>
        <AlertDialogContent>
          {resetStep === 1 ? (
            <>
              <AlertDialogHeader>
                <AlertDialogTitle>Reset Sales?</AlertDialogTitle>
                <AlertDialogDescription>
                  Reset all sales records? Use this after you have audited physical coins and want
                  to start a new cycle. This cannot be undone.
                </AlertDialogDescription>
              </AlertDialogHeader>
              <AlertDialogFooter>
                <AlertDialogCancel>Cancel</AlertDialogCancel>
                <Button type="button" onClick={() => setResetStep(2)}>
                  Continue
                </Button>
              </AlertDialogFooter>
            </>
          ) : (
            <>
              <AlertDialogHeader>
                <AlertDialogTitle>Confirm sales reset</AlertDialogTitle>
                <AlertDialogDescription>
                  Confirm sales reset. Charts and history will clear on this appliance.
                </AlertDialogDescription>
              </AlertDialogHeader>
              <AlertDialogFooter>
                <AlertDialogCancel>Cancel</AlertDialogCancel>
                <AlertDialogAction
                  className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
                  disabled={resetMutation.isPending}
                  onClick={() => resetMutation.mutate()}
                >
                  Reset Sales
                </AlertDialogAction>
              </AlertDialogFooter>
            </>
          )}
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}
