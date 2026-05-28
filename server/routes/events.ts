import { Router } from "express";
import { eventBus } from "../services/eventBus.js";

export const eventsRouter = Router();

eventsRouter.get("/", (req, res) => {
  eventBus.subscribe(res);
});

eventsRouter.get("/stream", (req, res) => {
  eventBus.subscribe(res);
});
