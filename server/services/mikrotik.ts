import { testRouterConnection as testRouterConnectionImpl } from "./mikrotik/connection.js";
import { getRouterConfig, saveRouterConfig } from "./mikrotik/hotspot.js";
import { disconnectUser } from "./mikrotik/users.js";
import type { RouterConfig } from "./mikrotik/types.js";

export type { RouterConfig };

export { getRouterConfig, saveRouterConfig };

// MikroTik RouterOS API adapter boundary — real integration plugs in here.
export async function testRouterConnection(config: RouterConfig): Promise<boolean> {
  return testRouterConnectionImpl(config);
}

export { disconnectUser };
