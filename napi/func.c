
// a more conventional addon

#include <stdio.h>
#include <stdlib.h>
#include <node_api.h>

#define NODE_API_CALL(env, call) \
	do { \
		napi_status status = (call); \
		if(status != napi_ok) \
		{ \
			const napi_extended_error_info * error_info = NULL; \
			napi_get_last_error_info((env), &error_info); \
			const char * error_message = error_info->error_message; \
			bool is_pending; \
			napi_is_exception_pending((env), &is_pending); \
			if(!is_pending) \
			{ \
				const char * message = error_message ? error_message : "error"; \
				napi_throw_error((env), NULL, message); \
			} \
			return NULL; \
		} \
	} while(0)

static napi_status ToString(napi_env env, napi_value value, size_t * length, char ** buffer)
{
	napi_status status;
	napi_value toString;
	if((status = napi_get_named_property(env, value, "toString", &toString)) != napi_ok)
		return status;

	napi_value result;
	if((status = napi_call_function(env, value, toString, 0, NULL, &result)) != napi_ok)
		return status;

	if((status = napi_get_value_string_utf8(env, result, NULL, 0, length)) != napi_ok)
		return status;
	*buffer = malloc(*length + 1);
	if((status = napi_get_value_string_utf8(env, result, *buffer, *length + 1, length)) != napi_ok)
		return free(buffer), status;

	return napi_ok;
}

static napi_value FunctionInformation(napi_env env, napi_callback_info info)
{
	size_t argc;
	napi_value * argv;
	napi_valuetype type;

	NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, NULL, NULL, NULL));
	printf("Argument count: %zd\n", argc);

	argv = malloc(sizeof(napi_value) * argc);

	size_t length;
	char * buffer;

	NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
	for(size_t i = 0; i < argc; i++)
	{
		printf("Argument %zd:\n", i);

#if 0
		napi_value toString;
		NODE_API_CALL(env, napi_get_named_property(env, argv[i], "toString", &toString));

		napi_value result;
		NODE_API_CALL(env, napi_call_function(env, argv[i], toString, 0, NULL, &result));

		size_t length;
		NODE_API_CALL(env, napi_get_value_string_utf8(env, result, NULL, 0, &length));
		char * buffer = malloc(length + 1);
		NODE_API_CALL(env, napi_get_value_string_utf8(env, result, buffer, length + 1, &length));
#endif
		NODE_API_CALL(env, ToString(env, argv[i], &length, &buffer));
		printf("> %.*s\n", length, buffer);
		free(buffer);
	}

	free(argv);

	napi_value thisArg;
	NODE_API_CALL(env, napi_get_cb_info(env, info, NULL, NULL, &thisArg, NULL));
	printf("This:\n");
#if 0
	size_t length;
	NODE_API_CALL(env, napi_get_value_string_utf8(env, thisArg, NULL, 0, &length));
	char * buffer = malloc(length + 1);
	NODE_API_CALL(env, napi_get_value_string_utf8(env, thisArg, buffer, length + 1, &length));
#endif
	NODE_API_CALL(env, ToString(env, thisArg, &length, &buffer));
	printf("> %.*s\n", length, buffer);
	free(buffer);

	napi_value newTarget;
	NODE_API_CALL(env, napi_get_new_target(env, info, &newTarget));
	printf("New Target:\n");

#if 0
	NODE_API_CALL(env, napi_get_value_string_utf8(env, newTarget, NULL, 0, &length));
	buffer = malloc(length + 1);
	NODE_API_CALL(env, napi_get_value_string_utf8(env, newTarget, buffer, length + 1, &length));
#endif
	if(newTarget)
	{
		NODE_API_CALL(env, ToString(env, newTarget, &length, &buffer));
		printf("> %.*s\n", length, buffer);
		free(buffer);
	}
	else
	{
		printf("> null\n");
	}

	return NULL;
}

NAPI_MODULE_INIT(/*napi_env env, napi_value exports*/)
{
	napi_value exportsValue;
	NODE_API_CALL(env, napi_create_object(env, &exportsValue));

	napi_value functionInformation;
	NODE_API_CALL(env, napi_create_function(
		env,
		"functionInformation",
		NAPI_AUTO_LENGTH,
		FunctionInformation,
		NULL,
		&functionInformation));

	NODE_API_CALL(env, napi_set_named_property(
		env,
		exportsValue,
		"functionInformation",
		functionInformation));

	return exportsValue;
}

