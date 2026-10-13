#ifndef AWM_JSON_H
#define AWM_JSON_H
#include <stdbool.h>
#include <stddef.h>
typedef struct { int start,end,next; char type; } JsonToken;
typedef struct { const char *text; JsonToken tokens[4096]; int count; } Json;
bool json_parse(Json *j,const char *text);
int json_get(const Json *j,int object,const char *key);
bool json_string(const Json *j,int t,char *out,size_t cap);
int json_int(const Json *j,int t,int fallback);
double json_number(const Json *j,int t,double fallback);
bool json_bool(const Json *j,int t,bool fallback);
bool json_equal(const Json *j,int t,const char *s);
bool json_escape(const char *in,char *out,size_t cap);
bool json_raw(const Json *j,int t,char *out,size_t cap);
#endif
