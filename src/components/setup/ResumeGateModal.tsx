import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { useProvisioning } from "@/contexts/ProvisioningContext";

export function ResumeGateModal() {
  const {
    resumePromptOpen,
    resumeElapsedMinutes,
    continueResume,
    startOver,
    loading,
  } = useProvisioning();

  const minutes = resumeElapsedMinutes ?? 0;

  return (
    <AlertDialog open={resumePromptOpen}>
      <AlertDialogContent>
        <AlertDialogHeader>
          <AlertDialogTitle>Resume installation?</AlertDialogTitle>
          <AlertDialogDescription>
            Resume installation started {minutes} minute{minutes === 1 ? "" : "s"} ago?
            Your progress has been saved on this appliance.
          </AlertDialogDescription>
        </AlertDialogHeader>
        <AlertDialogFooter>
          <AlertDialogCancel disabled={loading} onClick={() => void startOver()}>
            Start over
          </AlertDialogCancel>
          <AlertDialogAction disabled={loading} onClick={continueResume}>
            Continue
          </AlertDialogAction>
        </AlertDialogFooter>
      </AlertDialogContent>
    </AlertDialog>
  );
}
