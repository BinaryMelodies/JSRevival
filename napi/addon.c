
// a more conventional addon

#include <stdio.h>
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

static napi_value DisplayGreetings(napi_env env, napi_callback_info info)
{
	printf("Greetings!\n");
	return NULL;
}

NAPI_MODULE_INIT(/*napi_env env, napi_value exports*/)
{
	napi_value exportsValue;
	NODE_API_CALL(env, napi_create_object(env, &exportsValue));

	napi_value displayGreetings;
	NODE_API_CALL(env, napi_create_function(
		env,
		"displayGreetings",
		NAPI_AUTO_LENGTH,
		DisplayGreetings,
		NULL,
		&displayGreetings));

	NODE_API_CALL(env, napi_set_named_property(
		env,
		exportsValue,
		"displayGreetings",
		displayGreetings));

	return exportsValue;
}

