import { useEffect, useMemo, useRef, useState } from "react";
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
import { NumericInput } from "@/components/NumericInput";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import type { PromoRate } from "@/types/api";
import { usePromos, useSavePromo, useDeletePromo } from "@/hooks/api/usePromos";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { routerApi } from "@/services/router";
import { refreshProductionRouterViews } from "@/lib/refreshProductionRouterViews";
import { toast } from "sonner";

const CUSTOM_SPEED_VALUE = "__custom__";

function formatRateLimit(rateLimit?: string): string {
  const trimmed = rateLimit?.trim() ?? "";
  return trimmed.length > 0 ? trimmed : "Not set yet";
}

function promoSpeedLabel(p: PromoRate): string {
  if (p.speedMode === "custom" && p.customDownloadMbps && p.customUploadMbps) {
    return `${p.customDownloadMbps}/${p.customUploadMbps} Mbps`;
  }
  if (p.profileName) return p.profileName;
  if (p.managedProfileName) return p.managedProfileName;
  if (p.speed) return `${p.speed}M`;
  return "—";
}

export default function PromoRatesPage() {
  const queryClient = useQueryClient();
  const { data: promos = [] } = usePromos();
  const savePromo = useSavePromo();
  const deletePromo = useDeletePromo();
  const [open, setOpen] = useState(false);
  const [editing, setEditing] = useState<PromoRate | null>(null);
  const profileRefreshAttempted = useRef(false);

  const { data: profilesData, isLoading: profilesLoading } = useQuery({
    queryKey: ["router", "profiles"],
    queryFn: () => routerApi.profiles(),
    staleTime: Number.POSITIVE_INFINITY,
    refetchOnMount: false,
  });

  const profileOptions = useMemo(
    () => (Array.isArray(profilesData?.profiles) ? profilesData.profiles : []),
    [profilesData?.profiles],
  );

  const profileDetails = useMemo(() => {
    if (Array.isArray(profilesData?.profileDetails) && profilesData.profileDetails.length > 0) {
      return profilesData.profileDetails;
    }
    return profileOptions.map((name) => ({ name, rateLimit: "" }));
  }, [profilesData?.profileDetails, profileOptions]);

  // Cache miss once: one controlled worker refresh (0 if cache already has profiles).
  useEffect(() => {
    if (profilesLoading || profileRefreshAttempted.current) return;
    if (profileOptions.length > 0) return;
    if (profilesData?.error && !String(profilesData.error).includes("unavailable")) return;
    profileRefreshAttempted.current = true;
    void (async () => {
      try {
        await routerApi.refreshProfiles();
        await refreshProductionRouterViews(queryClient);
      } catch {
        // Credentials missing or worker busy — UI stays empty without storms.
      }
    })();
  }, [profilesLoading, profileOptions.length, profilesData?.error, queryClient]);

  const remove = (id: number) => deletePromo.mutate(id);

  const save = async (data: PromoRate) => {
    try {
      let payload = { ...data };
      if (payload.speedMode === "custom") {
        const down = payload.customDownloadMbps ?? 0;
        const up = payload.customUploadMbps ?? 0;
        if (down < 1 || up < 1) {
          toast.error("Enter download and upload speed in Mbps");
          return;
        }
        const ensured = (await routerApi.profileOp({
          action: "ensure-managed",
          downloadMbps: down,
          uploadMbps: up,
        })) as {
          name?: string;
          rateLimit?: string;
          ok?: boolean;
        };
        if (!ensured?.name) {
          toast.error("Failed to ensure managed MikroTik profile");
          return;
        }
        payload = {
          ...payload,
          managedProfileName: ensured.name,
          profileName: undefined,
          speed: down,
        };
        await refreshProductionRouterViews(queryClient);
      } else if (payload.speedMode === "profile" && payload.profileName) {
        payload = {
          ...payload,
          managedProfileName: undefined,
          customDownloadMbps: undefined,
          customUploadMbps: undefined,
        };
      }

      savePromo.mutate(payload, {
        onSuccess: () => {
          setOpen(false);
          setEditing(null);
        },
      });
    } catch (err) {
      toast.error(err instanceof Error ? err.message : "Failed to save promo speed");
    }
  };

  return (
    <div>
      <PageHeader
        title="Promo Rates"
        description="Configure coin value, internet time, and MikroTik speed profile"
        actions={
          <Dialog
            open={open}
            onOpenChange={(o) => {
              setOpen(o);
              if (!o) setEditing(null);
            }}
          >
            <DialogTrigger asChild>
              <Button size="sm">
                <Plus className="h-4 w-4" /> Add Promo
              </Button>
            </DialogTrigger>
            <PromoDialog
              initial={editing}
              onSave={(p) => void save(p)}
              profileDetails={profileDetails}
              profilesLoading={profilesLoading}
            />
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
                <Button
                  size="icon"
                  variant="ghost"
                  className="h-7 w-7"
                  onClick={() => {
                    setEditing(p);
                    setOpen(true);
                  }}
                >
                  <Pencil className="h-3.5 w-3.5" />
                </Button>
                <Button
                  size="icon"
                  variant="ghost"
                  className="h-7 w-7"
                  onClick={() => remove(p.id)}
                >
                  <Trash2 className="h-3.5 w-3.5" />
                </Button>
              </div>
            </div>
            <div className="grid grid-cols-3 gap-2 mt-3 text-xs">
              <Info label="Time" value={`${p.minutes}m`} />
              <Info label="Speed" value={promoSpeedLabel(p)} />
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

function PromoDialog({
  initial,
  onSave,
  profileDetails,
  profilesLoading,
}: {
  initial: PromoRate | null;
  onSave: (p: PromoRate) => void;
  profileDetails: Array<{ name: string; rateLimit?: string }>;
  profilesLoading: boolean;
}) {
  const [form, setForm] = useState<PromoRate>(
    initial ?? {
      id: 0,
      name: "",
      coin: 1,
      minutes: 15,
      speed: undefined,
      devices: 1,
      speedMode: "profile",
    },
  );

  useEffect(() => {
    setForm(
      initial ?? {
        id: 0,
        name: "",
        coin: 1,
        minutes: 15,
        speed: undefined,
        devices: 1,
        speedMode: "profile",
      },
    );
  }, [initial]);

  const speedSelectValue =
    form.speedMode === "custom"
      ? CUSTOM_SPEED_VALUE
      : form.profileName ||
        (profileDetails[0]?.name ?? "");

  return (
    <DialogContent className="max-h-[90vh] overflow-y-auto">
      <DialogHeader>
        <DialogTitle>{initial ? "Edit Promo" : "Add Promo"}</DialogTitle>
      </DialogHeader>
      <div className="grid grid-cols-2 gap-3">
        <Field label="Name">
          <Input value={form.name} onChange={(e) => setForm({ ...form, name: e.target.value })} />
        </Field>
        <Field label="Coin (₱)">
          <NumericInput
            value={form.coin}
            min={0}
            onValueChange={(n) => setForm({ ...form, coin: n ?? 0 })}
          />
        </Field>
        <Field label="Minutes">
          <NumericInput
            value={form.minutes}
            min={0}
            onValueChange={(n) => setForm({ ...form, minutes: n ?? 0 })}
          />
        </Field>
        <Field label="Device Limit">
          <NumericInput
            value={form.devices}
            min={1}
            onValueChange={(n) => setForm({ ...form, devices: n })}
          />
        </Field>
        <div className="col-span-2 space-y-1">
          <Label className="text-xs">Speed</Label>
          <Select
            value={speedSelectValue}
            disabled={profilesLoading}
            onValueChange={(value) => {
              if (value === CUSTOM_SPEED_VALUE) {
                setForm({
                  ...form,
                  speedMode: "custom",
                  profileName: undefined,
                  customDownloadMbps: form.customDownloadMbps ?? 10,
                  customUploadMbps: form.customUploadMbps ?? 5,
                });
                return;
              }
              setForm({
                ...form,
                speedMode: "profile",
                profileName: value,
                managedProfileName: undefined,
                customDownloadMbps: undefined,
                customUploadMbps: undefined,
              });
            }}
          >
            <SelectTrigger>
              <SelectValue
                placeholder={profilesLoading ? "Loading profiles…" : "Select speed profile"}
              />
            </SelectTrigger>
            <SelectContent>
              {profileDetails.map((p) => (
                <SelectItem key={p.name} value={p.name}>
                  {p.name} — {formatRateLimit(p.rateLimit)}
                </SelectItem>
              ))}
              <SelectItem value={CUSTOM_SPEED_VALUE}>Custom Speed Limit</SelectItem>
            </SelectContent>
          </Select>
          {profileDetails.length === 0 && !profilesLoading ? (
            <p className="text-xs text-amber-600 dark:text-amber-400">
              No MikroTik profiles cached. Synchronize Router or enter custom speeds.
            </p>
          ) : null}
        </div>
        {form.speedMode === "custom" ? (
          <>
            <Field label="Download (Mbps)">
              <NumericInput
                min={1}
                max={1000}
                value={form.customDownloadMbps}
                onValueChange={(n) => setForm({ ...form, customDownloadMbps: n })}
              />
            </Field>
            <Field label="Upload (Mbps)">
              <NumericInput
                min={1}
                max={1000}
                value={form.customUploadMbps}
                onValueChange={(n) => setForm({ ...form, customUploadMbps: n })}
              />
            </Field>
            <p className="col-span-2 text-xs text-muted-foreground">
              Creates or updates a Renz-Fi-managed MikroTik hotspot user profile
              (rate-limit = download/upload). Owner profiles are not overwritten.
            </p>
          </>
        ) : null}
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
