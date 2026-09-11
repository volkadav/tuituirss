// SPDX-License-Identifier: MIT
#include "test.h"
#include "api/api.h"

#include <jansson.h>

void test_api(void)
{
    CHECK(api_error_from_code("NOT_LOGGED_IN") == API_ERR_NOT_LOGGED_IN);
    CHECK(api_error_from_code("LOGIN_ERROR") == API_ERR_LOGIN);
    CHECK(api_error_from_code("API_DISABLED") == API_ERR_API_DISABLED);
    CHECK(api_error_from_code("UNKNOWN_METHOD") == API_ERR_UNKNOWN_METHOD);
    CHECK(api_error_from_code("INCORRECT_USAGE") == API_ERR_INCORRECT_USAGE);
    CHECK(api_error_from_code("E_OPERATION_FAILED") == API_ERR_OPERATION_FAILED);
    CHECK(api_error_from_code("E_NOT_FOUND") == API_ERR_NOT_FOUND);
    CHECK(api_error_from_code("SOMETHING_ELSE") == API_ERR_STATUS);
    CHECK(api_error_from_code(NULL) == API_ERR_STATUS);

    ApiError err;
    char msg[256];
    json_t *content = NULL;

    /* success envelope */
    CHECK(api_parse_envelope("{\"seq\":1,\"status\":0,\"content\":{\"a\":1}}",
                             &err, msg, sizeof msg, &content) == 0);
    CHECK(content != NULL);
    CHECK(json_integer_value(json_object_get(content, "a")) == 1);
    json_decref(content);
    content = NULL;

    /* server error envelope */
    CHECK(api_parse_envelope(
              "{\"seq\":1,\"status\":1,\"content\":{\"error\":\"NOT_LOGGED_IN\"}}",
              &err, msg, sizeof msg, &content) == -1);
    CHECK(err == API_ERR_NOT_LOGGED_IN);
    CHECK(content == NULL);
    CHECK(strstr(msg, "NOT_LOGGED_IN") != NULL);

    /* malformed JSON */
    CHECK(api_parse_envelope("{ nope", &err, msg, sizeof msg, &content) == -1);
    CHECK(err == API_ERR_PARSE);

    /* non-object */
    CHECK(api_parse_envelope("[1,2,3]", &err, msg, sizeof msg, &content) == -1);
    CHECK(err == API_ERR_PARSE);

    /* missing content is allowed and yields an empty object */
    CHECK(api_parse_envelope("{\"seq\":1,\"status\":0}",
                             &err, msg, sizeof msg, &content) == 0);
    CHECK(json_is_object(content));
    json_decref(content);
}
