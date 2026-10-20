"use client";

// ============================================================================
// useState that survives page changes and reloads in this browser tab
// (sessionStorage). For view settings and drafts: selected tab, filters,
// time windows, half-filled forms. Starts from the default on the first
// render so the static HTML still hydrates, then loads the saved value.
// ============================================================================

import { useCallback, useEffect, useState, type Dispatch, type SetStateAction } from "react";

const PREFIX = "resq-ui:";

export function useSessionState<T>(key: string, initial: T): [T, Dispatch<SetStateAction<T>>] {
  // `ready` flips once the saved value has been applied. Writing only after
  // that matters: a write in the same commit as the load would store the
  // default over the saved value (and React's dev double-mount then reads the
  // clobbered default back).
  const [state, setState] = useState<{ value: T; ready: boolean }>({ value: initial, ready: false });

  useEffect(() => {
    let saved: T | undefined;
    try {
      const raw = sessionStorage.getItem(PREFIX + key);
      if (raw !== null) saved = JSON.parse(raw) as T;
    } catch { /* storage blocked or bad JSON: keep the default */ }
    setState((s) => ({ value: saved !== undefined ? saved : s.value, ready: true }));
  }, [key]);

  useEffect(() => {
    if (!state.ready) return;
    try { sessionStorage.setItem(PREFIX + key, JSON.stringify(state.value)); } catch { /* full or blocked */ }
  }, [key, state]);

  const setValue = useCallback<Dispatch<SetStateAction<T>>>((action) => {
    setState((s) => ({
      ...s,
      value: typeof action === "function" ? (action as (prev: T) => T)(s.value) : action,
    }));
  }, []);

  return [state.value, setValue];
}

