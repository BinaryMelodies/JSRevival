
#include <node_api.h>

NAPI_MODULE_INIT()
{
	napi_value result;
	napi_create_object(env, &result);
	napi_value hello;
	napi_create_string_utf8(env, "This is a native module", NAPI_AUTO_LENGTH, &hello);
	napi_set_named_property(env, result, "hello", hello);
	return result;
}

