import "./migrate.js";
import "./seeds/index.js";
import { bootstrapAdminPassword } from "./bootstrapAuth.js";
import { runIntegrityCheck, scheduleAutoBackup } from "../services/dbHealth.js";

void bootstrapAdminPassword().then(() => {
  runIntegrityCheck();
  scheduleAutoBackup();
});
