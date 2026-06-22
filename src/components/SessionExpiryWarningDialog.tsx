import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";

type SessionExpiryWarningDialogProps = {
  open: boolean;
  onStayLoggedIn: () => void;
  onLogout: () => void;
};

export function SessionExpiryWarningDialog({
  open,
  onStayLoggedIn,
  onLogout,
}: SessionExpiryWarningDialogProps) {
  return (
    <Dialog open={open} onOpenChange={() => undefined}>
      <DialogContent
        className="sm:max-w-md"
        onEscapeKeyDown={(event) => event.preventDefault()}
        onPointerDownOutside={(event) => event.preventDefault()}
      >
        <DialogHeader>
          <DialogTitle>Session Expiring</DialogTitle>
          <DialogDescription>
            Your session will expire in 2 minutes.
          </DialogDescription>
        </DialogHeader>
        <DialogFooter className="gap-2 sm:gap-0">
          <Button type="button" variant="outline" onClick={onLogout}>
            Logout
          </Button>
          <Button type="button" onClick={onStayLoggedIn}>
            Stay Logged In
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
