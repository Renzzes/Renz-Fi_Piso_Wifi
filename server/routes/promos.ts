import { Router, type Request, type Response } from "express";
import { promosRepository } from "../db/repositories/promosRepository.js";
import { z } from "zod";
import { sendError, sendSuccess } from "../utils/response.js";
import { publishAdminEvent } from "../services/eventBus.js";

export const promosRouter = Router();

const createPromoSchema = z.object({
  name: z.string().min(1).max(64),
  coin: z.number().int().positive(),
  minutes: z.number().int().positive(),
  speed: z.number().int().positive().optional().nullable(),
  devices: z.number().int().positive().optional().nullable(),
  data_cap_mb: z.number().int().positive().optional().nullable(),
});

const updatePromoSchema = createPromoSchema.extend({
  id: z.number().int().positive(),
});

promosRouter.get("/", (_req, res) => {
  sendSuccess(res, promosRepository.list());
});

function createPromo(req: Request, res: Response) {
  const parsed = createPromoSchema.safeParse(req.body);
  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid promo payload",
      details: parsed.error.flatten(),
    });
  }

  const { id } = promosRepository.create({
    ...parsed.data,
    speed: parsed.data.speed ?? null,
    devices: parsed.data.devices ?? 1,
    data_cap_mb: parsed.data.data_cap_mb ?? null,
  });
  publishAdminEvent("promos.changed");
  return sendSuccess(res, { id });
}

function updatePromo(req: Request, res: Response) {
  const parsedId = req.params.id
    ? z.coerce.number().int().positive().safeParse(req.params.id)
    : undefined;
  const parsed = updatePromoSchema.safeParse({
    ...req.body,
    id: parsedId?.success ? parsedId.data : req.body?.id,
  });
  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid promo payload",
      details: parsed.error.flatten(),
    });
  }

  promosRepository.update({
    ...parsed.data,
    speed: parsed.data.speed ?? null,
    devices: parsed.data.devices ?? 1,
    data_cap_mb: parsed.data.data_cap_mb ?? null,
  });
  publishAdminEvent("promos.changed");
  return sendSuccess(res, { ok: true });
}

function deletePromo(req: Request, res: Response) {
  const parsedId = z.coerce.number().int().positive().safeParse(req.params.id);
  if (!parsedId.success) {
    return sendError(res, { status: 400, code: "BAD_REQUEST", error: "Invalid promo id" });
  }

  promosRepository.deleteById(parsedId.data);
  publishAdminEvent("promos.changed");
  return sendSuccess(res, { ok: true });
}

promosRouter.post("/", createPromo);
promosRouter.post("/create", createPromo);
promosRouter.put("/update", updatePromo);
promosRouter.put("/:id", updatePromo);
promosRouter.delete("/:id", deletePromo);
promosRouter.delete("/delete/:id", deletePromo);
