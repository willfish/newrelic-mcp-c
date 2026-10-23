#define _GNU_SOURCE
#include "nr.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "catalog.h"

Json *catalog(void) { return json_tokener_parse(tool_catalog); }

/* Validate the small, explicit schema vocabulary used by this catalog. */
static bool valid_value(Json *value, Json *schema) {
    const char *type = str(schema, "type");
    if (!value || !type) return false;
    enum json_type actual = json_object_get_type(value);
    if ((!strcmp(type, "string") && actual != json_type_string)
        || (!strcmp(type, "integer") && actual != json_type_int)
        || (!strcmp(type, "boolean") && actual != json_type_boolean)
        || (!strcmp(type, "array") && actual != json_type_array)
        || (!strcmp(type, "object") && actual != json_type_object)) return false;
    if (actual == json_type_string) {
        const char *text = json_object_get_string(value);
        size_t len = (size_t)json_object_get_string_len(value);
        if (strlen(text) != len) return false;
        if (get(schema, "minLength")) {
            bool nonblank = false;
            for (const unsigned char *p = (const unsigned char *)text; *p; p++)
                if (!isspace(*p)) nonblank = true;
            if (!nonblank) return false;
        }
    }
    if (actual == json_type_int) {
        long long n = json_object_get_int64(value);
        if (get(schema, "minimum") && n < json_object_get_int64(get(schema, "minimum"))) return false;
        if (get(schema, "maximum") && n > json_object_get_int64(get(schema, "maximum"))) return false;
    }
    Json *choices = get(schema, "enum");
    if (choices) {
        bool matched = false;
        for (size_t i = 0; i < json_object_array_length(choices); i++)
            if (json_object_equal(value, json_object_array_get_idx(choices, i))) matched = true;
        if (!matched) return false;
    }
    if (actual == json_type_array) {
        size_t count = json_object_array_length(value);
        if (get(schema, "minItems") && count < (size_t)json_object_get_int(get(schema, "minItems"))) return false;
        for (size_t i = 0; i < count; i++)
            if (!valid_value(json_object_array_get_idx(value, i), get(schema, "items"))) return false;
    }
    return true;
}
bool validate(Json *tool, Json *args) {
    if (!args || !json_object_is_type(args, json_type_object)) return fail("arguments must be an object");
    Json *schema = get(tool, "inputSchema"), *props = get(schema, "properties"), *required = get(schema, "required");
    if (required) {
        for (size_t i = 0; i < json_object_array_length(required); i++) {
            const char *name = json_object_get_string(json_object_array_get_idx(required, i));
            if (!get(args, name)) return fail("Missing required argument: %s", name);
        }
    }
    json_object_object_foreach(args, name, value) {
        Json *prop = get(props, name);
        if (!prop) return fail("Unknown argument: %s", name);
        if (!valid_value(value, prop)) return fail("Invalid argument: %s", name);
    }
    return true;
}

static int account_id(Json *args) {
    const char *id = str(args, "target_account_id");
    char *configured = NULL;
    if (!id) { configured = config_value("NEW_RELIC_ACCOUNT_ID"); id = configured; }
    bool digits = id && *id;
    if (id) for (const unsigned char *p = (const unsigned char *)id; *p; p++)
        if (!isdigit(*p)) digits = false;
    errno = 0;
    long value = digits ? strtol(id, NULL, 10) : 0;
    free(configured);
    if (errno || value <= 0 || value > INT_MAX) { fail("Provide a positive numeric target_account_id or NEW_RELIC_ACCOUNT_ID[_FILE]"); return 0; }
    return (int)value;
}
static Json *at(Json *obj, const char *path) {
    char *parts = strdup(path), *save = NULL;
    if (!parts) { fail("Allocation failed"); return NULL; }
    for (char *p = strtok_r(parts, "/", &save); p; p = strtok_r(NULL, "/", &save)) obj = get(obj, p);
    free(parts);
    return obj;
}
static Json *extract(Json *response, const char *path) {
    if (!response) return NULL;
    Json *result = copy_json(at(response, path));
    if (!result) fail("New Relic response is missing %s", path);
    json_object_put(response);
    return result;
}
static Json *gql_list(const char *query, Json *vars, const char *path, const char *field) {
    Json *all = json_object_new_array();
    char *previous = NULL;
    for (int page = 0; page < 100; page++) {
        Json *response = graphql(query, vars, NULL);
        if (!response) goto error;
        Json *container = at(response, path), *values = get(container, field);
        if (!values || !json_object_is_type(values, json_type_array)) {
            fail("New Relic response is missing list %s/%s", path, field);
            json_object_put(response); goto error;
        }
        for (size_t i = 0; i < json_object_array_length(values); i++)
            json_object_array_add(all, copy_json(json_object_array_get_idx(values, i)));
        const char *next = str(container, "nextCursor");
        if (!next || !*next) { json_object_put(response); free(previous); return all; }
        if (previous && !strcmp(previous, next)) {
            fail("New Relic pagination cursor did not advance"); json_object_put(response); goto error;
        }
        free(previous); previous = strdup(next);
        setstr(vars, "cursor", next);
        json_object_put(response);
    }
    fail("New Relic pagination exceeded 100 pages; narrow the query");
error:
    free(previous); json_object_put(all); return NULL;
}
static Json *entity_list(const char *expression, const char *fields) {
    char *query = NULL;
    if (asprintf(&query, "query($q:String!,$cursor:String){actor{entitySearch(query:$q){results(cursor:$cursor){entities{%s} nextCursor}}}}", fields) < 0) {
        fail("Allocation failed"); return NULL;
    }
    Json *vars = json_object_new_object();
    setstr(vars, "q", expression);
    Json *result = gql_list(query, vars, "data/actor/entitySearch/results", "entities");
    json_object_put(vars); free(query);
    return result;
}
static Json *search_entities(Json *args, int id) {
    char *expression = NULL;
    if (asprintf(&expression, "(%s) AND accountId = '%d'", str(args, "query"), id) < 0) return NULL;
    Json *types = get(args, "entity_types");
    if (types && json_object_array_length(types)) {
        for (size_t i = 0; i < json_object_array_length(types); i++) {
            const char *type = json_object_get_string(json_object_array_get_idx(types, i));
            for (const unsigned char *p = (const unsigned char *)type; *p; p++)
                if (!isalnum(*p) && *p != '_') { free(expression); fail("entity_types must contain type codes"); return NULL; }
            char *next = NULL;
            if (asprintf(&next, "%s%s'%s'", expression, i ? "," : " AND type IN (", type) < 0) { free(expression); return NULL; }
            free(expression); expression = next;
        }
        char *next = NULL;
        if (asprintf(&next, "%s)", expression) < 0) { free(expression); return NULL; }
        free(expression); expression = next;
    }
    Json *vars = json_object_new_object();
    setstr(vars, "q", expression); free(expression);
    if (str(args, "cursor")) setstr(vars, "cursor", str(args, "cursor"));
    Json *response = graphql("query($q:String!,$cursor:String){actor{entitySearch(query:$q){results(cursor:$cursor){entities{guid name type domain tags{key values}} nextCursor}}}}", vars, NULL);
    json_object_put(vars);
    return extract(response, "data/actor/entitySearch/results");
}
static void filter_incidents(Json *data, Json *args) {
    Json *items = get(data, "incidents");
    if (!items || !json_object_is_type(items, json_type_array)) return;
    Json *filtered = json_object_new_array();
    bool only_open = json_object_get_boolean(get(args, "only_open"));
    const char *priority = str(args, "priority");
    for (size_t i = 0; i < json_object_array_length(items); i++) {
        Json *inc = json_object_array_get_idx(items, i);
        Json *closed = get(inc, "closed_at");
        if (only_open && closed && json_object_get_int64(closed) != 0) continue;
        const char *p = str(inc, "priority");
        if (priority && (!p || strcmp(p, priority))) continue;
        json_object_array_add(filtered, copy_json(inc));
    }
    json_object_object_add(data, "incidents", filtered);
}
static Json *rest_list(const char *path, Json *query, Json *args, bool incidents) {
    Json *pages = json_object_new_array();
    bool auto_page = json_object_get_boolean(get(args, "auto_paginate"));
    long page = get(args, "page") ? (long)json_object_get_int64(get(args, "page")) : 1;
    for (int count = 0; count < 100; count++) {
        setint(query, "page", page);
        long next = 0;
        Json *data = http_request("GET", path, query, NULL, str(args, "region"), NULL, &next);
        if (!data) goto error;
        if (incidents) filter_incidents(data, args);
        json_object_array_add(pages, data);
        if (!auto_page || !next) {
            Json *result = json_object_new_object();
            json_object_object_add(result, "items", auto_page ? copy_json(pages) : copy_json(json_object_array_get_idx(pages, 0)));
            setint(result, "page", page);
            json_object_put(pages); return result;
        }
        if (next <= page) { fail("New Relic pagination page did not advance"); goto error; }
        page = next;
    }
    fail("New Relic pagination exceeded 100 pages; narrow the query");
error:
    json_object_put(pages); return NULL;
}
static Json *execute_rest(const char *name, Json *args) {
    char path[256];
    long long app = json_object_get_int64(get(args, "application_id"));
    long long host = json_object_get_int64(get(args, "host_id"));
    Json *query = json_object_new_object(), *result = NULL;
    if (!strcmp(name, "create_deployment") || !strcmp(name, "delete_deployment")) {
        snprintf(path, sizeof path, "/v2/applications/%lld/deployments%s", app, !strcmp(name, "create_deployment") ? ".json" : "");
        Json *body = NULL;
        if (!strcmp(name, "create_deployment")) {
            body = json_object_new_object();
            Json *deployment = json_object_new_object();
            const char *keys[] = {"revision", "changelog", "description", "user"};
            for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
                if (get(args, keys[i])) json_object_object_add(deployment, keys[i], copy_json(get(args, keys[i])));
            json_object_object_add(body, "deployment", deployment);
        } else {
            snprintf(path, sizeof path, "/v2/applications/%lld/deployments/%lld.json", app, (long long)json_object_get_int64(get(args, "id")));
        }
        long status = 0;
        Json *data = http_request(body ? "POST" : "DELETE", path, NULL, body, str(args, "region"), &status, NULL);
        json_object_put(body);
        if (data) {
            result = json_object_new_object();
            setint(result, "status", status);
            json_object_object_add(result, "data", data);
        }
        goto done;
    }
    bool incidents = false;
    if (!strcmp(name, "list_deployments_rest")) snprintf(path, sizeof path, "/v2/applications/%lld/deployments.json", app);
    else if (!strcmp(name, "list_apm_applications_rest")) {
        strcpy(path, "/v2/applications.json");
        const char *inputs[] = {"filter_name", "filter_host", "filter_language"};
        const char *outputs[] = {"filter[name]", "filter[host]", "filter[language]"};
        for (int i = 0; i < 3; i++) if (get(args, inputs[i])) json_object_object_add(query, outputs[i], copy_json(get(args, inputs[i])));
        Json *ids = get(args, "filter_ids");
        if (ids && json_object_array_length(ids)) {
            size_t count = json_object_array_length(ids);
            char *joined = calloc(count, 22);
            if (!joined) { fail("Allocation failed"); goto done; }
            size_t pos = 0;
            for (size_t i = 0; i < count; i++)
                pos += (size_t)sprintf(joined + pos, "%s%lld", i ? "," : "", (long long)json_object_get_int64(json_object_array_get_idx(ids, i)));
            setstr(query, "filter[ids]", joined); free(joined);
        }
    } else if (!strcmp(name, "list_application_hosts")) {
        snprintf(path, sizeof path, "/v2/applications/%lld/hosts.json", app);
        if (get(args, "filter_hostname")) json_object_object_add(query, "filter[hostname]", copy_json(get(args, "filter_hostname")));
        if (get(args, "filter_ids")) json_object_object_add(query, "filter[ids]", copy_json(get(args, "filter_ids")));
    } else if (!strcmp(name, "list_metric_names_for_host")) {
        snprintf(path, sizeof path, "/v2/applications/%lld/hosts/%lld/metrics.json", app, host);
        if (get(args, "name")) json_object_object_add(query, "name", copy_json(get(args, "name")));
    } else if (!strcmp(name, "get_metric_data_for_host")) {
        snprintf(path, sizeof path, "/v2/applications/%lld/hosts/%lld/metrics/data.json", app, host);
        const char *keys[] = {"names", "values", "from", "to", "period", "summarize"};
        for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
            if (get(args, keys[i])) json_object_object_add(query, keys[i], copy_json(get(args, keys[i])));
    } else if (!strcmp(name, "list_alert_policies_rest")) {
        strcpy(path, "/v2/alerts_policies.json");
        if (get(args, "filter_name")) json_object_object_add(query, "filter[name]", copy_json(get(args, "filter_name")));
    } else if (!strcmp(name, "list_open_incidents_rest")) {
        strcpy(path, "/v2/alerts_incidents.json"); incidents = true;
    } else { fail("Unknown REST tool"); goto done; }
    result = rest_list(path, query, args, incidents);
done:
    json_object_put(query); return result;
}
Json *execute(const char *name, Json *args) {
    bool raw = !strcmp(name, "run_nerdgraph_query");
    bool write = !strcmp(name, "create_deployment") || !strcmp(name, "delete_deployment")
                 || !strcmp(name, "create_browser_monitor") || !strcmp(name, "acknowledge_incident");
    if (readonly_mode() && (write || (raw && !graphql_readonly(str(args, "query"))))) {
        fail("Read-only mode: mutations are disabled (NEW_RELIC_READ_ONLY)"); return NULL;
    }
    if (raw) return graphql(str(args, "query"), get(args, "variables"), NULL);
    if (strstr(name, "_rest") || !strcmp(name, "create_deployment") || !strcmp(name, "delete_deployment")
        || !strcmp(name, "list_application_hosts") || !strcmp(name, "list_metric_names_for_host") || !strcmp(name, "get_metric_data_for_host"))
        return execute_rest(name, args);
    Json *vars = json_object_new_object(), *result = NULL;
    if (!strcmp(name, "get_entity_details")) {
        setstr(vars, "guid", str(args, "entity_guid"));
        result = extract(graphql("query($guid:EntityGuid!){actor{entity(guid:$guid){guid name type domain entityType reporting tags{key values} recentAlertViolations{alertSeverity violationId openedAt closedAt violationUrl} ...on ApmApplicationEntity{language settings{apdexTarget}} relatedEntities{nextCursor results{type source{guid entity{name}} target{guid entity{name}}}} goldenMetrics{metrics{name title query unit}}}}}", vars, NULL), "data/actor/entity");
        goto done;
    }
    int id = account_id(args);
    if (!id) goto done;
    setint(vars, "id", id);
    if (!strcmp(name, "get_account_details")) {
        Json *account = extract(graphql("query($id:Int!){actor{account(id:$id){id name}}}", vars, NULL), "data/actor/account");
        if (account) {
            result = json_object_new_object();
            json_object_object_add(result, "accountId", copy_json(get(account, "id")));
            setstr(result, "name", str(account, "name")); json_object_put(account);
        }
    } else if (!strcmp(name, "run_nrql_query")) {
        const char *nrql = str(args, "nrql");
        setstr(vars, "nrql", nrql);
        result = extract(graphql("query($id:Int!,$nrql:Nrql!){actor{account(id:$id){nrql(query:$nrql){results metadata{eventTypes timeWindow{begin end} facets}}}}}", vars, NULL), "data/actor/account/nrql");
        if (result) {
            Json *metadata = get(result, "metadata");
            if (!metadata) { metadata = json_object_new_object(); json_object_object_add(result, "metadata", metadata); }
            char *lower = strdup(nrql);
            if (!lower) { json_object_put(result); result = NULL; fail("Allocation failed"); goto done; }
            for (char *p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);
            json_object_object_add(metadata, "timeSeries", json_object_new_boolean(strstr(lower, "timeseries") != NULL)); free(lower);
        }
    } else if (!strcmp(name, "search_entities")) result = search_entities(args, id);
    else if (!strcmp(name, "list_apm_applications")) {
        char expression[160];
        snprintf(expression, sizeof expression, "domain = 'APM' AND type = 'APPLICATION' AND accountId = '%d'", id);
        result = entity_list(expression, "guid name ...on ApmApplicationEntityOutline{language reporting alertSeverity tags{key values}}");
        if (result) for (size_t i = 0; i < json_object_array_length(result); i++) {
            Json *app = json_object_array_get_idx(result, i), *tags = get(app, "tags"), *map = json_object_new_object();
            if (tags && json_object_is_type(tags, json_type_array)) for (size_t j = 0; j < json_object_array_length(tags); j++) {
                Json *tag = json_object_array_get_idx(tags, j), *values = get(tag, "values");
                if (str(tag, "key") && values && json_object_array_length(values))
                    json_object_object_add(map, str(tag, "key"), copy_json(json_object_array_get_idx(values, 0)));
            }
            json_object_object_add(app, "tags", map);
            if (!str(app, "language")) setstr(app, "language", "unknown");
            if (!get(app, "reporting")) json_object_object_add(app, "reporting", json_object_new_boolean(false));
        }
    } else if (!strcmp(name, "list_alert_policies"))
        result = gql_list("query($id:Int!,$cursor:String){actor{account(id:$id){alerts{policiesSearch(cursor:$cursor){policies{id name incidentPreference} nextCursor}}}}}", vars, "data/actor/account/alerts/policiesSearch", "policies");
    else if (!strcmp(name, "list_open_incidents")) {
        Json *filter = json_object_new_object(), *states = json_object_new_array();
        json_object_array_add(states, json_object_new_string("CREATED"));
        json_object_array_add(states, json_object_new_string("ACTIVATED"));
        json_object_array_add(states, json_object_new_string("DEACTIVATED"));
        json_object_object_add(filter, "states", states);
        if (str(args, "priority")) {
            Json *p = json_object_new_array(); json_object_array_add(p, copy_json(get(args, "priority")));
            json_object_object_add(filter, "priority", p);
        }
        json_object_object_add(vars, "filter", filter);
        result = gql_list("query($id:Int!,$cursor:String,$filter:AiIssuesFilterIssues!){actor{account(id:$id){aiIssues{issues(cursor:$cursor,filter:$filter){issues{issueId title priority state createdAt sources acknowledgedAt acknowledgedBy} nextCursor}}}}}", vars, "data/actor/account/aiIssues/issues", "issues");
    } else if (!strcmp(name, "acknowledge_incident")) {
        setstr(vars, "issue", str(args, "incident_id"));
        Json *r = extract(graphql("mutation($id:Int!,$issue:ID!){aiIssuesAckIssue(accountId:$id,issueId:$issue){error result{accountId issueId action}}}", vars, NULL), "data/aiIssuesAckIssue");
        if (r) {
            if (str(r, "error") && *str(r, "error")) fail("Acknowledge issue: %s", str(r, "error"));
            else { result = copy_json(get(r, "result")); if (!result) fail("Acknowledge issue returned no result"); }
            json_object_put(r);
        }
    } else if (!strcmp(name, "list_synthetics_monitors")) {
        char expression[256];
        snprintf(expression, sizeof expression, "domain = 'SYNTH' AND accountId = '%d'", id);
        result = entity_list(expression, "...on SyntheticMonitorEntityOutline{guid name monitorType period monitoredUrl tags{key values}}");
        /* monitorType is returned by entities but is not an entity-search field. */
        const char *type = str(args, "monitor_type");
        if (result && type) {
            Json *filtered = json_object_new_array();
            for (size_t i = 0; i < json_object_array_length(result); i++) {
                Json *monitor = json_object_array_get_idx(result, i);
                if (str(monitor, "monitorType") && !strcmp(type, str(monitor, "monitorType")))
                    json_object_array_add(filtered, copy_json(monitor));
            }
            json_object_put(result); result = filtered;
        }
    } else if (!strcmp(name, "create_browser_monitor")) {
        const char *url = str(args, "url");
        if (strncmp(url, "https://", 8) && strncmp(url, "http://", 7)) { fail("Monitor url must use HTTP or HTTPS"); goto done; }
        const char *period = NULL;
        switch (json_object_get_int(get(args, "frequency"))) {
            case 1: period = "EVERY_MINUTE"; break;
            case 5: period = "EVERY_5_MINUTES"; break;
            case 10: period = "EVERY_10_MINUTES"; break;
            case 15: period = "EVERY_15_MINUTES"; break;
            case 30: period = "EVERY_30_MINUTES"; break;
            case 60: period = "EVERY_HOUR"; break;
        }
        Json *monitor = json_object_new_object(), *locations = json_object_new_object();
        setstr(monitor, "name", str(args, "name")); setstr(monitor, "uri", url);
        setstr(monitor, "period", period); setstr(monitor, "status", "ENABLED");
        json_object_object_add(locations, "public", copy_json(get(args, "locations")));
        json_object_object_add(monitor, "locations", locations);
        json_object_object_add(vars, "monitor", monitor);
        Json *r = extract(graphql("mutation($id:Int!,$monitor:SyntheticsCreateSimpleBrowserMonitorInput!){syntheticsCreateSimpleBrowserMonitor(accountId:$id,monitor:$monitor){monitor{id guid name uri period status} errors{type description}}}", vars, NULL), "data/syntheticsCreateSimpleBrowserMonitor");
        if (r) {
            Json *errors = get(r, "errors");
            if (errors && json_object_array_length(errors)) fail("Create monitor: %s", str(json_object_array_get_idx(errors, 0), "description") ?: "unknown error");
            else { result = copy_json(get(r, "monitor")); if (!result) fail("Create monitor returned no monitor"); }
            json_object_put(r);
        }
    } else fail("Unknown tool");
done:
    json_object_put(vars); return result;
}
