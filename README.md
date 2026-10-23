# newrelic-mcp-c

Native C stdio MCP server for New Relic, using libcurl and json-c. No Node.js,
Python or package downloads at runtime. Python is used only to embed the tool
catalog at build time and for optional checks.

Implements the 20 capabilities advertised by
[cloudbring/newrelic-mcp](https://github.com/cloudbring/newrelic-mcp), including its
two unregistered REST alert tools. Uses the current NerdGraph schema rather than
reproducing broken upstream queries. Inspired by upstream revision
`dc6e4261f9af614a5ad1539469870780a84a9779`; MIT attribution is retained in `LICENSE`.

## Build and run

```sh
nix build
export NEW_RELIC_API_KEY_FILE="$HOME/.config/sops-nix/secrets/NEW_RELIC_API_KEY"
export NEW_RELIC_ACCOUNT_ID_FILE="$HOME/.config/sops-nix/secrets/NEW_RELIC_ACCOUNT_ID"
export NEW_RELIC_REGION_FILE="$HOME/.config/sops-nix/secrets/NEW_RELIC_REGION"
export NEW_RELIC_READ_ONLY=1
./result/bin/newrelic-mcp-c
```

The process reads one JSON-RPC message per line from stdin and writes responses
only to stdout. It supports tool discovery without credentials, initialization,
ping and MCP tools/list and tools/call. Negotiates MCP 2024-11-05, 2025-03-26,
2025-06-18 or 2025-11-25. It does not advertise resources or prompts.

Without Nix, install a C11 compiler, make, pkg-config, Python 3, libcurl development
headers and json-c development headers, then run `make` and `make install`.
`PREFIX` and `DESTDIR` are supported.

## Configuration

| Variable | Purpose |
| --- | --- |
| `NEW_RELIC_API_KEY` | New Relic **User API key**, required for API calls |
| `NEW_RELIC_ACCOUNT_ID` | Default numeric account ID for account-scoped tools |
| `NEW_RELIC_REGION` | `US` (default) or `EU`; applies to both NerdGraph and REST |
| `NEW_RELIC_READ_ONLY` | Set to `1` to enforce read-only operation |

The first three also accept a `_FILE` suffix. A configured file takes precedence
over the corresponding environment value; trailing newlines are removed. Keep
secret contents out of Nix expressions, the Nix store and version control.

**Writes are allowed by default.** Read-only mode is enabled when
`NEW_RELIC_READ_ONLY` is set to anything except `0` or `false` (case-insensitive).
It blocks all four named write tools and raw GraphQL containing mutation or
subscription keywords outside strings/comments. The raw-query guard is
conservative and can also reject those keywords used as field or operation names.
Only the process environment controls this setting, not tool arguments.

An MCP client can register a command with a configuration such as:

```json
{
  "mcpServers": {
    "newrelic": {
      "command": "/absolute/path/to/newrelic-mcp-c",
      "env": {
        "NEW_RELIC_API_KEY_FILE": "/run/user/1000/secrets/NEW_RELIC_API_KEY",
        "NEW_RELIC_ACCOUNT_ID": "1234567",
        "NEW_RELIC_REGION": "EU",
        "NEW_RELIC_READ_ONLY": "1"
      }
    }
  }
}
```

Use the actual secret paths on your machine. Pi also accepts a wrapper command
that exports these file paths before executing the binary.

## Capabilities

- **Queries:** `run_nrql_query`, `run_nerdgraph_query`, `get_account_details`.
- **Entities and APM:** `list_apm_applications`, `search_entities`,
  `get_entity_details`, `list_apm_applications_rest`, `list_application_hosts`.
- **Alerts:** `list_alert_policies`, `list_open_incidents`,
  `list_alert_policies_rest`, `list_open_incidents_rest`, `acknowledge_incident`.
- **Synthetics:** `list_synthetics_monitors`, `create_browser_monitor`.
- **Deployments:** `list_deployments_rest`, `create_deployment`,
  `delete_deployment`.
- **Host metrics:** `list_metric_names_for_host`, `get_metric_data_for_host`.

`tools/list` supplies the exact argument schemas. Account-scoped NerdGraph tools
accept `target_account_id`; REST tools accept an optional `region` override.
Read-only annotations are hints, not authorization. Monitor creation schedules
checks and can incur usage. Obtain authorization before any write, including raw
NerdGraph mutations. Deployment deletion additionally requires `confirm: true`.

### Pagination and response contracts

Tool results contain JSON encoded as MCP text content. API failures set
`isError: true`; invalid arguments produce JSON-RPC invalid-params errors. GraphQL
partial-data responses containing errors are failures, not empty successes.

- Entity search returns `{entities, nextCursor}` and accepts `cursor`.
- NerdGraph APM, policy, issue and monitor lists follow cursors and return arrays.
- REST reads return `{items, page}`. Normally `items` is one API page object;
  with `auto_paginate: true` it is an array of API page objects. `page` is the last
  fetched page. All REST reads support a starting `page`.
- REST writes return `{status, data}`, including successful empty DELETE bodies.
- Raw NerdGraph returns the full `{data: ...}` envelope. Other NerdGraph tools
  extract their relevant payload. NRQL includes metadata and a `timeSeries` hint.

These are capability-compatible, not byte-for-byte copies of upstream responses.
Open incidents use current **issues** IDs and acknowledgement requires an account
ID. Entity details include recent violations, APM settings, related entities with
a continuation cursor, and golden metric **definitions**, not fabricated values.
REST incident filtering operates on the nested incidents array in each page;
use `only_open: true` to exclude closed incidents.

The client uses fixed New Relic HTTPS endpoints, verifies TLS, refuses redirects,
limits each response to 16 MiB and pagination to 100 pages. Connection/request
timeouts are 10/60 seconds. Link headers contribute page numbers only, never a
new host or path. Requests are not retried automatically. A timed-out write can
have succeeded remotely; inspect state before deciding whether to retry.

## Checks

```sh
direnv allow
direnv exec . make test
```

This exercises all 20 capabilities against a local HTTP fixture with a fake key,
including mutation request bodies, pagination, filters and the read-only guard.
Only the separate test binary accepts a loopback fixture endpoint; production
builds have no endpoint override.

For opt-in **live reads only**, configure credentials as above and run:

```sh
python3 tests/live_reads.py ./result/bin/newrelic-mcp-c
```

The script forces read-only mode and checks all 16 read capabilities. It needs
an accessible APM application, a host and at least one host metric. An empty
account cannot establish full coverage. It prints capability names rather than
telemetry or credentials and never creates resources or acknowledges issues.
