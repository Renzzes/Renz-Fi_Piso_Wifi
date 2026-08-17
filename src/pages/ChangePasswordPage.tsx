import logoSrc from "../../public/Logo.png";
import { PageHeader } from "@/components/PageHeader";
import { ChangeAdminPasswordForm } from "@/components/ChangeAdminPasswordForm";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";

type ChangePasswordPageProps = {
  onComplete: () => void | Promise<void>;
  onLogout: () => void | Promise<void>;
};

export default function ChangePasswordPage({ onComplete, onLogout }: ChangePasswordPageProps) {
  return (
    <div className="min-h-screen bg-muted/40 px-4 py-8 flex items-center justify-center">
      <Card className="w-full max-w-md">
        <CardHeader className="items-center text-center">
          <img src={logoSrc} alt="Renz-Fi logo" className="mb-2 h-24 w-auto object-contain" />
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
