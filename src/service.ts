import { join } from "node:path"

/**
 * Set by the stub when it woke OpenCode with `--server <url>` for the
 * background service, to that URL. The next nap uses it to tell that
 * `--server` apart from one the user passed. OpenCode strips its password
 * variables from child processes but not this one; a nested OpenCode that
 * inherits it at worst re-reads the registration at its next nap.
 */
export const NAP_SERVER_ENV = "OPENCODE_NAP_SERVER"
const PASSWORD_ENV = "OPENCODE_PASSWORD"

/** Where OpenCode registers its background service: url, pid, password and version. */
export function serviceFile(env: NodeJS.ProcessEnv, home: string): string {
  // `??`, not `||`, as OpenCode itself reads it.
  return join(env.XDG_STATE_HOME ?? join(home, ".local", "state"), "opencode", "service.json")
}

/**
 * Removes the `--server <url>` and password the stub added on the last wake,
 * so this nap wakes the way the window was first started and reads the
 * service registration again.
 */
export function dropAddedServer(args: string[], env: NodeJS.ProcessEnv): { args: string[]; env: NodeJS.ProcessEnv } {
  const url = env[NAP_SERVER_ENV]
  if (url === undefined) return { args, env }
  const rest = { ...env }
  delete rest[NAP_SERVER_ENV]
  const at = args.findIndex((arg, i) => arg === "--server" && args[i + 1] === url)
  // Not the stub's: leave the user's --server and password alone.
  if (url === "" || at < 0) return { args, env: rest }
  delete rest[PASSWORD_ENV]
  return { args: [...args.slice(0, at), ...args.slice(at + 2)], env: rest }
}

/** Whether the command line picks a server itself, so the wake must not pick one. */
export const namesServer = (args: string[]): boolean =>
  args.some(arg => arg === "--server" || arg.startsWith("--server="))
