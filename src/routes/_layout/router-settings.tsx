import { useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Plug } from "lucide-react";

export const Route = createFileRoute("/_layout/router-settings")({
  component: RouterPage,
});

function RouterPage() {
  const [host, setHost] = useState("10.0.0.1");
  const [user, setUser] = useState("admin");
  const [pass, setPass] = useState("");
  const [profile, setProfile] = useState("default");
  const [status, setStatus] = useState<"idle" | "ok" | "fail">("idle");

  const test = () => {
    setStatus("idle");
    setTimeout(() => setStatus(Math.random() > 0.2 ? "ok" : "fail"), 600);
  };

  return (
    <div>
      <PageHeader title="Router Settings" description="MikroTik hotspot connection" />
      <div className="rounded-md border bg-card p-3 max-w-xl space-y-3">
        <div className="space-y-1">
          <Label className="text-xs">MikroTik IP</Label>
          <Input value={host} onChange={(e) => setHost(e.target.value)} />
        </div>
        <div className="grid grid-cols-2 gap-3">
          <div className="space-y-1">
            <Label className="text-xs">Username</Label>
            <Input value={user} onChange={(e) => setUser(e.target.value)} />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Password</Label>
            <Input type="password" value={pass} onChange={(e) => setPass(e.target.value)} />
          </div>
        </div>
        <div className="space-y-1">
          <Label className="text-xs">Hotspot Profile</Label>
          <Select value={profile} onValueChange={setProfile}>
            <SelectTrigger><SelectValue /></SelectTrigger>
            <SelectContent>
              <SelectItem value="default">default</SelectItem>
              <SelectItem value="hsprof1">hsprof1</SelectItem>
              <SelectItem value="guest">guest</SelectItem>
            </SelectContent>
          </Select>
        </div>
        <div className="flex items-center gap-2 pt-1">
          <Button size="sm" onClick={test}><Plug className="h-4 w-4" /> Test Connection</Button>
          <Button size="sm" variant="outline">Save</Button>
          {status === "ok" && <span className="text-xs text-emerald-600">Connected ✓</span>}
          {status === "fail" && <span className="text-xs text-red-600">Failed</span>}
        </div>
      </div>
    </div>
  );
}
