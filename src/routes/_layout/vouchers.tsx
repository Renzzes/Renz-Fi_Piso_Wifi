import { useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { Plus, Printer, Search } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import {
  Table, TableBody, TableCell, TableHead, TableHeader, TableRow,
} from "@/components/ui/table";
import { Badge } from "@/components/ui/badge";
import {
  Dialog, DialogContent, DialogFooter, DialogHeader, DialogTitle, DialogTrigger,
} from "@/components/ui/dialog";
import { Label } from "@/components/ui/label";
import {
  Select, SelectContent, SelectItem, SelectTrigger, SelectValue,
} from "@/components/ui/select";

export const Route = createFileRoute("/_layout/vouchers")({
  component: VouchersPage,
});

type Voucher = {
  code: string;
  amount: number;
  minutes: number;
  status: "unused" | "active" | "expired";
  expires: string;
};

const seed: Voucher[] = [
  { code: "R8K2-PMQX", amount: 10, minutes: 240, status: "unused", expires: "2026-06-01" },
  { code: "T4N9-LZBV", amount: 20, minutes: 600, status: "active", expires: "2026-05-28" },
  { code: "W1HC-X7DR", amount: 5, minutes: 90, status: "expired", expires: "2026-05-10" },
  { code: "Q3FE-2YML", amount: 5, minutes: 90, status: "unused", expires: "2026-06-15" },
];

function VouchersPage() {
  const [vouchers, setVouchers] = useState(seed);
  const [q, setQ] = useState("");
  const [filter, setFilter] = useState<string>("all");
  const [open, setOpen] = useState(false);

  const filtered = vouchers.filter(
    (v) =>
      (filter === "all" || v.status === filter) &&
      v.code.toLowerCase().includes(q.toLowerCase()),
  );

  const generate = (count: number, amount: number, minutes: number) => {
    const chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const rand = (n: number) =>
      Array.from({ length: n }, () => chars[Math.floor(Math.random() * chars.length)]).join("");
    const added: Voucher[] = Array.from({ length: count }, () => ({
      code: `${rand(4)}-${rand(4)}`,
      amount,
      minutes,
      status: "unused",
      expires: "2026-12-31",
    }));
    setVouchers((prev) => [...added, ...prev]);
    setOpen(false);
  };

  return (
    <div>
      <PageHeader
        title="Vouchers"
        description="Generate and manage WiFi vouchers"
        actions={
          <Dialog open={open} onOpenChange={setOpen}>
            <DialogTrigger asChild>
              <Button size="sm"><Plus className="h-4 w-4" /> Generate</Button>
            </DialogTrigger>
            <GenerateDialog onGenerate={generate} />
          </Dialog>
        }
      />

      <div className="flex flex-wrap gap-2 mb-3">
        <div className="relative flex-1 min-w-[200px]">
          <Search className="absolute left-2 top-1/2 -translate-y-1/2 h-4 w-4 text-muted-foreground" />
          <Input className="pl-8 h-8" placeholder="Search code" value={q}
            onChange={(e) => setQ(e.target.value)} />
        </div>
        <Select value={filter} onValueChange={setFilter}>
          <SelectTrigger className="w-[140px] h-8"><SelectValue /></SelectTrigger>
          <SelectContent>
            <SelectItem value="all">All</SelectItem>
            <SelectItem value="unused">Unused</SelectItem>
            <SelectItem value="active">Active</SelectItem>
            <SelectItem value="expired">Expired</SelectItem>
          </SelectContent>
        </Select>
      </div>

      <div className="rounded-md border bg-card overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Code</TableHead>
              <TableHead>Amount</TableHead>
              <TableHead>Time</TableHead>
              <TableHead>Status</TableHead>
              <TableHead>Expires</TableHead>
              <TableHead className="text-right">Action</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {filtered.map((v) => (
              <TableRow key={v.code}>
                <TableCell className="font-mono text-xs">{v.code}</TableCell>
                <TableCell>₱{v.amount}</TableCell>
                <TableCell>{v.minutes}m</TableCell>
                <TableCell>
                  <Badge
                    variant={v.status === "active" ? "default" : v.status === "expired" ? "destructive" : "secondary"}
                  >
                    {v.status}
                  </Badge>
                </TableCell>
                <TableCell className="text-xs text-muted-foreground">{v.expires}</TableCell>
                <TableCell className="text-right">
                  <Button size="sm" variant="ghost" className="h-7" onClick={() => window.print()}>
                    <Printer className="h-3.5 w-3.5" /> Print
                  </Button>
                </TableCell>
              </TableRow>
            ))}
            {filtered.length === 0 && (
              <TableRow>
                <TableCell colSpan={6} className="text-center text-muted-foreground py-6 text-sm">
                  No vouchers found
                </TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
      </div>
    </div>
  );
}

function GenerateDialog({ onGenerate }: { onGenerate: (c: number, a: number, m: number) => void }) {
  const [count, setCount] = useState(10);
  const [amount, setAmount] = useState(5);
  const [minutes, setMinutes] = useState(90);
  return (
    <DialogContent>
      <DialogHeader><DialogTitle>Generate Vouchers</DialogTitle></DialogHeader>
      <div className="grid grid-cols-3 gap-3">
        <div className="space-y-1">
          <Label className="text-xs">Count</Label>
          <Input type="number" value={count} onChange={(e) => setCount(+e.target.value)} />
        </div>
        <div className="space-y-1">
          <Label className="text-xs">Amount (₱)</Label>
          <Input type="number" value={amount} onChange={(e) => setAmount(+e.target.value)} />
        </div>
        <div className="space-y-1">
          <Label className="text-xs">Minutes</Label>
          <Input type="number" value={minutes} onChange={(e) => setMinutes(+e.target.value)} />
        </div>
      </div>
      <DialogFooter>
        <Button onClick={() => onGenerate(count, amount, minutes)}>Generate</Button>
      </DialogFooter>
    </DialogContent>
  );
}
