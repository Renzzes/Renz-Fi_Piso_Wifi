import type { SaleSessionRecord } from "@/types/api";

/** Bundled logo from `public/Logo.png` (Vite serves at site root). */
const LOGO_SRC = `${import.meta.env.BASE_URL}Logo.png`;

const COLUMNS: Array<{ key: keyof SaleSessionRecord | "date" | "time"; label: string; width: number }> = [
  { key: "date", label: "Date", width: 12 },
  { key: "time", label: "Time", width: 10 },
  { key: "paymentType", label: "Payment", width: 10 },
  { key: "amount", label: "Amount (PHP)", width: 14 },
  { key: "durationMinutes", label: "Duration (min)", width: 14 },
  { key: "macAddress", label: "MAC", width: 18 },
  { key: "ipAddress", label: "IP", width: 14 },
  { key: "voucherCode", label: "Voucher", width: 14 },
  { key: "profile", label: "Profile", width: 14 },
  { key: "speed", label: "Speed", width: 12 },
  { key: "operatorName", label: "Operator", width: 16 },
  { key: "status", label: "Status", width: 12 },
  { key: "terminationReason", label: "End reason", width: 16 },
  { key: "sessionId", label: "Session ID", width: 22 },
];

function splitDateTime(record: SaleSessionRecord): { date: string; time: string } {
  const raw = record.recordedAt || record.recorded_at || "";
  if (!raw) return { date: "", time: "" };
  const d = new Date(raw);
  if (Number.isNaN(d.getTime())) return { date: raw, time: "" };
  return {
    date: d.toLocaleDateString(undefined, { year: "numeric", month: "2-digit", day: "2-digit" }),
    time: d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit", second: "2-digit" }),
  };
}

function cellValue(record: SaleSessionRecord, key: (typeof COLUMNS)[number]["key"]): string | number {
  if (key === "date" || key === "time") {
    const { date, time } = splitDateTime(record);
    return key === "date" ? date : time;
  }
  const v = record[key as keyof SaleSessionRecord];
  if (v === undefined || v === null) return "";
  if (key === "amount") return Number(v);
  if (key === "durationMinutes") return Number(v);
  return String(v);
}

function totalProfitAmount(records: SaleSessionRecord[]): number {
  return records.reduce((sum, record) => {
    const amount = Number(record.amount);
    return sum + (Number.isFinite(amount) ? amount : 0);
  }, 0);
}

const AMOUNT_COLUMN_INDEX = COLUMNS.findIndex((col) => col.key === "amount") + 1;
const PAYMENT_COLUMN_INDEX = COLUMNS.findIndex((col) => col.key === "paymentType") + 1;

function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

async function loadLogoBase64(): Promise<string | null> {
  try {
    const res = await fetch(LOGO_SRC);
    if (!res.ok) return null;
    const buffer = await res.arrayBuffer();
    const bytes = new Uint8Array(buffer);
    let binary = "";
    for (let i = 0; i < bytes.length; i++) {
      binary += String.fromCharCode(bytes[i]!);
    }
    return btoa(binary);
  } catch {
    return null;
  }
}

/**
 * Export sales as a genuine `.xlsx` workbook (OOXML), not HTML/MHTML disguised as `.xls`.
 */
export async function exportSalesExcel(records: SaleSessionRecord[]): Promise<void> {
  const ExcelJS = (await import("exceljs")).default;
  const workbook = new ExcelJS.Workbook();
  workbook.creator = "Renz-Fi Admin";
  workbook.created = new Date();

  const sheet = workbook.addWorksheet("Sales", {
    views: [{ state: "frozen", ySplit: 5 }],
  });

  COLUMNS.forEach((col, index) => {
    sheet.getColumn(index + 1).width = col.width;
  });

  const logoBase64 = await loadLogoBase64();
  if (logoBase64) {
    const imageId = workbook.addImage({
      base64: logoBase64,
      extension: "png",
    });
    sheet.addImage(imageId, {
      tl: { col: 0, row: 0 },
      ext: { width: 240, height: 72 },
    });
  }

  const titleRow = sheet.getRow(4);
  titleRow.getCell(1).value = "Renz-Fi Sales Report";
  titleRow.getCell(1).font = { bold: true, size: 14, color: { argb: "FF0F172A" } };
  titleRow.getCell(COLUMNS.length).value = `Generated ${new Date().toLocaleString()}`;
  titleRow.getCell(COLUMNS.length).alignment = { horizontal: "right" };
  titleRow.getCell(COLUMNS.length).font = { size: 10, color: { argb: "FF64748B" } };
  titleRow.height = 22;

  const headerRow = sheet.getRow(5);
  COLUMNS.forEach((col, index) => {
    const cell = headerRow.getCell(index + 1);
    cell.value = col.label;
    cell.font = { bold: true, color: { argb: "FFFFFFFF" } };
    cell.fill = {
      type: "pattern",
      pattern: "solid",
      fgColor: { argb: "FF1E293B" },
    };
    cell.alignment = { vertical: "middle", horizontal: "left" };
    cell.border = {
      bottom: { style: "thin", color: { argb: "FF334155" } },
    };
  });
  headerRow.height = 20;

  records.forEach((record, rowIndex) => {
    const row = sheet.getRow(6 + rowIndex);
    COLUMNS.forEach((col, colIndex) => {
      const cell = row.getCell(colIndex + 1);
      cell.value = cellValue(record, col.key);
      if (col.key === "amount") {
        cell.numFmt = "#,##0.00";
      }
    });
  });

  const totalAmount = totalProfitAmount(records);
  const totalRowIndex = 6 + records.length + 1;
  const totalRow = sheet.getRow(totalRowIndex);
  totalRow.getCell(PAYMENT_COLUMN_INDEX).value =
    records.length === 1
      ? "Total Profit (1 transaction)"
      : `Total Profit (${records.length} transactions)`;
  totalRow.getCell(PAYMENT_COLUMN_INDEX).font = { bold: true };
  totalRow.getCell(PAYMENT_COLUMN_INDEX).alignment = { horizontal: "right" };

  const totalAmountCell = totalRow.getCell(AMOUNT_COLUMN_INDEX);
  totalAmountCell.value = totalAmount;
  totalAmountCell.numFmt = "#,##0.00";
  totalAmountCell.font = { bold: true };
  totalAmountCell.fill = {
    type: "pattern",
    pattern: "solid",
    fgColor: { argb: "FFF1F5F9" },
  };
  totalRow.eachCell({ includeEmpty: true }, (cell, colNumber) => {
    if (colNumber <= AMOUNT_COLUMN_INDEX) {
      cell.border = {
        top: { style: "medium", color: { argb: "FF334155" } },
      };
    }
  });

  const buffer = await workbook.xlsx.writeBuffer();
  const stamp = new Date().toISOString().slice(0, 10);
  downloadBlob(
    new Blob([buffer], {
      type: "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
    }),
    `renz-fi-sales-${stamp}.xlsx`,
  );
}
