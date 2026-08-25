import { type FormEvent, useState } from "react";
import logoSrc from "../../public/Logo.png";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Checkbox } from "@/components/ui/checkbox";
import { PasswordField } from "@/components/PasswordField";
import { SupportContactLinks } from "@/components/SupportContactLinks";
import { ThemeToggle } from "@/components/ThemeToggle";
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
    void onConnect(ipAddress.trim() || "10.40.0.2", password, rememberIpAddress);
  };

  return (
    <div className="relative flex min-h-screen items-center justify-center bg-muted/40 px-4 py-8">
      <div className="absolute right-3 top-3">
        <ThemeToggle />
      </div>
      <Card className="w-full max-w-md">
        <CardHeader className="items-center text-center">
          <img src={logoSrc} alt="Renz-Fi logo" className="mb-2 h-32 w-auto object-contain" />
          <CardTitle className="text-2xl">Connect to Your Renz-Fi Appliance</CardTitle>
          <CardDescription>Enter your appliance&rsquo;s IP address and password to continue.</CardDescription>
        </CardHeader>
        <CardContent>
          <form className="space-y-4" onSubmit={handleSubmit}>
            <div className="space-y-2">
              <Label htmlFor="ipAddress">Admin IP Address</Label>
              <Input
                id="ipAddress"
                value={ipAddress}
                onChange={(event) => setIpAddress(event.target.value)}
                placeholder="10.40.0.2"
                required
                autoComplete="off"
              />
              <p className="text-xs text-muted-foreground">Default appliance IP: 10.40.0.2</p>
            </div>
            <div className="space-y-2">
              <Label htmlFor="password">Password</Label>
              <PasswordField
                id="password"
                value={password}
                onChange={(event) => setPassword(event.target.value)}
                placeholder="Enter your password"
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
                Remember this appliance
              </Label>
            </div>
            <p className="text-xs text-muted-foreground">
              Your session stays signed in on this browser until you sign out or close the
              browser.
            </p>
            <Button type="submit" className="w-full" disabled={connecting}>
              {connecting ? "Connecting…" : "Connect"}
            </Button>
            <SupportContactLinks className="mt-2" compact />
          </form>
        </CardContent>
      </Card>
    </div>
  );
}
