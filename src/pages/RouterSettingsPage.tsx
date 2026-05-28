import { useState, useEffect } from "react";
import { useQuery, useMutation } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Plug } from "lucide-react";
import { routerApi } from "@/services/router";
import { toast } from "sonner";

export default function RouterSettingsPage() {
  const { data: config } = useQuery({
    queryKey: ["router", "settings"],
    queryFn: () => routerApi.settings(),
  });
  const [host, setHost] = useState("10.0.0.1");
  const [user, setUser] = useState("admin");
  const [pass, setPass] = useState("");
  const [profile, setProfile] = useState("default");
  const [status, setStatus] = useState<"idle" | "ok" | "fail">("idle");

  useEffect(() => {
    if (config) {
      setHost(config.host);
      setUser(config.username);
      setPass(config.password);
      setProfile(config.profile);
    }
  }, [config]);

  const saveMutation = useMutation({
    mutationFn: () => routerApi.save({ host, username: user, password: pass, profile }),
    onSuccess: () => toast.success("Router settings saved"),
  });

  const testMutation = useMutation({
    mutationFn: () => routerApi.test({ host, username: user, password: pass, profile }),
    onMutate: () => setStatus("idle"),
    onSuccess: (res) => setStatus(res.ok ? "ok" : "fail"),
    onError: () => setStatus("fail"),
  });

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
            <SelectTrigger>
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="default">default</SelectItem>
              <SelectItem value="hsprof1">hsprof1</SelectItem>
              <SelectItem value="guest">guest</SelectItem>
            </SelectContent>
          </Select>
        </div>
        <div className="flex items-center gap-2 pt-1">
          <Button size="sm" onClick={() => testMutation.mutate()}>
            <Plug className="h-4 w-4" /> Test Connection
          </Button>
          <Button size="sm" variant="outline" onClick={() => saveMutation.mutate()}>
            Save
          </Button>
          {status === "ok" && <span className="text-xs text-emerald-600">Connected ✓</span>}
          {status === "fail" && <span className="text-xs text-red-600">Failed</span>}
        </div>
      </div>
    </div>
  );
}
