import type { ValidationResult } from "@/models/ValidationResult";

const STORAGE_KEY = "renz_setup_validation";

export type ValidationDraft = {
  results: ValidationResult[];
  passed: boolean;
  canContinue: boolean;
  evaluated: boolean;
};

const defaultDraft = (): ValidationDraft => ({
  results: [],
  passed: false,
  canContinue: false,
  evaluated: false,
});

export function readValidationDraft(): ValidationDraft {
  try {
    const raw = sessionStorage.getItem(STORAGE_KEY);
    if (!raw) return defaultDraft();
    return { ...defaultDraft(), ...(JSON.parse(raw) as Partial<ValidationDraft>) };
  } catch {
    return defaultDraft();
  }
}

export function writeValidationDraft(partial: Partial<ValidationDraft>): ValidationDraft {
  const next = { ...readValidationDraft(), ...partial };
  sessionStorage.setItem(STORAGE_KEY, JSON.stringify(next));
  return next;
}

export function clearValidationDraft(): void {
  sessionStorage.removeItem(STORAGE_KEY);
}
