#ifndef PACKET_CONVERTER_H
#define PACKET_CONVERTER_H

#include "../cJSON/cJSON.h"
#include "../defines.h"
#include "../includes.h"

int get_buffer_size(char* buf);
cJSON* get_parsed_json(char* buf);
int type_finder(char *buf);
void cJSON_del_and_free(int cjson_num, ...);
void free_all(int ptr_num, ...);
bool raw_json_guard(const char *raw_json);
#endif