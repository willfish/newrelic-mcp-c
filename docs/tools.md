# Tool reference

[Back to README](../README.md)

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

The server speaks MCP over stdio: one JSON-RPC message per line on stdin,
responses only on stdout. It supports initialization, ping, `tools/list` and
`tools/call`, and negotiates MCP 2024-11-05, 2025-03-26, 2025-06-18 or
2025-11-25. It does not advertise resources or prompts.

## Pagination and response contracts

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

## Differences from cloudbring/newrelic-mcp

The tools are capability-compatible with
[cloudbring/newrelic-mcp](https://github.com/cloudbring/newrelic-mcp), including
its two unregistered REST alert tools, but responses are not byte-for-byte copies.
Queries use the current NerdGraph schema. Open incidents use current **issues**
IDs and acknowledgement requires an account ID. Entity details include recent
violations, APM settings, related entities with a continuation cursor, and golden
metric **definitions**, not fabricated values. REST incident filtering operates
on the nested incidents array in each page; use `only_open: true` to exclude
closed incidents.

## Network behaviour

The client uses fixed New Relic HTTPS endpoints, verifies TLS, refuses redirects,
limits each response to 16 MiB and pagination to 100 pages. Connection/request
timeouts are 10/60 seconds. Link headers contribute page numbers only, never a
new host or path. Requests are not retried automatically. A timed-out write can
have succeeded remotely; inspect state before deciding whether to retry.
