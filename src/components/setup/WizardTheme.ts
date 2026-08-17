/**
 * Frozen wizard visual tokens — single source for spacing, layout, and motion.
 * All setup UI must read from here (light/dark/tablet/desktop stay consistent).
 */
export const wizardTheme = {
  /** Page shell */
  shell: {
    minHeight: "min-h-svh",
    background: "bg-muted/40",
    flex: "flex flex-col",
  },

  /** Content width & horizontal rhythm */
  layout: {
    maxWidth: "max-w-3xl",
    paddingX: "px-4 md:px-6",
    paddingY: "py-6 md:py-8",
    gap: "gap-3",
    sectionGap: "space-y-4",
  },

  /** Header / footer chrome */
  chrome: {
    header:
      "border-b bg-background/95 backdrop-blur supports-[backdrop-filter]:bg-background/80",
    footer: "border-t bg-background",
    headerPadding: "py-3",
    footerPadding: "py-3",
  },

  /** SetupCard surface */
  card: {
    radius: "rounded-xl",
    border: "border bg-card text-card-foreground shadow",
    padding: "p-4 md:p-6",
    overflow: "relative overflow-hidden",
  },

  /** Progress bar */
  progress: {
    container: "space-y-2",
    track: "bg-primary/20",
    indicator: "bg-primary",
    height: "h-2",
    transition: "transition-all duration-300 ease-out",
    labelActive: "font-medium text-foreground",
    labelMuted: "text-muted-foreground tabular-nums",
    message: "text-xs text-muted-foreground truncate",
  },

  /** Step indicator colors */
  step: {
    active: "text-foreground",
    inactive: "text-muted-foreground",
    badge: "bg-secondary text-secondary-foreground",
  },

  /** Loading overlay (card + full-page) */
  overlay: {
    background: "bg-background/70 backdrop-blur-[1px]",
    zIndex: "z-10",
    spinner: "text-muted-foreground",
  },

  /** Motion */
  motion: {
    durationFast: "duration-150",
    durationNormal: "duration-200",
    durationSlow: "duration-300",
    ease: "ease-out",
    spin: "animate-spin",
  },

  /** Typography inside steps */
  typography: {
    title: "text-lg font-semibold leading-tight",
    description: "text-sm text-muted-foreground mt-1",
    meta: "text-xs text-muted-foreground",
    mono: "font-mono text-xs",
  },

  /** SetupForm layout — Router / Portal / Coin / Validation screens */
  form: {
    gap: "space-y-6",
    sectionGap: "space-y-4",
    fieldGap: "space-y-2",
    section:
      "rounded-lg border border-border/80 bg-muted/20 p-4 md:p-5 space-y-4",
    sectionTitle: "text-sm font-semibold leading-none",
    sectionDescription: "text-xs text-muted-foreground",
    label: "text-sm font-medium leading-none",
    hint: "text-xs text-muted-foreground",
    error: "text-xs text-destructive",
    control: "w-full",
    checkboxRow: "flex items-start gap-3",
    checkboxContent: "space-y-1 leading-none",
    actions: "flex flex-col-reverse gap-2 sm:flex-row sm:justify-end sm:gap-3",
    actionsDivider: "border-t border-border/60 pt-4 mt-2",
  },

  /** SetupStatusCard — validation, summary, diagnostics */
  status: {
    list: "space-y-2",
    card: "flex gap-3 rounded-lg border p-3 md:px-4 md:py-3.5",
    icon: "h-5 w-5 shrink-0 mt-0.5",
    body: "min-w-0 flex-1 space-y-0.5",
    title: "text-sm font-medium leading-tight",
    description: "text-xs text-muted-foreground",
    details: "text-xs text-muted-foreground/90 font-mono",
    success: {
      card: "border-emerald-500/25 bg-emerald-500/5",
      icon: "text-emerald-600 dark:text-emerald-400",
    },
    warning: {
      card: "border-amber-500/30 bg-amber-500/5",
      icon: "text-amber-600 dark:text-amber-400",
    },
    error: {
      card: "border-destructive/30 bg-destructive/5",
      icon: "text-destructive",
    },
    pending: {
      card: "border-border/80 bg-muted/30",
      icon: "text-muted-foreground",
    },
  },

  /** SetupEmptyState — no data / retry flows */
  empty: {
    container:
      "flex flex-col items-center justify-center text-center rounded-lg border border-dashed border-border/80 bg-muted/20 py-8 md:py-10 px-4 md:px-6 space-y-3",
    iconWrap: "rounded-full bg-muted/60 p-3",
    icon: "h-8 w-8 text-muted-foreground",
    title: "text-sm font-semibold leading-tight",
    description: "text-sm text-muted-foreground max-w-sm",
    actions: "flex flex-col sm:flex-row items-center gap-2 pt-1",
  },

  /** SetupInfoBanner — hints, compatibility, next steps */
  banner: {
    list: "space-y-3",
    base: "flex gap-3 rounded-lg border px-3 py-3 md:px-4 md:py-3.5",
    icon: "h-5 w-5 shrink-0 mt-0.5",
    body: "min-w-0 flex-1 space-y-1",
    title: "text-sm font-medium leading-tight",
    description: "text-xs text-muted-foreground leading-relaxed",
    actions: "pt-1",
    info: {
      surface: "border-sky-500/25 bg-sky-500/5",
      icon: "text-sky-600 dark:text-sky-400",
    },
    success: {
      surface: "border-emerald-500/25 bg-emerald-500/5",
      icon: "text-emerald-600 dark:text-emerald-400",
    },
    warning: {
      surface: "border-amber-500/30 bg-amber-500/5",
      icon: "text-amber-600 dark:text-amber-400",
    },
    error: {
      surface: "border-destructive/30 bg-destructive/5",
      icon: "text-destructive",
    },
    tip: {
      surface: "border-violet-500/25 bg-violet-500/5",
      icon: "text-violet-600 dark:text-violet-400",
    },
  },
} as const;

/** Pre-composed class strings for common wizard regions. */
export const wizardClasses = {
  shell: `${wizardTheme.shell.minHeight} ${wizardTheme.shell.background} ${wizardTheme.shell.flex}`,
  content: `flex-1 mx-auto w-full ${wizardTheme.layout.maxWidth} ${wizardTheme.layout.paddingX} ${wizardTheme.layout.paddingY}`,
  headerInner: `mx-auto flex ${wizardTheme.layout.maxWidth} items-center justify-between ${wizardTheme.layout.paddingX} ${wizardTheme.layout.gap} ${wizardTheme.chrome.headerPadding}`,
  headerProgress: `mx-auto ${wizardTheme.layout.maxWidth} ${wizardTheme.layout.paddingX} pb-3`,
  footerInner: `mx-auto flex ${wizardTheme.layout.maxWidth} items-center justify-between ${wizardTheme.layout.paddingX} ${wizardTheme.layout.gap} ${wizardTheme.chrome.footerPadding}`,
  bootstrap: `min-h-svh bg-muted/40 flex flex-col items-center justify-center gap-3 ${wizardTheme.layout.paddingX}`,
  errorPanel: `min-h-svh bg-muted/40 flex flex-col items-center justify-center gap-4 p-6`,
} as const;
