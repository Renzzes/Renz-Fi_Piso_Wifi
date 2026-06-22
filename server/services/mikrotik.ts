import { testRouterConnection as testRouterConnectionImpl } from "./mikrotik/connection.js";
import {
  getPublicRouterConfig,
  getRouterConfig,
  resolveRouterCredentials,
  saveRouterConfig,
} from "./mikrotik/hotspot.js";
import { listHotspotProfiles } from "./mikrotik/profiles.js";
import { disconnectUser } from "./mikrotik/users.js";
import type { RouterConfig, RouterPublicConfig, RouterTestResult } from "./mikrotik/types.js";

export type { RouterConfig, RouterPublicConfig, RouterTestResult };

export { getRouterConfig, getPublicRouterConfig, saveRouterConfig, listHotspotProfiles, resolveRouterCredentials };

// MikroTik RouterOS API adapter boundary — real integration plugs in here.
export async function testRouterConnection(config: RouterConfig): Promise<RouterTestResult> {
  return testRouterConnectionImpl(config);
}

export { disconnectUser };
