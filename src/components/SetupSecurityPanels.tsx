import { type FormEvent, useEffect, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Eye, EyeOff } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
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
      toast.error(err instanceof ApiError ? err.message : "Failed to change Setup Unlock Password.");
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
    recoverable && typeof statusQuery.data?.password === "string"
      ? statusQuery.data.password
      : "";

  return (
    <div className="rounded-md border bg-card p-3 space-y-3 max-w-xl">
      <div className="text-sm font-medium">Setup Security</div>
      <div className="space-y-1">
        <div className="text-xs font-medium">Stored unlock password</div>
        {statusQuery.isLoading ? (
          <p className="text-sm text-muted-foreground">Loading...</p>
        ) : statusQuery.isError ? (
          <p className="text-sm text-destructive">
            Unable to retrieve the Setup Unlock Password.
          </p>
        ) : recoverable && currentUnlockPassword ? (
          <PasswordRevealField
            id="setupUnlockCurrentStored"
            label="Stored unlock password"
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
          Used with Setup Unlock / Management AP to reconfigure after installation.
          The Setup Wizard is not available from production Admin.
        </p>
      </div>
      <div className="text-sm">
        Status:{" "}
        <span className={configured ? "text-emerald-600 dark:text-emerald-400" : "text-muted-foreground"}>
          {statusQuery.isLoading ? "Loading..." : configured ? "Configured" : "Not configured"}
        </span>
      </div>
      <form className="space-y-3" onSubmit={handleSubmit}>
        <PasswordRevealField
          id="setupUnlockCurrent"
          label="Current password (to authorize change)"
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
        <Button type="submit" size="sm" disabled={changeMutation.isPending || statusQuery.isLoading}>
          {changeMutation.isPending ? "Saving..." : "Change Setup Unlock Password"}
        </Button>
      </form>
    </div>
  );
}

export function OperatorAccountPanel() {
  const queryClient = useQueryClient();
  const statusQuery = useQuery({
    queryKey: ["settings", "operator"],
    queryFn: () => settingsApi.operator(),
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
    setPermissions((prev) =>
      prev.includes(key) ? prev.filter((p) => p !== key) : [...prev, key],
    );
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
    <div className="space-y-2 rounded-md border p-2">
      <div className="text-xs font-medium">Sidebar access</div>
      <p className="text-[11px] text-muted-foreground">
        Choose which Admin pages this operator may open. System Settings and Setup remain owner-only.
      </p>
      <div className="grid grid-cols-1 sm:grid-cols-2 gap-1.5">
        {OPERATOR_PERMISSION_KEYS.map((key) => (
          <label key={key} className="flex items-center gap-2 text-xs">
            <input
              type="checkbox"
              checked={permissions.includes(key)}
              onChange={() => togglePermission(key)}
            />
            {OPERATOR_PERMISSION_LABELS[key]}
          </label>
        ))}
      </div>
    </div>
  );

  return (
    <div className="rounded-md border bg-card p-3 space-y-3 max-w-xl">
      <div className="text-sm font-medium">Operator Account</div>
      <p className="text-xs text-muted-foreground">
        Optional staff login with assignable sidebar access.
      </p>
      <div className="text-sm">
        Status:{" "}
        {statusQuery.isLoading ? (
          <span className="text-muted-foreground">Checking…</span>
        ) : configured ? (
          <span className="text-emerald-600 dark:text-emerald-400">
            Operator configured
            {statusQuery.data?.username ? ` (${statusQuery.data.username})` : ""}
          </span>
        ) : (
          <span className="text-muted-foreground">No operator configured</span>
        )}
      </div>
      {configured ? (
        <div className="space-y-3">
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
      ) : (
        <form className="space-y-3" onSubmit={handleSubmit}>
          <div className="space-y-1">
            <Label htmlFor="operatorUsername" className="text-xs">
              Username
            </Label>
            <Input
              id="operatorUsername"
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              autoComplete="username"
              required
            />
          </div>
          <div className="space-y-1">
            <Label htmlFor="operatorPassword" className="text-xs">
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
            />
          </div>
          <div className="space-y-1">
            <Label htmlFor="operatorConfirm" className="text-xs">
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
            />
          </div>
          {permissionChecks}
          <Button type="submit" size="sm" disabled={createMutation.isPending || statusQuery.isLoading}>
            {createMutation.isPending ? "Creating..." : "Create Operator"}
          </Button>
        </form>
      )}
    </div>
  );
}
