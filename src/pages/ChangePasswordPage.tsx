import { BrandLogo } from "@/components/BrandLogo";
import { ChangeAdminPasswordForm } from "@/components/ChangeAdminPasswordForm";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { ThemeToggle } from "@/components/ThemeToggle";

type ChangePasswordPageProps = {
  onComplete: () => void | Promise<void>;
  onLogout: () => void | Promise<void>;
};

export default function ChangePasswordPage({ onComplete, onLogout }: ChangePasswordPageProps) {
  return (
    <div className="relative flex min-h-screen items-center justify-center overflow-hidden bg-background px-4 py-8">
      <div
        aria-hidden
        className="pointer-events-none absolute inset-0 bg-primary/10 [mask-image:radial-gradient(ellipse_at_50%_0%,black,transparent_55%)]"
      />
      <div className="absolute right-3 top-3 z-10">
        <ThemeToggle />
      </div>
      <Card className="relative z-10 w-full max-w-md border-border/80 bg-card/95 shadow-lg backdrop-blur-sm">
        <CardHeader className="items-center space-y-3 text-center">
          <BrandLogo height={72} />
          <CardTitle className="text-2xl">Change Password</CardTitle>
          <CardDescription>
            The default admin password must be changed before you can use the dashboard.
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <ChangeAdminPasswordForm defaultOldPassword="admin" onSuccess={onComplete} />
          <Button type="button" variant="outline" className="w-full" onClick={() => void onLogout()}>
            Sign Out
          </Button>
        </CardContent>
      </Card>
    </div>
  );
}
