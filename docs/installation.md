# Installation

[Back to README](../README.md)

## macOS

Prebuilt binaries support macOS 14 or newer on Intel and Apple Silicon.

```sh
curl -fsSL https://github.com/willfish/newrelic-mcp-c/releases/latest/download/install | sh
```

The installer downloads the archive for your architecture, checks it against the
release `SHA256SUMS`, confirms the binary starts, then places it at
`~/.local/bin/newrelic-mcp-c` (or `/usr/local/bin` when run as root). It does
not use sudo, Homebrew or shell startup files. Licence notices go to
`<prefix>/share/doc/newrelic-mcp-c`.

Set an absolute `PREFIX` on the **shell side** of the pipe to install elsewhere,
and `NEWRELIC_MCP_VERSION` to pin a release:

```sh
curl -fsSL https://github.com/willfish/newrelic-mcp-c/releases/latest/download/install \
  | PREFIX="$HOME/tools" NEWRELIC_MCP_VERSION=v0.1.0 sh
```

To skip the installer, download `newrelic-mcp-c-<version>-darwin-<arch>.tar.gz`
from the [releases page](https://github.com/willfish/newrelic-mcp-c/releases)
(`aarch64` for Apple Silicon, `x86_64` for Intel), check it against `SHA256SUMS`,
and extract it. The archive contains the binary, licence notices and a list of
build dependencies.

The binary links only to libraries that ship with macOS (`libSystem` and
`libcurl`); json-c is compiled in. It is ad-hoc signed, not notarised. Files
downloaded with curl are not quarantined, so Gatekeeper does not block them. If
you download the archive with a browser instead, clear the quarantine flag with
`xattr -d com.apple.quarantine newrelic-mcp-c` after checking the checksum.

Checksums detect corruption against the release manifest. They are not an
independent signature or a substitute for trusting the release publisher. You
can download `install`, read it, then run `sh install`.

To upgrade, rerun the installer. To uninstall, remove
`<prefix>/bin/newrelic-mcp-c` and `<prefix>/share/doc/newrelic-mcp-c`.

## Nix

```sh
nix profile install github:willfish/newrelic-mcp-c
```

The flake builds on Linux and macOS, x86_64 and ARM64.

## Linux and other systems

There are no prebuilt Linux binaries. Use Nix or [build from source](development.md#build-from-source).
