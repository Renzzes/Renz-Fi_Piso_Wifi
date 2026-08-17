import { useMemo, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Download } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
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
import { StatCard } from "@/components/StatCard";
import { salesApi } from "@/services/sales";
import { formatPeso } from "@/lib/currency";
import { toast } from "sonner";
import type { ChartData } from "@/types/api";

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
      <div className="flex items-center justify-center h-32 text-sm text-muted-foreground">
        Loading...
      </div>
    );
  }

  if (!data.length) {
    return (
      <div className="flex items-center justify-center h-32 text-sm text-muted-foreground">
        No sales data
      </div>
    );
  }

  const max = Math.max(...data, 1);
  return (
    <div className="flex items-end gap-1 h-32 px-1">
      {data.map((v, i) => (
        <div key={i} className="flex-1 flex flex-col items-center gap-1">
          <div
            className="w-full bg-primary rounded-sm transition-all"
            style={{ height: `${(v / max) * 100}%`, minHeight: 2 }}
            title={`${labels[i]}: ${formatPeso(v)}`}
          />
          <span className="text-[10px] text-muted-foreground">{labels[i]}</span>
        </div>
      ))}
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
  // amount already includes attributed undated when clockReady; when not ready,
  // surface undated so the UI never shows ₱0.00 while COIN records exist.
  const shown = summary?.clockReady === false ? dated + undated : dated;
  return formatPeso(shown);
}

export default function SalesReportsPage() {
  const queryClient = useQueryClient();
  const { data: today, isLoading: todayLoading } = useQuery({
    queryKey: ["sales", "today"],
    queryFn: () => salesApi.today(),
  });
  const { data: weekly, isLoading: weeklyLoading } = useQuery({
    queryKey: ["sales", "weekly"],
    queryFn: () => salesApi.weekly(),
  });
  const { data: dailyChart, isLoading: dailyChartLoading } = useQuery({
    queryKey: ["sales", "chart", "daily"],
    queryFn: () => salesApi.chartDaily(),
  });
  const { data: weeklyChart, isLoading: weeklyChartLoading } = useQuery({
    queryKey: ["sales", "chart", "weekly"],
    queryFn: () => salesApi.chartWeekly(),
  });
  const { data: monthlyChart, isLoading: monthlyChartLoading } = useQuery({
    queryKey: ["sales", "chart", "monthly"],
    queryFn: () => salesApi.chartMonthly(),
  });
  const { data: history = [], isLoading: historyLoading } = useQuery({
    queryKey: ["sales", "history"],
    queryFn: () => salesApi.history(),
  });
  const { data: records = [], isLoading: recordsLoading } = useQuery({
    queryKey: ["sales", "records"],
    queryFn: () => salesApi.records(),
  });

  const exportMutation = useMutation({
    mutationFn: () => salesApi.exportCsv(),
    onSuccess: () => toast.success("Sales report downloaded"),
    onError: (error: Error) => toast.error(error.message || "Export failed"),
  });

  const resetMutation = useMutation({
    mutationFn: () => salesApi.reset(),
    onSuccess: async () => {
      toast.success("Sales reset. A new audit cycle has started.");
      await queryClient.invalidateQueries({ queryKey: ["sales"] });
    },
    onError: (error: Error) => toast.error(error.message || "Reset failed"),
  });

  const daily = hasChartData(dailyChart) ? dailyChart.data : [];
  const dailyLabels = hasChartData(dailyChart) ? dailyChart.labels : [];
  const dailyTotal = daily.reduce((a, b) => a + b, 0);
  const avgPerDay = daily.length > 0 ? Math.round(dailyTotal / daily.length) : undefined;

  return (
    <div>
      <PageHeader
        title="Sales Reports"
        description="Revenue overview"
        actions={
          <div className="flex gap-2 flex-wrap">
            <Button
              size="sm"
              variant="destructive"
              disabled={resetMutation.isPending}
              onClick={() => {
                if (
                  !window.confirm(
                    "Reset all sales records? Use this after you have audited physical coins and want to start a new cycle. This cannot be undone.",
                  )
                ) {
                  return;
                }
                if (
                  !window.confirm(
                    "Confirm sales reset. Charts and history will clear on this appliance.",
                  )
                ) {
                  return;
                }
                resetMutation.mutate();
              }}
            >
              {resetMutation.isPending ? "Resetting…" : "Reset Sales"}
            </Button>
            <Button
              size="sm"
              variant="outline"
              disabled={exportMutation.isPending}
              onClick={() => exportMutation.mutate()}
            >
              <Download className="h-4 w-4" /> Export CSV
            </Button>
          </div>
        }
      />

      <div className="grid grid-cols-3 gap-2 mb-3">
        <StatCard label="Today" value={displaySalesAmount(today, todayLoading)} />
        <StatCard label="This Week" value={displaySalesAmount(weekly, weeklyLoading)} />
        <StatCard label="Avg / Day" value={formatPeso(avgPerDay, dailyChartLoading)} />
      </div>
      {((today?.undatedAmount || 0) > 0 || (weekly?.undatedAmount || 0) > 0) && (
        <p className="mb-3 text-xs text-muted-foreground">
          Includes offline/undated COIN sales
          {today?.clockReady === false
            ? " (device clock not ready — totals use unclocked transactions)."
            : " attributed to the current local business day once the clock is ready."}
        </p>
      )}

      <Tabs defaultValue="daily" className="rounded-md border bg-card p-3">
        <TabsList>
          <TabsTrigger value="daily">Daily</TabsTrigger>
          <TabsTrigger value="weekly">Weekly</TabsTrigger>
          <TabsTrigger value="monthly">Monthly</TabsTrigger>
        </TabsList>
        <TabsContent value="daily">
          <MiniBars data={daily} labels={dailyLabels} loading={dailyChartLoading} />
        </TabsContent>
        <TabsContent value="weekly">
          <MiniBars
            data={hasChartData(weeklyChart) ? weeklyChart.data : []}
            labels={hasChartData(weeklyChart) ? weeklyChart.labels : []}
            loading={weeklyChartLoading}
          />
        </TabsContent>
        <TabsContent value="monthly">
          <MiniBars
            data={hasChartData(monthlyChart) ? monthlyChart.data : []}
            labels={hasChartData(monthlyChart) ? monthlyChart.labels : []}
            loading={monthlyChartLoading}
          />
        </TabsContent>
      </Tabs>

      <div className="rounded-md border bg-card mt-3 overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Date</TableHead>
              <TableHead>Sessions</TableHead>
              <TableHead>Transactions</TableHead>
              <TableHead className="text-right">Revenue</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {historyLoading ? (
              <TableRow>
                <TableCell colSpan={4} className="text-center text-muted-foreground py-6 text-sm">
                  Loading...
                </TableCell>
              </TableRow>
            ) : (
              history.map((r) => (
                <TableRow key={r.date}>
                  <TableCell className="text-xs">{r.date}</TableCell>
                  <TableCell>{r.sessions}</TableCell>
                  <TableCell>{r.sessions}</TableCell>
                  <TableCell className="text-right tabular-nums">{formatPeso(r.revenue)}</TableCell>
                </TableRow>
              ))
            )}
            {!historyLoading && history.length === 0 && (
              <TableRow>
                <TableCell colSpan={4} className="text-center text-muted-foreground py-6 text-sm">
                  No sales history
                </TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
      </div>

      <div className="rounded-md border bg-card mt-3 overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Date / Time</TableHead>
              <TableHead>Transaction</TableHead>
              <TableHead>Minutes</TableHead>
              <TableHead>Status</TableHead>
              <TableHead>Payment</TableHead>
              <TableHead className="text-right">Amount</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {recordsLoading ? (
              <TableRow>
                <TableCell colSpan={6} className="text-center text-muted-foreground py-6 text-sm">
                  Loading transactions...
                </TableCell>
              </TableRow>
            ) : (
              records.map((record) => (
                <TableRow key={record.id}>
                  <TableCell className="text-xs">
                    {record.recordedAt || record.recorded_at || "—"}
                  </TableCell>
                  <TableCell className="text-xs">
                    {record.id || record.sessionId || "—"}
                  </TableCell>
                  <TableCell>{record.durationMinutes ?? 0}</TableCell>
                  <TableCell className="text-xs">
                    {record.status || "completed"}
                    {record.terminationReason ? (
                      <div className="text-muted-foreground">{record.terminationReason}</div>
                    ) : null}
                  </TableCell>
                  <TableCell className="uppercase text-xs">
                    {record.paymentType || "—"}
                  </TableCell>
                  <TableCell className="text-right tabular-nums">
                    {formatPeso(record.amount)}
                  </TableCell>
                </TableRow>
              ))
            )}
            {!recordsLoading && records.length === 0 && (
              <TableRow>
                <TableCell colSpan={6} className="text-center text-muted-foreground py-6 text-sm">
                  No transactions
                </TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
      </div>
    </div>
  );
}
