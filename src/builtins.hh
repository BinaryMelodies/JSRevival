#ifndef _BUILTINS_HH
#define _BUILTINS_HH

/* Structures and functions related to built ins (aside from built-in types and classes) */

#include <functional>
#include <optional>
#include <vector>

#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "core.hh"
#include "iterator.hh"

namespace JS
{
	using namespace Public;

	namespace Runtime
	{
		namespace Builtin
		{
			class BuiltinFunction : public JS::Class::OrdinaryObject
			{
			public:
				static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
				static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

				bool has_constructor_method = true;
				JS::Runtime::Realm * Realm;
				JSString InitialName;

				BuiltinFunction(JS::Runtime::Realm * Realm, JSString name, uint32_t length, bool has_constructor_method = true);

#if ENABLE_ES4
				std::optional<JS::Runtime::SignatureType> GetSignature() override;
#endif
				JS::Runtime::Realm * GetFunctionRealm() override;
				JSString GetClassString() override;

				Invocability GetInvocability() override;
				bool Begin_Call(JSValue this_argument, std::vector<JSValue> arguments) override;
			};

			// for functions that are a simple callback
			class NativeFunction : public BuiltinFunction
			{
			public:
				std::function<JS::Runtime::BuiltinFunction> callback;
				std::function<JS::Runtime::PrecompiledFunction> compile;

				NativeFunction(JS::Runtime::Realm * Realm, JSString name, uint32_t length, std::function<JS::Runtime::BuiltinFunction> callback, bool has_constructor_method = true)
					: BuiltinFunction(Realm, name, length, has_constructor_method), callback(callback)
				{
				}

				NativeFunction(JS::Runtime::Realm * Realm, JSString name, uint32_t length, std::function<JS::Runtime::PrecompiledFunction> compile, bool has_constructor_method = true)
					: BuiltinFunction(Realm, name, length, has_constructor_method), compile(compile)
				{
				}

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				bool Begin_Call(JSValue this_argument, std::vector<JSValue> arguments) override;

				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
			protected:
				void Begin_Construct(std::vector<JSValue> arguments, JSFunction new_target);
			};

#if 0
			//// automatically generated
			JSValue Array_prototype_join_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_reverse_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_sort_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Boolean_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Boolean_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Boolean_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_parse_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getDate_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getDay_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getFullYear_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getHours_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getMilliseconds_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getMinutes_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getMonth_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getSeconds_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getTime_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getTimezoneOffset_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getUTCDate_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getUTCDay_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getUTCFullYear_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getUTCHours_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getUTCMilliseconds_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getUTCMinutes_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getUTCMonth_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getUTCSeconds_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setDate_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setFullYear_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setHours_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setMilliseconds_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setMinutes_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setMonth_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setSeconds_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setTime_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setUTCDate_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setUTCFullYear_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setUTCHours_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setUTCMilliseconds_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setUTCMinutes_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setUTCMonth_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setUTCSeconds_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_setYear_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toGMTString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toLocaleString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toUTCString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_UTC_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_prototype_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue eval_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue isFinite_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue isNaN_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_abs_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_acos_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_asin_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_atan_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_atan2_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_ceil_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_cos_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_exp_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_floor_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_log_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_max_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_min_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_pow_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_random_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_round_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_sin_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_sqrt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_tan_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue parseFloat_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue parseInt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_fromCharCode_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_charAt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_charCodeAt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_indexOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_lastIndexOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_split_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_substring_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_toLowerCase_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_toUpperCase_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_getYear_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue escape_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue unescape_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_concat_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_pop_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_push_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_shift_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_slice_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_splice_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_toLocaleString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_unshift_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toDateString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toLocaleDateString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toLocaleTimeString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toTimeString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_prototype_apply_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_prototype_call_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue decodeURI_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue decodeURIComponent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue encodeURI_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue encodeURIComponent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Error_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Error_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue EvalError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_prototype_toExponential_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_prototype_toFixed_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_prototype_toLocaleString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_prototype_toPrecision_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype_hasOwnProperty_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype_isPrototypeOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype_propertyIsEnumerable_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype_toLocaleString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RangeError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ReferenceError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_prototype_exec_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_prototype_test_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_concat_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_localeCompare_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_match_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_replace_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_search_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_slice_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_toLocaleLowerCase_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_toLocaleUpperCase_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue SyntaxError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypeError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue URIError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue isXMLName_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Namespace_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Namespace_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue QName_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue QName_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_defaultSettings_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_addNamespace_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_appendChild_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_attribute_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_attributes_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_child_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_childIndex_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_children_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_comments_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_contains_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_copy_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_descendants_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_elements_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_hasComplexContent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_hasOwnProperty_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_hasSimpleContent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_inScopeNamespaces_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_insertChildAfter_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_insertChildBefore_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_length_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_localName_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_name_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_namespace_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_namespaceDeclarations_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_nodeKind_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_normalize_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_parent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_prependChild_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_processingInstructions_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_propertyIsEnumerable_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_removeNamespace_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_replace_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_setChildren_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_setLocalName_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_setName_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_setNamespace_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_text_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_toXMLString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_setSettings_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_settings_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_attribute_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_attributes_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_child_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_children_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_comments_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_contains_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_copy_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_descendants_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_elements_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_hasComplexContent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_hasOwnProperty_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_hasSimpleContent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_normalize_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_parent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_processingInstructions_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_propertyIsEnumerable_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_text_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_toXMLString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_substr_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_domNode_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_domNodeList_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_prototype_xpath_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_domNode_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_domNodeList_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_prototype_xpath_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArgumentError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Attribute_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AttributeError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue byte_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue char_fromCharCode_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue char_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue char_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue char_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Class_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Class_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ConstantError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DefinitionError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue float_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue float_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue float_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneralNumber_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneralNumber_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneralNumber_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue int_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue long_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue long_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue long_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Never_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Null_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype_sealProperty_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue override_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Package_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue parseLong_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue PrototypeFunction_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue sbyte_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue short_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue uint_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ulong_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ulong_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ulong_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue UninitializedError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ushort_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Void_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_isArray_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_every_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_filter_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_forEach_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_indexOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_lastIndexOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_map_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_reduce_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_reduceRight_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_some_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_now_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toISOString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toJSON_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_prototype_bind_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue JSON_parse_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue JSON_stringify_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_create_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_defineProperties_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_defineProperty_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_freeze_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_getOwnPropertyDescriptor_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_getOwnPropertyNames_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_getPrototypeOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_isExtensible_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_isFrozen_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_isSealed_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_keys_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_preventExtensions_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_seal_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_trim_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_from_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_of_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_copyWithin_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_entries_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_fill_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_find_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_findIndex_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_keys_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_values_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Date_prototype_toPrimitive_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_prototype_hasInstance_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArrayBuffer_isView_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArrayBuffer_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getFloat32_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getFloat64_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getInt16_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getInt32_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getInt8_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getUint16_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getUint32_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getUint8_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setFloat32_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setFloat64_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setInt16_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setInt32_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setInt8_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setUint16_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setUint32_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setUint8_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Float32Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Float64Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Int16Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Int32Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Int8Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_prototype_clear_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_prototype_delete_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_prototype_entries_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_prototype_forEach_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_prototype_get_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_prototype_has_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_prototype_keys_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_prototype_set_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_prototype_values_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_acosh_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_asinh_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_atanh_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_cbrt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_clz32_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_cosh_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_expm1_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_fround_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_hypot_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_imul_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_log10_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_log1p_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_log2_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_sign_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_sinh_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_tanh_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_trunc_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_isFinite_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_isInteger_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_isNaN_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_isSafeInteger_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_parseFloat_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_parseInt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_assign_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_getOwnPropertySymbols_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_is_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_setPrototypeOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_all_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_prototype_catch_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_prototype_then_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_race_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_reject_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_resolve_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Proxy_revocable_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Proxy_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_apply_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_construct_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_defineProperty_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_deleteProperty_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_get_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_getOwnPropertyDescriptor_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_getPrototypeOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_has_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_isExtensible_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_ownKeys_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_preventExtensions_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_set_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Reflect_setPrototypeOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_prototype_match_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_prototype_replace_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_prototype_search_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_prototype_split_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_add_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_clear_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_delete_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_entries_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_forEach_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_has_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_keys_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_values_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_fromCodePoint_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_codePointAt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_endsWith_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_includes_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_normalize_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_repeat_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_startsWith_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_iterator_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_raw_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Symbol_for_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Symbol_keyFor_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Symbol_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Symbol_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Symbol_prototype_toPrimitive_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Symbol_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Uint16Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Uint32Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Uint8Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Uint8ClampedArray_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakMap_prototype_delete_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakMap_prototype_get_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakMap_prototype_has_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakMap_prototype_set_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakMap_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakSet_prototype_add_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakSet_prototype_delete_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakSet_prototype_has_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakSet_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArrayIteratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneratorFunction_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneratorPrototype_return_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneratorPrototype_throw_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_iterator_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue MapIteratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue SetIteratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue StringIteratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_from_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_of_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_copyWithin_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_entries_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_every_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_fill_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_filter_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_find_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_findIndex_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_forEach_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_indexOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_join_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_keys_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_lastIndexOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_map_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_reduce_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_reduceRight_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_reverse_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_set_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_slice_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_some_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_sort_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_subarray_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_toLocaleString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_values_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_prototype_compile_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_anchor_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_big_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_blink_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_bold_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_fixed_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_fontcolor_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_fontsize_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_italics_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_link_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_small_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_strike_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_sub_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_sup_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_includes_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArrayBuffer_prototype_slice_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_includes_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_add_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_and_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_compareExchage_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_exchange_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_isLockFree_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_load_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_or_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_store_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_sub_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_wait_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_notify_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_xor_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_entries_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_getOwnPropertyDescriptors_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype___defineGetter___Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype___defineSetter___Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype___lookupGetter___Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_prototype___lookupSetter___Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_values_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue SharedArrayBuffer_prototype_slice_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue SharedArrayBuffer_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_padEnd_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_padStart_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncFunction_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_prototype_finally_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncFromSyncIteratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncFromSyncIteratorPrototype_return_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncFromSyncIteratorPrototype_throw_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncGeneratorFunction_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncGeneratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncGeneratorPrototype_return_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncGeneratorPrototype_throw_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncIteratorPrototype_asyncIterator_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_flat_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_flatMap_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_fromEntries_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_trimEnd_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_trimLeft_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue BigInt_asIntN_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue BigInt_asUintN_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue BigInt_prototype_toLocaleString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue BigInt_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue BigInt_prototype_valueOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue BigInt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue BigInt64Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue BigUint64Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getBigInt64_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getBigUint64_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setBigInt64_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setBigUint64_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_allSettled_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_prototype_matchAll_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_matchAll_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExpStringIteratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AggregateError_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue FinalizationRegistry_prototype_register_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue FinalizationRegistry_prototype_unregister_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue FinalizationRegistry_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_any_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_replaceAll_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakRef_prototype_deref_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakRef_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_at_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_hasOwn_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_at_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_at_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_findLast_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_findLastIndex_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_toReversed_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_toSorted_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_toSpliced_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_prototype_with_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_findLast_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_findLastIndex_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_toReversed_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_toSorted_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_prototype_with_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArrayBuffer_prototype_resize_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArrayBuffer_prototype_transfer_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArrayBuffer_prototype_transferToFixedLength_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Atomics_waitAsync_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_groupBy_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_groupBy_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_withResolvers_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue SharedArrayBuffer_prototype_grow_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_isWellFormed_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_prototype_toWellFormed_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_getFloat16_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue DataView_prototype_setFloat16_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Float16Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Iterator_from_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Iterator_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Math_f16round_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_try_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue RegExp_escape_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_difference_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_intersection_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_isDisjointFrom_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_isSubsetOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_isSupersetOf_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_symmetricDifference_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_prototype_union_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_drop_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_every_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_filter_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_find_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_flatMap_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_forEach_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_map_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_reduce_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_some_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_take_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorPrototype_toArray_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorHelperPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue IteratorHelperPrototype_return_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WrapForValidIteratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WrapForValidIteratorPrototype_return_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// ES1
			// generic ToString invocation on the this_argument
			JSValue toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			JSValue Object_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_prototype_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// ES6
			JSValue GeneratorFunction_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			// ESNext
			JSValue AsyncFunction_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue AsyncGeneratorFunction_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// ES1
			JSValue Boolean_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Number_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			// ES6
			JSValue Symbol_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			// ES11
			JSValue BigInt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// ES1
			JSValue Date_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			// ES3
			JSValue RegExp_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// also for other types of errors
			JSValue Error_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// E4X
			JSValue QName_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Namespace_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XML_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue XMLList_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// ES6
			JSValue Proxy_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArrayBuffer_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue TypedArray_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Map_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Set_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakMap_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue WeakSet_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			JSValue Promise_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Promise_prototype_then_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// ES1
			JSValue eval_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue parseInt_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue parseFloat_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue escape_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue unescape_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue isNaN_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue isFinite_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			// ES3
			JSValue decodeURI_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue decodeURIComponent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue encodeURI_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue encodeURIComponent_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
#if ENABLE_ES4
			// ES4
			JSValue override_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
#endif

#if ENABLE_DEBUG
			JSValue _context_stack_size_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue _log_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue _relog_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
#endif
			JSValue print_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue console_log_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// CommonJS
			JSValue require_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			// AMD
			JSValue define_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			JSValue Object_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Function_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
#if ENABLE_ES4
			JSValue Class_prototype_toString_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
#endif

			JSValue Function_prototype_call_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			JSValue IteratorPrototype_iterator_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneratorPrototype_return_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue GeneratorPrototype_throw_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// ES5
			JSValue Object_create_call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_defineProperties_call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_defineProperty_call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_getPrototypeOf_call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_getOwnPropertyDescriptor_call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Object_setPrototypeOf_call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

			// ES6
			JSValue TypedArray_prototype_toStringTag_get(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));

#if ENABLE_ES4
			JSValue Class_prototype_get(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue String_length_get(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_length_get(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue Array_length_set(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
#endif

			// ES6
			JSValue Array_prototype_values_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
			JSValue ArrayIteratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
#endif

			void DefineProperties(JSObject O, JSValue Properties);
			std::vector<JSValue> EnumerableOwnProperties(JSObject O, JS::Runtime::EnumerationKind kind);
			/** Makes object non-extensible, all properties non-configurable and potentially write-only
			 *
			 * This corresponds to the ES6 internal method SetIntegrityLevel() and the ES4 internal methods sealObject(),
			 * sealAllLocalProperties() and sealLocalProperty()
			 *
			 * Corresponds to ES5 Object.freeze, Object.seal and ES3 Object.prototype.sealProperty
			 *
			 * @param O The object to make non-extensible, ES4 permits using primitive values.
			 * @param isFrozen Set to false to seal the object (make all properties non-configurable) and true to freeze (make all properties non-writable as well). ES4 only supports sealing.
			 * @param propertyName ECMAScript 4 only, seal a specific dynamic property instead of the entire object.
			 * @return True on success, false or exception thrown on failure
			 */
			bool SetIntegrityLevel(JSValue O, bool isFrozen __ES4_ONLY(, std::optional<JSValue> propertyKey = {})); // TODO: QName
			bool TestIntegrityLevel(JSObject O, bool isFrozen);

			void AddEntriesFromIterable(JSObject target, JSValue iterable, std::function<void(JSValue, JSValue)> adder);

			// represents any of a set of valid typed array constructors
			class TypedArray : public BuiltinFunction
			{
			public:
				static JSFunction JS::Runtime::Intrinsics::* IntrinsicPrototype;

				JS::Class::TypedArray::ElementType element_type;
				JSObject JS::Runtime::Intrinsics::* prototype;

				TypedArray(JS::Runtime::Realm * Realm, JS::Class::TypedArray::ElementType element_type, JSObject JS::Runtime::Intrinsics::* prototype)
					: BuiltinFunction(Realm, JS::Class::TypedArray::ArrayName(element_type), 3),
					element_type(element_type),
					prototype(prototype)
				{
				}

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
			};

			// class structures

#if ENABLE_ES4
			class ObjectClass : public JS::Class::Class
			{
			public:
				ObjectClass(JS::Runtime::Realm * realm)
					: Class(u"Object", std::optional<JSType>{}, realm->Intrinsics.Object_prototype, true, false, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				//JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class NeverClass : public JS::Class::Class
			{
			public:
				NeverClass(JS::Runtime::Realm * realm)
					: Class(u"Never", realm->Intrinsics.Object_class, std::optional<JSValue>{}, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class VoidClass : public JS::Class::Class
			{
			public:
				VoidClass(JS::Runtime::Realm * realm)
					: Class(u"Void", realm->Intrinsics.Object_class, std::optional<JSValue>{}, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class NullClass : public JS::Class::Class
			{
			public:
				NullClass(JS::Runtime::Realm * realm)
					: Class(u"Null", realm->Intrinsics.Object_class, std::optional<JSValue>{}, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				//JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				//std::optional<JSValue> GetDefaultValue() override;
			};

			class BooleanClass : public JS::Class::Class
			{
			public:
				BooleanClass(JS::Runtime::Realm * realm)
					: Class(u"Boolean", realm->Intrinsics.Object_class, realm->Intrinsics.Boolean_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class GeneralNumberClass : public JS::Class::Class
			{
			public:
				GeneralNumberClass(JS::Runtime::Realm * realm)
					: Class(u"GeneralNumber", realm->Intrinsics.Object_class, realm->Intrinsics.GeneralNumber_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				//JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class LongClass : public JS::Class::Class
			{
			public:
				LongClass(JS::Runtime::Realm * realm)
					: Class(u"long", realm->Intrinsics.GeneralNumber_class, realm->Intrinsics.long_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class UlongClass : public JS::Class::Class
			{
			public:
				UlongClass(JS::Runtime::Realm * realm)
					: Class(u"ulong", realm->Intrinsics.GeneralNumber_class, realm->Intrinsics.ulong_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class FloatClass : public JS::Class::Class
			{
			public:
				FloatClass(JS::Runtime::Realm * realm)
					: Class(u"float", realm->Intrinsics.GeneralNumber_class, realm->Intrinsics.float_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

#if ENABLE_DEC128
			class DecimalClass : public JS::Class::Class
			{
			public:
				DecimalClass(JS::Runtime::Realm * realm)
					: Class(u"decimal", realm->Intrinsics.GeneralNumber_class, realm->Intrinsics.decimal_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};
#endif

			class NumberClass : public JS::Class::Class
			{
			public:
				NumberClass(JS::Runtime::Realm * realm)
					: Class(u"Number", realm->Intrinsics.GeneralNumber_class, realm->Intrinsics.Number_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			/* Family of integral classes, includes sbyte, byte, short, ushort, int, uint, but not long/ulong which do not always fit into a Number */
			class IntegerClass : public JS::Class::Class
			{
			public:
				int64_t lowest, highest;
				IntegerClass(JSString name, int64_t lowest, int64_t highest, JS::Runtime::Realm * realm)
					: Class(name, realm->Intrinsics.Number_class, realm->Intrinsics.Number_prototype, false, true, realm),
					lowest(lowest), highest(highest) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class CharClass : public JS::Class::Class
			{
			public:
				CharClass(JS::Runtime::Realm * realm)
					: Class(u"char", realm->Intrinsics.Object_class, realm->Intrinsics.char_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class StringClass : public JS::Class::Class
			{
			public:
				StringClass(JS::Runtime::Realm * realm)
					: Class(u"String", realm->Intrinsics.Object_class, realm->Intrinsics.String_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class ArrayClass : public JS::Class::Class
			{
			public:
				ArrayClass(JS::Runtime::Realm * realm)
					: Class(u"Array", realm->Intrinsics.Object_class, realm->Intrinsics.Array_prototype, true, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
			};

			class NamespaceClass : public JS::Class::Class
			{
			public:
				NamespaceClass(JS::Runtime::Realm * realm)
					: Class(u"Namespace", realm->Intrinsics.Object_class, realm->Intrinsics.Namespace_prototype, false, true, realm) { }

				//JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				JS::Runtime::TypeHint GetDefaultHint() override;
				//std::optional<JSValue> GetDefaultValue() override;
			};

			class AttributeClass : public JS::Class::Class
			{
			public:
				AttributeClass(JS::Runtime::Realm * realm)
					: Class(u"Attribute", realm->Intrinsics.Object_class, realm->Intrinsics.Object_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				//JSString GetTypeofString() override;
				JS::Runtime::TypeHint GetDefaultHint() override;
				//std::optional<JSValue> GetDefaultValue() override;
			};

			class DateClass : public JS::Class::Class
			{
			public:
				DateClass(JS::Runtime::Realm * realm)
					: Class(u"Date", realm->Intrinsics.Object_class, realm->Intrinsics.Date_prototype, true, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				//JSString GetTypeofString() override;
				JS::Runtime::TypeHint GetDefaultHint() override;
				//std::optional<JSValue> GetDefaultValue() override;
			};

			class RegExpClass : public JS::Class::Class
			{
			public:
				RegExpClass(JS::Runtime::Realm * realm)
					: Class(u"RegExp", realm->Intrinsics.Object_class, realm->Intrinsics.RegExp_prototype, true, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
			};

			class ClassClass : public JS::Class::Class
			{
			public:
				ClassClass(JS::Runtime::Realm * realm)
					: Class(u"Class", realm->Intrinsics.Object_class, realm->Intrinsics.Class_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				JS::Runtime::TypeHint GetDefaultHint() override;
				//std::optional<JSValue> GetDefaultValue() override;
			};

			class FunctionClass : public JS::Class::Class
			{
			public:
				FunctionClass(JS::Runtime::Realm * realm)
					: Class(u"Function", realm->Intrinsics.Object_class, realm->Intrinsics.Function_prototype, false, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				JS::Runtime::TypeHint GetDefaultHint() override;
				//std::optional<JSValue> GetDefaultValue() override;
			};

			class PrototypeFunctionClass : public JS::Class::Class
			{
			public:
				PrototypeFunctionClass(JS::Runtime::Realm * realm)
					: Class(u"Function", realm->Intrinsics.Function_class, realm->Intrinsics.Function_prototype, true, false, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				JS::Runtime::TypeHint GetDefaultHint() override;
				//std::optional<JSValue> GetDefaultValue() override;
			};

			class PackageClass : public JS::Class::Class
			{
			public:
				PackageClass(JS::Runtime::Realm * realm)
					: Class(u"Package", realm->Intrinsics.Object_class, realm->Intrinsics.Object_prototype, true, true, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				//JSString GetTypeofString() override;
				JS::Runtime::TypeHint GetDefaultHint() override;
				//std::optional<JSValue> GetDefaultValue() override;
			};

			class ErrorClass : public JS::Class::Class
			{
			public:
				ErrorClass(JS::Runtime::Realm * realm)
					: Class(u"Error", realm->Intrinsics.Object_class, realm->Intrinsics.Error_prototype, true, false, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				//JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
			};

			class ErrorSubClass : public JS::Class::Class
			{
			public:
				ErrorSubClass(JSString name, JSObject prototype, JS::Runtime::Realm * realm)
					: Class(name, realm->Intrinsics.Error_class, prototype, true, false, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				//JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
			};

			class QNameClass : public JS::Class::Class
			{
			public:
				QNameClass(JS::Runtime::Realm * realm)
					: Class(u"QName", realm->Intrinsics.Object_class, realm->Intrinsics.QName_prototype, true, false, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
			};

			class XMLClass : public JS::Class::Class
			{
			public:
				XMLClass(JS::Runtime::Realm * realm)
					: Class(u"XML", realm->Intrinsics.Object_class, realm->Intrinsics.XML_prototype, false, false, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			class XMLListClass : public JS::Class::Class
			{
			public:
				XMLListClass(JS::Runtime::Realm * realm)
					: Class(u"XMLList", realm->Intrinsics.Object_class, realm->Intrinsics.XML_prototype, false, false, realm) { }

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				std::optional<JSValue> GetDefaultValue() override;
			};

			// used for Math and JSON global objects
			class SingletonClass : public JS::Class::Class
			{
			public:
				SingletonClass(JSString name, JSObject class_prototype, JS::Runtime::Realm * realm)
					: Class(name, realm->Intrinsics.Object_class, class_prototype, false, false, realm) { }

				//JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				//JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
				//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
				//JSBoolean Is(JSValue object) override;
				//std::optional<JSValue> Coerce(JSValue object, bool strict = true) override;
				//JSString GetTypeofString() override;
				//JS::Runtime::TypeHint GetDefaultHint() override;
				//std::optional<JSValue> GetDefaultValue() override;
			};
#endif
		}
	}
}

#endif // _BUILTINS_HH
