"use client";

import { useState, KeyboardEvent } from "react";

interface Props {
  label: string;
  hint?: string;
  values: string[];
  onChange: (next: string[]) => void;
  placeholder?: string;
  suggestions?: string[];
  chipColor?: string;   // tailwind classes for chip bg+text
}

export function TagInput(props: Props) {
  const [draft, setDraft] = useState("");
  const chip = props.chipColor ?? "bg-app-raised text-app-text";

  function commit(text: string) {
    const t = text.trim();
    if (!t) return;
    if (props.values.includes(t)) { setDraft(""); return; }
    props.onChange([...props.values, t]);
    setDraft("");
  }
  function onKey(e: KeyboardEvent<HTMLInputElement>) {
    if (e.key === "Enter" || e.key === ",") {
      e.preventDefault();
      commit(draft);
    } else if (e.key === "Backspace" && draft === "" && props.values.length > 0) {
      props.onChange(props.values.slice(0, -1));
    }
  }
  function remove(i: number) {
    const next = props.values.slice();
    next.splice(i, 1);
    props.onChange(next);
  }

  return (
    <label className="block">
      <span className="field-label">{props.label}</span>
      <div className="flex flex-wrap gap-1 bg-app-input border border-app-divider px-1.5 py-1 focus-within:border-accent min-h-[28px] rounded-sm">
        {props.values.map((v, i) => (
          <span key={`${v}-${i}`} className={`inline-flex items-center gap-1 px-1.5 text-2xs uppercase tracking-wider font-semibold ${chip}`}>
            {v}
            <button type="button" onClick={() => remove(i)} className="opacity-70 hover:opacity-100" aria-label={`remove ${v}`}>×</button>
          </span>
        ))}
        <input
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={onKey}
          onBlur={() => draft && commit(draft)}
          placeholder={props.values.length === 0 ? props.placeholder : ""}
          className="min-w-[120px] flex-1 bg-transparent text-sm text-app-text outline-none px-1"
        />
      </div>
      {props.suggestions && props.suggestions.length > 0 && (
        <div className="mt-1 flex flex-wrap gap-1 text-2xs">
          <span className="text-app-muted">quick add:</span>
          {props.suggestions
            .filter((s) => !props.values.includes(s))
            .map((s) => (
              <button
                key={s}
                type="button"
                onClick={() => commit(s)}
                className="px-1 text-app-muted hover:text-app-text hover:bg-app-raised border border-app-divider"
              >
                + {s}
              </button>
            ))}
        </div>
      )}
      {props.hint && <span className="mt-1 block text-2xs text-app-muted">{props.hint}</span>}
    </label>
  );
}
