import type { Config } from "tailwindcss";

// ============================================================================
// ResQ design tokens - Adobe Premiere Pro-inspired dark workstation look.
// Cold neutrals, sharp corners, dense layout, minimal color. Color is
// reserved for status semantics (triage, alarm, accent action).
// ============================================================================
const config: Config = {
  content: ["./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        // App neutrals (Premiere-like greys)
        app: {
          bg:      "#1a1a1a",   // window background
          panel:   "#252525",   // panel surface
          surface: "#2d2d2d",   // panel header / chrome
          raised:  "#353535",   // hover
          active:  "#404040",   // pressed/active
          input:   "#181818",   // inset (form fields)
          divider: "#0e0e0e",   // hard division between panels
          border:  "#3a3a3a",   // visible 1px border inside panels
          text:    "#d4d4d4",   // body
          dim:     "#909090",   // secondary
          muted:   "#6a6a6a",   // hint
        },

        // Accent (cold blue, Adobe-ish - reserved for primary actions)
        accent: {
          DEFAULT: "#5dade2",
          hover:   "#7bbeed",
          pressed: "#3d8fc4",
          ink:     "#0e2638",
        },

        // Triage semantics (muted vs prior tailwind primaries)
        triage: {
          green:  "#5fb878",
          yellow: "#d4a235",
          red:    "#d04545",
          black:  "#2a2a2a",
        },

        // Status pills used by the hub banner / chips
        status: {
          ok:   "#5fb878",
          warn: "#d4a235",
          err:  "#d04545",
          info: "#5dade2",
        },

        // Legacy tokens still used by existing markup - mapped to Premiere
        // so we don't have to rewrite every className. New code should
        // prefer app.* / accent.* / triage.* instead.
        panel: {
          DEFAULT: "#252525",
          soft:    "#2d2d2d",
          border:  "#3a3a3a",
        },
      },

      fontFamily: {
        sans: [
          "Inter",
          "-apple-system",
          "BlinkMacSystemFont",
          "Segoe UI",
          "Noto Sans Thai",
          "sans-serif",
        ],
        mono: [
          "ui-monospace",
          "SF Mono",
          "SFMono-Regular",
          "Menlo",
          "Consolas",
          "monospace",
        ],
      },

      fontSize: {
        // Tighter scale for data-dense workstation UI
        "2xs": ["10px", "12px"],
        xs:    ["11px", "14px"],
        sm:    ["12px", "16px"],
        base:  ["13px", "18px"],
        lg:    ["15px", "20px"],
        xl:    ["17px", "22px"],
        "2xl": ["20px", "26px"],
      },

      borderRadius: {
        none: "0",
        DEFAULT: "2px",
        sm: "2px",
        md: "3px",
        lg: "4px",
        xl: "6px",
      },

      boxShadow: {
        // Premiere has almost no soft shadows - just sharp insets
        none: "none",
        inset: "inset 0 1px 0 0 rgba(255,255,255,0.04)",
        panel: "0 0 0 1px #0e0e0e",
      },
    },
  },
  plugins: [],
};

export default config;
