import { useQuery } from "@tanstack/react-query";
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
function MiniBars({ data, labels }: { data: number[]; labels: string[] }) {
  const max = Math.max(...data, 1);
  return (
    <div className="flex items-end gap-1 h-32 px-1">
      {data.map((v, i) => (
        <div key={i} className="flex-1 flex flex-col items-center gap-1">
          <div
            className="w-full bg-primary rounded-sm transition-all"
            style={{ height: `${(v / max) * 100}%`, minHeight: 2 }}
            title={`${labels[i]}: ₱${v}`}
          />
          <span className="text-[10px] text-muted-foreground">{labels[i]}</span>
        </div>
      ))}
    </div>
  );
}

export default function SalesReportsPage() {
  const { data: today } = useQuery({
    queryKey: ["sales", "today"],
    queryFn: () => salesApi.today(),
  });
  const { data: weekly } = useQuery({
    queryKey: ["sales", "weekly"],
    queryFn: () => salesApi.weekly(),
  });
  const { data: dailyChart } = useQuery({
    queryKey: ["sales", "chart", "daily"],
    queryFn: () => salesApi.chartDaily(),
  });
  const { data: weeklyChart } = useQuery({
    queryKey: ["sales", "chart", "weekly"],
    queryFn: () => salesApi.chartWeekly(),
  });
  const { data: monthlyChart } = useQuery({
    queryKey: ["sales", "chart", "monthly"],
    queryFn: () => salesApi.chartMonthly(),
  });
  const { data: history = [] } = useQuery({
    queryKey: ["sales", "history"],
    queryFn: () => salesApi.history(),
  });

  const daily = dailyChart?.data ?? [12, 28, 18, 42, 35, 56, 48];
  const dailyLabels = dailyChart?.labels ?? ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];
  const total = daily.reduce((a, b) => a + b, 0);

  return (
    <div>
      <PageHeader
        title="Sales Reports"
        description="Revenue overview"
        actions={
          <Button
            size="sm"
            variant="outline"
            onClick={() => window.open(salesApi.exportUrl(), "_blank")}
          >
            <Download className="h-4 w-4" /> Export CSV
          </Button>
        }
      />

      <div className="grid grid-cols-3 gap-2 mb-3">
        <StatCard label="Today" value={`₱${today?.amount ?? daily[daily.length - 1]}`} />
        <StatCard label="This Week" value={`₱${weekly?.amount ?? total}`} />
        <StatCard label="Avg / Day" value={`₱${Math.round(total / 7)}`} />
      </div>

      <Tabs defaultValue="daily" className="rounded-md border bg-card p-3">
        <TabsList>
          <TabsTrigger value="daily">Daily</TabsTrigger>
          <TabsTrigger value="weekly">Weekly</TabsTrigger>
          <TabsTrigger value="monthly">Monthly</TabsTrigger>
        </TabsList>
        <TabsContent value="daily">
          <MiniBars data={daily} labels={dailyLabels} />
        </TabsContent>
        <TabsContent value="weekly">
          <MiniBars
            data={weeklyChart?.data ?? [180, 220, 195, 260]}
            labels={weeklyChart?.labels ?? ["W1", "W2", "W3", "W4"]}
          />
        </TabsContent>
        <TabsContent value="monthly">
          <MiniBars
            data={monthlyChart?.data ?? [1820, 2100, 1980, 2400, 2680, 2950]}
            labels={monthlyChart?.labels ?? ["Jan", "Feb", "Mar", "Apr", "May", "Jun"]}
          />
        </TabsContent>
      </Tabs>

      <div className="rounded-md border bg-card mt-3 overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Date</TableHead>
              <TableHead>Sessions</TableHead>
              <TableHead>Coins</TableHead>
              <TableHead className="text-right">Revenue</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {history.map((r) => (
              <TableRow key={r.date}>
                <TableCell className="text-xs">{r.date}</TableCell>
                <TableCell>{r.sessions}</TableCell>
                <TableCell>{r.revenue}</TableCell>
                <TableCell className="text-right tabular-nums">₱{r.revenue}</TableCell>
              </TableRow>
            ))}
            {history.length === 0 && (
              <TableRow>
                <TableCell colSpan={4} className="text-center text-muted-foreground py-6 text-sm">
                  No sales history
                </TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
      </div>
    </div>
  );
}
