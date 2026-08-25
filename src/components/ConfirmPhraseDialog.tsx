import { useEffect, useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Progress } from "@/components/ui/progress";
import { Loader2 } from "lucide-react";

type ConfirmPhraseDialogProps = {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  title: string;
  description: React.ReactNode;
  confirmPhrase: string;
  confirmLabel: string;
  pending?: boolean;
  destructive?: boolean;
  /** 0–100 while pending; omit for indeterminate pulse */
  progress?: number | null;
  progressLabel?: string;
  onConfirm: () => void;
};

export function ConfirmPhraseDialog({
  open,
  onOpenChange,
  title,
  description,
  confirmPhrase,
  confirmLabel,
  pending = false,
  destructive = false,
  progress = null,
  progressLabel,
  onConfirm,
}: ConfirmPhraseDialogProps) {
  const [value, setValue] = useState("");

  useEffect(() => {
    if (!open) setValue("");
  }, [open]);

  const canConfirm = value === confirmPhrase && !pending;
  const progressValue =
    typeof progress === "number" ? Math.max(0, Math.min(100, progress)) : pending ? 15 : 0;

  return (
    <Dialog
      open={open}
      onOpenChange={(next) => {
        if (pending) return;
        onOpenChange(next);
      }}
    >
      <DialogContent
        className={
          pending
            ? "sm:max-w-md [&>button.absolute]:pointer-events-none [&>button.absolute]:opacity-0"
            : "sm:max-w-md"
        }
        onPointerDownOutside={(event) => {
          if (pending) event.preventDefault();
        }}
        onEscapeKeyDown={(event) => {
          if (pending) event.preventDefault();
        }}
      >
        <DialogHeader>
          <DialogTitle>{title}</DialogTitle>
          {!pending ? (
            <DialogDescription asChild>
              <div className="space-y-3 text-sm text-muted-foreground">{description}</div>
            </DialogDescription>
          ) : (
            <DialogDescription className="sr-only">
              {progressLabel || "Operation in progress"}
            </DialogDescription>
          )}
        </DialogHeader>

        {pending ? (
          <div className="space-y-4 py-2">
            <div className="flex flex-col items-center gap-3 text-center">
              <Loader2 className="h-9 w-9 animate-spin text-destructive" aria-hidden />
              <div className="space-y-1">
                <p className="text-sm font-medium text-foreground">
                  {progressLabel || "Working… Please wait."}
                </p>
                <p className="text-xs text-muted-foreground">
                  Do not close this page or power off the device.
                </p>
              </div>
            </div>
            <Progress
              value={progressValue}
              className={destructive ? "[&>div]:bg-destructive" : undefined}
            />
            {typeof progress === "number" ? (
              <p className="text-center text-xs tabular-nums text-muted-foreground">
                {progressValue}%
              </p>
            ) : null}
          </div>
        ) : (
          <div className="space-y-2">
            <label className="text-xs font-medium" htmlFor="confirm-phrase">
              Type <span className="font-mono">{confirmPhrase}</span> to continue
            </label>
            <Input
              id="confirm-phrase"
              value={value}
              onChange={(event) => setValue(event.target.value)}
              autoComplete="off"
              spellCheck={false}
            />
          </div>
        )}

        <DialogFooter className="flex-col gap-2 sm:flex-col">
          {!pending ? (
            <>
              <Button
                className="w-full"
                variant={destructive ? "destructive" : "default"}
                disabled={!canConfirm}
                onClick={onConfirm}
              >
                {confirmLabel}
              </Button>
              <Button
                className="w-full"
                variant="outline"
                onClick={() => onOpenChange(false)}
              >
                Cancel
              </Button>
            </>
          ) : (
            <Button className="w-full" variant="outline" disabled>
              Working…
            </Button>
          )}
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
