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

void cJSON_del(int cjson_num, ...) {
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

bool is_emoji(unsigned int codepoint) {
    return (codepoint >= 0x1F600 && codepoint <= 0x1F64F) ||
           (codepoint >= 0x1F300 && codepoint <= 0x1F5FF) ||
           (codepoint >= 0x1F680 && codepoint <= 0x1F6FF) ||
           (codepoint >= 0x1F700 && codepoint <= 0x1F77F) ||
           (codepoint >= 0x1F780 && codepoint <= 0x1F7FF) ||
           (codepoint >= 0x1F800 && codepoint <= 0x1F8FF) ||
           (codepoint >= 0x1F900 && codepoint <= 0x1F9FF) ||
           (codepoint >= 0x1FA00 && codepoint <= 0x1FA6F) ||
           (codepoint >= 0x1FA70 && codepoint <= 0x1FAFF) ||
           (codepoint >= 0x2600 && codepoint <= 0x26FF) ||
           (codepoint >= 0x2700 && codepoint <= 0x27BF);
}

bool contains_emoji(const char* str) {
    while (*str) {
        unsigned char byte = *str;

        if (byte >= 0xF0) {
            unsigned int codepoint = 0;
            if ((byte & 0xF8) == 0xF0) {
                codepoint = ((byte & 0x07) << 18) |
                            ((str[1] & 0x3F) << 12) |
                            ((str[2] & 0x3F) << 6) |
                            (str[3] & 0x3F);
                str += 4;
            }
            if (is_emoji(codepoint)) {
                return true;
            }
        } else if (byte >= 0xE0) {
            unsigned int codepoint = 0;
            if ((byte & 0xF0) == 0xE0) {
                codepoint = ((byte & 0x0F) << 12) |
                            ((str[1] & 0x3F) << 6) |
                            (str[2] & 0x3F);
                str += 3;
            }
            if (is_emoji(codepoint)) {
                return true;
            }
        } else if (byte >= 0xC0) {
            unsigned int codepoint = 0;
            if ((byte & 0xE0) == 0xC0) {
                codepoint = ((byte & 0x1F) << 6) |
                            (str[1] & 0x3F);
                str += 2;
            }
            if (is_emoji(codepoint)) {
                return true;
            }
        } else {
            if (is_emoji(byte)) {
                return true;
            }
            str += 1;
        }
    }
    return false;
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
        else if (contains_emoji(&id[i])) {
            *out_msg = "ID에 이모지가 포함될 수 없습니다.";
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

bool is_valid_password(const char* password, char** out_msg) {
    int length = strlen(password);
    if (length < 5 || length > 20) {
        *out_msg = "PASSWORD 길이는 5자 이상 20자 이하여야 합니다.";
        return false;
    }

    int has_alpha = 0;
    int has_digit = 0;

    for (int i = 0; i < length; i++) {
        unsigned char ch = password[i];

        if (isalpha(ch)) {
            has_alpha = 1;
        }
        else if (isdigit(ch)) {
            has_digit = 1;
        }
        else if (isspace(ch)) {
            *out_msg = "비밀번호에 공백이 포함될 수 없습니다.";
            return false;
        }
        else if ((ch >= 0xE0 && ch <= 0xEF) && (password[i+1] >= 0x80 && password[i+1] <= 0xBF) && (password[i+2] >= 0x80 && password[i+2] <= 0xBF)) {
            *out_msg = "비밀번호에 한글이 포함될 수 없습니다.";
            return false;
        }
        else if (contains_emoji(&password[i])) {
            *out_msg = "비밀번호에 이모지가 포함될 수 없습니다.";
            return false;
        }
        if (i >= 2 && password[i] == password[i-1] && password[i] == password[i-2]) {
            *out_msg = "비밀번호에 동일한 문자가 연속으로 세 번 이상 나타날 수 없습니다.";
            return false;
        }
    }
    if (!has_alpha) {
        *out_msg = "비밀번호에 최소 하나 이상의 알파벳 문자가 포함되어야 합니다.";
        return false;
    }
    if (!has_digit) {
        *out_msg = "비밀번호에 최소 하나 이상의 숫자가 포함되어야 합니다.";
        return false;
    }
    // 금칙어 검사
    const char* banned_words[] = {NULL};
    for (int i = 0; banned_words[i] != NULL; i++) {
        if (strstr(password, banned_words[i]) != NULL) {
            *out_msg = "비밀번호에 금칙어가 포함될 수 없습니다.";
            return false;
        }
    }
    return true;
}
