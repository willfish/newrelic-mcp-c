# Development

[Back to README](../README.md)

## Build from source

Requires a C11 compiler, Make, pkg-config, Python 3, and libcurl and json-c
development headers. Python only embeds `tools.json` into the binary at build time
and runs the checks; it is not a runtime dependency.

```sh
make
make install PREFIX="$HOME/.local"
```

`PREFIX` and `DESTDIR` are supported. With Nix, `nix build` produces
`./result/bin/newrelic-mcp-c`, and `direnv allow` provides a development shell.

## Checks

```sh
direnv exec . make test
```

This exercises all 20 capabilities against a local HTTP fixture with a fake key,
including mutation request bodies, pagination, filters and the read-only guard.
Only the separate test binary accepts a loopback fixture endpoint; production
builds have no endpoint override.

For opt-in **live reads only**, configure credentials as described in
[configuration](configuration.md) and run:

```sh
python3 tests/live_reads.py ./result/bin/newrelic-mcp-c
```

The script forces read-only mode and checks all 16 read capabilities. It needs
an accessible APM application, a host and at least one host metric. An empty
account cannot establish full coverage. It prints capability names rather than
telemetry or credentials and never creates resources or acknowledges issues.

## Release packaging

Releases are built on an Apple Silicon Mac and published from a Linux machine with
an authenticated `gh`. CI still builds and tests on GitHub's Intel and Apple
Silicon runners, but tags do not publish anything automatically.

- `packaging/build-macos` builds one architecture (`ARCH=aarch64` or
  `ARCH=x86_64`): checksum-pinned json-c source linked statically, Apple's libcurl
  and libSystem, a macOS 14 deployment target and an ad-hoc signature. It rejects
  any other dynamic library and runs `make test` against the packaged build; Intel
  tests run under Rosetta on Apple Silicon.
- `packaging/release-relay` copies the committed tree to the Mac over SSH
  (`RELAY_HOST`, default `relay.fritz.box`), builds both architectures, publishes
  both archives, `install` and `SHA256SUMS`, then installs each architecture from
  the public release on the Mac. The Mac needs the Xcode command line tools,
  Rosetta and either cmake or Nix.

To release:

1. Bump the version in `src/main.c`, `src/api.c` (user agent), `default.nix` and
   the `install` default, then commit. The build fails if the tag does not match
   the binary's `--version`.
2. Check the build without publishing: `packaging/release-relay --dry-run`.
3. Tag and push: `git tag -a vX.Y.Z -m vX.Y.Z && git push origin master vX.Y.Z`.
4. Publish: `packaging/release-relay vX.Y.Z`.

`packaging/release-relay --verify vX.Y.Z` repeats the install check for an existing
release.
