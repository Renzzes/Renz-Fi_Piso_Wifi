import { useMemo, useState } from "react";
import { ExternalLink, Pencil, Plus, RadioTower, Search, Trash2 } from "lucide-react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
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
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { toast } from "sonner";
import { isApiError } from "@/services/api";
import {
  ACCESS_POINT_VENDORS,
  accessPointsApi,
  type AccessPointDetectDevice,
  DetectJobError,
  type AccessPointRecord,
  type AccessPointStatus,
  type AccessPointWritePayload,
} from "@/services/accessPoints";

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
      return "Network reachable";
    case "management_reachable":
      return "Management reachable";
    case "unreachable":
      return "Unreachable";
    case "disabled":
      return "Disabled";
    case "auth_failed":
      return "Auth failed";
    case "unknown":
    default:
      return "Unknown";
  }
}

function detectLabel(status?: string): string {
  if (!status) return "—";
  if (status === "reachable") return "Reachable";
  if (status === "stale") return "Stale";
  if (status === "failed") return "Failed";
  return status.replace(/_/g, " ");
}

function openManagement(ip: string) {
  const trimmed = ip.trim();
  if (!trimmed) return;
  window.open(`http://${trimmed}`, "_blank", "noopener,noreferrer");
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
  const [detecting, setDetecting] = useState(false);
  const [detectOpen, setDetectOpen] = useState(false);
  const [detectedDevices, setDetectedDevices] = useState<AccessPointDetectDevice[]>([]);
  const [detectedByIp, setDetectedByIp] = useState<Record<string, string>>({});
  const [detectState, setDetectState] = useState<
    "idle" | "detecting" | "found" | "empty" | "failed" | "timeout"
  >("idle");
  const [detectMessage, setDetectMessage] = useState("");

  const title = useMemo(
    () => (editing ? "Edit access point" : "Add access point"),
    [editing],
  );

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

  const applyDetectedDevice = (device: AccessPointDetectDevice) => {
    setForm((prev) => ({
      ...prev,
      managementIp: device.ip ?? prev.managementIp,
      name: prev.name || `AP ${device.ip ?? ""}`.trim(),
      notes:
        prev.notes ||
        [device.interface, device.bridgePort, device.status]
          .filter(Boolean)
          .join(" / "),
    }));
    setDetectOpen(false);
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
      toast.success(editing ? "Access point updated" : "Access point added");
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

  const checkMutation = useMutation({
    mutationFn: async (id: string) => accessPointsApi.checkAndWait(id),
    onMutate: (id) => {
      setCheckingIds((prev) => {
        const next = new Set(prev);
        next.add(id);
        return next;
      });
    },
    onSuccess: (job) => {
      queryClient.invalidateQueries({ queryKey: ["access-points"] });
      const label = statusLabel(job.status);
      if (job.state === "failed") {
        toast.error(job.message || job.errorCode || "Access point check failed");
        return;
      }
      toast.success(job.message || `Check complete: ${label}`);
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
    mutationFn: async () => accessPointsApi.detectAndWait(),
    onMutate: () => {
      setDetecting(true);
      setDetectOpen(true);
      setDetectedDevices([]);
      setDetectState("detecting");
      setDetectMessage("Detecting...");
    },
    onSuccess: (result) => {
      const devices = result.devices ?? [];
      setDetectedDevices(devices);
      const byIp: Record<string, string> = {};
      for (const device of devices) {
        const ip = (device.ip ?? "").trim();
        if (!ip) continue;
        byIp[ip] = (device.status ?? "unknown").trim().toLowerCase();
      }
      setDetectedByIp(byIp);
      setDetectState("found");
      setDetectMessage(`Detect completed - candidate(s) found: ${devices.length}.`);
      toast.success(`Detect completed: ${devices.length} candidate(s) found.`);
    },
    onError: (error) => {
      if (error instanceof DetectJobError) {
        if (error.code === "DETECT_EMPTY") {
          setDetectState("empty");
          setDetectMessage("No unregistered access point was detected.");
          toast.message("Detect completed with no matching access point.");
          return;
        }
        if (error.code === "DETECT_TIMEOUT") {
          setDetectState("timeout");
          setDetectMessage("Detection timed out.");
          toast.error("Detection timed out.");
          return;
        }
        setDetectState("failed");
        setDetectMessage("Detection failed.");
        toast.error(error.message || "Detection failed.");
        return;
      }
      setDetectState("failed");
      setDetectMessage("Detection failed.");
      toast.error(isApiError(error) ? error.message : "Detection failed.");
    },
    onSettled: () => {
      setDetecting(false);
    },
  });

  return (
    <div>
      <PageHeader
        title="Access Points"
        description="External Access Points extend Wi-Fi coverage through your MikroTik LAN. Configure each access point in its own web interface first, then register its management IP here. Renz-Fi does not configure the access point."
        actions={
          <Button size="sm" onClick={openCreate}>
            <Plus className="h-3.5 w-3.5" /> Add Access Point
          </Button>
        }
      />

      {data?.registryError && (
        <Alert className="mb-3">
          <AlertTitle>Registry could not be loaded</AlertTitle>
          <AlertDescription>
            The saved access-point file was not applied ({data.registryError}). The
            file was not deleted. You can add new records after storage is healthy.
          </AlertDescription>
        </Alert>
      )}

      {records.length === 0 && !isLoading && (
        <Alert className="mb-3 max-w-3xl">
          <AlertTitle>No external access points registered</AlertTitle>
          <AlertDescription className="space-y-2">
            <p>
              External Access Points are optional coverage radios on the MikroTik
              LAN. Configure each device in its own web interface first. Renz-Fi
              does not configure the access point. The main system works without them.
            </p>
            <ul className="list-disc pl-5 text-sm">
              <li>On the AP: set AP or Bridge mode</li>
              <li>On the AP: disable DHCP and NAT</li>
              <li>Connect the AP LAN port to the MikroTik LAN or switch</li>
              <li>Confirm clients get DHCP and HotSpot from MikroTik</li>
              <li>Then register the AP management IP here</li>
            </ul>
          </AlertDescription>
        </Alert>
      )}

      {records.length > 0 && (
        <div className="rounded-md border overflow-x-auto">
          <table className="w-full text-sm">
            <thead className="bg-muted/50 text-left">
              <tr>
                <th className="px-3 py-2 font-medium">Name</th>
                <th className="px-3 py-2 font-medium">Brand</th>
                <th className="px-3 py-2 font-medium">Model</th>
                <th className="px-3 py-2 font-medium">Management IP</th>
                <th className="px-3 py-2 font-medium">SSID</th>
                <th className="px-3 py-2 font-medium">Enabled</th>
                <th className="px-3 py-2 font-medium">Detect</th>
                <th className="px-3 py-2 font-medium">Status</th>
                <th className="px-3 py-2 font-medium">Credentials</th>
                <th className="px-3 py-2 font-medium text-right">Actions</th>
              </tr>
            </thead>
            <tbody>
              {records.map((record) => (
                <tr key={record.id} className="border-t">
                  <td className="px-3 py-2">{record.name}</td>
                  <td className="px-3 py-2">{vendorLabel(String(record.vendor))}</td>
                  <td className="px-3 py-2">{record.model || "—"}</td>
                  <td className="px-3 py-2 font-mono text-xs">{record.managementIp}</td>
                  <td className="px-3 py-2">{record.ssid || "—"}</td>
                  <td className="px-3 py-2">{record.enabled ? "Yes" : "No"}</td>
                  <td className="px-3 py-2">{detectLabel(detectedByIp[record.managementIp])}</td>
                  <td className="px-3 py-2">
                    {statusLabel(record.status)}
                    {record.latencyMs != null ? ` (${record.latencyMs} ms)` : ""}
                  </td>
                  <td className="px-3 py-2">
                    {record.hasCredentials ? "Saved" : "None"}
                  </td>
                  <td className="px-3 py-2">
                    <div className="flex justify-end gap-1">
                      <Button
                        type="button"
                        size="sm"
                        variant="outline"
                        disabled={checkingIds.has(record.id)}
                        onClick={() => checkMutation.mutate(record.id)}
                      >
                        <RadioTower className="h-3.5 w-3.5" />
                        {checkingIds.has(record.id) ? "Checking" : "Check"}
                      </Button>
                      <Button
                        type="button"
                        size="sm"
                        variant="outline"
                        onClick={() => openManagement(record.managementIp)}
                      >
                        <ExternalLink className="h-3.5 w-3.5" /> Open
                      </Button>
                      <Button
                        type="button"
                        size="sm"
                        variant="outline"
                        onClick={() => openEdit(record)}
                      >
                        <Pencil className="h-3.5 w-3.5" /> Edit
                      </Button>
                      <Button
                        type="button"
                        size="sm"
                        variant="outline"
                        onClick={() => {
                          if (window.confirm(`Remove ${record.name}?`)) {
                            deleteMutation.mutate(record.id);
                          }
                        }}
                      >
                        <Trash2 className="h-3.5 w-3.5" /> Remove
                      </Button>
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      <Dialog open={open} onOpenChange={(next) => !next && closeDialog()}>
        <DialogContent className="max-w-2xl w-[96vw] sm:w-full max-h-[calc(100dvh-1.5rem)] overflow-hidden p-0 flex flex-col gap-0">
          <DialogHeader className="px-6 pt-6 pb-2 border-b shrink-0">
            <DialogTitle>{title}</DialogTitle>
          </DialogHeader>
          <div className="min-h-0 flex-1 overflow-y-auto px-6 py-4 space-y-3 overscroll-contain">
          <p className="text-xs text-muted-foreground">
            Register an AP that is already configured in AP/Bridge mode. Renz-Fi
            does not push SSID, DHCP, NAT, VLAN, or other settings to the device.
          </p>
          {!editing && (
            <Button
              type="button"
              variant="outline"
              onClick={() => detectMutation.mutate()}
              disabled={detecting}
              className="w-full sm:w-auto"
            >
              <Search className="h-3.5 w-3.5" />
              {detecting ? "Detecting..." : "Detect"}
            </Button>
          )}
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
              <Label htmlFor="ap-ip">IP Address</Label>
              <Input
                id="ap-ip"
                value={form.managementIp}
                onChange={(event) =>
                  setForm({ ...form, managementIp: event.target.value })
                }
              />
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
                Informational metadata. Set the SSID on the AP itself. Renz-Fi does
                not change it.
              </p>
            </div>
            <div className="grid gap-1">
              <Label htmlFor="ap-location">Location</Label>
              <Input
                id="ap-location"
                value={form.location}
                onChange={(event) =>
                  setForm({ ...form, location: event.target.value })
                }
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
                onChange={(event) =>
                  setForm({ ...form, username: event.target.value })
                }
              />
              <p className="text-xs text-muted-foreground">
                Username for this AP&apos;s own web interface (for example
                http://10.10.10.20). Not MikroTik / RouterOS / Renz-Fi credentials.
                Stored as protected metadata only; Renz-Fi does not log in to or
                configure the AP.
              </p>
            </div>
            <div className="grid gap-1">
              <Label htmlFor="ap-pass">Management Password</Label>
              <Input
                id="ap-pass"
                type="password"
                autoComplete="new-password"
                value={form.password}
                onChange={(event) =>
                  setForm({ ...form, password: event.target.value })
                }
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
                onCheckedChange={(checked) =>
                  setForm({ ...form, enabled: checked === true })
                }
              />
              Enabled
            </label>
            <p className="text-xs text-muted-foreground">
              Controls whether Renz-Fi includes this registered AP in its own checks. It does not power off or configure the Access Point.
            </p>
          </div>
          </div>
          <DialogFooter className="px-6 py-4 border-t shrink-0 bg-background pb-[max(1rem,env(safe-area-inset-bottom))]">
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

      <Dialog open={detectOpen} onOpenChange={setDetectOpen}>
        <DialogContent className="max-w-xl w-[96vw] sm:w-full max-h-[calc(100dvh-1.5rem)] overflow-hidden p-0 flex flex-col gap-0">
          <DialogHeader className="px-6 pt-6 pb-2 border-b shrink-0">
            <DialogTitle>Detect Network Devices</DialogTitle>
          </DialogHeader>
          <div className="min-h-0 flex-1 overflow-y-auto px-6 py-4 space-y-3 overscroll-contain">
            {detectState === "detecting" && (
              <p className="text-sm text-muted-foreground">Detecting...</p>
            )}
            {detectState === "empty" && (
              <p className="text-sm text-muted-foreground">
                Detect completed - no matching AP found. No unregistered access point was
                detected.
              </p>
            )}
            {detectState === "timeout" && (
              <p className="text-sm text-destructive">Detect timed out.</p>
            )}
            {detectState === "failed" && (
              <p className="text-sm text-destructive">Detect failed.</p>
            )}
            {detectState === "found" && (
              <p className="text-sm text-muted-foreground">{detectMessage}</p>
            )}
            {detectedDevices.map((device) => (
              <div key={`${device.ip}-${device.mac}`} className="rounded-md border p-3 space-y-1.5">
                <div className="text-sm font-medium">Detected Access Point</div>
                <div className="text-xs">
                  IP Address: <span className="font-mono">{device.ip}</span>
                </div>
                <div className="text-xs">
                  MAC Address: <span className="font-mono">{device.mac}</span>
                </div>
                <div className="text-xs">Interface: {device.interface || "—"}</div>
                <div className="text-xs">Bridge Port: {device.bridgePort || "—"}</div>
                <div className="text-xs">Status: {detectLabel(device.status)}</div>
                <Button
                  type="button"
                  size="sm"
                  variant="outline"
                  onClick={() => applyDetectedDevice(device)}
                >
                  Use This Device
                </Button>
              </div>
            ))}
          </div>
          <DialogFooter className="px-6 py-4 border-t shrink-0 bg-background pb-[max(1rem,env(safe-area-inset-bottom))]">
            <Button type="button" variant="outline" onClick={() => setDetectOpen(false)}>
              Close
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
