import { db } from "../connection.js";

const chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

function randCode() {
  const part = (n: number) =>
    Array.from({ length: n }, () => chars[Math.floor(Math.random() * chars.length)]).join("");
  return `${part(4)}-${part(4)}`;
}

export const vouchersRepository = {
  list: () => {
    return db
      .prepare(
        `SELECT id, code, amount, minutes, status, expires, created_at FROM vouchers ORDER BY id DESC`,
      )
      .all() as {
      id: number;
      code: string;
      amount: number;
      minutes: number;
      status: string;
      expires: string;
      created_at: string;
    }[];
  },

  generateBulk: (opts: { count: number; amount: number; minutes: number; expires: string }) => {
    const { count, amount, minutes, expires } = opts;
    const insert = db.prepare(
      `INSERT INTO vouchers (code, amount, minutes, status, expires) VALUES (?, ?, ?, 'unused', ?)`,
    );

    const created: string[] = [];
    db.transaction(() => {
      for (let i = 0; i < count; i++) {
        const code = randCode();
        insert.run(code, amount, minutes, expires);
        created.push(code);
      }
    })();

    return { created };
  },

  deleteByCode: (code: string) => {
    db.prepare("DELETE FROM vouchers WHERE code = ?").run(code);
  },

  getByCode: (code: string) => {
    return db
      .prepare(`SELECT code, amount, minutes, status, expires FROM vouchers WHERE code = ?`)
      .get(code) as
      | {
          code: string;
          amount: number;
          minutes: number;
          status: "unused" | "active" | "expired";
          expires: string;
        }
      | undefined;
  },
};
