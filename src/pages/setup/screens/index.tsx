import { lazy } from "react";
import type { SetupScreenComponent } from "@/pages/setup/SetupScreenProps";
import type { SetupScreenId } from "@/components/setup/stepRouter";

function lazyScreen(
  loader: () => Promise<{ [key: string]: SetupScreenComponent }>,
  exportName: string,
): SetupScreenComponent {
  return lazy(() =>
    loader().then((module) => ({
      default: module[exportName],
    })),
  );
}

const WelcomeScreen = lazyScreen(() => import("./WelcomeScreen"), "WelcomeScreen");
// Phase 6C: router_detection slot now renders the installer-friendly NetworkTypeScreen.
// RouterDetectionScreen is retained on disk but is no longer shown in the main flow.
const NetworkTypeScreen = lazyScreen(
  () => import("./NetworkTypeScreen"),
  "NetworkTypeScreen",
);
// Phase 6C: driver_selection slot is a silent passthrough — NetworkTypeScreen
// calls selectDriver() and jumps to router_connection directly.
const DriverSelectionScreen = lazyScreen(
  () => import("./DriverSelectionScreen"),
  "DriverSelectionScreen",
);
const RouterConnectionScreen = lazyScreen(
  () => import("./RouterConnectionScreen"),
  "RouterConnectionScreen",
);
const PortalConfigurationScreen = lazyScreen(
  () => import("./PortalConfigurationScreen"),
  "PortalConfigurationScreen",
);
const CoinConfigurationScreen = lazyScreen(
  () => import("./CoinConfigurationScreen"),
  "CoinConfigurationScreen",
);
const ValidationScreen = lazyScreen(() => import("./ValidationScreen"), "ValidationScreen");
const SummaryScreen = lazyScreen(() => import("./SummaryScreen"), "SummaryScreen");
const CompleteScreen = lazyScreen(() => import("./CompleteScreen"), "CompleteScreen");

/** workflowStep-driven screen registry — lazy-loaded per step for smaller initial bundle. */
export const SETUP_SCREEN_COMPONENTS: Record<SetupScreenId, SetupScreenComponent> = {
  welcome: WelcomeScreen,
  router_detection: NetworkTypeScreen,
  driver_selection: DriverSelectionScreen,
  router_connection: RouterConnectionScreen,
  portal_configuration: PortalConfigurationScreen,
  coin_configuration: CoinConfigurationScreen,
  validation: ValidationScreen,
  summary: SummaryScreen,
  complete: CompleteScreen,
};
