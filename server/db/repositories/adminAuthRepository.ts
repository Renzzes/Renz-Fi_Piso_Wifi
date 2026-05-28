import { db } from "../connection.js";

export const adminAuthRepository = {
  getState: () => {
    const row = db
      .prepare(
        `SELECT failed_attempts, locked_until, last_failed_at, last_success_at
         FROM admin_auth_state WHERE id = 1`,
      )
      .get() as
      | {
          failed_attempts: number;
          locked_until: string | null;
          last_failed_at: string | null;
          last_success_at: string | null;
        }
      | undefined;
    return (
      row ?? {
        failed_attempts: 0,
        locked_until: null,
        last_failed_at: null,
        last_success_at: null,
      }
    );
  },

  recordFailedAttempt: (maxAttempts: number, lockoutMinutes: number) => {
    const state = adminAuthRepository.getState();
    const next = state.failed_attempts + 1;
    const locked =
      next >= maxAttempts
        ? db
            .prepare(
              `UPDATE admin_auth_state
               SET failed_attempts = ?,
                   locked_until = datetime('now', ?),
                   last_failed_at = datetime('now')
               WHERE id = 1`,
            )
            .run(next, `+${lockoutMinutes} minutes`)
        : db
            .prepare(
              `UPDATE admin_auth_state
               SET failed_attempts = ?,
                   last_failed_at = datetime('now')
               WHERE id = 1`,
            )
            .run(next);
    return locked;
  },

  resetFailedAttempts: () => {
    db.prepare(
      `UPDATE admin_auth_state
       SET failed_attempts = 0,
           locked_until = NULL,
           last_success_at = datetime('now')
       WHERE id = 1`,
    ).run();
  },

  isLocked: () => {
    const row = db.prepare(`SELECT locked_until FROM admin_auth_state WHERE id = 1`).get() as
      | { locked_until: string | null }
      | undefined;
    if (!row?.locked_until) return false;
    const stillLocked = db
      .prepare(`SELECT datetime('now') < datetime(?) as locked`)
      .get(row.locked_until) as { locked: number };
    if (!stillLocked?.locked) {
      db.prepare(
        `UPDATE admin_auth_state SET failed_attempts = 0, locked_until = NULL WHERE id = 1`,
      ).run();
      return false;
    }
    return true;
  },

  getFailedAttempts: () => adminAuthRepository.getState().failed_attempts,
};
