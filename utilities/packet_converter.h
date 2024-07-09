#ifndef PACKET_CONVERTER_H
#define PACKET_CONVERTER_H

#include "../cJSON/cJSON.h"
#include "../defines.h"
#include "../includes.h"

int get_buffer_size(char* buf);
cJSON* get_parsed_json(char* buf);
int type_finder(char *buf);
void cJSON_del(int cjson_num, ...);
void free_all(int ptr_num, ...);
bool is_emoji(unsigned int codepoint);
bool contains_emoji(const char* str);
bool is_valid_login_id(const char* id, char** out_msg);
bool is_valid_password(const char* password, char** out_msg);
void JSON_guard(cJSON* json, char** out_msg);

#endif