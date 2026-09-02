import { Navigate } from "react-router-dom";

/** @deprecated Use /network */
export default function RouterSettingsPage() {
  return <Navigate to="/network" replace />;
}
