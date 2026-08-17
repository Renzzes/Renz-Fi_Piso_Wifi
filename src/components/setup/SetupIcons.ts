/**
 * Frozen setup wizard icons — import from here only; never pick icons in screen files.
 */
import {
  CheckCircle2,
  Coins,
  Globe,
  HardDrive,
  Info,
  Key,
  Lightbulb,
  MonitorSmartphone,
  Network,
  Router,
  User,
  AlertTriangle,
  WifiOff,
  XCircle,
  type LucideIcon,
} from "lucide-react";

export const SetupIcons = {
  router: Router,
  coin: Coins,
  portal: MonitorSmartphone,
  check: CheckCircle2,
  warning: AlertTriangle,
  error: XCircle,
  info: Info,
  sdCard: HardDrive,
  network: Network,
  internet: Globe,
  /** Offline / unreachable — empty states only */
  internetOff: WifiOff,
  user: User,
  key: Key,
  tip: Lightbulb,
} as const;

export type SetupIconName = keyof typeof SetupIcons;

export function getSetupIcon(name: SetupIconName): LucideIcon {
  return SetupIcons[name];
}
