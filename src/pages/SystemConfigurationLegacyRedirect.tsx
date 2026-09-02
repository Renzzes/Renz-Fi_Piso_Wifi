import { Navigate, useLocation } from "react-router-dom";

const HASH_ROUTES: Record<string, string> = {
  "syscfg-overview": "/network",
  "syscfg-network": "/network",
  "syscfg-wireless": "/wireless",
  "syscfg-hotspot": "/bandwidth",
  "syscfg-storage": "/storage",
  "syscfg-router": "/router-status",
};

/** Legacy /system-configuration bookmarks → dedicated sidebar routes. */
export default function SystemConfigurationLegacyRedirect() {
  const { hash } = useLocation();
  const sectionId = hash.replace(/^#/, "");
  const target = HASH_ROUTES[sectionId] ?? "/network";
  return <Navigate to={target} replace />;
}
