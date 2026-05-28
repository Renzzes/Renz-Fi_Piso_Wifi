import { db } from "../db/connection.js";

type QueueJob = {
  lane: string;
  run: () => void;
  retries: number;
};

const MAX_RETRIES = 3;
const RETRY_DELAY_MS = 25;

class WriteQueueService {
  private lanes = new Map<string, Promise<void>>();
  private pending = 0;
  private processed = 0;
  private failed = 0;
  private flushing = false;

  enqueue(lane: string, run: () => void) {
    this.pending += 1;
    const prev = this.lanes.get(lane) ?? Promise.resolve();
    const next = prev
      .then(() => this.executeWithRetry(run))
      .catch((err) => {
        this.failed += 1;
        console.error(`[writeQueue:${lane}]`, err);
      })
      .finally(() => {
        this.pending -= 1;
        this.processed += 1;
      });
    this.lanes.set(lane, next);
    return next;
  }

  enqueueTransaction<T>(lane: string, fn: () => T): Promise<T> {
    return new Promise((resolve, reject) => {
      this.enqueue(lane, () => {
        try {
          const result = db.transaction(fn)();
          resolve(result);
        } catch (err) {
          reject(err);
        }
      });
    });
  }

  private async executeWithRetry(run: () => void, attempt = 0): Promise<void> {
    try {
      run();
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      const isLocked = message.includes("SQLITE_BUSY") || message.includes("database is locked");
      if (isLocked && attempt < MAX_RETRIES) {
        await new Promise((r) => setTimeout(r, RETRY_DELAY_MS * (attempt + 1)));
        return this.executeWithRetry(run, attempt + 1);
      }
      throw err;
    }
  }

  async flush(timeoutMs = 5000) {
    if (this.flushing) return;
    this.flushing = true;
    const deadline = Date.now() + timeoutMs;
    while (this.pending > 0 && Date.now() < deadline) {
      await Promise.all([...this.lanes.values()]);
      await new Promise((r) => setTimeout(r, 10));
    }
    this.flushing = false;
  }

  getStatus() {
    return {
      pending: this.pending,
      processed: this.processed,
      failed: this.failed,
      laneCount: this.lanes.size,
    };
  }
}

export const writeQueue = new WriteQueueService();
