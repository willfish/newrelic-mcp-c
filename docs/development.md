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

- `packaging/build-macos` builds on the native architecture: checksum-pinned
  json-c source linked statically, Apple's libcurl and libSystem, a macOS 14
  deployment target and an ad-hoc signature. It rejects any other dynamic library
  and runs `make test` against the packaged build.
- `.github/workflows/release.yml` runs that script on Intel and Apple Silicon
  runners for a `v*` tag, then publishes both archives, `install` and
  `SHA256SUMS`.

For a release, keep the version in `src/main.c`, `src/api.c` (user agent),
`default.nix` and the `install` default in sync. The build fails if the tag does
not match the binary's `--version`. Push the tag, then check the public curl
installation on a Mac.
