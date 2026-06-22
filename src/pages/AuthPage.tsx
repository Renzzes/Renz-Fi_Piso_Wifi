import { type FormEvent, useState } from "react";
import logoSrc from "../../public/Logo.png";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Checkbox } from "@/components/ui/checkbox";
import { getRememberedIp } from "@/services/auth";
import { getDefaultAdminAddress } from "@/services/embeddedApi";

type AuthPageProps = {
  onConnect: (
    ipAddress: string,
    password: string,
    rememberIpAddress: boolean,
  ) => void | Promise<void>;
  connecting?: boolean;
};

export default function AuthPage({ onConnect, connecting }: AuthPageProps) {
  const [ipAddress, setIpAddress] = useState(() => getRememberedIp() ?? getDefaultAdminAddress());
  const [password, setPassword] = useState("admin");
  const [rememberIpAddress, setRememberIpAddress] = useState(true);

  const handleSubmit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    void onConnect(ipAddress.trim() || "10.10.10.1", password, rememberIpAddress);
  };

  return (
    <div className="min-h-screen bg-muted/40 px-4 py-8 flex items-center justify-center">
      <Card className="w-full max-w-md">
        <CardHeader className="items-center text-center">
          <img src={logoSrc} alt="Renz-Fi logo" className="mb-2 h-32 w-auto object-contain" />
          <CardTitle className="text-2xl">Welcome</CardTitle>
          <CardDescription>Enter the admin IP address to open the dashboard.</CardDescription>
        </CardHeader>
        <CardContent>
          <form className="space-y-4" onSubmit={handleSubmit}>
            <div className="space-y-2">
              <Label htmlFor="ipAddress">Admin IP address</Label>
              <Input
                id="ipAddress"
                value={ipAddress}
                onChange={(event) => setIpAddress(event.target.value)}
                placeholder="10.10.10.1"
                required
                autoComplete="off"
              />
              <p className="text-xs text-muted-foreground">Use 10.10.10.1 for testing.</p>
            </div>
            <div className="space-y-2">
              <Label htmlFor="password">Admin password</Label>
              <Input
                id="password"
                type="password"
                value={password}
                onChange={(event) => setPassword(event.target.value)}
                placeholder="Enter admin password"
                required
                autoComplete="current-password"
              />
            </div>
            <div className="flex items-center gap-2">
              <Checkbox
                id="rememberIpAddress"
                checked={rememberIpAddress}
                onCheckedChange={(v) => setRememberIpAddress(v === true)}
              />
              <Label htmlFor="rememberIpAddress" className="text-sm font-normal cursor-pointer">
                Remember IP address on this device
              </Label>
            </div>
            <p className="text-xs text-muted-foreground">
              Admin login uses a browser session cookie only. Closing the browser requires signing
              in again.
            </p>
            <Button type="submit" className="w-full" disabled={connecting}>
              {connecting ? "Connecting…" : "Connect to Admin"}
            </Button>
          </form>
        </CardContent>
      </Card>
    </div>
  );
}
