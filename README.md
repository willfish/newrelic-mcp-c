<div align="center">

# newrelic-mcp-c

**New Relic for your AI assistant, without a JavaScript runtime.**

A New Relic MCP server written in C. One small binary that starts in
milliseconds, so your assistant can query NRQL, inspect entities and check
alerts without installing Node.js or pulling hundreds of npm packages.

[![Release](https://img.shields.io/github/v/release/willfish/newrelic-mcp-c?style=flat-square)](https://github.com/willfish/newrelic-mcp-c/releases/latest)
[![C](https://img.shields.io/badge/written_in-C-555?style=flat-square)](docs/development.md)
[![Platforms](https://img.shields.io/badge/macOS-Intel_%7C_Apple_Silicon-5277c3?style=flat-square)](#quick-start)
[![MCP](https://img.shields.io/badge/MCP-stdio-0f766e?style=flat-square)](#connect-your-client)

[Get started](#quick-start) · [Why C?](#why-c) · [Documentation](#documentation)

</div>

## Why C?

An MCP server is a thin translation layer: read JSON-RPC from stdin, call an HTTPS
API, write JSON back. MCP clients launch one per session, often alongside several
others, and keep them running in the background. A layer that thin should not
need a language runtime and a dependency tree larger than the job it does.

The [Node.js server](https://github.com/cloudbring/newrelic-mcp) offers
the same capabilities. Measured side by side on the same Linux x86_64 machine,
listing tools without credentials:

| | newrelic-mcp-c | newrelic-mcp 2.1.0 on Node.js 22 |
| --- | --- | --- |
| **What you install** | One binary, a 36 KB download | Node.js plus 385 npm packages (208 MB of `node_modules`) |
| **Start to first tool list** | ~4 ms | ~100 ms |
| **Peak memory** | ~14 MiB | ~74 MiB |
| **Runtime dependencies** | libcurl and json-c | Node.js 20+ and the npm tree |
| **Nix closure** | 63 MiB, mostly glibc and OpenSSL shared with the rest of the system | 247 MiB for Node.js alone, before any packages |

What that buys you:

- **Nothing to manage on a Mac.** The release binary links only to libraries that
  ship with macOS. No Homebrew, no `nvm`, no runtime version to keep compatible.
- **Fewer things to trust.** Two widely deployed C libraries instead of a
  transitive npm tree, and no `npx` step resolving packages when your client
  starts.
- **Instant, cheap startup.** Clients that spawn servers per session or per
  window do not wait for a JavaScript runtime to boot, and idle servers stay small.
- **Safer defaults for an API key with write access.** A read-only mode the
  model cannot switch off, secrets read from files, fixed HTTPS endpoints with no
  redirects and no automatic retries of writes.

The trade-off is that C needs more care than TypeScript. The server is about 800
lines, compiled with strict warnings, and every tool is exercised against a local
New Relic fixture before each release.

## Quick start

On macOS 14 or newer, Intel or Apple Silicon:

```sh
curl -fsSL https://github.com/willfish/newrelic-mcp-c/releases/latest/download/install | sh
```

This installs `~/.local/bin/newrelic-mcp-c` after checking its checksum. No sudo
or Homebrew needed. You can also download the binary for your architecture
directly from the [latest release](https://github.com/willfish/newrelic-mcp-c/releases/latest).

On Linux, use [Nix](docs/installation.md#nix) or
[build from source](docs/development.md#build-from-source).

### Connect your client

Create a New Relic **User API key**, then add the server to your client's MCP
configuration using absolute paths:

```json
{
  "mcpServers": {
    "newrelic": {
      "command": "/Users/you/.local/bin/newrelic-mcp-c",
      "env": {
        "NEW_RELIC_API_KEY_FILE": "/Users/you/.config/newrelic/api-key",
        "NEW_RELIC_ACCOUNT_ID": "1234567",
        "NEW_RELIC_READ_ONLY": "1"
      }
    }
  }
}
```

Writes are allowed unless `NEW_RELIC_READ_ONLY` is set. EU accounts also need
`NEW_RELIC_REGION=EU`. See [configuration](docs/configuration.md) for every option.

## What it does

Twenty tools covering NRQL and NerdGraph queries, APM applications and hosts,
entity search and details, alert policies and incidents, synthetic monitors,
deployment markers and host metrics.

Try asking your assistant:

> What is the error rate for checkout-api over the last hour?
>
> Which alerts are open right now, and on which entities?
>
> Record a deployment marker for revision 4f2a1c on billing-service.

[See every tool and its response format →](docs/tools.md)

## Documentation

[Installation](docs/installation.md) · [Configuration](docs/configuration.md) ·
[Tool reference](docs/tools.md) · [Development](docs/development.md)

Tool coverage is derived from
[cloudbring/newrelic-mcp](https://github.com/cloudbring/newrelic-mcp); its MIT
attribution is retained in [LICENSE](LICENSE).
