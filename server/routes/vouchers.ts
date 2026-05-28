import { Router, type Request, type Response } from "express";
import { vouchersRepository } from "../db/repositories/vouchersRepository.js";
import { z } from "zod";
import { sendError, sendSuccess } from "../utils/response.js";
import { publishAdminEvent } from "../services/eventBus.js";

export const vouchersRouter = Router();

const generateVouchersSchema = z.object({
  count: z.number().int().positive().max(5000).default(1),
  amount: z.number().int().positive(),
  minutes: z
    .number()
    .int()
    .positive()
    .max(365 * 24 * 60),
  expires: z
    .string()
    .regex(/^\d{4}-\d{2}-\d{2}$/)
    .optional()
    .default("2026-12-31"),
});

const voucherCodeSchema = z
  .string()
  .trim()
  .min(4)
  .max(32)
  .regex(/^[A-Z0-9-]+$/);

vouchersRouter.get("/", (_req, res) => {
  return sendSuccess(
    res,
    vouchersRepository.list().map((v) => ({
      code: v.code,
      amount: v.amount,
      minutes: v.minutes,
      status: v.status,
      expires: v.expires,
    })),
  );
});

function generateVouchers(req: Request, res: Response) {
  const parsed = generateVouchersSchema.safeParse(req.body);
  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid voucher payload",
      details: parsed.error.flatten(),
    });
  }

  const { created } = vouchersRepository.generateBulk(parsed.data);
  publishAdminEvent("vouchers.changed", { count: created.length });
  publishAdminEvent("system.status");
  return sendSuccess(res, { created });
}

function deleteVoucher(req: Request, res: Response) {
  const parsedCode = voucherCodeSchema.safeParse(req.params.code);
  if (!parsedCode.success) {
    return sendError(res, { status: 400, code: "BAD_REQUEST", error: "Invalid voucher code" });
  }

  vouchersRepository.deleteByCode(parsedCode.data);
  publishAdminEvent("vouchers.changed");
  return sendSuccess(res, { ok: true });
}

function getVoucher(req: Request, res: Response) {
  const parsedCode = voucherCodeSchema.safeParse(req.params.code);
  if (!parsedCode.success) {
    return sendError(res, { status: 400, code: "BAD_REQUEST", error: "Invalid voucher code" });
  }

  const row = vouchersRepository.getByCode(parsedCode.data);
  if (!row) return sendError(res, { status: 404, code: "NOT_FOUND", error: "Voucher not found" });
  return sendSuccess(res, row);
}

vouchersRouter.post("/", generateVouchers);
vouchersRouter.post("/generate", generateVouchers);
vouchersRouter.delete("/:code", deleteVoucher);
vouchersRouter.delete("/delete/:code", deleteVoucher);
vouchersRouter.get("/print/:code", getVoucher);
vouchersRouter.get("/:code", getVoucher);
