#include "packet_converter.h"

int get_buffer_size(char* buf) {
    return *((int*)buf);
}

cJSON* get_parsed_json(char* buf) {
    char* rawJsonStartPtr = buf + HEADER_SIZE;
    cJSON* json = cJSON_Parse(rawJsonStartPtr);
    return json;
}

int type_finder(char *buf) {
    char *type_str = "\"type\":";
    char *type_pos = strstr(buf, type_str);
    
    if (type_pos == NULL) {
        return -1;  // "type" 키를 찾을 수 없음
    }
    
    type_pos += strlen(type_str);
    while (*type_pos == ' ' || *type_pos == '\t' || *type_pos == '\n' || *type_pos == '\r') {
        type_pos++;  // 공백 문자를 건너뜀
    }

    char *end_pos = type_pos;
    while (*end_pos != ',' && *end_pos != '}' && *end_pos != '\0') {
        end_pos++;
    }

    // 숫자를 추출하여 정수로 변환
    char type_value[16];
    int length = end_pos - type_pos;
    if (length >= sizeof(type_value)) {
        return -1;  // 값이 너무 김
    }

    strncpy(type_value, type_pos, length);
    type_value[length] = '\0';

    int type_int = atoi(type_value);
    if (type_int == 0 && strcmp(type_value, "0") != 0) {
        return -1;  // 변환 실패
    }

    return type_int;
}

// todo~~~
bool is_exception(const char *input) {
    const char *exceptions[] = {"NULL", "null", "invalid", "forbidden", "unauthorized"};
    const int num_exceptions = sizeof(exceptions) / sizeof(exceptions[0]);

    for (int i = 0; i < num_exceptions; i++) {
        if (strcmp(input, exceptions[i]) == 0) {
            return true;
        }
    }
    return false;
}

void cJSON_del_and_free(int cjson_num, ...) {
    va_list VA_LIST;
    va_start(VA_LIST, cjson_num);
    for (int i = 0; i < cjson_num; i ++) {
        cJSON* cjson_ptr = va_arg(VA_LIST, cJSON*);
        if (cjson_ptr == NULL) {
            continue;
        }
        cJSON_Delete(cjson_ptr);
        //cJSON_free(cjson_ptr);
    }
    va_end(VA_LIST);
}

void free_all(int ptr_num, ...) {
    va_list VA_LIST;
    va_start(VA_LIST, ptr_num);
    for (int i = 0; i < ptr_num; i ++) {
        void* allocated_ptr = va_arg(VA_LIST, void*);
        if (allocated_ptr == NULL) {
            continue;
        }
        free(allocated_ptr);
    }
    va_end(VA_LIST);
}

bool raw_json_guard(const char *raw_json) {
    const char *invalid_str_arr[] = {":\t\n", ":\n", ": \n", NULL};
    bool in_quotes = false;
    bool escape = false;

    while (*raw_json) {
        if (escape) {
            escape = false;
        } 
        else if (*raw_json == '\\') {
            escape = true;
        }
        else if (*raw_json == '"') {
            in_quotes = !in_quotes;
        } 
        else if (!in_quotes) {
            for (int i = 0; invalid_str_arr[i] != NULL; i++) {
                const char *substr_position = strstr(raw_json, invalid_str_arr[i]);
                if (substr_position != NULL) {
                    // Ensure invalid substring is not inside quotes
                    const char *quote_check = raw_json;
                    bool inside_quotes = false;
                    while (quote_check < substr_position) {
                        if (*quote_check == '"' && (quote_check == raw_json || *(quote_check - 1) != '\\')) {
                            inside_quotes = !inside_quotes;
                        }
                        quote_check++;
                    }
                    if (!inside_quotes) {
                        return false;
                    }
                }
            }
        }
        raw_json++;
    }
    return true;
}

bool is_valid_login_id(const char* id, char** out_msg) {
    int length = strlen(id);
    if (length < 5 || length > 20) {
        *out_msg = "ID 길이는 5자 이상 20자 이하여야 합니다.";
        return false;
    }

    int has_alpha = 0;
    int has_digit = 0;

    for (int i = 0; i < length; i++) {
        unsigned char ch = id[i];

        if (isalpha(ch)) {
            has_alpha = 1;
        }
        else if (isdigit(ch)) {
            has_digit = 1;
        }
        else if (ispunct(ch)) {
            *out_msg = "ID에 특수 문자가 포함될 수 없습니다.";
            return false;
        }
        else if (isspace(ch)) {
            *out_msg = "ID에 공백이 포함될 수 없습니다.";
            return false;
        }
        else if ((ch >= 0xE0 && ch <= 0xEF) && (id[i+1] >= 0x80 && id[i+1] <= 0xBF) && (id[i+2] >= 0x80 && id[i+2] <= 0xBF)) {
            *out_msg = "ID에 한글이 포함될 수 없습니다.";
            return false;
        }
        if (i >= 2 && id[i] == id[i-1] && id[i] == id[i-2]) {
            *out_msg = "ID에 동일한 문자가 연속으로 세 번 이상 나타날 수 없습니다.";
            return false;
        }
    }
    if (!has_alpha) {
        *out_msg = "ID에 최소 하나 이상의 알파벳 문자가 포함되어야 합니다.";
        return false;
    }
    if (!has_digit) {
        *out_msg = "ID에 최소 하나 이상의 숫자가 포함되어야 합니다.";
        return false;
    }
    // 금칙어 검사
    const char* banned_words[] = {"admin", "root", "user", NULL};
    for (int i = 0; banned_words[i] != NULL; i++) {
        if (strstr(id, banned_words[i]) != NULL) {
            *out_msg = "ID에 금칙어가 포함될 수 없습니다.";
            return false;
        }
    }
    return true;
}