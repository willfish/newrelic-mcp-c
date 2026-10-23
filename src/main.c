#define _POSIX_C_SOURCE 200809L
#include "nr.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#define INPUT_LIMIT (1024U * 1024U)

static void emit(Json *response) {
    const char *text = json_object_to_json_string_ext(response, JSON_C_TO_STRING_PLAIN);
    /* Do not leak the configured credential even if an API error echoes it. */
    size_t key_len = nr_api_key ? strlen(nr_api_key) : 0;
    const char *p = text, *match;
    while (key_len && (match = strstr(p, nr_api_key))) {
        fwrite(p, 1, (size_t)(match - p), stdout);
        fputs("[redacted]", stdout); p = match + key_len;
    }
    fputs(p, stdout); fputc('\n', stdout); fflush(stdout);
    json_object_put(response);
}
static void reply(Json *id, Json *result, int code, const char *message) {
    Json *response = json_object_new_object();
    setstr(response, "jsonrpc", "2.0");
    json_object_object_add(response, "id", copy_json(id));
    if (code) {
        Json *error = json_object_new_object(); setint(error, "code", code); setstr(error, "message", message);
        json_object_object_add(response, "error", error);
        json_object_put(result);
    } else json_object_object_add(response, "result", result);
    emit(response);
}
static Json *find_tool(Json *tools, const char *name) {
    for (size_t i = 0; i < json_object_array_length(tools); i++) {
        Json *tool = json_object_array_get_idx(tools, i);
        if (!strcmp(str(tool, "name"), name)) return tool;
    }
    return NULL;
}
static void handle(Json *request, Json *tools) {
    Json *id = get(request, "id"), *params = get(request, "params");
    const char *version = str(request, "jsonrpc"), *method = str(request, "method");
    if (!version || strcmp(version, "2.0") || !method ||
        (id && !json_object_is_type(id, json_type_int) && !json_object_is_type(id, json_type_string)) ||
        (params && !json_object_is_type(params, json_type_object))) {
        reply(NULL, NULL, -32600, "Invalid JSON-RPC request"); return;
    }
    if (!id) return;
    if (!strcmp(method, "initialize")) {
        const char *requested = str(params, "protocolVersion");
        const char *supported[] = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
        const char *protocol = supported[0];
        for (size_t i = 0; i < sizeof supported / sizeof *supported; i++)
            if (requested && !strcmp(requested, supported[i])) protocol = requested;
        Json *result = json_object_new_object(), *caps = json_object_new_object(), *info = json_object_new_object();
        setstr(result, "protocolVersion", protocol);
        setstr(info, "name", "newrelic-mcp-c"); setstr(info, "version", "0.1.1");
        json_object_object_add(caps, "tools", json_object_new_object());
        json_object_object_add(result, "capabilities", caps);
        json_object_object_add(result, "serverInfo", info);
        setstr(result, "instructions", readonly_mode()
            ? "New Relic NRQL, APM, entities, alerts, Synthetics and REST v2. Read-only mode is enforced; mutation tools return an error. Account-scoped calls use the configured default or target_account_id."
            : "New Relic NRQL, APM, entities, alerts, Synthetics and REST v2. Obtain explicit authorization before mutations. Never retry a write with an uncertain outcome.");
        reply(id, result, 0, NULL);
    } else if (!strcmp(method, "ping")) reply(id, json_object_new_object(), 0, NULL);
    else if (!strcmp(method, "tools/list")) {
        Json *result = json_object_new_object();
        json_object_object_add(result, "tools", copy_json(tools)); reply(id, result, 0, NULL);
    } else if (!strcmp(method, "tools/call")) {
        const char *name = str(params, "name");
        Json *tool = name ? find_tool(tools, name) : NULL;
        if (!tool) { reply(id, NULL, -32602, "Unknown tool"); return; }
        Json *args = get(params, "arguments"), *empty = NULL;
        if (!args) args = empty = json_object_new_object();
        nr_error[0] = 0;
        if (!validate(tool, args)) { reply(id, NULL, -32602, nr_error); json_object_put(empty); return; }
        Json *value = execute(name, args);
        json_object_put(empty);
        bool error = !value;
        if (error && !*nr_error) fail("Tool execution failed");
        Json *result = json_object_new_object(), *content = json_object_new_array(), *text = json_object_new_object();
        setstr(text, "type", "text");
        setstr(text, "text", error ? nr_error : json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN));
        json_object_array_add(content, text);
        json_object_object_add(result, "content", content);
        json_object_object_add(result, "isError", json_object_new_boolean(error));
        json_object_put(value);
        reply(id, result, 0, NULL);
    } else reply(id, NULL, -32601, "Method not found");
}
int main(int argc, char **argv) {
    if (argc > 1) {
        if (argc == 2 && !strcmp(argv[1], "--version")) { puts("newrelic-mcp-c 0.1.1"); return 0; }
        fputs("Usage: newrelic-mcp-c [--version]\nMCP stdio server. Configure NEW_RELIC_API_KEY_FILE, NEW_RELIC_ACCOUNT_ID_FILE, NEW_RELIC_REGION_FILE and NEW_RELIC_READ_ONLY.\n", stderr);
        return argc == 2 && !strcmp(argv[1], "--help") ? 0 : 2;
    }
    struct rlimit core = {0, 0};
    setrlimit(RLIMIT_CORE, &core);
    if (curl_global_init(CURL_GLOBAL_DEFAULT)) return 1;
    Json *tools = catalog();
    char *line = malloc(INPUT_LIMIT + 2);
    if (!line || !tools) { fputs("Cannot allocate MCP server state\n", stderr); return 1; }
    if (readonly_mode()) {
        Json *raw = find_tool(tools, "run_nerdgraph_query"), *annotations = get(raw, "annotations");
        json_object_object_add(annotations, "readOnlyHint", json_object_new_boolean(true));
        json_object_object_add(annotations, "destructiveHint", json_object_new_boolean(false));
        json_object_object_add(annotations, "idempotentHint", json_object_new_boolean(true));
    }
    while (fgets(line, (int)INPUT_LIMIT + 2, stdin)) {
        size_t length = strlen(line);
        if (length > INPUT_LIMIT) {
            int c;
            if (line[length - 1] != '\n') while ((c = fgetc(stdin)) != '\n' && c != EOF) {}
            reply(NULL, NULL, -32700, "MCP input exceeds 1 MiB"); continue;
        }
        if (strspn(line, " \t\r\n") == length) continue;
        struct json_tokener *tok = json_tokener_new();
        json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
        Json *request = json_tokener_parse_ex(tok, line, (int)length);
        bool parsed = json_tokener_get_error(tok) == json_tokener_success;
        json_tokener_free(tok);
        if (!parsed) reply(NULL, NULL, -32700, "Invalid JSON");
        else if (!request || !json_object_is_type(request, json_type_object)) reply(NULL, NULL, -32600, "Invalid JSON-RPC request");
        else handle(request, tools);
        json_object_put(request);
    }
    free(line); json_object_put(tools);
    if (nr_api_key) { memset(nr_api_key, 0, strlen(nr_api_key)); free(nr_api_key); }
    curl_global_cleanup();
    return ferror(stdin) || ferror(stdout) ? 1 : 0;
}
