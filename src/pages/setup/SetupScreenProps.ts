/**
 * Frozen screen contract — every setup screen implements SetupScreenProps.
 * See SETUP_WIZARD_UI_CONTRACT.md
 */
import type { ComponentType } from "react";
import type {
  InstallationSession,
  InstallationStatus,
  WorkflowStep,
} from "@/types/provisioning";

/** Alias used by screen components (matches workflow API naming). */
export type Installation = InstallationStatus;

export interface SetupScreenProps {
  installation: Installation;
  session: InstallationSession;
  workflowStep: WorkflowStep;
  loading: boolean;
  onNext(): Promise<void>;
  onBack(): void;
}

export type SetupScreenComponent = ComponentType<SetupScreenProps>;

/** Build props from context values — keeps null-handling in one place. */
export function buildSetupScreenProps(input: {
  installation: Installation;
  workflowStep: WorkflowStep;
  loading: boolean;
  onNext: () => void | Promise<void>;
  onBack: () => void;
}): SetupScreenProps {
  return {
    installation: input.installation,
    session: input.installation.session ?? {},
    workflowStep: input.workflowStep,
    loading: input.loading,
    onNext: async () => {
      await input.onNext();
    },
    onBack: input.onBack,
  };
}
