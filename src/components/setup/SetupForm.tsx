import {
  forwardRef,
  type ComponentProps,
  type FormHTMLAttributes,
  type ReactNode,
  type SelectHTMLAttributes,
} from "react";
import { Checkbox } from "@/components/ui/checkbox";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { cn } from "@/lib/utils";

/* ── SetupForm ───────────────────────────────────────────────────────────── */

export type SetupFormProps = FormHTMLAttributes<HTMLFormElement> & {
  children: ReactNode;
};

/** Standard wizard form root — use inside SetupCard on every step. */
export function SetupForm({ children, className, ...props }: SetupFormProps) {
  return (
    <form className={cn(wizardTheme.form.gap, className)} noValidate {...props}>
      {children}
    </form>
  );
}

/* ── SetupFormSection ────────────────────────────────────────────────────── */

export type SetupFormSectionProps = {
  title?: string;
  description?: string;
  children: ReactNode;
  className?: string;
};

export function SetupFormSection({
  title,
  description,
  children,
  className,
}: SetupFormSectionProps) {
  return (
    <section className={cn(wizardTheme.form.section, className)}>
      {(title || description) && (
        <header className={wizardTheme.form.sectionGap}>
          {title ? <h3 className={wizardTheme.form.sectionTitle}>{title}</h3> : null}
          {description ? (
            <p className={wizardTheme.form.sectionDescription}>{description}</p>
          ) : null}
        </header>
      )}
      <div className={wizardTheme.form.sectionGap}>{children}</div>
    </section>
  );
}

/* ── SetupFormField ──────────────────────────────────────────────────────── */

export type SetupFormFieldProps = {
  label: string;
  htmlFor?: string;
  hint?: string;
  error?: string;
  required?: boolean;
  children: ReactNode;
  className?: string;
};

/** Label + hint + error wrapper for any control (Field). */
export function SetupFormField({
  label,
  htmlFor,
  hint,
  error,
  required,
  children,
  className,
}: SetupFormFieldProps) {
  const fieldId = htmlFor;

  return (
    <div className={cn(wizardTheme.form.fieldGap, className)}>
      <Label htmlFor={fieldId} className={wizardTheme.form.label}>
        {label}
        {required ? <span className="text-destructive ml-0.5">*</span> : null}
      </Label>
      {children}
      {hint && !error ? <p className={wizardTheme.form.hint}>{hint}</p> : null}
      {error ? (
        <p className={wizardTheme.form.error} role="alert">
          {error}
        </p>
      ) : null}
    </div>
  );
}

/* ── SetupInput ──────────────────────────────────────────────────────────── */

export type SetupInputProps = ComponentProps<typeof Input>;

export const SetupInput = forwardRef<HTMLInputElement, SetupInputProps>(
  ({ className, ...props }, ref) => (
    <Input ref={ref} className={cn(wizardTheme.form.control, className)} {...props} />
  ),
);
SetupInput.displayName = "SetupInput";

/* ── SetupSelect ─────────────────────────────────────────────────────────── */

export type SetupSelectOption = {
  value: string;
  label: string;
  disabled?: boolean;
};

export type SetupSelectProps = {
  id?: string;
  value?: string;
  defaultValue?: string;
  onValueChange?: (value: string) => void;
  placeholder?: string;
  options: SetupSelectOption[];
  disabled?: boolean;
  className?: string;
};

export function SetupSelect({
  id,
  value,
  defaultValue,
  onValueChange,
  placeholder = "Select…",
  options,
  disabled,
  className,
}: SetupSelectProps) {
  return (
    <Select
      value={value}
      defaultValue={defaultValue}
      onValueChange={onValueChange}
      disabled={disabled}
    >
      <SelectTrigger id={id} className={cn(wizardTheme.form.control, className)}>
        <SelectValue placeholder={placeholder} />
      </SelectTrigger>
      <SelectContent>
        {options.map((option) => (
          <SelectItem
            key={option.value}
            value={option.value}
            disabled={option.disabled}
          >
            {option.label}
          </SelectItem>
        ))}
      </SelectContent>
    </Select>
  );
}

/* ── SetupCheckbox ───────────────────────────────────────────────────────── */

export type SetupCheckboxProps = Omit<ComponentProps<typeof Checkbox>, "children"> & {
  id: string;
  label: string;
  description?: string;
  className?: string;
};

export function SetupCheckbox({
  id,
  label,
  description,
  className,
  ...props
}: SetupCheckboxProps) {
  return (
    <div className={cn(wizardTheme.form.checkboxRow, className)}>
      <Checkbox id={id} {...props} />
      <div className={wizardTheme.form.checkboxContent}>
        <Label htmlFor={id} className={wizardTheme.form.label}>
          {label}
        </Label>
        {description ? <p className={wizardTheme.form.hint}>{description}</p> : null}
      </div>
    </div>
  );
}

/* ── SetupActions ────────────────────────────────────────────────────────── */

export type SetupActionsProps = {
  children: ReactNode;
  className?: string;
  bordered?: boolean;
};

/** In-form actions (Verify, Test connection) — shell footer keeps Back/Next/Cancel. */
export function SetupActions({
  children,
  className,
  bordered = true,
}: SetupActionsProps) {
  return (
    <div
      className={cn(
        wizardTheme.form.actions,
        bordered && wizardTheme.form.actionsDivider,
        className,
      )}
    >
      {children}
    </div>
  );
}

/* ── SetupReadOnlyValue (utility for summary / status rows) ───────────────── */

export type SetupReadOnlyValueProps = {
  value: ReactNode;
  className?: string;
};

export function SetupReadOnlyValue({ value, className }: SetupReadOnlyValueProps) {
  return (
    <div
      className={cn(
        "rounded-md border border-input bg-muted/30 px-3 py-2 text-sm",
        wizardTheme.typography.mono,
        className,
      )}
    >
      {value}
    </div>
  );
}

// Re-export for screens that need native select (e.g. react-hook-form register)
export type SetupNativeSelectProps = SelectHTMLAttributes<HTMLSelectElement>;

export const SetupNativeSelect = forwardRef<HTMLSelectElement, SetupNativeSelectProps>(
  ({ className, children, ...props }, ref) => (
    <select
      ref={ref}
      className={cn(
        "flex h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-sm shadow-sm",
        "focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring",
        "disabled:cursor-not-allowed disabled:opacity-50",
        wizardTheme.form.control,
        className,
      )}
      {...props}
    >
      {children}
    </select>
  ),
);
SetupNativeSelect.displayName = "SetupNativeSelect";
