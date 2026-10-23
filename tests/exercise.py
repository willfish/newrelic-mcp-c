#!/usr/bin/env python3
"""Exercise the 20 capabilities offline. Uses only local fixtures and a fake key."""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

PRODUCTION, FIXTURE = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
del sys.argv[1:3]
KEY = "NRAK-offline-fixture-key"
REQUESTS = []


class API(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def answer(self, data, status=200, more=False):
        self.send_response(status)
        if more:
            # The credential must never follow a supplied URL, only a page number.
            self.send_header("Link", '<https://untrusted.invalid/v2/ignored?page=2>; rel="next"')
        self.end_headers()
        if data is not None:
            self.wfile.write(json.dumps(data).encode())

    def do_POST(self):
        assert self.headers["API-Key"] == KEY
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        REQUESTS.append(("POST", self.path, body))
        if self.path != "/graphql":
            assert self.path == "/v2/applications/42/deployments.json"
            assert body["deployment"]["revision"] == 'testing"\\revision'
            self.answer({"deployment": {"id": 99, **body["deployment"]}}, 201)
            return
        q, v = body["query"], body["variables"]
        if "failFixture" in q:
            self.answer({"errors": [{"message": "fixture GraphQL failure"}]})
        elif "echoCredential" in q:
            self.answer({"data": {"echoCredential": KEY}})
        elif "syntheticsCreateSimpleBrowserMonitor" in q:
            assert v["monitor"]["name"] == 'testing"\\monitor'
            assert v["monitor"]["period"] == "EVERY_HOUR"
            assert v["monitor"]["status"] == "ENABLED"
            assert v["monitor"]["locations"]["public"] == ["AWS_EU_WEST_1"]
            self.answer({"data": {"syntheticsCreateSimpleBrowserMonitor": {
                "monitor": {"id": "monitor-1", "guid": "guid-1", **v["monitor"]}, "errors": []}}})
        elif "aiIssuesAckIssue" in q:
            assert v["issue"] == 'issue"\\id' and v["id"] == 123
            self.answer({"data": {"aiIssuesAckIssue": {"error": None, "result": {
                "accountId": 123, "issueId": v["issue"], "action": "ACK"}}}})
        elif "nrql(query:" in q:
            assert v["id"] == 123 and '\\' in v["nrql"] and '"' in v["nrql"]
            self.answer({"data": {"actor": {"account": {"nrql": {
                "results": [{"count": 7}], "metadata": {"eventTypes": ["Transaction"],
                "facets": [], "timeWindow": {"begin": 1, "end": 2}}}}}}})
        elif "policiesSearch" in q:
            second = bool(v.get("cursor"))
            self.answer({"data": {"actor": {"account": {"alerts": {"policiesSearch": {
                "policies": [{"id": "2" if second else "1", "name": "policy", "incidentPreference": "PER_POLICY"}],
                "nextCursor": None if second else "next"}}}}}})
        elif "aiIssues{issues" in q:
            assert v["filter"]["priority"] == ["HIGH"]
            assert "CLOSED" not in v["filter"]["states"]
            self.answer({"data": {"actor": {"account": {"aiIssues": {"issues": {
                "issues": [{"issueId": "open-issue", "state": "ACTIVATED"}], "nextCursor": None}}}}}})
        elif "entitySearch" in q:
            expression = v["q"]
            second = bool(v.get("cursor"))
            if "SYNTH" in expression:
                assert "monitorType" not in expression
                items = [{"guid": "monitor", "monitorType": "BROWSER"},
                         {"guid": "other-monitor", "monitorType": "SCRIPT_API"}]
            elif "language reporting" in q:
                items = [{"guid": "app-2" if second else "app-1", "name": "test app", "language": "ruby",
                          "reporting": True, "alertSeverity": "NOT_ALERTING", "tags": [{"key": "env", "values": ["test", "other"]}]}]
            else:
                assert expression.startswith("(name = 'a' OR name = 'b') AND accountId = '123'")
                assert "type IN ('APPLICATION','HOST')" in expression
                items = [{"guid": "app-1", "name": "test app", "tags": []}]
            self.answer({"data": {"actor": {"entitySearch": {"results": {
                "entities": items, "nextCursor": None if second else "next"}}}}})
        elif "entity(guid:" in q:
            assert v["guid"] == 'guid"\\test'
            assert "relatedEntities" in q and "goldenMetrics" in q
            self.answer({"data": {"actor": {"entity": {"guid": v["guid"], "name": "details", "settings": {"apdexTarget": 0.5}}}}})
        elif "account(id:" in q:
            assert v["id"] == 123
            self.answer({"data": {"actor": {"account": {"id": 123, "name": "fixture account"}}}})
        else:
            self.answer({"data": {"actor": {"fixture": v}}})

    def do_GET(self):
        assert self.headers["API-Key"] == KEY
        parts = urlsplit(self.path)
        query = parse_qs(parts.query)
        REQUESTS.append(("GET", parts.path, query))
        second = query.get("page") == ["2"]
        if parts.path == "/v2/applications.json":
            assert query["filter[name]"] == ["test & app"]
            assert query["filter[ids]"] == ["42,43"]
            self.answer({"applications": [{"id": 43 if second else 42}]}, more=not second)
        elif parts.path.endswith("/deployments.json"):
            self.answer({"deployments": [{"id": 100 if second else 99}]}, more=not second)
        elif parts.path.endswith("/hosts.json"):
            assert query["filter[hostname]"] == ["host & one"]
            assert query["filter[ids]"] == ["7,8"]
            self.answer({"application_hosts": [{"id": 8 if second else 7}]}, more=not second)
        elif parts.path.endswith("/metrics/data.json"):
            assert query["names[]"] == ["HttpDispatcher", "Custom/quote & slash"]
            assert query["values[]"] == ["average_response_time", "call_count"]
            assert query["from"] == ["2026-10-01T00:00:00+00:00"]
            assert query["period"] == ["60"] and query["summarize"] == ["false"]
            self.answer({"metric_data": {"metrics": [{"name": "HttpDispatcher", "timeslices": []}]}}, more=not second)
        elif parts.path.endswith("/metrics.json"):
            assert query["name"] == ["Http & Dispatcher"]
            self.answer({"metrics": [{"name": "HttpDispatcher", "values": ["call_count"]}]}, more=not second)
        elif parts.path == "/v2/alerts_policies.json":
            assert query["filter[name]"] == ["policy & one"]
            self.answer({"policies": [{"id": 1}]}, more=not second)
        elif parts.path == "/v2/alerts_incidents.json":
            self.answer({"incidents": [
                {"id": 1, "closed_at": None, "priority": "HIGH"},
                {"id": 2, "closed_at": 10, "priority": "HIGH"},
                {"id": 3, "closed_at": 0, "priority": "LOW"}]}, more=not second)
        else:
            self.answer({"error": "fixture missing"}, 404)

    def do_DELETE(self):
        REQUESTS.append(("DELETE", self.path, None))
        assert self.headers["API-Key"] == KEY
        assert self.path == "/v2/applications/42/deployments/99.json"
        self.answer(None, 204)


class Client:
    def __init__(self, binary=FIXTURE, **overrides):
        env = {k: v for k, v in os.environ.items() if not k.startswith(("NEW_RELIC_", "NR_TEST_"))}
        env.update(NEW_RELIC_API_KEY=KEY, NEW_RELIC_ACCOUNT_ID="123", NEW_RELIC_REGION="EU",
                   NR_TEST_ENDPOINT=ENDPOINT)
        env.update(overrides)
        self.proc = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, env=env)
        self.id = 0

    def request(self, method, params=None):
        self.id += 1
        req = {"jsonrpc": "2.0", "id": self.id, "method": method}
        if params is not None:
            req["params"] = params
        self.proc.stdin.write(json.dumps(req) + "\n")
        self.proc.stdin.flush()
        assert select.select([self.proc.stdout], [], [], 10)[0], "MCP response timeout"
        result = json.loads(self.proc.stdout.readline())
        assert result["id"] == self.id
        return result

    def call(self, tool_name, **args):
        response = self.request("tools/call", {"name": tool_name, "arguments": args})
        assert "error" not in response, response
        result = response["result"]
        assert not result.get("isError"), result
        return json.loads(result["content"][0]["text"])

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)
        err = self.proc.stderr.read()
        self.proc.stdout.close()
        self.proc.stderr.close()
        assert self.proc.returncode == 0 and not err, (self.proc.returncode, err)


class Capabilities(unittest.TestCase):
    def setUp(self):
        self.client = Client()
        self.before = len(REQUESTS)

    def tearDown(self):
        self.client.close()

    def test_protocol_and_credential_free_discovery(self):
        c = Client(PRODUCTION, NEW_RELIC_API_KEY="")
        try:
            r = c.request("initialize", {"protocolVersion": "2024-11-05"})["result"]
            self.assertEqual(r["protocolVersion"], "2024-11-05")
            self.assertEqual(r["serverInfo"]["name"], "newrelic-mcp-c")
            self.assertEqual(c.request("ping")["result"], {})
            tools = c.request("tools/list")["result"]["tools"]
            self.assertEqual(len(tools), 20)
            self.assertEqual(len({t["name"] for t in tools}), 20)
            self.assertEqual(c.request("missing")["error"]["code"], -32601)
            self.assertNotIn(b"NR_TEST_ENDPOINT", Path(PRODUCTION).read_bytes())
        finally:
            c.close()

    def test_graphql_read_capabilities(self):
        c = self.client
        self.assertEqual(c.call("get_account_details")["accountId"], 123)
        r = c.call("run_nrql_query", nrql='SELECT count(*) FROM Transaction WHERE name = \'a"\\b\' TIMESERIES')
        self.assertEqual(r["results"], [{"count": 7}])
        self.assertTrue(r["metadata"]["timeSeries"])
        apps = c.call("list_apm_applications")
        self.assertEqual(len(apps), 2)
        self.assertEqual(apps[0]["tags"], {"env": "test"})
        self.assertEqual(len(c.call("list_alert_policies")), 2)
        self.assertEqual(len(c.call("list_open_incidents", priority="HIGH")), 1)
        self.assertEqual(len(c.call("list_synthetics_monitors", monitor_type="BROWSER")), 2)
        r = c.call("search_entities", query="name = 'a' OR name = 'b'", entity_types=["APPLICATION", "HOST"])
        self.assertEqual(r["nextCursor"], "next")
        r = c.call("search_entities", query="name = 'a' OR name = 'b'", entity_types=["APPLICATION", "HOST"], cursor="next")
        self.assertIsNone(r["nextCursor"])
        self.assertEqual(c.call("get_entity_details", entity_guid='guid"\\test')["settings"]["apdexTarget"], 0.5)
        self.assertEqual(c.call("run_nerdgraph_query", query="query($v:String!){actor{fixture}}", variables={"v": "quoted\nvalue"})["data"]["actor"]["fixture"], {"v": "quoted\nvalue"})

    def test_offline_mutations(self):
        c = self.client
        self.assertEqual(c.call("acknowledge_incident", incident_id='issue"\\id')["action"], "ACK")
        self.assertEqual(c.call("create_browser_monitor", name='testing"\\monitor', url="https://example.com/", frequency=60, locations=["AWS_EU_WEST_1"])["period"], "EVERY_HOUR")
        self.assertEqual(c.call("create_deployment", application_id=42, revision='testing"\\revision', changelog="test", description="testing", user="testing")["status"], 201)
        self.assertEqual(c.call("delete_deployment", application_id=42, id=99, confirm=True), {"status": 204, "data": {}})
        self.assertEqual(c.call("run_nerdgraph_query", query="mutation{actor{fixture}}", variables={"offline": True})["data"]["actor"]["fixture"], {"offline": True})

    def test_all_rest_reads_filters_and_pagination(self):
        c = self.client
        calls = [
            ("list_deployments_rest", {"application_id": 42}),
            ("list_apm_applications_rest", {"filter_name": "test & app", "filter_host": "host", "filter_ids": [42, 43], "filter_language": "ruby"}),
            ("list_application_hosts", {"application_id": 42, "filter_hostname": "host & one", "filter_ids": "7,8"}),
            ("list_metric_names_for_host", {"application_id": 42, "host_id": 7, "name": "Http & Dispatcher"}),
            ("get_metric_data_for_host", {"application_id": 42, "host_id": 7, "names": ["HttpDispatcher", "Custom/quote & slash"], "values": ["average_response_time", "call_count"], "from": "2026-10-01T00:00:00+00:00", "to": "2026-10-02T00:00:00Z", "period": 60, "summarize": False}),
            ("list_alert_policies_rest", {"filter_name": "policy & one"}),
            ("list_open_incidents_rest", {"only_open": True, "priority": "HIGH"}),
        ]
        for name, args in calls:
            with self.subTest(tool=name):
                one = c.call(name, **args)
                self.assertIsInstance(one["items"], dict)
                all_pages = c.call(name, auto_paginate=True, region="US", **args)
                self.assertEqual(len(all_pages["items"]), 2)
                self.assertEqual(all_pages["page"], 2)
                if name == "list_open_incidents_rest":
                    self.assertEqual(all_pages["items"][0]["incidents"], [{"id": 1, "closed_at": None, "priority": "HIGH"}])

    def test_validation_and_graphql_errors_are_not_empty_success(self):
        cases = [("delete_deployment", {"application_id": 42, "id": 99, "confirm": False}),
                 ("get_metric_data_for_host", {"application_id": 42, "host_id": 7, "names": []}),
                 ("create_browser_monitor", {"name": "test", "url": "https://example.com", "frequency": 2, "locations": ["x"]}),
                 ("list_apm_applications_rest", {"page": -1}),
                 ("run_nrql_query", {"nrql": " "}),
                 ("get_account_details", {"unexpected": True}),
                 ("list_deployments_rest", {"application_id": "42"})]
        for name, args in cases:
            with self.subTest(tool=name):
                r = self.client.request("tools/call", {"name": name, "arguments": args})
                self.assertEqual(r["error"]["code"], -32602)
        self.assertEqual(len(REQUESTS), self.before)
        r = self.client.request("tools/call", {"name": "run_nerdgraph_query", "arguments": {"query": "{failFixture}"}})
        self.assertTrue(r["result"]["isError"])
        self.assertIn("fixture GraphQL failure", r["result"]["content"][0]["text"])
        self.assertEqual(self.client.call("run_nerdgraph_query", query="{echoCredential}")["data"]["echoCredential"], "[redacted]")
        r = self.client.request("tools/call", {"name": "get_account_details", "arguments": {"target_account_id": "123) { injection }"}})
        self.assertTrue(r["result"]["isError"])

    def test_readonly_file_credentials_and_raw_mutation_guard(self):
        with tempfile.TemporaryDirectory() as d:
            key = Path(d) / "key"
            key.write_text(KEY + "\n")
            c = Client(NEW_RELIC_API_KEY="", NEW_RELIC_API_KEY_FILE=str(key), NEW_RELIC_READ_ONLY="1")
            try:
                self.assertEqual(c.call("get_account_details")["accountId"], 123)
                before = len(REQUESTS)
                writes = [("create_deployment", {"application_id": 42, "revision": "x"}),
                          ("delete_deployment", {"application_id": 42, "id": 99, "confirm": True}),
                          ("acknowledge_incident", {"incident_id": "x"}),
                          ("create_browser_monitor", {"name": "x", "url": "https://example.com", "frequency": 5, "locations": ["x"]})]
                for query in ["mutation{anything}", "# misleading query\n mutation X{anything}", "query{ok} mutation{anything}", 'query{f(s:"""block""")} mutation{anything}', "subscription{anything}"]:
                    writes.append(("run_nerdgraph_query", {"query": query}))
                for name, args in writes:
                    r = c.request("tools/call", {"name": name, "arguments": args})
                    self.assertTrue(r["result"]["isError"], r)
                    self.assertIn("Read-only", r["result"]["content"][0]["text"])
                self.assertEqual(len(REQUESTS), before)
                c.call("run_nerdgraph_query", query='# mutation is only a comment\nquery{actor{fixture(arg:"mutation and subscription")}}')
            finally:
                c.close()


if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", 0), API)
    ENDPOINT = f"http://127.0.0.1:{server.server_port}"
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        unittest.main(verbosity=2)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
