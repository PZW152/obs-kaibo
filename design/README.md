# Kaibo design system

The source of Kaibo's look. The live, browsable version (colours, type, components, logo, icons) is the **Kaibo design system** artifact; this folder keeps the parts the plugin builds from.

| Path | What |
| --- | --- |
| `../data/theme/tokens.json` | All colour, spacing, radius and size tokens, dark and light. The plugin reads this file at runtime. |
| `../data/theme/kaibo.qss` | Qt stylesheet template; `@token@` placeholders are filled from `tokens.json`. |
| `../data/icons/*.svg` | Kaibo's 24px stroke icon set (`currentColor`, tinted at runtime). |
| `../tools/check-theme.py` | Standalone static guard: fails when `src/ui` or `kaibo.qss` hard-codes a colour or uses an undefined `@token@`. Run `python tools/check-theme.py`. |
| `logo/` | Mark (`kaibo-mark.svg`), single-ink glyphs and the lockup. |

Rules in short: colours, spacing, radii and control sizes come only from `tokens.json` — the QSS uses `@token@` placeholders and C++ reads them through `Theme::color()`/`Theme::length()`, never literal values; run `python tools/check-theme.py` before pushing (a genuinely unavoidable fallback needs a `check-theme: allow` comment with the reason). `brand` (Kaibo Rose `#d92d55`) once per view — the Go Live button, or the action bar while on air; status always as word + colour; danger is orange so it never reads as the brand; QR codes always dark on white; every text pair ≥ 4.5:1 in both themes. Never use Bilibili's TV mark or imply official status.
