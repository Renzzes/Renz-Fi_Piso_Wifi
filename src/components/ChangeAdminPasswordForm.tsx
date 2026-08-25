import { type FormEvent, useState } from "react";
import { Button } from "@/components/ui/button";
import { Label } from "@/components/ui/label";
import { PasswordField } from "@/components/PasswordField";
import { authApi } from "@/services/auth";
import { ApiError } from "@/services/api";
import { toast } from "sonner";

export const MIN_ADMIN_PASSWORD_LENGTH = 8;

type ChangeAdminPasswordFormProps = {
  onSuccess: () => void | Promise<void>;
  submitLabel?: string;
  defaultOldPassword?: string;
};

export function ChangeAdminPasswordForm({
  onSuccess,
  submitLabel = "Change Password",
  defaultOldPassword = "",
}: ChangeAdminPasswordFormProps) {
  const [oldPassword, setOldPassword] = useState(defaultOldPassword);
  const [newPassword, setNewPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");
  const [saving, setSaving] = useState(false);

  const handleSubmit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (newPassword.length < MIN_ADMIN_PASSWORD_LENGTH) {
      toast.error(`New password must be at least ${MIN_ADMIN_PASSWORD_LENGTH} characters.`);
      return;
    }
    if (newPassword !== confirmPassword) {
      toast.error("New passwords do not match.");
      return;
    }

    setSaving(true);
    try {
      await authApi.changePassword({ oldPassword, newPassword });
      toast.success("Admin password changed.");
      setOldPassword("");
      setNewPassword("");
      setConfirmPassword("");
      await onSuccess();
    } catch (err) {
      toast.error(err instanceof ApiError ? err.message : "Failed to change password.");
    } finally {
      setSaving(false);
    }
  };

  return (
    <form className="space-y-3" onSubmit={handleSubmit}>
      <div className="space-y-1">
        <Label htmlFor="currentPassword" className="text-xs">
          Current Password
        </Label>
        <PasswordField
          id="currentPassword"
          value={oldPassword}
          onChange={(event) => setOldPassword(event.target.value)}
          autoComplete="current-password"
          placeholder={defaultOldPassword || "Enter current password"}
          required
        />
        <p className="text-[11px] text-muted-foreground">
          The appliance stores a password hash only — your saved password cannot be
          retrieved for display. Enter it here to confirm the change.
        </p>
      </div>
      <div className="space-y-1">
        <Label htmlFor="newPassword" className="text-xs">
          New Password
        </Label>
        <PasswordField
          id="newPassword"
          value={newPassword}
          onChange={(event) => setNewPassword(event.target.value)}
          autoComplete="new-password"
          minLength={MIN_ADMIN_PASSWORD_LENGTH}
          required
        />
      </div>
      <div className="space-y-1">
        <Label htmlFor="confirmPassword" className="text-xs">
          Confirm New Password
        </Label>
        <PasswordField
          id="confirmPassword"
          value={confirmPassword}
          onChange={(event) => setConfirmPassword(event.target.value)}
          autoComplete="new-password"
          minLength={MIN_ADMIN_PASSWORD_LENGTH}
          required
        />
      </div>
      <p className="text-xs text-muted-foreground">
        Minimum {MIN_ADMIN_PASSWORD_LENGTH} characters.
      </p>
      <Button type="submit" size="sm" disabled={saving}>
        {saving ? "Saving..." : submitLabel}
      </Button>
    </form>
  );
}
