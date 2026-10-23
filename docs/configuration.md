# Configuration

[Back to README](../README.md)

## Environment

| Variable | Purpose |
| --- | --- |
| `NEW_RELIC_API_KEY` | New Relic **User API key**, required for API calls |
| `NEW_RELIC_ACCOUNT_ID` | Default numeric account ID for account-scoped tools |
| `NEW_RELIC_REGION` | `US` (default) or `EU`; applies to both NerdGraph and REST |
| `NEW_RELIC_READ_ONLY` | Set to `1` to enforce read-only operation |

The first three also accept a `_FILE` suffix, for example
`NEW_RELIC_API_KEY_FILE`. A configured file takes precedence over the
corresponding environment value; trailing newlines are removed. Use files when
your secrets already live in a secret manager such as sops-nix, and keep secret
contents out of MCP client configuration that you share or commit.

Tool discovery works without credentials, so a client can list tools before a key
is configured.

## Read-only mode

**Writes are allowed by default.** Read-only mode is enabled when
`NEW_RELIC_READ_ONLY` is set to anything except `0` or `false` (case-insensitive).
It blocks all four named write tools and raw GraphQL containing mutation or
subscription keywords outside strings/comments. The raw-query guard is
conservative and can also reject those keywords used as field or operation names.
Only the process environment controls this setting, not tool arguments.

## MCP client configuration

```json
{
  "mcpServers": {
    "newrelic": {
      "command": "/Users/you/.local/bin/newrelic-mcp-c",
      "env": {
        "NEW_RELIC_API_KEY_FILE": "/Users/you/.config/newrelic/api-key",
        "NEW_RELIC_ACCOUNT_ID": "1234567",
        "NEW_RELIC_REGION": "EU",
        "NEW_RELIC_READ_ONLY": "1"
      }
    }
  }
}
```

Use absolute paths; most clients do not expand `~` or inherit your shell's
`PATH`. A wrapper script that exports these variables before executing the
binary also works.
