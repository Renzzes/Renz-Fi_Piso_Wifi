import { type FormEvent, useEffect, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Eye, EyeOff } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Checkbox } from "@/components/ui/checkbox";
import { ConfigCard } from "@/components/system-config/ConfigCard";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import { settingsApi } from "@/services/settings";
import { ApiError } from "@/services/api";
import { toast } from "sonner";
import { MIN_ADMIN_PASSWORD_LENGTH } from "@/components/ChangeAdminPasswordForm";
import {
  DEFAULT_OPERATOR_PERMISSIONS,
  OPERATOR_PERMISSION_KEYS,
  OPERATOR_PERMISSION_LABELS,
  normalizeOperatorPermissions,
  type OperatorPermission,
} from "@/lib/operatorPermissions";
import { isFactoryResetQuiesced, onFactoryResetQuiesce } from "@/services/factoryResetQuiesce";

/** Client-side mask toggle. Never submits or persists the value. */
function PasswordRevealField({
  id,
  label,
  value,
  onChange,
  autoComplete,
  minLength,
  required,
  readOnly,
  showLabel = true,
}: {
  id: string;
  label: string;
  value: string;
  onChange?: (value: string) => void;
  autoComplete: string;
  minLength?: number;
  required?: boolean;
  readOnly?: boolean;
  showLabel?: boolean;
}) {
  const [visible, setVisible] = useState(false);
  return (
    <div className="space-y-1">
      <Label htmlFor={id} className={showLabel ? "text-xs" : "sr-only"}>
        {label}
      </Label>
      <div className="flex gap-2">
        <Input
          id={id}
          type={visible ? "text" : "password"}
          value={value}
          onChange={readOnly ? undefined : (event) => onChange?.(event.target.value)}
          autoComplete={autoComplete}
          minLength={minLength}
          required={required}
          readOnly={readOnly}
          spellCheck={false}
          aria-label={label}
          className="min-w-0 w-full"
        />
        <Button
          type="button"
          variant="outline"
          size="sm"
          className="shrink-0 px-2"
          onClick={() => setVisible((open) => !open)}
          aria-label={visible ? "Hide password" : "Show password"}
          aria-pressed={visible}
        >
          {visible ? <EyeOff className="h-4 w-4" /> : <Eye className="h-4 w-4" />}
        </Button>
      </div>
    </div>
  );
}

export function SetupUnlockPasswordPanel() {
  const queryClient = useQueryClient();
  const statusQuery = useQuery({
    queryKey: ["settings", "setup-unlock"],
    queryFn: () => settingsApi.setupUnlock(),
    gcTime: 0,
    staleTime: 0,
  });

  const [currentPassword, setCurrentPassword] = useState("");
  const [newPassword, setNewPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");

  const changeMutation = useMutation({
    mutationFn: () =>
      settingsApi.changeSetupUnlock({
        currentPassword,
        newPassword,
        confirmPassword,
      }),
    onSuccess: async () => {
      toast.success("Setup Unlock Password updated.");
      setCurrentPassword("");
      setNewPassword("");
      setConfirmPassword("");
      await queryClient.invalidateQueries({ queryKey: ["settings", "setup-unlock"] });
    },
    onError: (err) => {
      toast.error(
        err instanceof ApiError ? err.message : "Failed to change Setup Unlock Password.",
      );
    },
  });

  const handleSubmit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (newPassword.length < MIN_ADMIN_PASSWORD_LENGTH) {
      toast.error(`New password must be at least ${MIN_ADMIN_PASSWORD_LENGTH} characters.`);
      return;
    }
    if (newPassword !== confirmPassword) {
      toast.error("New passwords do not match.");
      return;
    }
    changeMutation.mutate();
  };

  const configured = statusQuery.data?.configured === true;
  const recoverable = statusQuery.data?.recoverable === true;
  const currentUnlockPassword =
    recoverable && typeof statusQuery.data?.password === "string" ? statusQuery.data.password : "";

  return (
    <ConfigCard title="Setup Security">
      <div className="space-y-1.5">
        <div className="text-[11px] font-medium text-muted-foreground">Stored Unlock Password</div>
        {statusQuery.isLoading ? (
          <p className="text-sm text-muted-foreground">Loading...</p>
        ) : statusQuery.isError ? (
          <p className="text-sm text-destructive">Unable to retrieve the Setup Unlock Password.</p>
        ) : recoverable && currentUnlockPassword ? (
          <PasswordRevealField
            id="setupUnlockCurrentStored"
            label="Stored Unlock Password"
            value={currentUnlockPassword}
            autoComplete="off"
            readOnly
            showLabel={false}
          />
        ) : (
          <p className="text-sm text-muted-foreground">
            This password is stored securely and cannot be displayed.
          </p>
        )}
        <p className="text-xs text-muted-foreground">
          Used with Setup Unlock / Management AP to reconfigure after installation. The Setup Wizard
          is not available from production Admin.
        </p>
      </div>
      <div className="flex items-center justify-between gap-3">
        <span className="text-[11px] font-medium text-muted-foreground">Status</span>
        <ConfigStatusBadge
          label={
            statusQuery.isLoading ? "Loading..." : configured ? "Configured" : "Not configured"
          }
          tone={configured ? "ok" : "unknown"}
        />
      </div>
      <form className="space-y-3" onSubmit={handleSubmit}>
        <PasswordRevealField
          id="setupUnlockCurrent"
          label="Current Password"
          value={currentPassword}
          onChange={setCurrentPassword}
          autoComplete="current-password"
          required
        />
        <PasswordRevealField
          id="setupUnlockNew"
          label="New Setup Unlock Password"
          value={newPassword}
          onChange={setNewPassword}
          autoComplete="new-password"
          minLength={MIN_ADMIN_PASSWORD_LENGTH}
          required
        />
        <PasswordRevealField
          id="setupUnlockConfirm"
          label="Confirm New Setup Unlock Password"
          value={confirmPassword}
          onChange={setConfirmPassword}
          autoComplete="new-password"
          minLength={MIN_ADMIN_PASSWORD_LENGTH}
          required
        />
        <Button
          type="submit"
          size="sm"
          disabled={changeMutation.isPending || statusQuery.isLoading}
        >
          {changeMutation.isPending ? "Saving..." : "Change Setup Unlock Password"}
        </Button>
      </form>
    </ConfigCard>
  );
}

export function OperatorAccountPanel() {
  const queryClient = useQueryClient();
  const [factoryResetQuiesced, setFactoryResetQuiescedState] = useState(isFactoryResetQuiesced);
  useEffect(() => onFactoryResetQuiesce(setFactoryResetQuiescedState), []);

  const statusQuery = useQuery({
    queryKey: ["settings", "operator"],
    queryFn: () => settingsApi.operator(),
    enabled: !factoryResetQuiesced,
  });

  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");
  const [permissions, setPermissions] = useState<OperatorPermission[]>([
    ...DEFAULT_OPERATOR_PERMISSIONS,
  ]);

  useEffect(() => {
    if (statusQuery.data?.configured) {
      setPermissions(normalizeOperatorPermissions(statusQuery.data.permissions));
    }
  }, [statusQuery.data]);

  const createMutation = useMutation({
    mutationFn: () =>
      settingsApi.createOperator({
        username: username.trim(),
        password,
        confirmPassword,
        permissions,
      }),
    onSuccess: async () => {
      toast.success("Operator account created.");
      setUsername("");
      setPassword("");
      setConfirmPassword("");
      await queryClient.invalidateQueries({ queryKey: ["settings", "operator"] });
    },
    onError: (err) => {
      toast.error(err instanceof ApiError ? err.message : "Failed to create operator account.");
    },
  });

  const permsMutation = useMutation({
    mutationFn: () => settingsApi.updateOperatorPermissions(permissions),
    onSuccess: async () => {
      toast.success("Operator sidebar access updated.");
      await queryClient.invalidateQueries({ queryKey: ["settings", "operator"] });
    },
    onError: (err) => {
      toast.error(err instanceof ApiError ? err.message : "Failed to update permissions.");
    },
  });

  const togglePermission = (key: OperatorPermission) => {
    setPermissions((prev) => (prev.includes(key) ? prev.filter((p) => p !== key) : [...prev, key]));
  };

  const handleSubmit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (username.trim().length < 3) {
      toast.error("Username must be at least 3 characters.");
      return;
    }
    if (password.length < MIN_ADMIN_PASSWORD_LENGTH) {
      toast.error(`Password must be at least ${MIN_ADMIN_PASSWORD_LENGTH} characters.`);
      return;
    }
    if (password !== confirmPassword) {
      toast.error("Passwords do not match.");
      return;
    }
    if (permissions.length === 0) {
      toast.error("Select at least one sidebar page for the operator.");
      return;
    }
    createMutation.mutate();
  };

  const configured = statusQuery.data?.configured === true;

  const permissionChecks = (
    <div className="space-y-3">
      <div>
        <h4 className="text-[13px] font-semibold">Sidebar Access</h4>
        <p className="mt-0.5 text-[11px] text-muted-foreground">
          Choose which Admin pages this operator may open. System Settings and Setup remain
          owner-only.
        </p>
      </div>
      <div className="grid grid-cols-1 gap-x-4 gap-y-2 sm:grid-cols-2">
        {OPERATOR_PERMISSION_KEYS.map((key) => (
          <label key={key} className="flex min-w-0 items-center gap-2 text-[13px]">
            <Checkbox
              checked={permissions.includes(key)}
              onCheckedChange={() => togglePermission(key)}
              aria-label={OPERATOR_PERMISSION_LABELS[key]}
            />
            <span className="truncate">{OPERATOR_PERMISSION_LABELS[key]}</span>
          </label>
        ))}
      </div>
    </div>
  );

  const accountStatus = (
    <div className="flex items-center justify-between gap-3">
      <span className="text-[11px] font-medium text-muted-foreground">Status</span>
      {statusQuery.isLoading ? (
        <ConfigStatusBadge label="Checking…" tone="unknown" />
      ) : configured ? (
        <ConfigStatusBadge
          label={
            statusQuery.data?.username ? `Configured (${statusQuery.data.username})` : "Configured"
          }
          tone="ok"
        />
      ) : (
        <ConfigStatusBadge label="No operator configured" tone="unknown" />
      )}
    </div>
  );

  return (
    <ConfigCard
      title="Operator Account"
      description="Optional staff login with assignable sidebar access."
    >
      {configured ? (
        <div className="grid grid-cols-1 items-start gap-4 lg:grid-cols-2">
          <div className="space-y-3 rounded-lg border bg-muted/10 p-4">
            <h4 className="text-[13px] font-semibold">Account Information</h4>
            {accountStatus}
          </div>
          <div className="space-y-3 rounded-lg border bg-muted/10 p-4">
            {permissionChecks}
            <Button
              type="button"
              size="sm"
              disabled={permsMutation.isPending || permissions.length === 0}
              onClick={() => permsMutation.mutate()}
            >
              {permsMutation.isPending ? "Saving…" : "Save sidebar access"}
            </Button>
          </div>
        </div>
      ) : (
        <form className="grid grid-cols-1 items-start gap-4 lg:grid-cols-2" onSubmit={handleSubmit}>
          <div className="space-y-3 rounded-lg border bg-muted/10 p-4">
            <h4 className="text-[13px] font-semibold">Account Information</h4>
            {accountStatus}
            <div className="space-y-1.5">
              <Label
                htmlFor="operatorUsername"
                className="text-[11px] font-medium text-muted-foreground"
              >
                Username
              </Label>
              <Input
                id="operatorUsername"
                value={username}
                onChange={(e) => setUsername(e.target.value)}
                autoComplete="username"
                required
                className="min-w-0 w-full"
              />
            </div>
            <div className="space-y-1.5">
              <Label
                htmlFor="operatorPassword"
                className="text-[11px] font-medium text-muted-foreground"
              >
                Password
              </Label>
              <Input
                id="operatorPassword"
                type="password"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                autoComplete="new-password"
                minLength={MIN_ADMIN_PASSWORD_LENGTH}
                required
                className="min-w-0 w-full"
              />
            </div>
            <div className="space-y-1.5">
              <Label
                htmlFor="operatorConfirm"
                className="text-[11px] font-medium text-muted-foreground"
              >
                Confirm Password
              </Label>
              <Input
                id="operatorConfirm"
                type="password"
                value={confirmPassword}
                onChange={(e) => setConfirmPassword(e.target.value)}
                autoComplete="new-password"
                minLength={MIN_ADMIN_PASSWORD_LENGTH}
                required
                className="min-w-0 w-full"
              />
            </div>
            <Button
              type="submit"
              size="sm"
              disabled={createMutation.isPending || statusQuery.isLoading}
            >
              {createMutation.isPending ? "Creating..." : "Create Operator"}
            </Button>
          </div>
          <div className="space-y-3 rounded-lg border bg-muted/10 p-4">{permissionChecks}</div>
        </form>
      )}
    </ConfigCard>
  );
}
