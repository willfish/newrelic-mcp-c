#define _POSIX_C_SOURCE 200809L
#include "nr.h"
#include <curl/curl.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RESPONSE_LIMIT (16U * 1024U * 1024U)
char nr_error[512];
char *nr_api_key;

Json *get(Json *obj, const char *key) {
    Json *value = NULL;
    if (obj && json_object_is_type(obj, json_type_object))
        json_object_object_get_ex(obj, key, &value);
    return value;
}
const char *str(Json *obj, const char *key) {
    Json *v = get(obj, key);
    return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}
Json *copy_json(Json *obj) { return obj ? json_object_get(obj) : NULL; }
void setstr(Json *obj, const char *key, const char *value) {
    json_object_object_add(obj, key, value ? json_object_new_string(value) : NULL);
}
void setint(Json *obj, const char *key, long long value) {
    json_object_object_add(obj, key, json_object_new_int64(value));
}
bool fail(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vsnprintf(nr_error, sizeof nr_error, format, ap);
    va_end(ap);
    return false;
}
char *config_value(const char *name) {
    char file_name[128];
    snprintf(file_name, sizeof file_name, "%s_FILE", name);
    const char *file = getenv(file_name);
    if (file && *file) {
        FILE *f = fopen(file, "r");
        if (!f) { fail("Cannot read %s", file_name); return NULL; }
        char value[4097];
        size_t n = fread(value, 1, sizeof value - 1, f);
        bool error = ferror(f) || n == sizeof value - 1;
        fclose(f);
        if (error || memchr(value, 0, n)) { fail("Invalid %s", file_name); return NULL; }
        while (n && (value[n - 1] == '\n' || value[n - 1] == '\r')) n--;
        value[n] = 0;
        return strdup(value);
    }
    const char *value = getenv(name);
    return value ? strdup(value) : NULL;
}
bool readonly_mode(void) {
    const char *v = getenv("NEW_RELIC_READ_ONLY");
    return v && strcmp(v, "0") && strcasecmp(v, "false");
}

struct buffer { char *data; size_t size; };
static size_t receive(char *data, size_t size, size_t count, void *user) {
    struct buffer *b = user;
    if (size && count > RESPONSE_LIMIT / size) return 0;
    size_t n = size * count;
    if (n > RESPONSE_LIMIT - b->size) return 0;
    char *p = realloc(b->data, b->size + n + 1);
    if (!p) return 0;
    b->data = p;
    memcpy(p + b->size, data, n);
    b->size += n;
    p[b->size] = 0;
    return n;
}
static size_t header(char *data, size_t size, size_t count, void *user) {
    size_t n = size * count;
    long *next = user;
    if (n > 65536 || n < 6 || strncasecmp(data, "link:", 5)) return n;
    char *line = strndup(data + 5, n - 5);
    if (!line) return 0;
    char *save = NULL;
    for (char *part = strtok_r(line, ",", &save); part; part = strtok_r(NULL, ",", &save)) {
        if (!strstr(part, "rel=\"next\"") && !strstr(part, "rel=next")) continue;
        char *page = strstr(part, "?page=");
        if (!page) page = strstr(part, "&page=");
        if (page) {
            char *end = NULL;
            long value = strtol(page + 6, &end, 10);
            if (value > 0 && value <= 1000000 && end && (*end == '&' || *end == '>'))
                *next = value;
            else *next = -1;
        } else *next = -1;
    }
    free(line);
    return n;
}

static bool append_param(CURL *curl, struct buffer *url, const char *name, const char *value) {
    char *k = curl_easy_escape(curl, name, 0), *v = curl_easy_escape(curl, value, 0);
    bool ok = k && v;
    if (ok) {
        const char *sep = strchr(url->data, '?') ? "&" : "?";
        ok = receive((char *)sep, 1, 1, url) && receive(k, 1, strlen(k), url)
             && receive("=", 1, 1, url) && receive(v, 1, strlen(v), url);
    }
    curl_free(k); curl_free(v);
    return ok;
}
Json *http_request(const char *method, const char *path, Json *query, Json *body,
                   const char *region, long *status, long *next_page) {
    if (!nr_api_key) nr_api_key = config_value("NEW_RELIC_API_KEY");
    if (!nr_api_key || !*nr_api_key) {
        if (!*nr_error) fail("Set NEW_RELIC_API_KEY or NEW_RELIC_API_KEY_FILE to a User API key");
        return NULL;
    }
    for (const unsigned char *p = (unsigned char *)nr_api_key; *p; p++) {
        if (*p <= 32 || *p >= 127) { fail("Invalid API key characters"); return NULL; }
    }
    char *configured = NULL;
    if (!region) {
        configured = config_value("NEW_RELIC_REGION");
        if (*nr_error) { free(configured); return NULL; }
        region = configured;
    }
    if (!region || !*region) region = "US";
    if (strcasecmp(region, "US") && strcasecmp(region, "EU")) {
        free(configured); fail("NEW_RELIC_REGION must be US or EU"); return NULL;
    }
    const char *base = !strcasecmp(region, "EU") ? "https://api.eu.newrelic.com" : "https://api.newrelic.com";
#ifdef NR_TEST
    /* Local fixtures only. Production builds have no endpoint override. */
    const char *test = getenv("NR_TEST_ENDPOINT");
    if (test && !strncmp(test, "http://127.0.0.1:", 17)) base = test;
#endif
    free(configured);
    CURL *curl = curl_easy_init();
    struct buffer url = {0}, response = {0};
    struct curl_slist *headers = NULL;
    Json *result = NULL;
    long next = 0, code = 0;
    if (!curl) { fail("Cannot initialize HTTPS client"); return NULL; }
    if (!receive((char *)base, 1, strlen(base), &url) || !receive((char *)path, 1, strlen(path), &url)) goto memory;
    if (query) {
        json_object_object_foreach(query, key, value) {
            if (json_object_is_type(value, json_type_array)) {
                char array_key[128];
                snprintf(array_key, sizeof array_key, "%s[]", key);
                for (size_t i = 0; i < json_object_array_length(value); i++)
                    if (!append_param(curl, &url, array_key, json_object_get_string(json_object_array_get_idx(value, i)))) goto memory;
            } else if (value && !append_param(curl, &url, key, json_object_get_string(value))) goto memory;
        }
    }
    char *auth = malloc(strlen(nr_api_key) + 10);
    if (!auth) goto memory;
    sprintf(auth, "API-Key: %s", nr_api_key);
    headers = curl_slist_append(headers, auth);
    free(auth);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    if (!headers) goto memory;
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "newrelic-mcp-c/0.1.0");
#ifndef NR_TEST
    /* Older libcurl rejects this option; do not send without the HTTPS-only restriction. */
    if (curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https") != CURLE_OK) {
        fail("libcurl 7.85 or newer is required"); goto done;
    }
#endif
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &next);
    if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string_ext(body, JSON_C_TO_STRING_PLAIN));
    CURLcode error = curl_easy_perform(curl);
    if (error != CURLE_OK) {
        fail("New Relic transport: %s%s", curl_easy_strerror(error),
             strcmp(method, "GET") ? "; do not automatically retry writes" : "");
        goto done;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if (status) *status = code;
    if (next_page) *next_page = next;
    if (code < 200 || code >= 300) { fail("New Relic HTTP %ld", code); goto done; }
    if (!response.size) { result = json_object_new_object(); goto done; }
    struct json_tokener *tok = json_tokener_new();
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    result = json_tokener_parse_ex(tok, response.data, (int)response.size);
    if (json_tokener_get_error(tok) != json_tokener_success) {
        json_object_put(result); result = NULL; fail("New Relic returned invalid JSON");
    }
    json_tokener_free(tok);
    goto done;
memory:
    fail("Request exceeds memory limit or allocation failed");
done:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(url.data); free(response.data);
    return result;
}
Json *graphql(const char *query, Json *variables, const char *region) {
    Json *body = json_object_new_object();
    setstr(body, "query", query);
    json_object_object_add(body, "variables", variables ? copy_json(variables) : json_object_new_object());
    Json *response = http_request("POST", "/graphql", NULL, body, region, NULL, NULL);
    json_object_put(body);
    Json *errors = get(response, "errors");
    if (errors && json_object_array_length(errors)) {
        const char *message = str(json_object_array_get_idx(errors, 0), "message");
        fail("NerdGraph: %s", message ? message : "query failed");
        json_object_put(response);
        return NULL;
    }
    if (response && !get(response, "data")) {
        fail("NerdGraph returned no data"); json_object_put(response); return NULL;
    }
    return response;
}
/* Scan operation keywords outside comments and strings. Fail closed on malformed
 * strings; a read-only process never forwards a raw mutation or subscription. */
bool graphql_readonly(const char *query) {
    const unsigned char *p = (const unsigned char *)query;
    while (*p) {
        if (*p == '#') { while (*p && *p != '\n') p++; }
        else if (*p == '"') {
            bool block = !strncmp((const char *)p, "\"\"\"", 3);
            p += block ? 3 : 1;
            bool closed = false;
            while (*p) {
                if (*p == '\\' && p[1]) { p += 2; continue; }
                if (block ? !strncmp((const char *)p, "\"\"\"", 3) : *p == '"') {
                    p += block ? 3 : 1; closed = true; break;
                }
                p++;
            }
            if (!closed) return false;
        } else if (isalpha(*p) || *p == '_') {
            const unsigned char *start = p;
            while (isalnum(*p) || *p == '_') p++;
            size_t n = (size_t)(p - start);
            if ((n == 8 && !memcmp(start, "mutation", 8)) || (n == 12 && !memcmp(start, "subscription", 12))) return false;
        } else p++;
    }
    return true;
}
