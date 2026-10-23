#!/usr/bin/env python3
"""Opt-in live read-only smoke check. Prints no account names, telemetry or keys.

Usage: python3 tests/live_reads.py /path/to/newrelic-mcp-c
Requires an account with an APM application, host and metric to cover all reads.
Credentials come from the server's normal environment or *_FILE configuration.
"""
import json
import os
from pathlib import Path
import select
import subprocess
import sys

binary = str(Path(sys.argv[1]).resolve())
env = os.environ.copy()
env["NEW_RELIC_READ_ONLY"] = "1"
proc = subprocess.Popen([binary], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, text=True)
sequence = 0
covered = set()


def request(method, params=None):
    global sequence
    sequence += 1
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": sequence, "method": method,
                                "params": params or {}}) + "\n")
    proc.stdin.flush()
    if not select.select([proc.stdout], [], [], 90)[0]:
        raise RuntimeError("Live MCP request timed out")
    response = json.loads(proc.stdout.readline())
    assert response["id"] == sequence and "error" not in response, "MCP protocol error"
    return response["result"]


def call(tool_name, **arguments):
    result = request("tools/call", {"name": tool_name, "arguments": arguments})
    if result.get("isError"):
        raise RuntimeError(f"{tool_name}: {result['content'][0]['text']}")
    value = json.loads(result["content"][0]["text"])
    covered.add(tool_name)
    print(f"PASS {tool_name}", flush=True)
    return value


try:
    init = request("initialize", {"protocolVersion": "2025-03-26", "capabilities": {},
                                  "clientInfo": {"name": "live-read-check", "version": "1"}})
    assert init["serverInfo"]["name"] == "newrelic-mcp-c"
    tools = request("tools/list")["tools"]
    assert len(tools) == 20
    assert request("ping") == {}
    account = call("get_account_details")
    call("run_nrql_query", nrql="SELECT count(*) FROM Transaction SINCE 5 minutes ago")
    call("run_nerdgraph_query", query="{actor{accounts{id name}}}")
    apps = call("list_apm_applications")
    assert apps, "No APM entity available for the entity-details check"
    call("get_entity_details", entity_guid=apps[0]["guid"])
    call("search_entities", query="domain = 'APM'", entity_types=["APPLICATION"])
    call("list_alert_policies")
    call("list_open_incidents", priority="HIGH")
    call("list_synthetics_monitors", monitor_type="BROWSER")
    call("list_alert_policies_rest", auto_paginate=True)
    call("list_open_incidents_rest", only_open=True, auto_paginate=True)
    pages = call("list_apm_applications_rest", auto_paginate=True)["items"]
    rest_apps = [app for page in pages for app in page["applications"]]
    assert rest_apps, "No REST APM application available"
    app = next((a for a in rest_apps if a.get("reporting")), rest_apps[0])
    app_id = app["id"]
    call("list_deployments_rest", application_id=app_id, auto_paginate=True)
    host_pages = call("list_application_hosts", application_id=app_id, auto_paginate=True)["items"]
    hosts = [host for page in host_pages for host in page["application_hosts"]]
    assert hosts, "No host available for metric checks"
    host_id = hosts[0]["id"]
    metrics = call("list_metric_names_for_host", application_id=app_id, host_id=host_id)["items"]["metrics"]
    assert metrics, "No host metric available"
    call("get_metric_data_for_host", application_id=app_id, host_id=host_id,
         names=[metrics[0]["name"]], summarize=True)
    expected = {t["name"] for t in tools} - {"create_browser_monitor", "acknowledge_incident",
                                              "create_deployment", "delete_deployment"}
    assert covered == expected, "Incomplete read-only coverage"
    print("16/16 live read capabilities passed; no mutations executed.")
finally:
    proc.stdin.close()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    stderr = proc.stderr.read()
    proc.stdout.close()
    proc.stderr.close()
    if stderr:
        print("Server wrote to stderr; inspect locally without publishing credentials.", file=sys.stderr)
    if proc.returncode:
        raise RuntimeError(f"Server exited with status {proc.returncode}")
