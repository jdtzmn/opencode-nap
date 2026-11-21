import { describe, expect, it } from "bun:test"
import { dropAddedServer, NAP_SERVER_ENV, namesServer, serviceFile } from "../src/service.js"

const URL = "http://127.0.0.1:49374"

describe("serviceFile", () => {
  it("is service.json under the XDG state directory, or ~/.local/state", () => {
    expect(serviceFile({ XDG_STATE_HOME: "/state" }, "/home/me")).toBe("/state/opencode/service.json")
    expect(serviceFile({}, "/home/me")).toBe("/home/me/.local/state/opencode/service.json")
  })
})

describe("dropAddedServer", () => {
  it("removes the --server, password and marker the stub added", () => {
    const env = { PATH: "/usr/bin", OPENCODE_PASSWORD: "pw", [NAP_SERVER_ENV]: URL }
    expect(dropAddedServer(["--server", URL, "-s", "ses_1", "project"], env)).toEqual({
      args: ["-s", "ses_1", "project"],
      env: { PATH: "/usr/bin" },
    })
  })

  it("leaves a window without the marker as it is", () => {
    const env = { OPENCODE_PASSWORD: "mine" }
    const args = ["--server", URL, "-s", "ses_1"]
    expect(dropAddedServer(args, env)).toEqual({ args, env })
  })

  it("keeps the user's own --server and password when the marker names another URL", () => {
    const args = ["--server", "http://127.0.0.1:4096", "-s", "ses_1"]
    expect(dropAddedServer(args, { OPENCODE_PASSWORD: "mine", [NAP_SERVER_ENV]: URL })).toEqual({
      args,
      env: { OPENCODE_PASSWORD: "mine" },
    })
    expect(dropAddedServer([`--server=${URL}`], { [NAP_SERVER_ENV]: URL }).args).toEqual([`--server=${URL}`])
  })
})

describe("namesServer", () => {
  it("sees both spellings of --server", () => {
    expect(namesServer(["--server", URL])).toBe(true)
    expect(namesServer([`--server=${URL}`])).toBe(true)
    expect(namesServer(["-s", "ses_1", "project"])).toBe(false)
  })
})
