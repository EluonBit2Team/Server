#include "packet_converter.h"

int get_buffer_size(char* buf) {
    return *((int*)buf);
}

cJSON* get_parsed_json(char* buf) {
    char* rawJsonStartPtr = buf + BUFF_SIZE_LENGTH;
    cJSON* json = cJSON_Parse(rawJsonStartPtr);
    return json;
}


