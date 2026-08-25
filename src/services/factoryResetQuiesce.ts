/**
 * Factory-reset communication quiesce flag (Admin SPA).
 * While set, normal polling and SSE must stop; only factory-reset/status polls.
 */

type Listener = (active: boolean) => void;

let active = false;
const listeners = new Set<Listener>();

export function isFactoryResetQuiesced(): boolean {
  return active;
}

export function setFactoryResetQuiesced(next: boolean): void {
  if (active === next) return;
  active = next;
  for (const listener of listeners) listener(active);
}

/** Subscribe to quiesce changes. Returns unsubscribe. */
export function onFactoryResetQuiesce(listener: Listener): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}
