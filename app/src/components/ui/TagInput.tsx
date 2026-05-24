"use client";

// ----------------------------------------------------------------------------
// TagInput - chip-style multi-value input. Users type a value and press Enter
// or comma to add it. Backspace on empty input removes the last tag.
// Used for medical lists (allergies, conditions, medications).
// ----------------------------------------------------------------------------

import { useState, KeyboardEvent } from "react";

interface Props {
  label: string;
  hint?: string;
  values: string[];
  onChange: (next: string[]) => void;
  placeholder?: string;
  suggestions?: string[];
  chipColor?: string;     // tailwind bg class, e.g. "bg-amber-700"
}

export function TagInput(props: Props) {
  const [draft, setDraft] = useState("");
  const chip = props.chipColor ?? "bg-slate-700";

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
      <span className="mb-1 block text-xs font-semibold uppercase tracking-wide text-slate-400">
        {props.label}
      </span>
      <div className="flex flex-wrap gap-1.5 rounded border border-panel-border bg-panel px-2 py-1.5 focus-within:border-emerald-500">
        {props.values.map((v, i) => (
          <span
            key={`${v}-${i}`}
            className={`inline-flex items-center gap-1 rounded px-2 py-0.5 text-xs ${chip} text-white`}
          >
            {v}
            <button
              type="button"
              onClick={() => remove(i)}
              className="text-white/70 hover:text-white"
              aria-label={`ลบ ${v}`}
            >
              ×
            </button>
          </span>
        ))}
        <input
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={onKey}
          onBlur={() => draft && commit(draft)}
          placeholder={props.values.length === 0 ? props.placeholder : ""}
          className="min-w-[120px] flex-1 bg-transparent px-1 text-sm text-slate-100 outline-none"
        />
      </div>
      {props.suggestions && props.suggestions.length > 0 && (
        <div className="mt-1 flex flex-wrap gap-1 text-[11px]">
          <span className="text-slate-500">เลือกด่วน:</span>
          {props.suggestions
            .filter((s) => !props.values.includes(s))
            .map((s) => (
              <button
                key={s}
                type="button"
                onClick={() => commit(s)}
                className="rounded border border-slate-700 px-1.5 py-0.5 text-slate-400 hover:border-slate-500 hover:text-slate-200"
              >
                + {s}
              </button>
            ))}
        </div>
      )}
      {props.hint && <span className="mt-1 block text-[11px] text-slate-500">{props.hint}</span>}
    </label>
  );
}
