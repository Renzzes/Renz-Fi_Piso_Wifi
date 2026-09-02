import { useMemo, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { ShieldBan } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
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
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import { toast } from "sonner";
import { isApiError } from "@/services/api";
import {
  CONTENT_FILTER_QUERY_KEY,
  contentFilterApi,
  contentFilterJobSucceeded,
  isPlausibleDomain,
  normalizeDomainInput,
  type ContentFilterDomain,
  type ContentFilterDomainStatus,
} from "@/services/contentFilter";
import type { StatusTone } from "@/lib/dashboardDisplay";

function domainStatusTone(status: ContentFilterDomainStatus): StatusTone {
  switch (status) {
    case "active":
      return "ok";
    case "failed":
      return "bad";
    case "pending":
      return "warn";
    case "disabled":
      return "neutral";
    default:
      return "unknown";
  }
}

function domainStatusLabel(status: ContentFilterDomainStatus): string {
  switch (status) {
    case "active":
      return "Active";
    case "failed":
      return "Failed";
    case "pending":
      return "Pending";
    case "disabled":
      return "Disabled";
    default:
      return status;
  }
}

function parseApiError(err: unknown, fallback: string): string {
  if (isApiError(err)) return err.message || fallback;
  if (err instanceof Error) return err.message;
  return fallback;
}

export default function ContentFilteringPage() {
  const queryClient = useQueryClient();
  const [domainInput, setDomainInput] = useState("");
  const [removeTarget, setRemoveTarget] = useState<ContentFilterDomain | null>(null);

  const { data, isLoading, isError, refetch } = useQuery({
    queryKey: CONTENT_FILTER_QUERY_KEY,
    queryFn: () => contentFilterApi.get(),
    staleTime: 10_000,
  });

  const invalidate = () => queryClient.invalidateQueries({ queryKey: CONTENT_FILTER_QUERY_KEY });

  const toggleMutation = useMutation({
    mutationFn: (enabled: boolean) => contentFilterApi.setEnabled(enabled),
    onSuccess: async (job, enabled) => {
      await invalidate();
      if (contentFilterJobSucceeded(job)) {
        toast.success(
          enabled ? "Content filtering enabled on guest network." : "Content filtering disabled.",
        );
      } else {
        toast.error("MikroTik apply failed — check domain statuses and router connection.");
      }
    },
    onError: (err) => {
      toast.error(parseApiError(err, "Unable to update content filtering."));
    },
  });

  const addMutation = useMutation({
    mutationFn: (domain: string) => contentFilterApi.addDomain(domain),
    onSuccess: async (job) => {
      setDomainInput("");
      await invalidate();
      if (contentFilterJobSucceeded(job)) {
        toast.success("Domain queued and applied on MikroTik.");
      } else {
        toast.error("Domain saved locally but MikroTik apply failed — see status column.");
      }
    },
    onError: (err) => {
      toast.error(parseApiError(err, "Unable to add blocked domain."));
    },
  });

  const removeMutation = useMutation({
    mutationFn: (domain: string) => contentFilterApi.removeDomain(domain),
    onSuccess: async (job) => {
      setRemoveTarget(null);
      await invalidate();
      if (contentFilterJobSucceeded(job)) {
        toast.success("Blocked domain removed from MikroTik.");
      } else {
        toast.error("Removal saved locally but MikroTik sync failed.");
      }
    },
    onError: (err) => {
      toast.error(parseApiError(err, "Unable to remove blocked domain."));
    },
  });

  const syncMutation = useMutation({
    mutationFn: () => contentFilterApi.sync(),
    onSuccess: async (job) => {
      await invalidate();
      if (contentFilterJobSucceeded(job)) {
        toast.success("Content filter rules synchronized with MikroTik.");
      } else {
        toast.error("Synchronization failed — review domain statuses.");
      }
    },
    onError: (err) => {
      toast.error(parseApiError(err, "Unable to synchronize content filtering."));
    },
  });

  const domains = useMemo(
    () =>
      [...(data?.domains ?? [])].sort((a, b) =>
        a.domain.localeCompare(b.domain, undefined, { sensitivity: "base" }),
      ),
    [data?.domains],
  );

  const pending =
    toggleMutation.isPending ||
    addMutation.isPending ||
    removeMutation.isPending ||
    syncMutation.isPending;

  const handleAdd = () => {
    const normalized = normalizeDomainInput(domainInput);
    if (!normalized) {
      toast.error("Enter a website or domain to block.");
      return;
    }
    if (!isPlausibleDomain(normalized)) {
      toast.error(
        "Enter a valid domain such as example.com (paths and full URLs are not supported).",
      );
      return;
    }
    addMutation.mutate(normalized);
  };

  return (
    <div className="space-y-4">
      <PageHeader
        title="Content Filtering"
        description="Block websites and domains for guest HotSpot traffic only. Filtering applies at the domain level — not individual HTTPS page paths."
        actions={
          <Button
            type="button"
            variant="outline"
            size="sm"
            disabled={pending || isLoading}
            onClick={() => syncMutation.mutate()}
          >
            {syncMutation.isPending ? "Syncing…" : "Sync with MikroTik"}
          </Button>
        }
      />

      {isError ? (
        <Alert variant="destructive">
          <AlertTitle>Unable to load content filtering</AlertTitle>
          <AlertDescription className="flex flex-wrap items-center gap-2">
            <span>Check appliance connectivity and try again.</span>
            <Button type="button" size="sm" variant="outline" onClick={() => void refetch()}>
              Retry
            </Button>
          </AlertDescription>
        </Alert>
      ) : null}

      {data?.lastSyncError ? (
        <Alert>
          <AlertTitle>Router verification issue</AlertTitle>
          <AlertDescription>{data.lastSyncError}</AlertDescription>
        </Alert>
      ) : null}

      <div className="rounded-lg border border-border/70 bg-card p-4 shadow-sm">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div className="flex items-start gap-3">
            <ShieldBan className="mt-0.5 h-5 w-5 shrink-0 text-primary" aria-hidden />
            <div>
              <p className="text-sm font-medium">Guest network filtering</p>
              <p className="mt-0.5 max-w-xl text-xs text-muted-foreground">
                When enabled, blocked domains are dropped on the guest bridge via MikroTik DNS
                address-list and firewall rules. Management and admin access are not affected.
              </p>
            </div>
          </div>
          <div className="flex items-center gap-2">
            <Label htmlFor="cf-enabled" className="text-sm">
              {data?.enabled ? "Enabled" : "Disabled"}
            </Label>
            <Switch
              id="cf-enabled"
              checked={Boolean(data?.enabled)}
              disabled={isLoading || pending}
              onCheckedChange={(checked) => toggleMutation.mutate(checked)}
            />
          </div>
        </div>
      </div>

      <div className="rounded-lg border border-border/70 bg-card p-4 shadow-sm">
        <h3 className="text-sm font-semibold">Add blocked website/domain</h3>
        <p className="mt-1 text-xs text-muted-foreground">
          Enter a site name such as <span className="font-mono">facebook.com</span> — not a full URL
          with path (<span className="font-mono">example.com/page</span>).
        </p>
        <div className="mt-3 flex flex-col gap-2 sm:flex-row">
          <Input
            value={domainInput}
            onChange={(event) => setDomainInput(event.target.value)}
            placeholder="example.com"
            disabled={pending}
            onKeyDown={(event) => {
              if (event.key === "Enter") {
                event.preventDefault();
                handleAdd();
              }
            }}
          />
          <Button type="button" disabled={pending} onClick={handleAdd}>
            {addMutation.isPending ? "Adding…" : "Add Domain"}
          </Button>
        </div>
      </div>

      <div className="rounded-lg border border-border/70 bg-card shadow-sm">
        <div className="border-b px-4 py-3">
          <h3 className="text-sm font-semibold">Blocked Websites</h3>
          <p className="text-xs text-muted-foreground">
            {isLoading ? "Loading…" : `${domains.length} domain${domains.length === 1 ? "" : "s"}`}
          </p>
        </div>
        {domains.length === 0 && !isLoading ? (
          <p className="px-4 py-8 text-center text-sm text-muted-foreground">
            No blocked domains yet. Add a domain above to block guest access.
          </p>
        ) : (
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>Domain</TableHead>
                <TableHead>Status</TableHead>
                <TableHead className="text-right">Action</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {domains.map((row) => (
                <TableRow key={row.domain}>
                  <TableCell className="font-mono text-sm">{row.domain}</TableCell>
                  <TableCell>
                    <div className="space-y-1">
                      <ConfigStatusBadge
                        label={domainStatusLabel(row.status)}
                        tone={domainStatusTone(row.status)}
                      />
                      {row.lastError ? (
                        <p className="max-w-xs text-[11px] text-destructive">{row.lastError}</p>
                      ) : null}
                    </div>
                  </TableCell>
                  <TableCell className="text-right">
                    <Button
                      type="button"
                      variant="outline"
                      size="sm"
                      disabled={pending}
                      onClick={() => setRemoveTarget(row)}
                    >
                      Remove
                    </Button>
                  </TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        )}
      </div>

      <AlertDialog
        open={removeTarget != null}
        onOpenChange={(open) => !open && setRemoveTarget(null)}
      >
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Remove blocked domain?</AlertDialogTitle>
            <AlertDialogDescription>
              Guests will be able to reach{" "}
              <span className="font-mono font-medium">{removeTarget?.domain}</span> again after
              MikroTik sync completes.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel disabled={removeMutation.isPending}>Cancel</AlertDialogCancel>
            <AlertDialogAction
              disabled={removeMutation.isPending || !removeTarget}
              onClick={(event) => {
                event.preventDefault();
                if (removeTarget) removeMutation.mutate(removeTarget.domain);
              }}
            >
              {removeMutation.isPending ? "Removing…" : "Remove domain"}
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}
