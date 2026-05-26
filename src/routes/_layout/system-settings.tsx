import { useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Power, RotateCcw, Download, Upload } from "lucide-react";

export const Route = createFileRoute("/_layout/system-settings")({
  component: SystemPage,
});

function SystemPage() {
  const [user, setUser] = useState("admin");
  const [pass, setPass] = useState("");
  const [pass2, setPass2] = useState("");

  return (
    <div>
      <PageHeader title="System Settings" description="Admin and device maintenance" />
      <div className="grid md:grid-cols-2 gap-3">
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Admin Credentials</div>
          <div className="space-y-1">
            <Label className="text-xs">Username</Label>
            <Input value={user} onChange={(e) => setUser(e.target.value)} />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">New Password</Label>
            <Input type="password" value={pass} onChange={(e) => setPass(e.target.value)} />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Confirm Password</Label>
            <Input type="password" value={pass2} onChange={(e) => setPass2(e.target.value)} />
          </div>
          <Button size="sm">Update</Button>
        </div>

        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Backup & Restore</div>
          <p className="text-xs text-muted-foreground">
            Export and import all configuration including promos, vouchers and settings.
          </p>
          <div className="flex gap-2">
            <Button size="sm" variant="outline"><Download className="h-4 w-4" /> Backup</Button>
            <Button size="sm" variant="outline"><Upload className="h-4 w-4" /> Restore</Button>
          </div>
        </div>

        <div className="rounded-md border bg-card p-3 space-y-3 md:col-span-2">
          <div className="text-sm font-medium">Maintenance</div>
          <div className="flex flex-wrap gap-2">
            <Button size="sm" variant="outline">
              <RotateCcw className="h-4 w-4" /> Reboot
            </Button>
            <Button size="sm" variant="destructive">
              <Power className="h-4 w-4" /> Factory Reset
            </Button>
          </div>
          <p className="text-xs text-muted-foreground">
            Factory reset will erase all promos, vouchers, and connection settings.
          </p>
        </div>
      </div>
    </div>
  );
}
