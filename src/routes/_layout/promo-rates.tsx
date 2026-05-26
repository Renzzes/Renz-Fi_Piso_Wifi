import { useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { Plus, Pencil, Trash2 } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";

export const Route = createFileRoute("/_layout/promo-rates")({
  component: PromoPage,
});

type Promo = {
  id: number;
  name: string;
  coin: number;
  minutes: number;
  speed?: number;
  devices?: number;
};

const initial: Promo[] = [
  { id: 1, name: "1 Peso", coin: 1, minutes: 15, speed: 5, devices: 1 },
  { id: 2, name: "5 Pesos", coin: 5, minutes: 90, speed: 10, devices: 1 },
  { id: 3, name: "10 Pesos", coin: 10, minutes: 240, speed: 10, devices: 2 },
  { id: 4, name: "20 Pesos", coin: 20, minutes: 600, speed: 15, devices: 2 },
];

function PromoPage() {
  const [promos, setPromos] = useState<Promo[]>(initial);
  const [open, setOpen] = useState(false);
  const [editing, setEditing] = useState<Promo | null>(null);

  const remove = (id: number) => setPromos((p) => p.filter((x) => x.id !== id));

  const save = (data: Promo) => {
    setPromos((p) =>
      editing ? p.map((x) => (x.id === data.id ? data : x)) : [...p, { ...data, id: Date.now() }],
    );
    setOpen(false);
    setEditing(null);
  };

  return (
    <div>
      <PageHeader
        title="Promo Rates"
        description="Configure coin value and internet time"
        actions={
          <Dialog open={open} onOpenChange={(o) => { setOpen(o); if (!o) setEditing(null); }}>
            <DialogTrigger asChild>
              <Button size="sm"><Plus className="h-4 w-4" /> Add Promo</Button>
            </DialogTrigger>
            <PromoDialog initial={editing} onSave={save} />
          </Dialog>
        }
      />
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-3">
        {promos.map((p) => (
          <div key={p.id} className="rounded-md border bg-card p-3">
            <div className="flex items-start justify-between">
              <div>
                <div className="text-xs text-muted-foreground">{p.name}</div>
                <div className="text-2xl font-semibold">₱{p.coin}</div>
              </div>
              <div className="flex gap-1">
                <Button size="icon" variant="ghost" className="h-7 w-7"
                  onClick={() => { setEditing(p); setOpen(true); }}>
                  <Pencil className="h-3.5 w-3.5" />
                </Button>
                <Button size="icon" variant="ghost" className="h-7 w-7" onClick={() => remove(p.id)}>
                  <Trash2 className="h-3.5 w-3.5" />
                </Button>
              </div>
            </div>
            <div className="grid grid-cols-3 gap-2 mt-3 text-xs">
              <Info label="Time" value={`${p.minutes}m`} />
              <Info label="Speed" value={p.speed ? `${p.speed}M` : "—"} />
              <Info label="Devices" value={p.devices ?? "—"} />
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

function Info({ label, value }: { label: string; value: string | number }) {
  return (
    <div className="rounded-sm bg-muted/50 px-2 py-1.5">
      <div className="text-[10px] text-muted-foreground uppercase tracking-wide">{label}</div>
      <div className="font-medium tabular-nums">{value}</div>
    </div>
  );
}

function PromoDialog({ initial, onSave }: { initial: Promo | null; onSave: (p: Promo) => void }) {
  const [form, setForm] = useState<Promo>(
    initial ?? { id: 0, name: "", coin: 1, minutes: 15, speed: undefined, devices: 1 },
  );
  return (
    <DialogContent>
      <DialogHeader>
        <DialogTitle>{initial ? "Edit Promo" : "Add Promo"}</DialogTitle>
      </DialogHeader>
      <div className="grid grid-cols-2 gap-3">
        <Field label="Name">
          <Input value={form.name} onChange={(e) => setForm({ ...form, name: e.target.value })} />
        </Field>
        <Field label="Coin (₱)">
          <Input type="number" value={form.coin}
            onChange={(e) => setForm({ ...form, coin: +e.target.value })} />
        </Field>
        <Field label="Minutes">
          <Input type="number" value={form.minutes}
            onChange={(e) => setForm({ ...form, minutes: +e.target.value })} />
        </Field>
        <Field label="Speed (Mbps)">
          <Input type="number" value={form.speed ?? ""}
            onChange={(e) => setForm({ ...form, speed: e.target.value ? +e.target.value : undefined })} />
        </Field>
        <Field label="Device Limit">
          <Input type="number" value={form.devices ?? ""}
            onChange={(e) => setForm({ ...form, devices: e.target.value ? +e.target.value : undefined })} />
        </Field>
      </div>
      <DialogFooter>
        <Button onClick={() => onSave(form)}>Save</Button>
      </DialogFooter>
    </DialogContent>
  );
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="space-y-1">
      <Label className="text-xs">{label}</Label>
      {children}
    </div>
  );
}
