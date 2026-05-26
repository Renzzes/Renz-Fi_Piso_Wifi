import { createFileRoute } from "@tanstack/react-router";
import { Download } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { StatCard } from "@/components/StatCard";

export const Route = createFileRoute("/_layout/sales-reports")({
  component: SalesPage,
});

const daily = [12, 28, 18, 42, 35, 56, 48];
const weekly = [180, 220, 195, 260];
const monthly = [1820, 2100, 1980, 2400, 2680, 2950];

function MiniBars({ data, labels }: { data: number[]; labels: string[] }) {
  const max = Math.max(...data);
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

function SalesPage() {
  const total = daily.reduce((a, b) => a + b, 0);
  return (
    <div>
      <PageHeader
        title="Sales Reports"
        description="Revenue overview"
        actions={
          <Button size="sm" variant="outline">
            <Download className="h-4 w-4" /> Export CSV
          </Button>
        }
      />

      <div className="grid grid-cols-3 gap-2 mb-3">
        <StatCard label="Today" value={`₱${daily[daily.length - 1]}`} />
        <StatCard label="This Week" value={`₱${total}`} />
        <StatCard label="Avg / Day" value={`₱${Math.round(total / 7)}`} />
      </div>

      <Tabs defaultValue="daily" className="rounded-md border bg-card p-3">
        <TabsList>
          <TabsTrigger value="daily">Daily</TabsTrigger>
          <TabsTrigger value="weekly">Weekly</TabsTrigger>
          <TabsTrigger value="monthly">Monthly</TabsTrigger>
        </TabsList>
        <TabsContent value="daily">
          <MiniBars data={daily} labels={["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]} />
        </TabsContent>
        <TabsContent value="weekly">
          <MiniBars data={weekly} labels={["W1", "W2", "W3", "W4"]} />
        </TabsContent>
        <TabsContent value="monthly">
          <MiniBars data={monthly} labels={["Jan", "Feb", "Mar", "Apr", "May", "Jun"]} />
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
            {[
              { d: "2026-05-26", s: 32, c: 248, r: 248 },
              { d: "2026-05-25", s: 28, c: 195, r: 195 },
              { d: "2026-05-24", s: 41, c: 312, r: 312 },
              { d: "2026-05-23", s: 19, c: 110, r: 110 },
            ].map((r) => (
              <TableRow key={r.d}>
                <TableCell className="text-xs">{r.d}</TableCell>
                <TableCell>{r.s}</TableCell>
                <TableCell>{r.c}</TableCell>
                <TableCell className="text-right tabular-nums">₱{r.r}</TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </div>
    </div>
  );
}
