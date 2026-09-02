import { useEffect, useMemo, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
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
  GAMING_PRIORITY_QUERY_KEY,
  gamingPriorityApi,
  gamingPriorityJobSucceeded,
  gamingPriorityStatusLabel,
  type GameProfile,
  type GamingPriorityLevel,
  type GamingPriorityState,
} from "@/services/gamingPriority";
import type { StatusTone } from "@/lib/dashboardDisplay";

function applyStatusTone(status: GamingPriorityState["applyStatus"]): StatusTone {
  switch (status) {
    case "applied":
      return "ok";
    case "pending_changes":
      return "warn";
    case "error":
      return "bad";
    case "disabled":
      return "neutral";
    default:
      return "unknown";
  }
}

function parseApiError(err: unknown, fallback: string): string {
  if (isApiError(err)) return err.message || fallback;
  if (err instanceof Error) return err.message;
  return fallback;
}

function toDraft(data: GamingPriorityState | undefined): GamingPriorityState | null {
  if (!data) return null;
  return {
    ...data,
    gameProfiles: data.gameProfiles.map((profile) => ({ ...profile })),
  };
}

export default function GamingPriorityPage() {
  const queryClient = useQueryClient();
  const { data, isLoading, isError, refetch } = useQuery({
    queryKey: GAMING_PRIORITY_QUERY_KEY,
    queryFn: () => gamingPriorityApi.get(),
    staleTime: 10_000,
  });

  const [draft, setDraft] = useState<GamingPriorityState | null>(null);

  useEffect(() => {
    if (data) setDraft(toDraft(data));
  }, [data]);

  const hasUnsavedChanges = useMemo(() => {
    if (!draft || !data) return false;
    return JSON.stringify(draft) !== JSON.stringify(data);
  }, [draft, data]);

  const invalidate = () =>
    queryClient.invalidateQueries({ queryKey: GAMING_PRIORITY_QUERY_KEY });

  const saveMutation = useMutation({
    mutationFn: (payload: GamingPriorityState) => gamingPriorityApi.save(payload),
    onSuccess: async () => {
      await invalidate();
      toast.success("Gaming priority configuration saved.");
    },
    onError: (err) => {
      toast.error(parseApiError(err, "Unable to save gaming priority configuration."));
    },
  });

  const applyMutation = useMutation({
    mutationFn: () => gamingPriorityApi.apply(),
    onSuccess: async (job) => {
      await invalidate();
      if (gamingPriorityJobSucceeded(job)) {
        toast.success("Gaming priority applied on MikroTik.");
      } else {
        toast.error("MikroTik apply failed — check router connection and try again.");
      }
    },
    onError: (err) => {
      toast.error(parseApiError(err, "Unable to apply gaming priority."));
    },
  });

  const updateDraft = (patch: Partial<GamingPriorityState>) => {
    setDraft((current) => (current ? { ...current, ...patch } : current));
  };

  const updateProfile = (index: number, patch: Partial<GameProfile>) => {
    setDraft((current) => {
      if (!current) return current;
      const profiles = current.gameProfiles.map((row, i) =>
        i === index ? { ...row, ...patch } : row,
      );
      return { ...current, gameProfiles: profiles };
    });
  };

  if (isLoading || !draft) {
    return (
      <div className="space-y-6">
        <PageHeader title="Gaming Priority" description="Loading configuration..." />
      </div>
    );
  }

  if (isError) {
    return (
      <div className="space-y-6">
        <PageHeader title="Gaming Priority" description="Unable to load configuration." />
        <Button onClick={() => refetch()}>Retry</Button>
      </div>
    );
  }

  const statusLabel = gamingPriorityStatusLabel(draft);
  const statusTone = applyStatusTone(draft.applyStatus);

  return (
    <div className="space-y-6">
      <PageHeader
        title="Gaming Priority"
        description="Prioritize gaming traffic on the guest network without changing HotSpot rate limits."
      />

      <Alert>
        <AlertTitle>Pilot feature</AlertTitle>
        <AlertDescription>
          Classification is configuration-driven. Mobile Legends and Call of Duty Mobile are
          seeded pilot profiles — detection is not guaranteed. Save changes first, then apply to
          MikroTik.
        </AlertDescription>
      </Alert>

      <div className="flex flex-wrap items-center gap-3">
        <ConfigStatusBadge label={statusLabel} tone={statusTone} />
        {draft.lastApplyError ? (
          <span className="text-sm text-destructive">{draft.lastApplyError}</span>
        ) : null}
      </div>

      <div className="grid gap-6 rounded-lg border bg-card p-6">
        <div className="flex items-center justify-between gap-4">
          <div className="space-y-1">
            <Label htmlFor="gp-enabled">Enabled</Label>
            <p className="text-sm text-muted-foreground">
              When enabled, gaming traffic is marked and queued on the guest bridge.
            </p>
          </div>
          <Switch
            id="gp-enabled"
            checked={draft.enabled}
            onCheckedChange={(enabled) => updateDraft({ enabled })}
          />
        </div>

        <div className="grid gap-4 md:grid-cols-2">
          <div className="space-y-2">
            <Label>Priority</Label>
            <Select
              value={draft.priority}
              onValueChange={(value: GamingPriorityLevel) => updateDraft({ priority: value })}
            >
              <SelectTrigger>
                <SelectValue placeholder="Select priority" />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="highest">Highest</SelectItem>
                <SelectItem value="high">High</SelectItem>
                <SelectItem value="normal">Normal</SelectItem>
              </SelectContent>
            </Select>
          </div>

          <div className="space-y-2">
            <Label htmlFor="gp-min">Gaming Minimum (Mbps)</Label>
            <Input
              id="gp-min"
              type="number"
              min={1}
              max={1000}
              value={draft.minimumGamingMbps}
              onChange={(e) =>
                updateDraft({ minimumGamingMbps: Number(e.target.value) || 0 })
              }
            />
          </div>

          <div className="space-y-2">
            <Label htmlFor="gp-max">Gaming Maximum (Mbps)</Label>
            <Input
              id="gp-max"
              type="number"
              min={1}
              max={1000}
              value={draft.maximumGamingMbps}
              onChange={(e) =>
                updateDraft({ maximumGamingMbps: Number(e.target.value) || 0 })
              }
            />
          </div>

          <div className="space-y-2">
            <Label htmlFor="gp-per-user">Per-user Gaming Limit (Mbps)</Label>
            <Input
              id="gp-per-user"
              type="number"
              min={1}
              max={1000}
              value={draft.perUserGamingMbps}
              onChange={(e) =>
                updateDraft({ perUserGamingMbps: Number(e.target.value) || 0 })
              }
              disabled={!draft.enabled}
            />
          </div>
        </div>

        <div className="space-y-3">
          <Label>Game Profiles</Label>
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>Name</TableHead>
                <TableHead>Slug</TableHead>
                <TableHead>Method</TableHead>
                <TableHead>Ports</TableHead>
                <TableHead>Priority</TableHead>
                <TableHead className="text-right">Enabled</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {draft.gameProfiles.map((profile, index) => (
                <TableRow key={profile.id}>
                  <TableCell>{profile.name}</TableCell>
                  <TableCell className="font-mono text-xs">{profile.slug}</TableCell>
                  <TableCell>{profile.classificationMethod}</TableCell>
                  <TableCell className="font-mono text-xs">
                    {profile.classificationData?.protocol}:{profile.classificationData?.ports}
                  </TableCell>
                  <TableCell>{profile.priority}</TableCell>
                  <TableCell className="text-right">
                    <Switch
                      checked={profile.enabled}
                      onCheckedChange={(enabled) => updateProfile(index, { enabled })}
                    />
                  </TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </div>

        <div className="flex flex-wrap gap-3">
          <Button
            onClick={() => saveMutation.mutate(draft)}
            disabled={saveMutation.isPending || !hasUnsavedChanges}
          >
            {saveMutation.isPending ? "Saving..." : "Save"}
          </Button>
          <Button
            variant="secondary"
            onClick={() => applyMutation.mutate()}
            disabled={applyMutation.isPending || saveMutation.isPending || hasUnsavedChanges}
          >
            {applyMutation.isPending ? "Applying..." : "Apply to MikroTik"}
          </Button>
          {hasUnsavedChanges ? (
            <span className="self-center text-sm text-muted-foreground">
              Save changes before applying.
            </span>
          ) : null}
        </div>
      </div>
    </div>
  );
}
