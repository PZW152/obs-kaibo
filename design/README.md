# Kaibo design system

The source of Kaibo's look. The live, browsable version (colours, type, components, logo, icons) is the **Kaibo design system** artifact; this folder keeps the parts the plugin builds from.

| Path | What |
| --- | --- |
| `../data/theme/tokens.json` | All colour, spacing, radius and size tokens, dark and light. The plugin reads this file at runtime. |
| `../data/theme/kaibo.qss` | Qt stylesheet template; `@token@` placeholders are filled from `tokens.json`. |
| `../data/icons/*.svg` | Kaibo's 24px stroke icon set (`currentColor`, tinted at runtime). |
| `logo/` | Mark (`kaibo-mark.svg`), single-ink glyphs and the lockup. |

Rules in short: `brand` (Kaibo Rose `#d92d55`) once per view — the Go Live button; status always as word + colour; danger is orange so it never reads as the brand; QR codes always dark on white; every text pair ≥ 4.5:1 in both themes. Never use Bilibili's TV mark or imply official status.
