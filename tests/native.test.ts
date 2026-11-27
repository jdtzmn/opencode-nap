import { afterAll, beforeAll, describe, expect, it, setDefaultTimeout } from "bun:test"
import { mkdtempSync, rmSync } from "node:fs"
import { tmpdir } from "node:os"
import { join, resolve } from "node:path"
import { startPty } from "./e2e/pty.js"

const ESC = "\x1b"
const QUERY = `${ESC}[?2026$p`
const REPLY = `${ESC}[?2026;2$y`
const ON = `${ESC}[?2026h`
const OFF = `${ESC}[?2026l`
const ALT = `${ESC}[?1049h`
const CLEAR = `${ESC}[0m${ESC}[2J${ESC}[H`
// An app that asks whether the terminal holds the screen, opens the alternate
// screen with a blank frame, and paints after `delay` seconds.
const app = (delay: number) =>
  `printf '\\033[?2026$p'; sleep 0.2; printf '\\033[?1049h\\033[?2026h\\033[2J\\033[?2026l'; sleep ${delay}; printf '\\033[?2026hAPP\\033[?2026l'; sleep 0.1`

const root = resolve(import.meta.dir, "..")
let dir: string
let relay: string
let runs = 0

setDefaultTimeout(15_000)

describe.skipIf(process.platform === "win32")("native wake relay", () => {
  beforeAll(() => {
    dir = mkdtempSync(join(tmpdir(), "opencode-nap-native-"))
    relay = join(dir, "nap-relay")
    const build = Bun.spawnSync(["sh", join(root, "scripts", "build-native.sh"), dir], { stderr: "pipe" })
    if (build.exitCode !== 0) throw new Error(`the relay did not build:\n${build.stderr.toString()}`)
  })

  afterAll(() => {
    rmSync(dir, { recursive: true, force: true })
  })

  it("wake-screen unit tests pass", () => {
    const binary = join(dir, "wake-screen-test")
    const build = Bun.spawnSync([
      process.env.CC ?? "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", binary,
      join(root, "native", "wake-screen.c"), join(root, "tests", "native", "wake-screen.c"),
    ], { stderr: "pipe" })
    expect(build.exitCode, build.stderr.toString()).toBe(0)
    const run = Bun.spawnSync([binary], { stderr: "pipe" })
    expect(run.exitCode, run.stderr.toString()).toBe(0)
  })

  /** Runs a command through the relay, answering the sync query when `reply`, typing `input` once it says READY. */
  async function relayed(command: string[], options: { reply?: boolean; input?: string } = {}) {
    const pty = await startPty({
      command: [relay, "--wake", ...command],
      env: process.env as Record<string, string>,
      cwd: dir,
      outputFile: join(dir, `run-${++runs}.out`),
    })
    let replied = !options.reply
    let typed = options.input === undefined
    const watch = setInterval(() => {
      const output = pty.output()
      if (!replied && output.includes(QUERY)) {
        pty.write(REPLY)
        replied = true
      }
      if (!typed && output.includes("READY")) {
        pty.write(options.input ?? "")
        typed = true
      }
    }, 5)
    try {
      const status = await Promise.race([pty.exited, Bun.sleep(5_000).then(() => "timeout" as const)])
      return { status, output: pty.output() }
    } finally {
      clearInterval(watch)
      await pty.stop()
    }
  }

  it("passes output and the exit status through", async () => {
    expect(await relayed(["/bin/echo", "hello"])).toEqual({ status: 0, output: "hello\r\n" })
    expect((await relayed(["/bin/sh", "-c", "exit 7"])).status).toBe(7)
  })

  it("passes raw input through", async () => {
    const run = await relayed(["/bin/sh", "-c", "stty raw -echo; printf READY; head -c 3; echo"], { input: "abc" })
    expect(run.status).toBe(0)
    expect(run.output).toContain("READYabc")
  })

  it("holds the screen from the alternate screen until the first visible frame", async () => {
    const run = await relayed(["/bin/sh", "-c", app(0.3)], { reply: true })
    expect(run.status).toBe(0)
    // One held frame: the blank frame's own sync boundaries are dropped.
    expect(run.output).toEndWith(`${ON}${CLEAR}${ALT}${ESC}[2JAPP${OFF}`)
  })

  it("lets go after 750 ms when the app is slow to paint", async () => {
    const run = await relayed(["/bin/sh", "-c", app(1.2)], { reply: true })
    expect(run.status).toBe(0)
    expect(run.output).toEndWith(`${ON}${CLEAR}${ALT}${ESC}[2J${OFF}${ON}APP${OFF}`)
  })

  it("does not hold on a terminal that never answers the query", async () => {
    const run = await relayed(["/bin/sh", "-c", app(0.3)])
    expect(run.status).toBe(0)
    expect(run.output).toEndWith(`${CLEAR}${ALT}${ON}${ESC}[2J${OFF}${ON}APP${OFF}`)
  })
})
