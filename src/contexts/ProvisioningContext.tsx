import {
  createContext,
  useCallback,
  useContext,
  useMemo,
  useReducer,
  type ReactNode,
} from "react";
import { screenFromWorkflowStep, type SetupScreenId } from "@/components/setup/stepRouter";
import { useProvisioningEvents } from "@/hooks/provisioning/useProvisioningEvents";
import { provisioningClient } from "@/services/provisioning/provisioningClient";
import { ApiError } from "@/services/api";
import {
  fetchInstallationState,
  isProductionInstallationState,
} from "@/lib/installationState";
import type {
  InstallationStatus,
  ProvisioningProgress,
  ResumeResponse,
  WorkflowStep,
} from "@/types/provisioning";

export type ProvisioningContextValue = {
  /** Authoritative backend snapshot */
  installation: InstallationStatus | null;
  workflowStep: WorkflowStep | null;
  /** Active screen (may differ from workflowStep when navigating back) */
  currentScreen: SetupScreenId;
  progress: ProvisioningProgress | null;
  session: InstallationStatus["session"] | null;
  loading: boolean;
  bootstrapping: boolean;
  error: string | null;
  sseConnected: boolean;
  resumePromptOpen: boolean;
  resumeElapsedMinutes: number | null;
  /** Apply resume / workflow API payload */
  applyWorkflowData: (data: ResumeResponse) => void;
  setCurrentScreen: (screen: SetupScreenId) => void;
  bootstrap: () => Promise<void>;
  continueResume: () => void;
  startOver: () => Promise<void>;
  abortSetup: () => Promise<void>;
  clearError: () => void;
  setLoading: (loading: boolean) => void;
};

type State = {
  installation: InstallationStatus | null;
  workflowStep: WorkflowStep | null;
  currentScreen: SetupScreenId;
  progress: ProvisioningProgress | null;
  loading: boolean;
  bootstrapping: boolean;
  error: string | null;
  sseConnected: boolean;
  resumePromptOpen: boolean;
  resumeElapsedMinutes: number | null;
};

type Action =
  | { type: "BOOTSTRAP_START" }
  | { type: "BOOTSTRAP_SUCCESS"; payload: ResumeResponse }
  | { type: "BOOTSTRAP_ERROR"; message: string }
  | { type: "SET_LOADING"; loading: boolean }
  | { type: "SET_ERROR"; message: string | null }
  | { type: "APPLY_WORKFLOW"; payload: ResumeResponse }
  | { type: "SET_SCREEN"; screen: SetupScreenId }
  | { type: "SET_PROGRESS"; progress: ProvisioningProgress }
  | { type: "STATE_CHANGED"; installation: InstallationStatus }
  | { type: "SSE_CONNECTED"; connected: boolean }
  | { type: "RESUME_PROMPT_DISMISS" }
  | { type: "RESUME_PROMPT"; elapsedMinutes: number };

const initialState: State = {
  installation: null,
  workflowStep: null,
  currentScreen: "welcome",
  progress: null,
  loading: false,
  bootstrapping: true,
  error: null,
  sseConnected: false,
  resumePromptOpen: false,
  resumeElapsedMinutes: null,
};

function applyWorkflowPayload(state: State, data: ResumeResponse): State {
  const workflowStep = data.workflowStep;
  const screen = screenFromWorkflowStep(workflowStep);
  const progressPercent = data.installation?.progressPercent ?? 0;

  return {
    ...state,
    installation: data.installation,
    workflowStep,
    currentScreen: screen,
    progress: state.progress
      ? { ...state.progress, percent: progressPercent }
      : {
          step: workflowStep,
          percent: progressPercent,
          message: "",
        },
    error: null,
  };
}

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case "BOOTSTRAP_START":
      return { ...state, bootstrapping: true, error: null };
    case "BOOTSTRAP_SUCCESS": {
      const next = applyWorkflowPayload(state, action.payload);
      const showResume = Boolean(action.payload.resumePrompt) && Boolean(action.payload.resumed);
      return {
        ...next,
        bootstrapping: false,
        resumePromptOpen: showResume,
        resumeElapsedMinutes: action.payload.elapsedMinutes ?? null,
      };
    }
    case "BOOTSTRAP_ERROR":
      return {
        ...state,
        bootstrapping: false,
        error: action.message,
      };
    case "SET_LOADING":
      return { ...state, loading: action.loading };
    case "SET_ERROR":
      return { ...state, error: action.message, loading: false };
    case "APPLY_WORKFLOW":
      return applyWorkflowPayload(state, action.payload);
    case "SET_SCREEN":
      return { ...state, currentScreen: action.screen };
    case "SET_PROGRESS":
      return { ...state, progress: action.progress };
    case "STATE_CHANGED":
      if (!state.installation) return state;
      return {
        ...state,
        installation: {
          ...state.installation,
          state: action.installation.state ?? state.installation.state,
          nextState: action.installation.nextState ?? state.installation.nextState,
          progressPercent:
            action.installation.progressPercent ?? state.installation.progressPercent,
        },
        progress: state.progress
          ? {
              ...state.progress,
              percent:
                action.installation.progressPercent ?? state.progress.percent,
            }
          : state.progress,
      };
    case "SSE_CONNECTED":
      return { ...state, sseConnected: action.connected };
    case "RESUME_PROMPT_DISMISS":
      return { ...state, resumePromptOpen: false };
    case "RESUME_PROMPT":
      return {
        ...state,
        resumePromptOpen: true,
        resumeElapsedMinutes: action.elapsedMinutes,
      };
    default:
      return state;
  }
}

const ProvisioningContext = createContext<ProvisioningContextValue | null>(null);

export function ProvisioningProvider({
  children,
  enabled,
  onReady,
  onAborted,
}: {
  children: ReactNode;
  enabled: boolean;
  onReady?: () => void;
  onAborted?: () => void;
}) {
  const [state, dispatch] = useReducer(reducer, initialState);

  const applyWorkflowData = useCallback((data: ResumeResponse) => {
    dispatch({ type: "APPLY_WORKFLOW", payload: data });
  }, []);

  const bootstrap = useCallback(async () => {
    dispatch({ type: "BOOTSTRAP_START" });
    try {
      // Production appliances must never hit /api/provisioning/* (those routes
      // are not registered on the ESP32 production plane → 404 spam).
      const installationState = await fetchInstallationState();
      if (isProductionInstallationState(installationState)) {
        dispatch({ type: "BOOTSTRAP_ERROR", message: "Setup is already complete." });
        onReady?.();
        return;
      }

      const data = await provisioningClient.resume();
      dispatch({ type: "BOOTSTRAP_SUCCESS", payload: data });
      if (data.installation?.ready || data.ready) {
        onReady?.();
      }
    } catch (err) {
      // Production plane does not register /api/provisioning/* — surface a
      // clear message instead of raw "API endpoint not found".
      let message = "Unable to load installation status.";
      if (err instanceof ApiError) {
        if (
          err.code === "NOT_FOUND" ||
          /endpoint not found/i.test(err.message)
        ) {
          message =
            "Setup is unavailable on this connection (production mode). Use Setup Unlock or the Management AP to reconfigure.";
        } else {
          message = err.message;
        }
      }
      dispatch({ type: "BOOTSTRAP_ERROR", message });
    }
  }, [onReady]);

  const continueResume = useCallback(() => {
    dispatch({ type: "RESUME_PROMPT_DISMISS" });
  }, []);

  const startOver = useCallback(async () => {
    dispatch({ type: "SET_LOADING", loading: true });
    try {
      const data = await provisioningClient.factoryReset();
      dispatch({ type: "APPLY_WORKFLOW", payload: data });
      dispatch({ type: "RESUME_PROMPT_DISMISS" });
    } catch (err) {
      const message =
        err instanceof ApiError ? err.message : "Unable to reset installation.";
      dispatch({ type: "SET_ERROR", message });
    } finally {
      dispatch({ type: "SET_LOADING", loading: false });
    }
  }, []);

  const abortSetup = useCallback(async () => {
    dispatch({ type: "SET_LOADING", loading: true });
    try {
      await provisioningClient.abort();
      onAborted?.();
    } catch (err) {
      const message =
        err instanceof ApiError ? err.message : "Unable to pause setup.";
      dispatch({ type: "SET_ERROR", message });
    } finally {
      dispatch({ type: "SET_LOADING", loading: false });
    }
  }, [onAborted]);

  const setCurrentScreen = useCallback((screen: SetupScreenId) => {
    dispatch({ type: "SET_SCREEN", screen });
  }, []);

  const clearError = useCallback(() => {
    dispatch({ type: "SET_ERROR", message: null });
  }, []);

  const setLoading = useCallback((loading: boolean) => {
    dispatch({ type: "SET_LOADING", loading });
  }, []);

  useProvisioningEvents(enabled, {
    onProgress: (payload) => {
      dispatch({ type: "SET_PROGRESS", payload });
    },
    onStateChanged: (payload) => {
      dispatch({
        type: "STATE_CHANGED",
        installation: {
          state: payload.state,
          progressPercent: payload.progressPercent ?? 0,
          nextState: payload.nextState,
        } as InstallationStatus,
      });
    },
    onCompleted: () => {
      onReady?.();
    },
    onAborted: () => {
      onAborted?.();
    },
    onConnectionChange: (connected) => {
      dispatch({ type: "SSE_CONNECTED", connected });
    },
  });

  const value = useMemo<ProvisioningContextValue>(
    () => ({
      installation: state.installation,
      workflowStep: state.workflowStep,
      currentScreen: state.currentScreen,
      progress: state.progress,
      session: state.installation?.session ?? null,
      loading: state.loading,
      bootstrapping: state.bootstrapping,
      error: state.error,
      sseConnected: state.sseConnected,
      resumePromptOpen: state.resumePromptOpen,
      resumeElapsedMinutes: state.resumeElapsedMinutes,
      applyWorkflowData,
      setCurrentScreen,
      bootstrap,
      continueResume,
      startOver,
      abortSetup,
      clearError,
      setLoading,
    }),
    [
      state,
      applyWorkflowData,
      setCurrentScreen,
      bootstrap,
      continueResume,
      startOver,
      abortSetup,
      clearError,
      setLoading,
    ],
  );

  return (
    <ProvisioningContext.Provider value={value}>{children}</ProvisioningContext.Provider>
  );
}

export function useProvisioning() {
  const ctx = useContext(ProvisioningContext);
  if (!ctx) {
    throw new Error("useProvisioning must be used within ProvisioningProvider");
  }
  return ctx;
}
