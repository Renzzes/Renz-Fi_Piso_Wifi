import { db } from "../connection.js";
import { writeQueue } from "../../services/writeQueue.js";
import { publishAdminEvent } from "../../services/eventBus.js";

export const logsRepository = {
  list: (opts?: { q?: string; type?: string }) => {
    const q = String(opts?.q ?? "").toLowerCase();
    const type = opts?.type ? String(opts.type) : null;

    const rows = db
      .prepare(
        `SELECT id, strftime('%H:%M:%S', created_at) as t, level as lvl, message as msg, type
         FROM logs
         ${type ? "WHERE type = ?" : ""}
         ORDER BY id DESC
         LIMIT 200`,
      )
      .all(type ? [type] : []) as {
      id: number;
      t: string;
      lvl: string;
      msg: string;
      type?: string;
    }[];

    if (!q) return rows;
    return rows.filter((r) => r.msg.toLowerCase().includes(q));
  },

  clear: () => {
    writeQueue.enqueue("logs", () => {
      db.prepare("DELETE FROM logs").run();
      publishAdminEvent("logs.changed");
      publishAdminEvent("system.status");
    });
  },

  exportCsv: (opts?: { q?: string; type?: string }) => {
    const q = String(opts?.q ?? "").toLowerCase();
    const type = opts?.type ? String(opts.type) : null;

    const rows = db
      .prepare(
        `SELECT created_at, level, message, type
         FROM logs
         ${type ? "WHERE type = ?" : ""}
         ORDER BY id DESC`,
      )
      .all(type ? [type] : []) as {
      created_at: string;
      level: string;
      message: string;
      type?: string;
    }[];

    const filtered = q ? rows.filter((r) => r.message.toLowerCase().includes(q)) : rows;

    return [
      "timestamp,level,type,message",
      ...filtered.map(
        (r) =>
          `${r.created_at},${r.level},${(r.type ?? "system").replace(/,/g, ";")},"${r.message.replace(/"/g, '""')}"`,
      ),
    ].join("\n");
  },
};
