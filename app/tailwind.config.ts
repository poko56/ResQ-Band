import type { Config } from "tailwindcss";

const config: Config = {
  content: ["./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        triage: {
          green: "#22c55e",
          yellow: "#eab308",
          red: "#ef4444",
          black: "#1f2937",
        },
        panel: {
          DEFAULT: "#0f172a",
          soft: "#1e293b",
          border: "#334155",
        },
      },
      fontFamily: {
        mono: ["ui-monospace", "SFMono-Regular", "Menlo", "monospace"],
      },
    },
  },
  plugins: [],
};

export default config;
