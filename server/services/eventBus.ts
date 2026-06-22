import crypto from "node:crypto";
import type { Response } from "express";

export type AdminEventType =
  | "system.status"
  | "sessions.changed"
  | "users.active"
  | "logs.changed"
  | "sync.queue"
  | "coin.diagnostics"
  | "vouchers.changed"
  | "sales.changed"
  | "promos.changed"
  | "log.entry"
  | "portal.changed"
  | "firmware.progress";

export type AdminEvent = {
  type: AdminEventType;
  ts: string;
  payload?: Record<string, unknown>;
};

type SseClient = {
  id: string;
  res: Response;
};

class EventBusService {
  private clients = new Map<string, SseClient>();

  subscribe(res: Response) {
    const id = crypto.randomUUID();
    res.writeHead(200, {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache",
      Connection: "keep-alive",
    });
    res.write(": connected\n\n");

    this.clients.set(id, { id, res });

    const heartbeat = setInterval(() => {
      try {
        res.write(": heartbeat\n\n");
      } catch {
        clearInterval(heartbeat);
        this.clients.delete(id);
      }
    }, 25000);

    res.on("close", () => {
      clearInterval(heartbeat);
      this.clients.delete(id);
    });

    return id;
  }

  publish(event: AdminEvent) {
    const data = JSON.stringify(event);
    for (const [id, client] of this.clients) {
      try {
        client.res.write(`event: ${event.type}\n`);
        client.res.write(`data: ${data}\n\n`);
      } catch {
        this.clients.delete(id);
      }
    }
  }

  getConnectionCount() {
    return this.clients.size;
  }
}

export const eventBus = new EventBusService();

export function publishAdminEvent(type: AdminEventType, payload?: Record<string, unknown>) {
  eventBus.publish({ type, ts: new Date().toISOString(), payload });
}
