import { useMemo, useState } from "react";
import { Eye, ExternalLink, Pencil, Plus, RadioTower, Search, Trash2 } from "lucide-react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Checkbox } from "@/components/ui/checkbox";
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
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
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Skeleton } from "@/components/ui/skeleton";
import { toast } from "sonner";
import { isApiError } from "@/services/api";
import {
  ACCESS_POINT_VENDORS,
  accessPointJobSucceeded,
  accessPointsApi,
  parseDetectDevices,
  type AccessPointDetectDevice,
  type AccessPointList,
  type AccessPointRecord,
  type AccessPointStatus,
  type AccessPointWritePayload,
} from "@/services/accessPoints";
import { AdminTableCard } from "@/components/admin/AdminTableCard";
import { DataPagination } from "@/components/admin/DataPagination";
import { EmptyState } from "@/components/admin/EmptyState";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import type { StatusTone } from "@/lib/dashboardDisplay";
import { clampPage, PAGE_SIZE_DEFAULT, pageSlice } from "@/lib/pagination";

const EMPTY_FORM: AccessPointWritePayload = {
  name: "",
  managementIp: "",
  enabled: true,
  vendor: "generic",
  model: "",
  username: "",
  password: "",
  ssid: "",
  location: "",
  notes: "",
};

function vendorLabel(vendor: string): string {
  if (vendor === "tp-link") return "TP-Link";
  if (vendor === "ruijie") return "Ruijie";
  if (vendor === "tenda") return "Tenda";
  if (vendor === "other") return "Other";
  return "Generic";
}

function statusLabel(status?: AccessPointStatus | null): string {
  switch (status) {
    case "online":
      return "Online";
    case "network_reachable":
      return "Online";
    case "management_reachable":
      return "Online";
    case "unreachable":
      return "Offline";
    case "disabled":
      return "Disabled";
    case "auth_failed":
      return "Auth failed";
    case "unknown":
    default:
      return "Unknown";
  }
}

function statusTone(status?: AccessPointStatus | null): StatusTone {
  switch (status) {
    case "online":
    case "network_reachable":
    case "management_reachable":
      return "ok";
    case "unreachable":
    case "auth_failed":
      return "bad";
    case "disabled":
      return "neutral";
    default:
      return "unknown";
  }
}

function enabledLabel(record: AccessPointRecord): string {
  return record.enabled ? "Yes" : "No";
}

function openManagement(ip: string) {
  const trimmed = ip.trim();
  if (!trimmed) return;
  window.open(`http://${trimmed}`, "_blank", "noopener,noreferrer");
}

function suggestName(device: AccessPointDetectDevice): string {
  const host = (device.hostname ?? "").trim();
  if (host) return host.slice(0, 32);
  const tail = device.mac.replace(/:/g, "").slice(-4).toUpperCase();
  return `AP-${tail || "LAN"}`.slice(0, 32);
}

export default function AccessPointsPage() {
  const queryClient = useQueryClient();
  const { data, isLoading } = useQuery({
    queryKey: ["access-points"],
    queryFn: () => accessPointsApi.list(),
    staleTime: Number.POSITIVE_INFINITY,
    refetchOnMount: false,
  });
  const records = data?.accessPoints ?? [];

  const [open, setOpen] = useState(false);
  const [editing, setEditing] = useState<AccessPointRecord | null>(null);
  const [form, setForm] = useState<AccessPointWritePayload>(EMPTY_FORM);
  const [checkingIds, setCheckingIds] = useState<Set<string>>(new Set());
  const [enablingIds, setEnablingIds] = useState<Set<string>>(new Set());
  const [detectOpen, setDetectOpen] = useState(false);
  const [detectDevices, setDetectDevices] = useState<AccessPointDetectDevice[]>([]);
  const [viewing, setViewing] = useState<AccessPointRecord | null>(null);
  const [removeTarget, setRemoveTarget] = useState<AccessPointRecord | null>(null);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(PAGE_SIZE_DEFAULT);

  const safePage = clampPage(page, records.length, pageSize);
  const pageRows = pageSlice(records, safePage, pageSize);

  const title = useMemo(() => (editing ? "Edit access point" : "Add access point"), [editing]);

  const closeDialog = () => {
    setOpen(false);
    setEditing(null);
    setForm(EMPTY_FORM);
  };

  const openCreate = () => {
    setEditing(null);
    setForm(EMPTY_FORM);
    setOpen(true);
  };

  const openEdit = (record: AccessPointRecord) => {
    setEditing(record);
    setForm({
      name: record.name,
      managementIp: record.managementIp,
      enabled: record.enabled,
      vendor: record.vendor || "generic",
      model: record.model,
      username: "",
      password: "",
      ssid: record.ssid,
      location: record.location,
      notes: record.notes,
    });
    setOpen(true);
  };

  const applyDetectedDevice = (device: AccessPointDetectDevice) => {
    setEditing(null);
    setForm({
      ...EMPTY_FORM,
      name: suggestName(device),
      managementIp: device.ip,
      enabled: true,
      notes: device.bridgePort
        ? `Detected on ${device.interface || "LAN"} / ${device.bridgePort}`
        : `Detected on ${device.interface || "LAN"}`,
    });
    setDetectOpen(false);
    setOpen(true);
  };

  const saveMutation = useMutation({
    mutationFn: async () => {
      const payload: AccessPointWritePayload = {
        name: form.name.trim(),
        managementIp: form.managementIp.trim(),
        enabled: form.enabled !== false,
        vendor: form.vendor || "generic",
        model: form.model?.trim() ?? "",
        username: form.username?.trim() ?? "",
        ssid: form.ssid?.trim() ?? "",
        location: form.location?.trim() ?? "",
        notes: form.notes?.trim() ?? "",
      };
      if (form.password && form.password.length > 0) {
        payload.password = form.password;
      }
      if (editing) return accessPointsApi.update(editing.id, payload);
      return accessPointsApi.create(payload);
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["access-points"] });
      toast.success(editing ? "Access point updated" : "Access point registered");
      closeDialog();
    },
    onError: (error) => {
      toast.error(isApiError(error) ? error.message : "Unable to save access point");
    },
  });

  const deleteMutation = useMutation({
    mutationFn: (id: string) => accessPointsApi.remove(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["access-points"] });
      toast.success("Access point removed");
    },
    onError: (error) => {
      toast.error(isApiError(error) ? error.message : "Unable to remove access point");
    },
  });

  const enableMutation = useMutation({
    mutationFn: async (record: AccessPointRecord) =>
      accessPointsApi.update(record.id, {
        name: record.name,
        managementIp: record.managementIp,
        enabled: true,
        vendor: record.vendor || "generic",
        model: record.model,
        ssid: record.ssid,
        location: record.location,
        notes: record.notes,
      }),
    onMutate: (record) => {
      setEnablingIds((prev) => {
        const next = new Set(prev);
        next.add(record.id);
        return next;
      });
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["access-points"] });
      toast.success("Access point enabled");
    },
    onError: (error) => {
      toast.error(isApiError(error) ? error.message : "Unable to enable access point");
    },
    onSettled: (_data, _error, record) => {
      setEnablingIds((prev) => {
        const next = new Set(prev);
        next.delete(record.id);
        return next;
      });
    },
  });

  const checkMutation = useMutation({
    mutationFn: async (id: string) => accessPointsApi.checkAndWait(id),
    onMutate: (id) => {
      setCheckingIds((prev) => {
        const next = new Set(prev);
        next.add(id);
        return next;
      });
    },
    onSuccess: (job, id) => {
      const online = job.online === true || (job.online !== false && accessPointJobSucceeded(job));

      // MikroTik/worker failure is not "AP down" — keep prior Online/Offline.
      // Only a completed Check with a reachability verdict updates status.
      if (job.state !== "failed") {
        const nextStatus: AccessPointStatus = online ? "online" : "unreachable";
        const now = Date.now();
        queryClient.setQueryData<AccessPointList>(["access-points"], (prev) => {
          if (!prev) return prev;
          return {
            ...prev,
            accessPoints: prev.accessPoints.map((row) =>
              row.id !== id
                ? row
                : {
                    ...row,
                    status: nextStatus,
                    lastCheck: now,
                    lastSuccessfulCheck: online ? now : (row.lastSuccessfulCheck ?? null),
                    lastError: online ? null : job.errorCode || "ACCESS_POINT_OFFLINE",
                  },
            ),
          };
        });
        setViewing((current) =>
          current && current.id === id
            ? {
                ...current,
                status: nextStatus,
                lastCheck: now,
                lastSuccessfulCheck: online ? now : (current.lastSuccessfulCheck ?? null),
                lastError: online ? null : job.errorCode || "ACCESS_POINT_OFFLINE",
              }
            : current,
        );
        // Background refresh; UI already shows sticky last Check result.
        void queryClient.invalidateQueries({ queryKey: ["access-points"] });
      }

      if (job.state === "failed") {
        toast.error(
          job.message || job.errorCode || "Access point check failed (MikroTik unavailable)",
        );
        return;
      }
      if (online) {
        toast.success(job.message || "Online");
        return;
      }
      toast.error(job.message || "Offline");
    },
    onError: (error) => {
      toast.error(isApiError(error) ? error.message : "Unable to check access point");
    },
    onSettled: (_data, _error, id) => {
      setCheckingIds((prev) => {
        const next = new Set(prev);
        next.delete(id);
        return next;
      });
    },
  });

  const detectMutation = useMutation({
    mutationFn: () => accessPointsApi.detectAndWait(),
    onSuccess: (job) => {
      if (job.state === "failed" || job.ok === false) {
        toast.error("Detect failed — confirm MikroTik credentials and Router Worker are healthy.");
        setDetectDevices([]);
        setDetectOpen(true);
        return;
      }
      const devices = parseDetectDevices(job);
      setDetectDevices(devices);
      setDetectOpen(true);
      if (devices.length === 0) {
        toast.message("Detect finished — no new private LAN candidates found in MikroTik ARP.");
        return;
      }
      toast.success(`Detect found ${devices.length} candidate${devices.length === 1 ? "" : "s"}`);
    },
    onError: (error) => {
      toast.error(isApiError(error) ? error.message : "Unable to detect access points");
    },
  });

  return (
    <div className="w-full max-w-none space-y-3">
      <div className="flex flex-col gap-3 lg:flex-row lg:items-start lg:justify-between">
        <div>
          <h2 className="text-2xl font-semibold leading-tight">Access Points</h2>
          <p className="mt-0.5 text-[13px] text-muted-foreground">
            Configure each access point in its own web interface first, then Detect it from MikroTik
            ARP or enter its management IP. Renz-Fi does not configure the access point.
          </p>
        </div>
        <div className="flex flex-nowrap items-center gap-2.5 overflow-x-auto">
          <Button
            size="sm"
            variant="outline"
            className="h-9 shrink-0 px-4"
            disabled={detectMutation.isPending}
            onClick={() => detectMutation.mutate()}
          >
            <Search className="h-4 w-4" />
            {detectMutation.isPending ? "Detecting..." : "Detect"}
          </Button>
          <Button size="sm" className="h-9 shrink-0 px-4" onClick={openCreate}>
            <Plus className="h-4 w-4" /> Add Access Point
          </Button>
        </div>
      </div>

      {data?.registryError ? (
        <Alert>
          <AlertTitle>Registry could not be loaded</AlertTitle>
          <AlertDescription>
            The saved access-point file was not applied ({data.registryError}). The file was not
            deleted. You can add new records after storage is healthy.
          </AlertDescription>
        </Alert>
      ) : null}

      {!isLoading && records.length === 0 ? (
        <Alert>
          <AlertTitle>How to register an access point</AlertTitle>
          <AlertDescription className="space-y-2">
            <p>
              External Access Points are optional coverage radios on the MikroTik LAN. Configure
              each device in its own web interface first. Renz-Fi does not configure the access
              point. The main system works without them.
            </p>
            <ul className="list-disc pl-5 text-sm">
              <li>On the AP: set AP or Bridge mode with a static management IP</li>
              <li>On the AP: disable DHCP and NAT</li>
              <li>Connect the AP LAN port to the MikroTik LAN or switch</li>
              <li>Click Detect to list devices MikroTik already sees via ARP</li>
              <li>Pick a candidate and Save — or Add Access Point and enter the IP manually</li>
            </ul>
          </AlertDescription>
        </Alert>
      ) : null}

      <AdminTableCard
        footer={
          !isLoading && records.length > 0 ? (
            <DataPagination
              page={safePage}
              pageSize={pageSize}
              total={records.length}
              onPageChange={setPage}
              onPageSizeChange={setPageSize}
              itemLabel="access points"
            />
          ) : null
        }
      >
        <Table>
          <TableHeader>
            <TableRow className="hover:bg-transparent">
              <TableHead className="h-10 bg-muted/40 text-[12px]">Brand</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Model</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">IP Address</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">SSID</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Registered</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Status</TableHead>
              <TableHead className="h-10 bg-muted/40 text-right text-[12px]">Actions</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {isLoading
              ? Array.from({ length: 4 }).map((_, i) => (
                  <TableRow key={`ap-sk-${i}`} className="h-12">
                    {Array.from({ length: 7 }).map((__, cell) => (
                      <TableCell key={cell}>
                        <Skeleton className="h-3.5 w-full max-w-[7rem]" />
                      </TableCell>
                    ))}
                  </TableRow>
                ))
              : pageRows.map((record) => (
                  <TableRow key={record.id} className="h-12">
                    <TableCell className="text-[13px]">
                      {vendorLabel(String(record.vendor))}
                    </TableCell>
                    <TableCell className="text-[13px]">{record.model || "—"}</TableCell>
                    <TableCell className="font-mono text-[12px]">{record.managementIp}</TableCell>
                    <TableCell className="text-[13px]">{record.ssid || "—"}</TableCell>
                    <TableCell className="text-[13px]">{enabledLabel(record)}</TableCell>
                    <TableCell>
                      <ConfigStatusBadge
                        label={statusLabel(record.status)}
                        tone={statusTone(record.status)}
                      />
                    </TableCell>
                    <TableCell className="text-right">
                      <div className="flex justify-end gap-1">
                        <Button
                          type="button"
                          size="sm"
                          variant="outline"
                          className="h-7 px-2 text-[11px]"
                          onClick={() => setViewing(record)}
                        >
                          <Eye className="h-3.5 w-3.5" /> View
                        </Button>
                        {record.enabled ? (
                          <Button
                            type="button"
                            size="sm"
                            variant="outline"
                            className="h-7 px-2 text-[11px]"
                            disabled={checkingIds.has(record.id)}
                            onClick={() => checkMutation.mutate(record.id)}
                          >
                            <RadioTower className="h-3.5 w-3.5" />
                            {checkingIds.has(record.id) ? "Checking" : "Check"}
                          </Button>
                        ) : (
                          <Button
                            type="button"
                            size="sm"
                            className="h-7 px-2 text-[11px]"
                            disabled={enablingIds.has(record.id)}
                            onClick={() => enableMutation.mutate(record)}
                          >
                            {enablingIds.has(record.id) ? "Enabling..." : "Enable"}
                          </Button>
                        )}
                      </div>
                    </TableCell>
                  </TableRow>
                ))}
          </TableBody>
        </Table>
        {!isLoading && records.length === 0 ? (
          <EmptyState
            title="No external access points registered"
            description="The main system works without them. Use Detect or Add Access Point when you have coverage radios on the MikroTik LAN."
            action={
              <div className="flex flex-wrap justify-center gap-2">
                <Button
                  size="sm"
                  variant="outline"
                  disabled={detectMutation.isPending}
                  onClick={() => detectMutation.mutate()}
                >
                  <Search className="h-4 w-4" /> Detect
                </Button>
                <Button size="sm" onClick={openCreate}>
                  <Plus className="h-4 w-4" /> Add Access Point
                </Button>
              </div>
            }
          />
        ) : null}
      </AdminTableCard>

      <Dialog open={Boolean(viewing)} onOpenChange={(next) => !next && setViewing(null)}>
        <DialogContent className="max-w-lg w-[96vw] sm:w-full max-h-[calc(100dvh-1.5rem)] overflow-hidden p-0 flex flex-col gap-0">
          <DialogHeader className="px-6 pt-6 pb-2 border-b shrink-0">
            <DialogTitle>{viewing?.name || "Access point details"}</DialogTitle>
          </DialogHeader>
          {viewing && (
            <div className="min-h-0 flex-1 overflow-y-auto px-6 py-4 space-y-3 text-sm">
              <dl className="grid grid-cols-[8rem_1fr] gap-x-3 gap-y-2">
                <dt className="text-muted-foreground">Name</dt>
                <dd>{viewing.name}</dd>
                <dt className="text-muted-foreground">Brand</dt>
                <dd>{vendorLabel(String(viewing.vendor))}</dd>
                <dt className="text-muted-foreground">Model</dt>
                <dd>{viewing.model || "—"}</dd>
                <dt className="text-muted-foreground">IP Address</dt>
                <dd className="font-mono text-xs">{viewing.managementIp}</dd>
                <dt className="text-muted-foreground">SSID</dt>
                <dd>{viewing.ssid || "—"}</dd>
                <dt className="text-muted-foreground">Location</dt>
                <dd>{viewing.location || "—"}</dd>
                <dt className="text-muted-foreground">Notes</dt>
                <dd>{viewing.notes || "—"}</dd>
                <dt className="text-muted-foreground">Registered</dt>
                <dd>{enabledLabel(viewing)}</dd>
                <dt className="text-muted-foreground">Status</dt>
                <dd>
                  <ConfigStatusBadge
                    label={statusLabel(viewing.status)}
                    tone={statusTone(viewing.status)}
                  />
                </dd>
                <dt className="text-muted-foreground">Credentials</dt>
                <dd>{viewing.hasCredentials ? "Saved" : "None"}</dd>
              </dl>
            </div>
          )}
          <DialogFooter className="px-6 py-4 border-t shrink-0 flex-wrap gap-2">
            <Button
              type="button"
              variant="outline"
              disabled={!viewing?.managementIp}
              onClick={() => viewing && openManagement(viewing.managementIp)}
            >
              <ExternalLink className="h-3.5 w-3.5" /> Open
            </Button>
            <Button
              type="button"
              variant="outline"
              onClick={() => {
                if (!viewing) return;
                const record = viewing;
                setViewing(null);
                openEdit(record);
              }}
            >
              <Pencil className="h-3.5 w-3.5" /> Edit
            </Button>
            <Button
              type="button"
              variant="outline"
              className="border-red-500/40 bg-red-500/10 text-red-600 hover:bg-red-500/20 dark:text-red-400"
              onClick={() => {
                if (!viewing) return;
                setRemoveTarget(viewing);
              }}
            >
              <Trash2 className="h-3.5 w-3.5" /> Remove
            </Button>
            <Button type="button" variant="outline" onClick={() => setViewing(null)}>
              Close
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={detectOpen} onOpenChange={setDetectOpen}>
        <DialogContent className="max-w-2xl w-[96vw] sm:w-full max-h-[calc(100dvh-1.5rem)] overflow-hidden p-0 flex flex-col gap-0">
          <DialogHeader className="px-6 pt-6 pb-2 border-b shrink-0">
            <DialogTitle>Detected on MikroTik ARP</DialogTitle>
          </DialogHeader>
          <div className="min-h-0 flex-1 overflow-y-auto px-6 py-4 space-y-3">
            <Alert>
              <AlertTitle>How to choose a device</AlertTitle>
              <AlertDescription>
                Choose the IP address that matches your Access Point, and confirm the MAC address
                matches the AP label or manufacturer sticker. Already registered IPs are hidden.
              </AlertDescription>
            </Alert>
            <p className="text-xs text-muted-foreground">
              One-time read from MikroTik ARP (not an ESP32 ping). Pick a row to register it in
              Renz-Fi.
            </p>
            {detectDevices.length === 0 ? (
              <Alert>
                <AlertTitle>No candidates</AlertTitle>
                <AlertDescription>
                  Plug the AP into the MikroTik LAN, confirm its management IP is static, then run
                  Detect again. You can also Add Access Point and enter the IP manually.
                </AlertDescription>
              </Alert>
            ) : (
              <div className="rounded-md border overflow-x-auto">
                <table className="w-full text-sm">
                  <thead className="bg-muted/50 text-left">
                    <tr>
                      <th className="px-3 py-2 font-medium">IP</th>
                      <th className="px-3 py-2 font-medium">MAC</th>
                      <th className="px-3 py-2 font-medium">Interface</th>
                      <th className="px-3 py-2 font-medium">Port</th>
                      <th className="px-3 py-2 font-medium">Status</th>
                      <th className="px-3 py-2 font-medium text-right">Action</th>
                    </tr>
                  </thead>
                  <tbody>
                    {detectDevices.map((device) => (
                      <tr key={`${device.ip}-${device.mac}`} className="border-t">
                        <td className="px-3 py-2 font-mono text-xs">{device.ip}</td>
                        <td className="px-3 py-2 font-mono text-xs">{device.mac}</td>
                        <td className="px-3 py-2">{device.interface || "—"}</td>
                        <td className="px-3 py-2">{device.bridgePort || "—"}</td>
                        <td className="px-3 py-2">{device.status || "—"}</td>
                        <td className="px-3 py-2 text-right">
                          <Button
                            type="button"
                            size="sm"
                            onClick={() => applyDetectedDevice(device)}
                          >
                            Use
                          </Button>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
          </div>
          <DialogFooter className="px-6 py-4 border-t shrink-0 justify-between sm:justify-between gap-3">
            <Button type="button" variant="outline" onClick={() => setDetectOpen(false)}>
              Close
            </Button>
            <Button
              type="button"
              variant="outline"
              disabled={detectMutation.isPending}
              onClick={() => detectMutation.mutate()}
            >
              {detectMutation.isPending ? "Detecting..." : "Detect again"}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={open} onOpenChange={(next) => !next && closeDialog()}>
        <DialogContent className="max-w-2xl w-[96vw] sm:w-full max-h-[calc(100dvh-1.5rem)] overflow-hidden p-0 flex flex-col gap-0">
          <DialogHeader className="px-6 pt-6 pb-2 border-b shrink-0">
            <DialogTitle>{title}</DialogTitle>
          </DialogHeader>
          <div className="min-h-0 flex-1 overflow-y-auto px-6 py-4 space-y-3 overscroll-contain">
            <p className="text-xs text-muted-foreground">
              Register an AP that is already configured in AP/Bridge mode with a static management
              IP. Prefer Detect from MikroTik ARP, then Save. Renz-Fi does not push SSID, DHCP, NAT,
              VLAN, or other settings to the device.
            </p>
            <div className="grid gap-3">
              <div className="grid gap-1">
                <Label htmlFor="ap-name">Name</Label>
                <Input
                  id="ap-name"
                  maxLength={32}
                  value={form.name}
                  onChange={(event) => setForm({ ...form, name: event.target.value })}
                />
              </div>
              <div className="grid gap-1">
                <Label htmlFor="ap-ip">Management IP</Label>
                <Input
                  id="ap-ip"
                  value={form.managementIp}
                  onChange={(event) => setForm({ ...form, managementIp: event.target.value })}
                />
                <p className="text-xs text-muted-foreground">
                  Use the static IP configured on the AP itself (for example 10.10.10.20).
                </p>
              </div>
              <div className="grid gap-1">
                <Label>Brand (label only)</Label>
                <Select
                  value={form.vendor || "generic"}
                  onValueChange={(vendor) => setForm({ ...form, vendor })}
                >
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {ACCESS_POINT_VENDORS.map((vendor) => (
                      <SelectItem key={vendor} value={vendor}>
                        {vendorLabel(vendor)}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
                <p className="text-xs text-muted-foreground">
                  Informational. This does not select a driver or configure the AP.
                </p>
              </div>
              <div className="grid gap-1">
                <Label htmlFor="ap-model">Model</Label>
                <Input
                  id="ap-model"
                  value={form.model}
                  onChange={(event) => setForm({ ...form, model: event.target.value })}
                />
              </div>
              <div className="grid gap-1">
                <Label htmlFor="ap-ssid">SSID (label only)</Label>
                <Input
                  id="ap-ssid"
                  value={form.ssid}
                  onChange={(event) => setForm({ ...form, ssid: event.target.value })}
                />
                <p className="text-xs text-muted-foreground">
                  Informational metadata. Set the SSID on the AP itself. Renz-Fi does not change it.
                </p>
              </div>
              <div className="grid gap-1">
                <Label htmlFor="ap-location">Location</Label>
                <Input
                  id="ap-location"
                  value={form.location}
                  onChange={(event) => setForm({ ...form, location: event.target.value })}
                />
              </div>
              <div className="grid gap-1">
                <Label htmlFor="ap-notes">Notes</Label>
                <Input
                  id="ap-notes"
                  value={form.notes}
                  onChange={(event) => setForm({ ...form, notes: event.target.value })}
                />
              </div>
              <div className="grid gap-1">
                <Label htmlFor="ap-user">Management Username</Label>
                <Input
                  id="ap-user"
                  autoComplete="off"
                  value={form.username}
                  onChange={(event) => setForm({ ...form, username: event.target.value })}
                />
                <p className="text-xs text-muted-foreground">
                  Username for this AP&apos;s own web interface (for example http://10.10.10.20).
                  Not MikroTik / RouterOS / Renz-Fi credentials. Stored as protected metadata only;
                  Renz-Fi does not log in to or configure the AP.
                </p>
              </div>
              <div className="grid gap-1">
                <Label htmlFor="ap-pass">Management Password</Label>
                <Input
                  id="ap-pass"
                  type="password"
                  autoComplete="new-password"
                  value={form.password}
                  onChange={(event) => setForm({ ...form, password: event.target.value })}
                />
                <p className="text-xs text-muted-foreground">
                  {editing
                    ? "Leave blank to keep the stored password. Owner-configured AP password only — not the MikroTik password. Metadata only; not used for AP login or configuration."
                    : "Optional. Owner-configured AP password only — not the MikroTik password. Stored encrypted as metadata; never returned to the browser and never used to configure the AP."}
                </p>
              </div>
              <label className="flex items-center gap-2 text-sm">
                <Checkbox
                  checked={form.enabled !== false}
                  onCheckedChange={(checked) => setForm({ ...form, enabled: checked === true })}
                />
                Enabled in Renz-Fi
              </label>
              <p className="text-xs text-muted-foreground">
                Registration uses MikroTik Detect (ARP), not ESP32 Sync. Uncheck to pause optional
                Check probes. This does not power off or configure the Access Point.
              </p>
            </div>
          </div>
          <DialogFooter className="px-6 py-4 border-t shrink-0 bg-background pb-[max(1rem,env(safe-area-inset-bottom))] flex-wrap gap-2">
            <Button type="button" variant="outline" onClick={closeDialog}>
              Cancel
            </Button>
            <Button
              type="button"
              disabled={saveMutation.isPending}
              onClick={() => saveMutation.mutate()}
            >
              Save
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <AlertDialog
        open={removeTarget !== null}
        onOpenChange={(open) => !open && setRemoveTarget(null)}
      >
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Remove access point?</AlertDialogTitle>
            <AlertDialogDescription>
              This removes {removeTarget?.name || "this access point"} from Renz-Fi. The physical
              access point is not changed.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>Cancel</AlertDialogCancel>
            <AlertDialogAction
              className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
              onClick={() => {
                if (!removeTarget) return;
                const id = removeTarget.id;
                setRemoveTarget(null);
                setViewing(null);
                deleteMutation.mutate(id);
              }}
            >
              Remove
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}
