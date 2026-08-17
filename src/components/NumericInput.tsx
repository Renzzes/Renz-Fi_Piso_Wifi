import { useState } from "react";
import { Input } from "@/components/ui/input";
import { cn } from "@/lib/utils";

type NumericInputProps = {
  id?: string;
  className?: string;
  value: number | undefined;
  onValueChange: (next: number | undefined) => void;
  min?: number;
  max?: number;
  placeholder?: string;
  disabled?: boolean;
};

/**
 * Allows clearing the field while editing. Empty normalizes to undefined until
 * blur, then to 0 (or min) so controlled forms never force "01".
 */
export function NumericInput({
  id,
  className,
  value,
  onValueChange,
  min,
  max,
  placeholder,
  disabled,
}: NumericInputProps) {
  const [text, setText] = useState<string | null>(null);
  const display = text !== null ? text : value === undefined || Number.isNaN(value) ? "" : String(value);

  const commit = (raw: string) => {
    const trimmed = raw.trim();
    if (trimmed === "" || trimmed === "-" || trimmed === ".") {
      onValueChange(undefined);
      setText(null);
      return;
    }
    let n = Number(trimmed);
    if (Number.isNaN(n)) {
      onValueChange(0);
      setText(null);
      return;
    }
    if (min !== undefined && n < min) n = min;
    if (max !== undefined && n > max) n = max;
    onValueChange(n);
    setText(null);
  };

  return (
    <Input
      id={id}
      type="text"
      inputMode="numeric"
      disabled={disabled}
      className={cn(className)}
      placeholder={placeholder}
      value={display}
      onChange={(e) => {
        const raw = e.target.value;
        if (raw === "" || /^-?\d*\.?\d*$/.test(raw)) {
          setText(raw);
          if (raw === "") {
            onValueChange(undefined);
            return;
          }
          const n = Number(raw);
          if (!Number.isNaN(n)) onValueChange(n);
        }
      }}
      onBlur={() => {
        const raw = text !== null ? text : display;
        commit(raw === "" ? "0" : raw);
      }}
    />
  );
}
