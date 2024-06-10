#ifndef PACKET_CONVERTER_H
#define PACKET_CONVERTER_H

#include "../cJSON/cJSON.h"
#include "../defines.h"

int get_buffer_size(char* buf);
cJSON* get_parsed_json(char* buf);
#endif