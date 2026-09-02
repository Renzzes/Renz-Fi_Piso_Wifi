import type { LucideIcon } from "lucide-react";
import { Coins, Users, Wallet } from "lucide-react";
import { formatPeso } from "@/lib/currency";
import { formatSessions, formatTimeOfDay } from "@/lib/dashboardFormat";
import { coinHardwareTone, type StatusTone } from "@/lib/dashboardDisplay";
import {
  DashboardCard,
  Sparkline,
  StatusBadge,
  CardSkeleton,
} from "@/components/dashboard/DashboardPrimitives";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import type { CoinState } from "@/types/api";

export type SalesPeriod = "today" | "weekly" | "monthly";

function SummaryTitle({ title, icon: Icon }: { title: string; icon: LucideIcon }) {
  return (
    <div className="flex items-center justify-between gap-2">
      <div className="text-[11px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">
        {title}
      </div>
      <span className="flex h-7 w-7 items-center justify-center rounded-md bg-muted text-muted-foreground">
        <Icon className="h-3.5 w-3.5" aria-hidden />
      </span>
    </div>
  );
}

function salesPeriodTitle(period: SalesPeriod): string {
  if (period === "weekly") return "Weekly Sales";
  if (period === "monthly") return "Monthly Sales";
  return "Today's Sales";
}

export function MonthlySalesCard({
  loading,
  period,
  onPeriodChange,
  todayAmount,
  todaySessions,
  weekAmount,
  weekSessions,
  monthAmount,
  monthSessions,
  chartValues,
}: {
  loading: boolean;
  period: SalesPeriod;
  onPeriodChange: (period: SalesPeriod) => void;
  todayAmount: number | undefined;
  todaySessions: number | undefined;
  weekAmount: number | undefined;
  weekSessions: number | undefined;
  monthAmount: number | undefined;
  monthSessions: number | undefined;
  chartValues: number[];
}) {
  const amount =
    period === "weekly" ? weekAmount : period === "monthly" ? monthAmount : todayAmount;
  const sessions =
    period === "weekly"
      ? weekSessions
      : period === "monthly"
        ? monthSessions
        : todaySessions;

  if (loading && amount === undefined) return <CardSkeleton rows={3} />;

  const subtitle =
    period === "today"
      ? `This week ${formatPeso(weekAmount, loading)} • This month ${formatPeso(monthAmount, loading)}`
      : period === "weekly"
        ? `Today ${formatPeso(todayAmount, loading)} • This month ${formatPeso(monthAmount, loading)}`
        : `Today ${formatPeso(todayAmount, loading)} • This week ${formatPeso(weekAmount, loading)}`;

  return (
    <DashboardCard className="border-amber-500/20">
      <div className="flex items-start justify-between gap-2">
        <div className="text-[11px] font-semibold uppercase tracking-[0.12em] text-amber-700 dark:text-amber-300/90">
          {salesPeriodTitle(period)}
        </div>
        <Select
          value={period}
          onValueChange={(value) => {
            if (value === "today" || value === "weekly" || value === "monthly") {
              onPeriodChange(value);
            }
          }}
        >
          <SelectTrigger
            aria-label="Sales period"
            className="h-7 w-[6.75rem] shrink-0 border-amber-500/25 bg-background/80 px-2 text-[11px] font-medium"
          >
            <SelectValue />
          </SelectTrigger>
          <SelectContent align="end">
            <SelectItem value="today">Today</SelectItem>
            <SelectItem value="weekly">Weekly</SelectItem>
            <SelectItem value="monthly">Monthly</SelectItem>
          </SelectContent>
        </Select>
      </div>
      <div className="mt-2 flex min-w-0 items-end justify-between gap-3">
        <div className="min-w-0 text-[28px] font-semibold leading-none tabular-nums text-foreground">
          {formatPeso(amount, loading)}
        </div>
        <div className="shrink-0 pb-0.5 text-[12px] text-muted-foreground">
          {formatSessions(sessions, loading)}
        </div>
      </div>
      <div className="mt-2 text-[12px] text-muted-foreground">{subtitle}</div>
      {chartValues.length >= 2 ? (
        <div className="mt-3">
          <Sparkline values={chartValues} />
        </div>
      ) : null}
    </DashboardCard>
  );
}

export function ActiveUsersCard({
  loading,
  count,
  paused,
}: {
  loading: boolean;
  count: number | undefined;
  paused: number | undefined;
}) {
  if (loading && count === undefined) return <CardSkeleton rows={3} />;
  const tone: StatusTone = !loading && paused !== undefined && paused > 0 ? "warn" : "ok";
  return (
    <DashboardCard>
      <SummaryTitle title="Active Users" icon={Users} />
      <div className="mt-2 flex min-w-0 items-end justify-between gap-3">
        <div className="text-[28px] font-semibold leading-none tabular-nums text-foreground">
          {loading && count === undefined ? "…" : (count ?? "N/A")}
        </div>
        <div className="pb-0.5 text-[12px] text-muted-foreground">
          {paused === undefined ? "N/A" : `${paused} paused`}
        </div>
      </div>
      <div className="mt-3">
        <StatusBadge label={count === 0 ? "0 active users" : "Live user count"} tone={tone} />
      </div>
    </DashboardCard>
  );
}

export function CoinSummaryCard({
  loading,
  coinsToday,
  lastCoin,
  hardwareState,
}: {
  loading: boolean;
  coinsToday: number | undefined;
  lastCoin: string | undefined | null;
  hardwareState: CoinState | string | undefined;
}) {
  if (loading && coinsToday === undefined) return <CardSkeleton rows={3} />;
  const tone = coinHardwareTone(hardwareState);
  const lastLabel = formatTimeOfDay(lastCoin);
  const recent = tone === "ok";
  return (
    <DashboardCard>
      <SummaryTitle title="Coins Today" icon={Coins} />
      <div className="mt-2 text-[28px] font-semibold leading-none tabular-nums text-foreground">
        {coinsToday === undefined ? "N/A" : coinsToday}{" "}
        <span className="text-[14px] font-medium text-muted-foreground">inserted today</span>
      </div>
      <div className="mt-3 text-[12px]">
        {recent && lastLabel !== "N/A" ? (
          <span className="text-emerald-600 dark:text-emerald-400">↑ last insert {lastLabel}</span>
        ) : (
          <span
            className={
              tone === "warn" ? "text-amber-700 dark:text-amber-300" : "text-muted-foreground"
            }
          >
            {tone === "warn"
              ? "No recent activity"
              : lastLabel === "N/A"
                ? "No coin inserted yet today"
                : `Last insert ${lastLabel}`}
          </span>
        )}
      </div>
    </DashboardCard>
  );
}

export function TotalCoinsCard({
  loading,
  totalCoins,
  monthlySessions,
}: {
  loading: boolean;
  totalCoins: number | undefined;
  monthlySessions: number | undefined;
}) {
  if (loading && totalCoins === undefined) return <CardSkeleton rows={3} />;
  return (
    <DashboardCard>
      <SummaryTitle title="Lifetime Coins" icon={Wallet} />
      <div className="mt-2 text-[28px] font-semibold leading-none tabular-nums text-foreground">
        {totalCoins === undefined ? "N/A" : totalCoins.toLocaleString()}{" "}
        <span className="text-[14px] font-medium text-muted-foreground">inserted</span>
      </div>
      <div className="mt-3 text-[12px] leading-snug text-muted-foreground">
        Every coin this appliance has accepted. This count clears only after a factory reset.
        {monthlySessions === undefined ? "" : ` ${monthlySessions} sessions this month.`}
      </div>
    </DashboardCard>
  );
}
