
// a more conventional addon

#include <stdio.h>
#include <stdlib.h>
#include <node_api.h>
#include <gmp.h>

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

static napi_value ReadValue(napi_env env, napi_callback_info info)
{
	size_t argc = 1;
	napi_value arg;

	NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, &arg, NULL, NULL));

	if(argc == 0)
	{
		NODE_API_CALL(env, napi_get_undefined(env, &arg));
	}

	napi_valuetype type;
	NODE_API_CALL(env, napi_typeof(env, arg, &type));

	switch(type)
	{
	case napi_undefined:
		printf("undefined\n");
		break;
	case napi_null:
		printf("null\n");
		break;
	case napi_boolean:
		printf("boolean\n");
		{
			bool result;
			NODE_API_CALL(env, napi_get_value_bool(env, arg, &result));
			printf("> %s\n", result ? "true" : "false");
		}
		break;
	case napi_number:
		printf("number\n");
		{
			double result;
			NODE_API_CALL(env, napi_get_value_double(env, arg, &result));
			printf("> %g\n", result);
		}
		break;
	case napi_string:
		printf("string\n");
		{
			size_t length;
			NODE_API_CALL(env, napi_get_value_string_utf8(env, arg, NULL, 0, &length));
			char * buffer = malloc(length + 1);
			NODE_API_CALL(env, napi_get_value_string_utf8(env, arg, buffer, length + 1, &length));
			printf("> %.*s\n", length, buffer);
			free(buffer);
		}
		break;
	case napi_symbol:
		printf("symbol\n");
		break;
	case napi_object:
		printf("object\n");
		{
			// access .name, assume it is a string
			napi_value name;
			// call on global with this argument
			NODE_API_CALL(env, napi_get_named_property(env, arg, "name", &name));
			NODE_API_CALL(env, napi_typeof(env, name, &type));
			if(type == napi_string)
			{
				size_t length;
				NODE_API_CALL(env, napi_get_value_string_utf8(env, name, NULL, 0, &length));
				char * buffer = malloc(length + 1);
				NODE_API_CALL(env, napi_get_value_string_utf8(env, name, buffer, length + 1, &length));
				printf("> name: %.*s\n", length, buffer);
				free(buffer);
			}
		}
		break;
	case napi_function:
		printf("function\n");
		{
			napi_value argument;
			napi_value this_value;
			// call on global with this argument
			NODE_API_CALL(env, napi_create_string_utf8(env, "Message from native code!", NAPI_AUTO_LENGTH, &argument));
			NODE_API_CALL(env, napi_get_global(env, &this_value));
			napi_value result;
			NODE_API_CALL(env, napi_call_function(env, this_value, arg, 1, &argument, &result));
		}
		break;
	case napi_external:
		printf("external\n");
		{
			void * result;
			NODE_API_CALL(env, napi_get_value_external(env, arg, &result));
			printf("> %p\n", result);
		}
		break;
	case napi_bigint:
		printf("bigint\n");
		{
			size_t word_count;
			NODE_API_CALL(env, napi_get_value_bigint_words(env, arg, NULL, &word_count, NULL));
			uint64_t * words = malloc(sizeof(uint64_t) * word_count);
			int sign_bit;
			NODE_API_CALL(env, napi_get_value_bigint_words(env, arg, &sign_bit, &word_count, words));

			mpz_t integer;
			mpz_init(integer);
			mpz_import(integer, word_count, -1, sizeof(uint64_t), 0, 0, words);
			if(sign_bit)
			{
				mpz_neg(integer, integer);
			}
			printf("> ");
			mpz_out_str(stdout, 10, integer);
			printf("\n");
			mpz_clear(integer);
			free(words);
		}
		break;
	default:
		printf("invalid\n");
		break;
	}

	return NULL;
}

static napi_value CreateExternal(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_external(env, (void *)0x1234, NULL, NULL, &result));
	return result;
}

NAPI_MODULE_INIT(/*napi_env env, napi_value exports*/)
{
	napi_value exportsValue;
	NODE_API_CALL(env, napi_create_object(env, &exportsValue));

	napi_value readValue;
	NODE_API_CALL(env, napi_create_function(
		env,
		"readValue",
		NAPI_AUTO_LENGTH,
		ReadValue,
		NULL,
		&readValue));

	NODE_API_CALL(env, napi_set_named_property(
		env,
		exportsValue,
		"readValue",
		readValue));

	napi_value createExternal;
	NODE_API_CALL(env, napi_create_function(
		env,
		"createExternal",
		NAPI_AUTO_LENGTH,
		CreateExternal,
		NULL,
		&createExternal));

	NODE_API_CALL(env, napi_set_named_property(
		env,
		exportsValue,
		"createExternal",
		createExternal));

	return exportsValue;
}

