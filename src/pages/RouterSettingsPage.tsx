import { Navigate } from "react-router-dom";

/** @deprecated Use /system-configuration */
export default function RouterSettingsPage() {
  return <Navigate to="/system-configuration" replace />;
}
