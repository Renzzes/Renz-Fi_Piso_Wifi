import { db } from "../connection.js";

function aggregate(since: string) {
  return db
    .prepare(
      `SELECT COALESCE(SUM(amount), 0) as amount, COALESCE(SUM(sessions), 0) as sessions
       FROM sales_transactions WHERE date(recorded_at) >= date(?)`,
    )
    .get(since) as { amount: number; sessions: number };
}

export const salesRepository = {
  aggregateSince: (since: string) => aggregate(since),

  history: () => {
    return db
      .prepare(
        `SELECT date(recorded_at) as date, SUM(sessions) as sessions, SUM(amount) as revenue
         FROM sales_transactions GROUP BY date(recorded_at) ORDER BY date DESC LIMIT 30`,
      )
      .all() as { date: string; sessions: number; revenue: number }[];
  },
};
