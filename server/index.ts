import express from "express";
import cors from "cors";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { config } from "./config/index.js";
import "./db/init.js";
import { systemRouter } from "./routes/system.js";
import { salesRouter } from "./routes/sales.js";
import { promosRouter } from "./routes/promos.js";
import { vouchersRouter } from "./routes/vouchers.js";
import { usersRouter } from "./routes/users.js";
import { portalRouter } from "./routes/portal.js";
import { coinRouter } from "./routes/coin.js";
import { routerRouter } from "./routes/router.js";
import { logsRouter } from "./routes/logs.js";
import { firmwareRouter } from "./routes/firmware.js";
import { settingsRouter } from "./routes/settings.js";
import { syncRouter } from "./routes/sync.js";
import { authRouter } from "./routes/auth.js";
import { eventsRouter } from "./routes/events.js";
import { apiRateLimitMiddleware } from "./middleware/apiRateLimit.js";
import { errorHandler } from "./middleware/errorHandler.js";
import { notFoundApiHandler } from "./middleware/notFound.js";
import { requestIdMiddleware } from "./middleware/requestId.js";
import { securityMiddleware } from "./middleware/security.js";
import { db } from "./db/connection.js";
import { adminSessionRequiredMiddleware } from "./middleware/adminSession.js";
import { validateAdminSessionToken } from "./services/adminSession.js";
import { getCookie } from "./utils/cookies.js";
import { lanSecurityMiddleware } from "./middleware/lanSecurity.js";
import { requestLoggerMiddleware } from "./middleware/requestLogger.js";
import { sanitizeBodyMiddleware } from "./middleware/sanitize.js";
import { csrfMiddleware } from "./middleware/csrf.js";
import { writeQueue } from "./services/writeQueue.js";
import { getDistPath, resolveFromImportMeta } from "./paths.js";
import { getSystemStatus } from "./services/systemStatus.js";
import { sendSuccess } from "./utils/response.js";

const app = express();
const port = config.port;
const isProd = config.isProd;

app.set("trust proxy", 1);
app.disable("x-powered-by");
app.use(securityMiddleware());
app.use(cors({ origin: false, credentials: true }));
app.use(express.json({ limit: config.apiJsonLimit }));
app.use(express.urlencoded({ extended: true, limit: config.apiJsonLimit }));
app.use(requestIdMiddleware);
app.use(lanSecurityMiddleware);
app.use(sanitizeBodyMiddleware);
app.use(requestLoggerMiddleware);
app.use("/api", apiRateLimitMiddleware);

app.get("/api/health", (req, res) => {
  let dbOk = true;
  let pendingSync = 0;
  let session: { authenticated: boolean } = { authenticated: false };

  const cookieToken = getCookie(req, config.adminSessionCookie.name);
  const existingSession = cookieToken ? validateAdminSessionToken(cookieToken) : null;
  if (existingSession) {
    session = { authenticated: true };
  }

  try {
    db.prepare("SELECT 1").get();
  } catch {
    dbOk = false;
  }

  try {
    const row = db
      .prepare("SELECT COUNT(*) as c FROM sync_queue WHERE status = 'pending'")
      .get() as { c: number };
    pendingSync = row?.c ?? 0;
  } catch {
    pendingSync = 0;
  }

  return sendSuccess(res, {
    ok: true,
    service: "renz-fi-admin",
    database: { ok: dbOk },
    sync: { pending: pendingSync },
    session,
  });
});

app.use("/api", csrfMiddleware);
app.use("/api", adminSessionRequiredMiddleware);

app.use("/api/auth", authRouter);
app.use("/api/events", eventsRouter);
app.get("/api/status", (_req, res) => {
  return sendSuccess(res, getSystemStatus());
});
app.use("/api/system", systemRouter);
app.use("/api/sales", salesRouter);
app.use("/api/promos", promosRouter);
app.use("/api/vouchers", vouchersRouter);
app.use("/api/users", usersRouter);
app.use("/api/captive-portal", portalRouter);
app.use("/api/settings/portal", portalRouter);
app.use("/api/coin", coinRouter);
app.use("/api/router", routerRouter);
app.use("/api/logs", logsRouter);
app.use("/api/firmware", firmwareRouter);
app.use("/api/system/firmware", firmwareRouter);
app.use("/api/settings", settingsRouter);
app.use("/api/sync", syncRouter);
app.use("/api/system/sync", syncRouter);

if (isProd) {
  const serverDir = resolveFromImportMeta(import.meta.url);
  const distPath = getDistPath(serverDir);
  app.use(express.static(distPath));
  app.get(/^(?!\/api).*/, (_req, res) => {
    res.sendFile(path.join(distPath, "index.html"));
  });
}

app.use("/api", notFoundApiHandler);
app.use(errorHandler);

const server = app.listen(port, "0.0.0.0", () => {
  console.log(`Renz-Fi admin API listening on http://0.0.0.0:${port}`);
});

async function shutdown(signal: string) {
  console.log(`[shutdown] ${signal} — flushing write queue`);
  await writeQueue.flush();
  server.close(() => process.exit(0));
}

process.on("SIGINT", () => void shutdown("SIGINT"));
process.on("SIGTERM", () => void shutdown("SIGTERM"));
