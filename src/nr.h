#ifndef NR_H
#define NR_H

#include <json-c/json.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct json_object Json;
extern char nr_error[512];
extern char *nr_api_key;

Json *get(Json *obj, const char *key);
const char *str(Json *obj, const char *key);
Json *copy_json(Json *obj);
void setstr(Json *obj, const char *key, const char *value);
void setint(Json *obj, const char *key, long long value);
bool fail(const char *format, ...);
char *config_value(const char *name);
bool readonly_mode(void);
Json *catalog(void);
bool validate(Json *tool, Json *args);
Json *execute(const char *name, Json *args);
Json *http_request(const char *method, const char *path, Json *query, Json *body,
                   const char *region, long *status, long *next_page);
Json *graphql(const char *query, Json *variables, const char *region);
bool graphql_readonly(const char *query);

#endif
