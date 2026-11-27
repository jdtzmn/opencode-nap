import { channel, tint, type Color } from "./color.js"
import type { Frame } from "./frame.js"
import { NAP_DETAIL, NAP_MESSAGE } from "./message.js"
import type { Palette } from "./palette.js"
import { paint, type Placement, type Style } from "./screen.js"
import { textWidth } from "./text.js"

/** How far each colour of the frame moves toward the theme's background. */
export const DIM = { fg: 0.6, bg: 0.4 }
/** How far the dialog's background moves from the theme's background toward its text. */
export const DIALOG_LIFT = 0.08
/**
 * The largest screen the stub is handed, in bytes. It is one argument, and
 * Linux takes at most 128 KiB per argument; a bigger frame gets the logo card.
 */
export const FRAME_CARD_BUDGET = 96 * 1024

const TITLE = "OpenCode is sleeping"
const PAD_X = 4
const PAD_Y = 1
// Cells kept clear of the dialog at each side of the screen.
const MARGIN = 2

// eslint-disable-next-line no-control-regex
const CONTROL = /[\u0000-\u001f\u007f-\u009f]/g

const same = (a: Color, b: Color): boolean =>
  channel(a.r) === channel(b.r) && channel(a.g) === channel(b.g) && channel(a.b) === channel(b.b)

/** The frame's cells, dimmed, as placements. Blank runs on the plain background are left to the fill. */
function framePlacements(frame: Frame, palette: Palette): Placement[] {
  const base = palette.background
  const placements: Placement[] = []
  frame.forEach((spans, row) => {
    let col = 0
    for (const span of spans) {
      const at = col
      col += span.width
      // A transparent cell shows the terminal's own background, as it did in the TUI.
      const bg = span.bg.a > 0 ? tint(span.bg, base, DIM.bg) : undefined
      const plain = bg ? base.a > 0 && same(bg, base) : base.a === 0
      const text = (plain ? span.text.trimEnd() : span.text).replace(CONTROL, " ")
      if (!text) continue
      const style: Style = { fg: tint(span.fg, base, DIM.fg), bold: span.bold }
      if (bg) style.bg = bg
      // Each span is placed on its own, so a character the terminal draws wider or narrower than OpenTUI counted shifts nothing after it.
      placements.push({ row, col: at, spans: [{ text, style }] })
    }
  })
  return placements
}

/** The dialog, centred near the top so it covers little of the frame: title, then the message, on a background lifted a little from the theme's. Undefined when it does not fit. */
function dialogPlacements(width: number, height: number, palette: Palette): Placement[] | undefined {
  const room = width - 2 * MARGIN - 2 * PAD_X
  const joined = NAP_MESSAGE.join(" ")
  const message = textWidth(joined) <= room ? [joined, NAP_DETAIL] : [...NAP_MESSAGE, NAP_DETAIL]
  const lines: Array<{ text: string; style: Style }> = [
    { text: TITLE, style: { fg: palette.text, bold: true } },
    { text: "", style: {} },
    ...message.map(text => ({ text, style: { fg: palette.muted } })),
  ]
  const inner = Math.max(...lines.map(line => textWidth(line.text)))
  const boxWidth = inner + 2 * PAD_X
  const boxHeight = lines.length + 2 * PAD_Y
  if (inner > room || boxHeight > height) return undefined
  const left = Math.floor((width - boxWidth) / 2)
  // One row down from the top, or flush with it when the screen has no row to spare.
  const top = Math.min(1, height - boxHeight)
  const bg = tint(palette.background, palette.text, DIALOG_LIFT)
  return Array.from({ length: boxHeight }, (_, index) => {
    const line = lines[index - PAD_Y]
    const text = line?.text ?? ""
    const offset = Math.floor((inner - textWidth(text)) / 2)
    return {
      row: top + index,
      col: left,
      spans: [
        { text: " ".repeat(PAD_X + offset), style: { bg } },
        { text, style: { ...line?.style, bg } },
        { text: " ".repeat(PAD_X + inner - offset - textWidth(text)), style: { bg } },
      ],
    }
  })
}

/**
 * The nap screen from the TUI's last frame: the frame dimmed toward the
 * theme's background with the nap dialog near its top. Undefined when the
 * dialog does not fit or the screen is over the budget.
 */
export function frameCard(frame: Frame, palette: Palette): string | undefined {
  const height = frame.length
  const width = frame[0]?.reduce((sum, span) => sum + span.width, 0) ?? 0
  const dialog = dialogPlacements(width, height, palette)
  if (!dialog) return undefined
  const card = paint([...framePlacements(frame, palette), ...dialog], palette.background)
  return Buffer.byteLength(card) <= FRAME_CARD_BUDGET ? card : undefined
}
