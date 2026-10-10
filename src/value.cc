
// TODO: remove superfluous includes
// TODO: split off parts to types.cc/types.hh
//#include <bit>
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <gmpxx.h>
#include <optional>
#include <sstream>
#include <stdfloat>

#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "core.hh"
#include "runtime.hh"
#include "execute.hh" // TODO: move ExecutionContext to execute.cc?
#include "environment.hh"
#include "parser.hh" // required to check character types
#include "ast.hh" // required for Functional related behavior
#include "builtins.hh"
#include "debugger.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

JSValue JS::Public::JS_undefined = JSValue();
JSValue JS::Public::JS_null = JSValue(JSObject(nullptr));
#if ENABLE_ES4
JSNamespace JS::Public::JS_public = gcn::make_gc<::JS::Type::Internal::PublicNamespace>();
JSNamespace JS::Public::JS_meta = gcn::make_gc<::JS::Class::Namespace>(u"meta");
#endif

//// PrimitiveValue

std::set<JS::Class::Internal::PrimitiveValue *> JS::Class::Internal::PrimitiveValue::to_keep_alive;

#if ENABLE_ES4
std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> JS::Class::Internal::PrimitiveValue::FindSlot([[maybe_unused]] JSInstanceVariable id)
{
	return {};
}

#endif
bool JS::Class::Internal::PrimitiveValue::IsExtensible()
{
	return false;
}

bool JS::Class::Internal::PrimitiveValue::PreventExtensions()
{
	return true;
}

std::optional<JSValue> JS::Class::Internal::PrimitiveValue::GetPrototypeOf()
{
	return {};
}

bool JS::Class::Internal::PrimitiveValue::SetPrototypeOf(JSValue new_prototype)
{
	return IsNull(new_prototype);
}

std::optional<JSProperty> JS::Class::Internal::PrimitiveValue::GetOwnProperty([[maybe_unused]] JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	return {};
}

std::optional<JSProperty> JS::Class::Internal::PrimitiveValue::GetProperty([[maybe_unused]] JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	return {};
}

bool JS::Class::Internal::PrimitiveValue::HasProperty([[maybe_unused]] JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
#if ENABLE_ES4
	// copied over from JS::Class::OrdinaryObject::HasPrototype
	if(IsVersionES4())
	{
		auto name = assert_property_key(property_key);

		for(Access access : Accesses(accesses))
		{
			if(GetClass()->GetBaseInstanceProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access))))
			{
				return true;
			}
		}
	}
#endif

	return false;
}

JSValue JS::Class::Internal::PrimitiveValue::Get([[maybe_unused]] JSValue property_key, [[maybe_unused]] JSValue receiver, [[maybe_unused]] Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		auto property_name_option = GetString(property_key);
		if(!property_name_option)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		auto name = property_name_option.value();
		// an extracted portion of the Object::Get method
		if(limit != nullptr)
		{
			auto property_option = limit->GetBaseInstanceProperty(name, namespaces, AccessSet::Read);
			if(property_option.has_value())
			{
				auto property = limit->GetDerivedInstanceProperty(name, namespaces, AccessSet::Read).value();
				return property->GetValue(receiver WITH_PHASE(phase));
			}
		}
		return JS_undefined;
	}
#endif

	// it should be first converted to an object via ToObject
	JS::Throw(JS::New<InternalError> __LOC(u"attemting to access member of primitive type"));
}

bool JS::Class::Internal::PrimitiveValue::Set([[maybe_unused]] JSValue property_key, [[maybe_unused]] JSValue value, [[maybe_unused]] JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), [[maybe_unused]] bool check_can_put)
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		auto name = assert_property_key(property_key);

		if(limit != nullptr)
		{
			auto property_option = limit->GetBaseInstanceProperty(name, namespaces, AccessSet::Write);
			if(property_option.has_value())
			{
				auto property = limit->GetDerivedInstanceProperty(name, namespaces, AccessSet::Write).value();
				if(!check_can_put)
				{
					property->SetValue(receiver, value);
				}
				return true;
			}
		}
		return false;
	}
#endif
	JS::Throw(JS::New<InternalError> __LOC("setting property of primitive value"));
}

JSValue JS::Class::Internal::PrimitiveValue::GetPrivate([[maybe_unused]] JSPrivateName name)
{
	JS::Throw(JS::New<InternalError> __LOC(u"primitive value has no private members"));
}

void JS::Class::Internal::PrimitiveValue::SetPrivate([[maybe_unused]] JSPrivateName name, [[maybe_unused]] JSValue value)
{
	JS::Throw(JS::New<InternalError> __LOC(u"primitive value has no private members"));
}

bool JS::Class::Internal::PrimitiveValue::Delete([[maybe_unused]] JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return true;
	}
#endif
	JS::Throw(JS::New<InternalError> __LOC(u"cannot delete property of primitive value"));
}

bool JS::Class::Internal::PrimitiveValue::DefineOwnProperty([[maybe_unused]] JSPropertyKey property_key, [[maybe_unused]] JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	JS::Throw(JS::New<InternalError> __LOC(u"cannot define property on primitive value"));
}

bool JS::Class::Internal::PrimitiveValue::Define([[maybe_unused]] JSPropertyKey property_key, [[maybe_unused]] JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return false; // TODO
	}
#endif
	JS::Throw(JS::New<InternalError> __LOC(u"cannot define property on primitive value"));
}

std::vector<JSPropertyKey> JS::Class::Internal::PrimitiveValue::OwnPropertyKeys(FOR_CLASS(limit))
{
	return {};
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::Internal::PrimitiveValue::ExtractAllProperties()
{
	return {};
}

std::map<JSString, JSValue> JS::Class::Internal::PrimitiveValue::GetInternalSlots([[maybe_unused]] bool unified)
{
	return {};
}

JSString JS::Class::Internal::PrimitiveValue::GetClassString()
{
	JS::Throw(JS::New<InternalError> __LOC(u"primitive value has no [[Class]] property"));
}

JS::Invocability JS::Class::Internal::PrimitiveValue::GetInvocability()
{
	return Invocability::NonCallable;
}

#if ENABLE_ES4
std::optional<JS::Runtime::SignatureType> JS::Class::Internal::PrimitiveValue::GetSignature()
{
	JS::Throw(JS::New<TypeError> __LOC(u"not callable"));
}
#endif

JSValue JS::Class::Internal::PrimitiveValue::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC(u"not callable"));
}

JSValue JS::Class::Internal::PrimitiveValue::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC(u"not constructable"));
}

void JS::Class::Internal::PrimitiveValue::PutValue([[maybe_unused]] JSValue value)
{
	JS::Throw(JS::New<ReferenceError> __LOC(u"cannot assign to primitive value"));
}

JSValue JS::Class::Internal::PrimitiveValue::ToNumeric(FOR_PHASE(phase))
{
	auto primitive = ToPrimitive(JS::Runtime::TypeHint::Number WITH_PHASE(phase));
	// TODO: make sure this does not result in an infinite recursion
	return primitive.ToNumeric(VIA_PHASE(phase));
}

JS::Runtime::Realm * JS::Class::Internal::PrimitiveValue::GetFunctionRealm()
{
	JS::Throw(JS::New<TypeError> __LOC(u"not callable"));
}


JSString JS::Class::Internal::PrimitiveValue::ToXMLString([[maybe_unused]] std::map<JSString, JSNamespace> AncestorNamespaces, [[maybe_unused]] uint32_t IndentLevel)
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert to XML string"));
}

JSXML JS::Class::Internal::PrimitiveValue::ToXML()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert to XML"));
}

JSXMLList JS::Class::Internal::PrimitiveValue::ToXMLList()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert to XML list"));
}

JSAttributeName JS::Class::Internal::PrimitiveValue::ToAttributeName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert to attribute name"));
}

JSValue JS::Class::Internal::PrimitiveValue::ToXMLName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert to XML name"));
}

//// Primitive<JS::Type::Internal::Undefined>

JSValue JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JS_undefined;
}

bool JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::SameValue(const JSValue& other) const
{
	return IsUndefined(other);
}

JSValue JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return JS_undefined;
}

JSBoolean JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToBoolean()
{
	return false;
}

JSNumber JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToNumber(FOR_PHASE(phase))
{
	return NAN;
}

JSValue JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToNumeric(FOR_PHASE(phase))
{
	return NAN;
}

JSBigInt JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToBigInt()
{
	JS::Throw(JS::New<TypeError> __LOC(u"undefined cannot be converted to BigInt"));
}

JSString JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToString(FOR_PHASE(phase))
{
	return u"undefined";
}

JSObject JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToObject()
{
	JS::Throw(JS::New<TypeError> __LOC(u"undefined cannot be converted to an Object"));
}

JSString JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::Typeof()
{
	return u"undefined";
}

JSString JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToStringTag()
{
	return u"Undefined";
}

#if ENABLE_ES4
JSType JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::GetType()
{
	//return JS::Class::Void;
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->Void_class : nullptr;
}
#endif

JSString JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToXMLString([[maybe_unused]] std::map<JSString, JSNamespace> AncestorNamespaces, [[maybe_unused]] uint32_t IndentLevel)
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert undefined to XML string"));
}

JSXML JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToXML()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert undefined to XML"));
}

JSXMLList JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToXMLList()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert undefined to XML list"));
}

JSAttributeName JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToAttributeName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert undefined to attribute name"));
}

JSValue JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::ToXMLName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert undefined to XML name"));
}

bool JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::has_value(const std::type_info& type) const
{
	return type == typeid(JS::Type::Internal::Undefined);
}

const void * JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::get_if([[maybe_unused]] const std::type_info& type) const
{
	return nullptr;
}

void JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::inspect([[maybe_unused]] std::ostream& out, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited,[[maybe_unused]]  std::optional<std::string> access)
{
	out << "\33[37mundefined\33[m";
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("undefined")
		->CloseXMLElement();
}
#endif

//// Primitive<JS::Type::Internal::Null>

JSValue JS::Class::Internal::Primitive<JS::Type::Internal::Null>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JS_null;
}

bool JS::Class::Internal::Primitive<JS::Type::Internal::Null>::SameValue(const JSValue& other) const
{
	return IsNull(other);
}

JSValue JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return JS_null;
}

JSBoolean JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToBoolean()
{
	return false;
}

JSNumber JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToNumber(FOR_PHASE(phase))
{
	return 0.0;
}

JSValue JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToNumeric(FOR_PHASE(phase))
{
	return 0.0;
}

JSBigInt JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToBigInt()
{
	JS::Throw(JS::New<TypeError> __LOC(u"null cannot be converted to BigInt"));
}

JSString JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToString(FOR_PHASE(phase))
{
	return u"null";
}

JSObject JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToObject()
{
	JS::Throw(JS::New<TypeError> __LOC(u"null cannot be converted to an object"));
}

JSString JS::Class::Internal::Primitive<JS::Type::Internal::Null>::Typeof()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		// Note: ES4.2006 defines it this way
		return u"null";
	}
	else
#endif
	{
		return u"object";
	}
}

JSString JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToStringTag()
{
	return u"Null";
}

#if ENABLE_ES4
JSType JS::Class::Internal::Primitive<JS::Type::Internal::Null>::GetType()
{
//	return JS::Class::Null;
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->Null_class : nullptr;
}
#endif

JSString JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToXMLString([[maybe_unused]] std::map<JSString, JSNamespace> AncestorNamespaces, [[maybe_unused]] uint32_t IndentLevel)
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert null to XML string"));
}

JSXML JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToXML()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert null to XML"));
}

JSXMLList JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToXMLList()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert null to XML list"));
}

JSAttributeName JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToAttributeName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert null to attribute name"));
}

JSValue JS::Class::Internal::Primitive<JS::Type::Internal::Null>::ToXMLName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert null to XML name"));
}

bool JS::Class::Internal::Primitive<JS::Type::Internal::Null>::has_value(const std::type_info& type) const
{
	return type == typeid(JS::Type::Internal::Null);
}

const void * JS::Class::Internal::Primitive<JS::Type::Internal::Null>::get_if([[maybe_unused]] const std::type_info& type) const
{
	return nullptr;
}

void JS::Class::Internal::Primitive<JS::Type::Internal::Null>::inspect(std::ostream& out, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[1mnull\33[m";
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JS::Type::Internal::Null>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("null")
		->CloseXMLElement();
}
#endif

//// Primitive<JSBoolean>

JSValue JS::Class::Internal::Primitive<JSBoolean>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return BooleanData;
}

bool JS::Class::Internal::Primitive<JSBoolean>::SameValue(const JSValue& other) const
{
	return GetBoolean(other) && BooleanData == GetBoolean(other).value();
}

JSValue JS::Class::Internal::Primitive<JSBoolean>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return BooleanData;
}

JSBoolean JS::Class::Internal::Primitive<JSBoolean>::ToBoolean()
{
	return BooleanData;
}

JSNumber JS::Class::Internal::Primitive<JSBoolean>::ToNumber(FOR_PHASE(phase))
{
	return BooleanData ? 1.0 : 0.0;
}

JSValue JS::Class::Internal::Primitive<JSBoolean>::ToNumeric(FOR_PHASE(phase))
{
	return BooleanData ? 1.0 : 0.0;
}

JSBigInt JS::Class::Internal::Primitive<JSBoolean>::ToBigInt()
{
	return BooleanData ? 1_mpz : 0_mpz;
}

JSString JS::Class::Internal::Primitive<JSBoolean>::ToString(FOR_PHASE(phase))
{
	return BooleanData ? u"true" : u"false";
}

JSObject JS::Class::Internal::Primitive<JSBoolean>::ToObject()
{
	return JS::New<JS::Class::Boolean>(BooleanData);
}

JSString JS::Class::Internal::Primitive<JSBoolean>::Typeof()
{
	return u"boolean";
}

JSString JS::Class::Internal::Primitive<JSBoolean>::ToStringTag()
{
	return u"Boolean";
}

#if ENABLE_ES4
JSType JS::Class::Internal::Primitive<JSBoolean>::GetType()
{
//	return JS::Class::Boolean;
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->Boolean_class : nullptr;
}
#endif

JSString JS::Class::Internal::Primitive<JSBoolean>::ToXMLString([[maybe_unused]] std::map<JSString, JSNamespace> AncestorNamespaces, [[maybe_unused]] uint32_t IndentLevel)
{
	return ToString(VIA_RUNTIME_PHASE);
}

JSXML JS::Class::Internal::Primitive<JSBoolean>::ToXML()
{
	return JSValue(ToString(VIA_RUNTIME_PHASE)).ToXML();
}

JSXMLList JS::Class::Internal::Primitive<JSBoolean>::ToXMLList()
{
	return JSValue(ToString(VIA_RUNTIME_PHASE)).ToXMLList();
}

JSAttributeName JS::Class::Internal::Primitive<JSBoolean>::ToAttributeName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert boolean to attribute name"));
}

JSValue JS::Class::Internal::Primitive<JSBoolean>::ToXMLName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert boolean to XML name"));
}

bool JS::Class::Internal::Primitive<JSBoolean>::has_value(const std::type_info& type) const
{
	return type == typeid(JSBoolean);
}

const void * JS::Class::Internal::Primitive<JSBoolean>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSBoolean))
		return &BooleanData;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSBoolean>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSBoolean>::inspect_value(out, BooleanData, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSBoolean>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("boolean")
		->AppendText(BooleanData ? "true" : "false")
		->CloseXMLElement();
}
#endif

//// Primitive<JSNumber>

JSValue JS::Class::Internal::Primitive<JSNumber>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return NumberData;
}

bool JS::Class::Internal::Primitive<JSNumber>::SameValue(const JSValue& other) const
{
	if(auto other_number = GetNumber(other))
	{
		if(std::fpclassify(NumberData) == FP_NAN && std::fpclassify(*other_number) == FP_NAN)
		{
			return true;
		}
		return *reinterpret_cast<const uint64_t *>(&NumberData) == *reinterpret_cast<uint64_t *>(&*other_number);
	}
	return false;
}

bool JS::Class::Internal::Primitive<JSNumber>::SameValueZero(const JSValue& other) const
{
	if(auto other_number = GetNumber(other))
	{
		return std::isunordered(NumberData, *other_number) || NumberData == *other_number;
	}
	return false;
}

bool JS::Class::Internal::Primitive<JSNumber>::IsStrictlyEqual(const JSValue& other) const
{
	if(auto other_number = GetNumber(other))
	{
		return NumberData == *other_number;
	}
	return false;
}

JSValue JS::Class::Internal::Primitive<JSNumber>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return NumberData;
}

JSBoolean JS::Class::Internal::Primitive<JSNumber>::ToBoolean()
{
	switch(std::fpclassify(NumberData))
	{
	case FP_NAN:
	case FP_ZERO:
		return false;
	default:
		return true;
	}
}

JSNumber JS::Class::Internal::Primitive<JSNumber>::ToNumber(FOR_PHASE(phase))
{
	return NumberData;
}

JSValue JS::Class::Internal::Primitive<JSNumber>::ToNumeric(FOR_PHASE(phase))
{
	return NumberData;
}

JSBigInt JS::Class::Internal::Primitive<JSNumber>::ToBigInt()
{
	JS::Throw(JS::New<TypeError> __LOC(u"number cannot be converted to BigInt"));
}

JSString JS::Class::Internal::Primitive<JSNumber>::ToString(FOR_PHASE(phase))
{
	return JS::NumberToString(NumberData, 10);
}

JSObject JS::Class::Internal::Primitive<JSNumber>::ToObject()
{
	return JS::New<JS::Class::Number>(NumberData);
}

JSString JS::Class::Internal::Primitive<JSNumber>::Typeof()
{
	return u"number";
}

JSString JS::Class::Internal::Primitive<JSNumber>::ToStringTag()
{
	return u"Number";
}

#if ENABLE_ES4
JSType JS::Class::Internal::Primitive<JSNumber>::GetType()
{
//	return JS::Class::Number;
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->Number_class : nullptr;
}
#endif

JSString JS::Class::Internal::Primitive<JSNumber>::ToXMLString([[maybe_unused]] std::map<JSString, JSNamespace> AncestorNamespaces, [[maybe_unused]] uint32_t IndentLevel)
{
	return ToString(VIA_RUNTIME_PHASE);
}

JSXML JS::Class::Internal::Primitive<JSNumber>::ToXML()
{
	return JSValue(ToString(VIA_RUNTIME_PHASE)).ToXML();
}

JSXMLList JS::Class::Internal::Primitive<JSNumber>::ToXMLList()
{
	return JSValue(ToString(VIA_RUNTIME_PHASE)).ToXMLList();
}

JSAttributeName JS::Class::Internal::Primitive<JSNumber>::ToAttributeName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert number to attribute name"));
}

JSValue JS::Class::Internal::Primitive<JSNumber>::ToXMLName()
{
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert number to XML name"));
}

bool JS::Class::Internal::Primitive<JSNumber>::has_value(const std::type_info& type) const
{
	return type == typeid(JSNumber);
}

const void * JS::Class::Internal::Primitive<JSNumber>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSNumber))
		return &NumberData;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSNumber>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSNumber>::inspect_value(out, NumberData, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSNumber>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("number")
		->AppendText(NumberData)
		->CloseXMLElement();
}
#endif

//// Primitive<JSString>

JSValue JS::Class::Internal::Primitive<JSString>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return StringData;
}

bool JS::Class::Internal::Primitive<JSString>::SameValue(const JSValue& other) const
{
	return GetString(other) && StringData == GetString(other).value();
}

JSValue JS::Class::Internal::Primitive<JSString>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return StringData;
}

JSBoolean JS::Class::Internal::Primitive<JSString>::ToBoolean()
{
	return StringData.size() != 0;
}

JSNumber JS::Class::Internal::Primitive<JSString>::ToNumber(FOR_PHASE(phase))
{
	JSString trimmed = StringData;

	trimmed.erase(
		trimmed.begin(),
		std::find_if(
			trimmed.begin(),
			trimmed.end(),
			[](char16_t c) { return !JS::Parser::Tokenizer::IsWhiteSpace(c) && !JS::Parser::Tokenizer::IsLineTerminator(c); }));

	trimmed.erase(
		std::find_if(
			trimmed.rbegin(),
			trimmed.rend(),
			[](char16_t c) { return !JS::Parser::Tokenizer::IsWhiteSpace(c) && !JS::Parser::Tokenizer::IsLineTerminator(c); }).base(),
		trimmed.end());

	if(trimmed == u"Infinity")
	{
		return INFINITY;
	}

	std::string converted;
	for(char16_t c : StringData)
	{
		if(c < 0x7F)
			converted += char(c);
		else
			return NAN;
	}
	try
	{
		return std::stod(converted);
	}
	catch(const std::invalid_argument&)
	{
		return NAN;
	}
}

JSValue JS::Class::Internal::Primitive<JSString>::ToNumeric(FOR_PHASE(phase))
{
	return ToNumber(VIA_PHASE(phase));
}

JSBigInt JS::Class::Internal::Primitive<JSString>::ToBigInt()
{
	JSString trimmed = StringData;

	trimmed.erase(
		trimmed.begin(),
		std::find_if(
			trimmed.begin(),
			trimmed.end(),
			[](char16_t c) { return !JS::Parser::Tokenizer::IsWhiteSpace(c) && !JS::Parser::Tokenizer::IsLineTerminator(c); }));

	trimmed.erase(
		std::find_if(
			trimmed.rbegin(),
			trimmed.rend(),
			[](char16_t c) { return !JS::Parser::Tokenizer::IsWhiteSpace(c) && !JS::Parser::Tokenizer::IsLineTerminator(c); }).base(),
		trimmed.end());

	std::string converted;
	for(char16_t c : StringData)
	{
		if(c < 0x7F)
			converted += char(c);
		else
			JS::Throw(JS::New<SyntaxError> __LOC(u""));
	}
	try
	{
		return JSBigInt(converted);
	}
	catch(const std::invalid_argument&)
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u""));
	}
}

JSString JS::Class::Internal::Primitive<JSString>::ToString(FOR_PHASE(phase))
{
	return StringData;
}

JSObject JS::Class::Internal::Primitive<JSString>::ToObject()
{
	return JS::New<JS::Class::String>(StringData);
}

JSString JS::Class::Internal::Primitive<JSString>::Typeof()
{
	return u"string";
}

JSString JS::Class::Internal::Primitive<JSString>::ToStringTag()
{
	return u"String";
}

#if ENABLE_ES4
JSType JS::Class::Internal::Primitive<JSString>::GetType()
{
//	return JS::Class::String;
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->String_class : nullptr;
}
#endif

JSString JS::Class::Internal::Primitive<JSString>::ToXMLString([[maybe_unused]] std::map<JSString, JSNamespace> AncestorNamespaces, [[maybe_unused]] uint32_t IndentLevel)
{
	return JS::EscapeElementValue(StringData);
}

JSXML JS::Class::Internal::Primitive<JSString>::ToXML()
{
	// TODO
	return nullptr;
}

JSXMLList JS::Class::Internal::Primitive<JSString>::ToXMLList()
{
	// TODO
	return nullptr;
}

JSAttributeName JS::Class::Internal::Primitive<JSString>::ToAttributeName()
{
	// TODO
	return JSAttributeName{JS::Type::QName(u"")};
}

JSValue JS::Class::Internal::Primitive<JSString>::ToXMLName()
{
	uint32_t index;
	if(is_array_index(StringData, index))
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
	if(StringData.size() > 0 && StringData[0] == u'@')
	{
		// simulate AttributeName(new QName(new Namespace(), StringData.substr(1)))
		return JSAttributeName(JS::Type::QName(
			/* uri = */ u"",
			/* localName = */ StringData.substr(1),
			/* prefix = */ u""));
	}
	else
	{
		// simulate new QName(StringData)
		if(StringData == u"*")
		{
			return JS::New<JS::Class::QName>(u"*");
		}
		else
		{
			JSNamespace DefaultNamespace = GetCurrentExecutionContext()->LexicalEnvironment->GetDefaultNamespace();
			return JS::New<JS::Class::QName>(
				/* uri = */ DefaultNamespace->uri,
				/* localName = */ StringData,
				/* prefix = */ DefaultNamespace->prefix);
		}
	}
}

bool JS::Class::Internal::Primitive<JSString>::has_value(const std::type_info& type) const
{
	return type == typeid(JSString);
}

const void * JS::Class::Internal::Primitive<JSString>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSString))
		return &StringData;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSString>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSString>::inspect_value(out, StringData, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSString>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("string")
		->AppendText(StringData)
		->CloseXMLElement();
}
#endif

//// Primitive<JSSymbol>

JSValue JS::Class::Internal::Primitive<JSSymbol>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return SymbolData;
}

bool JS::Class::Internal::Primitive<JSSymbol>::SameValue(const JSValue& other) const
{
	return GetSymbol(other) && SymbolData == GetSymbol(other).value();
}

JSValue JS::Class::Internal::Primitive<JSSymbol>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return SymbolData;
}

JSBoolean JS::Class::Internal::Primitive<JSSymbol>::ToBoolean()
{
	return true;
}

JSNumber JS::Class::Internal::Primitive<JSSymbol>::ToNumber(FOR_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC(u"cannot convert symbol to number"));
}

JSValue JS::Class::Internal::Primitive<JSSymbol>::ToNumeric(FOR_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC(u"cannot convert symbol to numeric"));
}

JSBigInt JS::Class::Internal::Primitive<JSSymbol>::ToBigInt()
{
	JS::Throw(JS::New<TypeError> __LOC(u"cannot convert symbol to bigint"));
}

JSString JS::Class::Internal::Primitive<JSSymbol>::ToString(FOR_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC(u"cannot convert symbol to string"));
}

JSObject JS::Class::Internal::Primitive<JSSymbol>::ToObject()
{
	return JS::New<JS::Class::Symbol>(SymbolData);
}

JSString JS::Class::Internal::Primitive<JSSymbol>::Typeof()
{
	return u"symbol";
}

JSString JS::Class::Internal::Primitive<JSSymbol>::ToStringTag()
{
	return JS::ToStringTag(ToObject());
}

#if ENABLE_ES4
JSType JS::Class::Internal::Primitive<JSSymbol>::GetType()
{
	JS::Throw(JS::New<InternalError> __LOC()); // Symbols not supported in ES4
}
#endif

bool JS::Class::Internal::Primitive<JSSymbol>::has_value(const std::type_info& type) const
{
	return type == typeid(JSSymbol);
}

const void * JS::Class::Internal::Primitive<JSSymbol>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSSymbol))
		return &SymbolData;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSSymbol>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSSymbol>::inspect_value(out, SymbolData, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSSymbol>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	auto symbol_id = debugger->LookupSymbol(SymbolData);
	debugger
		->OpenXMLElement("symbol")
		->AddXMLAttribute("ref", symbol_id)
		->CloseXMLElement();
}
#endif

//// Primitive<JSBigInt>

JSValue JS::Class::Internal::Primitive<JSBigInt>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return BigIntData;
}

bool JS::Class::Internal::Primitive<JSBigInt>::SameValue(const JSValue& other) const
{
	return GetBigInt(other) && BigIntData == GetBigInt(other).value();
}

JSValue JS::Class::Internal::Primitive<JSBigInt>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return BigIntData;
}

JSBoolean JS::Class::Internal::Primitive<JSBigInt>::ToBoolean()
{
	return sgn(BigIntData) != 0;
}

JSNumber JS::Class::Internal::Primitive<JSBigInt>::ToNumber(FOR_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC(u"cannot convert bigint to number"));
}

JSValue JS::Class::Internal::Primitive<JSBigInt>::ToNumeric(FOR_PHASE(phase))
{
	return BigIntData;
}

JSBigInt JS::Class::Internal::Primitive<JSBigInt>::ToBigInt()
{
	return BigIntData;
}

JSString JS::Class::Internal::Primitive<JSBigInt>::ToString(FOR_PHASE(phase))
{
	std::string str = BigIntData.get_str();
	std::u16string result;
	for(size_t i = 0; i < str.size(); i++)
	{
		result += char16_t(str[i]);
	}
	return result;
}

JSObject JS::Class::Internal::Primitive<JSBigInt>::ToObject()
{
	return JS::New<JS::Class::BigInt>(BigIntData);
}

JSString JS::Class::Internal::Primitive<JSBigInt>::Typeof()
{
	return u"bigint";
}

JSString JS::Class::Internal::Primitive<JSBigInt>::ToStringTag()
{
	return JS::ToStringTag(ToObject());
}

#if ENABLE_ES4
JSType JS::Class::Internal::Primitive<JSBigInt>::GetType()
{
	JS::Throw(JS::New<InternalError> __LOC()); // BigInts not supported in ES4
}
#endif

bool JS::Class::Internal::Primitive<JSBigInt>::has_value(const std::type_info& type) const
{
	return type == typeid(JSBigInt);
}

const void * JS::Class::Internal::Primitive<JSBigInt>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSBigInt))
		return &BigIntData;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSBigInt>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSBigInt>::inspect_value(out, BigIntData, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSBigInt>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("bigint")
		->AppendText(BigIntData)
		->CloseXMLElement();
}
#endif

#if ENABLE_ES4
//// Primitive<JSLong>

JSValue JS::Class::Internal::Primitive<JSLong>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JSValue(value);
}

bool JS::Class::Internal::Primitive<JSLong>::SameValue(const JSValue& other) const
{
	return other.has_value<JSLong>() && value == *other.get_if<JSLong>();
}

JSValue JS::Class::Internal::Primitive<JSLong>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return JSValue(value);
}

JSBoolean JS::Class::Internal::Primitive<JSLong>::ToBoolean()
{
	return value != 0;
}

JSNumber JS::Class::Internal::Primitive<JSLong>::ToNumber(FOR_PHASE(phase))
{
	return JSNumber(value);
}

JSValue JS::Class::Internal::Primitive<JSLong>::ToNumeric(FOR_PHASE(phase))
{
	return JSValue(value);
}

JSBigInt JS::Class::Internal::Primitive<JSLong>::ToBigInt()
{
	JS::Throw(JS::New<InternalError> __LOC()); // long (ES4) and BigInt (ES11) never coexisted
}

JSString JS::Class::Internal::Primitive<JSLong>::ToString(FOR_PHASE(phase))
{
	std::stringstream oss;
	oss << value;
	std::string str = oss.str();
	std::u16string result;
	for(size_t i = 0; i < str.size(); i++)
	{
		result += char16_t(str[i]);
	}
	return result;
}

JSObject JS::Class::Internal::Primitive<JSLong>::ToObject()
{
	JS::Throw(JS::New<InternalError> __LOC()); // not supported outside ES4
}

JSString JS::Class::Internal::Primitive<JSLong>::Typeof()
{
	return u"long";
}

JSString JS::Class::Internal::Primitive<JSLong>::ToStringTag()
{
	return u"long";
}

JSType JS::Class::Internal::Primitive<JSLong>::GetType()
{
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->long_class : nullptr;
}

bool JS::Class::Internal::Primitive<JSLong>::has_value(const std::type_info& type) const
{
	return type == typeid(JSLong);
}

const void * JS::Class::Internal::Primitive<JSLong>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSLong))
		return &value;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSLong>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSLong>::inspect_value(out, value, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSLong>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("long")
		->AppendText(value)
		->CloseXMLElement();
}
#endif

//// Primitive<JSUlong>

JSValue JS::Class::Internal::Primitive<JSUlong>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JSValue(value);
}

bool JS::Class::Internal::Primitive<JSUlong>::SameValue(const JSValue& other) const
{
	return other.has_value<JSUlong>() && value == *other.get_if<JSUlong>();
}

JSValue JS::Class::Internal::Primitive<JSUlong>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return JSValue(value);
}

JSBoolean JS::Class::Internal::Primitive<JSUlong>::ToBoolean()
{
	return value != 0;
}

JSNumber JS::Class::Internal::Primitive<JSUlong>::ToNumber(FOR_PHASE(phase))
{
	return JSNumber(value);
}

JSValue JS::Class::Internal::Primitive<JSUlong>::ToNumeric(FOR_PHASE(phase))
{
	return JSValue(value);
}

JSBigInt JS::Class::Internal::Primitive<JSUlong>::ToBigInt()
{
	JS::Throw(JS::New<InternalError> __LOC()); // ulong (ES4) and BigInt (ES11) never coexisted
}

JSString JS::Class::Internal::Primitive<JSUlong>::ToString(FOR_PHASE(phase))
{
	std::stringstream oss;
	oss << value;
	std::string str = oss.str();
	std::u16string result;
	for(size_t i = 0; i < str.size(); i++)
	{
		result += char16_t(str[i]);
	}
	return result;
}

JSObject JS::Class::Internal::Primitive<JSUlong>::ToObject()
{
	JS::Throw(JS::New<InternalError> __LOC()); // not supported outside ES4
}

JSString JS::Class::Internal::Primitive<JSUlong>::Typeof()
{
	return u"ulong";
}

JSString JS::Class::Internal::Primitive<JSUlong>::ToStringTag()
{
	return u"ulong";
}

JSType JS::Class::Internal::Primitive<JSUlong>::GetType()
{
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->ulong_class : nullptr;
}

bool JS::Class::Internal::Primitive<JSUlong>::has_value(const std::type_info& type) const
{
	return type == typeid(JSUlong);
}

const void * JS::Class::Internal::Primitive<JSUlong>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSUlong))
		return &value;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSUlong>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSUlong>::inspect_value(out, value, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSUlong>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("ulong")
		->AppendText(value)
		->CloseXMLElement();
}
#endif

//// Primitive<JSFloat>

JSValue JS::Class::Internal::Primitive<JSFloat>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JSValue(value);
}

bool JS::Class::Internal::Primitive<JSFloat>::SameValue(const JSValue& other) const
{
	return other.has_value<JSFloat>() && value == *other.get_if<JSFloat>();
}

JSValue JS::Class::Internal::Primitive<JSFloat>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return JSValue(value);
}

JSBoolean JS::Class::Internal::Primitive<JSFloat>::ToBoolean()
{
	switch(std::fpclassify(value))
	{
	case FP_NAN:
	case FP_ZERO:
		return false;
	default:
		return true;
	}
}

JSNumber JS::Class::Internal::Primitive<JSFloat>::ToNumber(FOR_PHASE(phase))
{
	return JSNumber(value);
}

JSValue JS::Class::Internal::Primitive<JSFloat>::ToNumeric(FOR_PHASE(phase))
{
	return JSValue(value);
}

JSBigInt JS::Class::Internal::Primitive<JSFloat>::ToBigInt()
{
	JS::Throw(JS::New<InternalError> __LOC()); // float (ES4) and BigInt (ES11) never coexisted
}

JSString JS::Class::Internal::Primitive<JSFloat>::ToString(FOR_PHASE(phase))
{
	return NumberToString(value, 10);
}

JSObject JS::Class::Internal::Primitive<JSFloat>::ToObject()
{
	JS::Throw(JS::New<InternalError> __LOC()); // not supported outside ES4
}

JSString JS::Class::Internal::Primitive<JSFloat>::Typeof()
{
	return u"float";
}

JSString JS::Class::Internal::Primitive<JSFloat>::ToStringTag()
{
	return u"float";
}

JSType JS::Class::Internal::Primitive<JSFloat>::GetType()
{
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->float_class : nullptr;
}

bool JS::Class::Internal::Primitive<JSFloat>::has_value(const std::type_info& type) const
{
	return type == typeid(JSFloat);
}

const void * JS::Class::Internal::Primitive<JSFloat>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSFloat))
		return &value;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSFloat>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSFloat>::inspect_value(out, value, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSFloat>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("float")
		->AppendText(value)
		->CloseXMLElement();
}
#endif

#if ENABLE_DEC128
//// Primitive<JSDecimal>

JSValue JS::Class::Internal::Primitive<JSDecimal>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JSValue(value);
}

bool JS::Class::Internal::Primitive<JSDecimal>::SameValue(const JSValue& other) const
{
	return other.has_value<JSDecimal>() && value == *other.get_if<JSDecimal>();
}

JSValue JS::Class::Internal::Primitive<JSDecimal>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return JSValue(value);
}

JSBoolean JS::Class::Internal::Primitive<JSDecimal>::ToBoolean()
{
	/*switch(std::fpclassify(value))
	{
	case FP_NAN:
	case FP_ZERO:
		return false;
	default:
		return true;
	}*/
	// TODO
	return value != 0;
}

JSNumber JS::Class::Internal::Primitive<JSDecimal>::ToNumber(FOR_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC()); // TODO
}

JSValue JS::Class::Internal::Primitive<JSDecimal>::ToNumeric(FOR_PHASE(phase))
{
	return JSValue(value);
}

JSBigInt JS::Class::Internal::Primitive<JSDecimal>::ToBigInt()
{
	JS::Throw(JS::New<InternalError> __LOC()); // decimal (ES4) and BigInt (ES11) never coexisted
}

JSString JS::Class::Internal::Primitive<JSDecimal>::ToString(FOR_PHASE(phase))
{
	switch(std::fpclassify(std::decimal::decimal_to_long_double(value))) // TODO: better method?
	{
	case FP_NAN:
		return u"NaN";
	case FP_ZERO:
		return u"0";
	case FP_INFINITE:
		return value < 0 ? u"-Infinity" : u"Infinity"; // TODO: better method?
	default:
		break;
	}

	JSString result = u"";
	if(value < 0) // TODO: better method?
	{
		result = u"-";
		value = -value;
	}

	int shift = 0;
	if(std::decimal::decimal_to_long_long(value) == value)
	{
		for(JSDecimal shifted = value / 10;
			std::decimal::decimal_to_long_long(shifted) == shifted;
			shifted /= 10)
		{
			shift++;
			value = shifted;
		}
	}
	else
	{
		shift--;
		for(value *= 10;
			std::decimal::decimal_to_long_long(value) != value;
			value *= 10)
		{
			shift--;
		}
	}
	JSString intpart = UInt64ToString(std::decimal::decimal_to_long_long(value), 10);
	shift += intpart.size();
	if(-5 <= shift && shift <= 21)
	{
		if(shift >= int(intpart.size()))
		{
			result += intpart;
			result += std::u16string(shift - intpart.size(), u'0');
		}
		else if(shift > 0)
		{
			result += intpart.substr(0, shift);
			result += u".";
			result += intpart.substr(shift);
		}
		else
		{
			result += u"0.";
			result += std::u16string(-shift, u'0');
			result += intpart;
		}
	}
	else
	{
		if(intpart.size() == 1)
		{
			result += intpart;
		}
		else
		{
			result += intpart.substr(0, 1);
			result += u".";
			result += intpart.substr(1);
		}
		result += u"e";
		result += shift < 0 ? u"-" : u"+";
		result += UInt64ToString(std::abs(shift));
	}
	return result;
}

JSObject JS::Class::Internal::Primitive<JSDecimal>::ToObject()
{
	JS::Throw(JS::New<InternalError> __LOC()); // not supported outside ES4
}

JSString JS::Class::Internal::Primitive<JSDecimal>::Typeof()
{
	return u"float";
}

JSString JS::Class::Internal::Primitive<JSDecimal>::ToStringTag()
{
	return u"float";
}

JSType JS::Class::Internal::Primitive<JSDecimal>::GetType()
{
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->decimal_class : nullptr;
}

bool JS::Class::Internal::Primitive<JSDecimal>::has_value(const std::type_info& type) const
{
	return type == typeid(JSDecimal);
}

const void * JS::Class::Internal::Primitive<JSDecimal>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSDecimal))
		return &value;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSDecimal>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSDecimal>::inspect_value(out, value, depth, visited, access);
}
#endif

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSDecimal>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("long")
		->AppendText(ToString(__ES4_ONLY(JS::Runtime::ExecutionPhase::Compile)))
		->CloseXMLElement();
}
#endif

//// Primitive<JSChar>

JSValue JS::Class::Internal::Primitive<JSChar>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JSValue(value);
}

bool JS::Class::Internal::Primitive<JSChar>::SameValue(const JSValue& other) const
{
	return other.has_value<JSChar>() && value == *other.get_if<JSChar>();
}

JSValue JS::Class::Internal::Primitive<JSChar>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return JSValue(value);
}

JSBoolean JS::Class::Internal::Primitive<JSChar>::ToBoolean()
{
	return true; // even '\0' is true according to the Netscape proposal
}

JSNumber JS::Class::Internal::Primitive<JSChar>::ToNumber(FOR_PHASE(phase))
{
	return ::JS::ToNumber(ToString(__ES4_ONLY(phase)) WITH_PHASE(phase));
}

JSValue JS::Class::Internal::Primitive<JSChar>::ToNumeric(FOR_PHASE(phase))
{
	return JSValue(std::u16string(1, value)).ToNumeric(__ES4_ONLY(phase));
}

JSBigInt JS::Class::Internal::Primitive<JSChar>::ToBigInt()
{
	JS::Throw(JS::New<InternalError> __LOC()); // char (ES4) and BigInt (ES11) never coexisted
}

JSString JS::Class::Internal::Primitive<JSChar>::ToString(FOR_PHASE(phase))
{
	return std::u16string(1, value);
}

JSObject JS::Class::Internal::Primitive<JSChar>::ToObject()
{
	JS::Throw(JS::New<InternalError> __LOC()); // not supported outside ES4
}

JSString JS::Class::Internal::Primitive<JSChar>::Typeof()
{
	return u"char";
}

JSString JS::Class::Internal::Primitive<JSChar>::ToStringTag()
{
	return u"char";
}

JSType JS::Class::Internal::Primitive<JSChar>::GetType()
{
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->char_class : nullptr;
}

bool JS::Class::Internal::Primitive<JSChar>::has_value(const std::type_info& type) const
{
	return type == typeid(JSChar);
}

const void * JS::Class::Internal::Primitive<JSChar>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSChar))
		return &value;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSChar>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSChar>::inspect_value(out, value, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSChar>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("char")
		->AppendText(int(value))
		->CloseXMLElement();
}
#endif

//// Primitive<JSMethodClosure>

JSValue JS::Class::Internal::Primitive<JSMethodClosure>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JSValue(value);
}

bool JS::Class::Internal::Primitive<JSMethodClosure>::SameValue(const JSValue& other) const
{
	if(!other.has_value<JSMethodClosure>())
	{
		return false;
	}
	auto other_method = other.get_if<JSMethodClosure>();
	return value.this_ == other_method->this_
		&& value.method == other_method->method;
}

JSValue JS::Class::Internal::Primitive<JSMethodClosure>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return ToString(VIA_PHASE(phase));
}

JSBoolean JS::Class::Internal::Primitive<JSMethodClosure>::ToBoolean()
{
	return true;
}

JSNumber JS::Class::Internal::Primitive<JSMethodClosure>::ToNumber(FOR_PHASE(phase))
{
	return ::JS::ToNumber(ToString(__ES4_ONLY(phase)) WITH_PHASE(phase));
}

JSBigInt JS::Class::Internal::Primitive<JSMethodClosure>::ToBigInt()
{
	JS::Throw(JS::New<InternalError> __LOC()); // MethodClosure (ES4) and BigInt (ES11) never coexisted
}

JSString JS::Class::Internal::Primitive<JSMethodClosure>::ToString(FOR_PHASE(phase))
{
	return ObjectToString(JSValue(value) WITH_PHASE(phase));
}

JSObject JS::Class::Internal::Primitive<JSMethodClosure>::ToObject()
{
	JS::Throw(JS::New<InternalError> __LOC()); // not supported outside ES4
}

JSString JS::Class::Internal::Primitive<JSMethodClosure>::Typeof()
{
	return u"function";
}

JSString JS::Class::Internal::Primitive<JSMethodClosure>::ToStringTag()
{
	return u"Function";
}

JSType JS::Class::Internal::Primitive<JSMethodClosure>::GetType()
{
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->Function_class : nullptr;
}

JSValue JS::Class::Internal::Primitive<JSMethodClosure>::Get(JSValue property_key, [[maybe_unused]] JSValue receiver, [[maybe_unused]] Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	if(property_key == u"length" && namespaces.find(JS_public) != namespaces.end())
	{
		return JSNumber(value.method->Length);
	}
	return JS_undefined;
}

JS::Invocability JS::Class::Internal::Primitive<JSMethodClosure>::GetInvocability()
{
	return Invocability::NonConstructor;
}

#if ENABLE_ES4
std::optional<JS::Runtime::SignatureType> JS::Class::Internal::Primitive<JSMethodClosure>::GetSignature()
{
	return std::make_optional<JS::Runtime::SignatureType>(value.method->Signature);
}
#endif

JSValue JS::Class::Internal::Primitive<JSMethodClosure>::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return value.method->Function.Call(value.this_, arguments WITH_PHASE(phase));
}

bool JS::Class::Internal::Primitive<JSMethodClosure>::Begin_Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments)
{
	return value.method->Function.Begin_Call(value.this_, arguments);
}

bool JS::Class::Internal::Primitive<JSMethodClosure>::has_value(const std::type_info& type) const
{
	return type == typeid(JSMethodClosure);
}

const void * JS::Class::Internal::Primitive<JSMethodClosure>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSMethodClosure))
		return &value;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSMethodClosure>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSMethodClosure>::inspect_value(out, value, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSMethodClosure>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("method-closure")
		->OpenXMLElement("this");
	value.this_.serialize(debugger);
		// TODO
	debugger
		->CloseXMLElement()
		->OpenXMLElement("method")
		->AddXMLAttribute("name", value.method->name);
		// TODO: add namespaces
	debugger
		->CloseXMLElement()
		->CloseXMLElement();
}
#endif

//// Primitive<JSCompoundAttribute>

JSValue JS::Class::Internal::Primitive<JSCompoundAttribute>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JSValue(value);
}

bool JS::Class::Internal::Primitive<JSCompoundAttribute>::SameValue(const JSValue& other) const
{
	if(!other.has_value<JSCompoundAttribute>())
	{
		return false;
	}
	auto other_method = other.get_if<JSCompoundAttribute>();
	return value.Namespaces == other_method->Namespaces
		&& value.Explicit == other_method->Explicit
		&& value.Enumerable == other_method->Enumerable
		&& value.Dynamic == other_method->Dynamic
		&& value.Category == other_method->Category
		&& value.Override == other_method->Override
		&& value.Prototype == other_method->Prototype
		&& value.Unused == other_method->Unused;
}

JSValue JS::Class::Internal::Primitive<JSCompoundAttribute>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return ToString(VIA_PHASE(phase));
}

JSBoolean JS::Class::Internal::Primitive<JSCompoundAttribute>::ToBoolean()
{
	return true;
}

JSNumber JS::Class::Internal::Primitive<JSCompoundAttribute>::ToNumber(FOR_PHASE(phase))
{
	return ::JS::ToNumber(ToString(VIA_PHASE(phase)) WITH_PHASE(phase));
}

JSBigInt JS::Class::Internal::Primitive<JSCompoundAttribute>::ToBigInt()
{
	JS::Throw(JS::New<InternalError> __LOC()); // CompoundAttribute (ES4) and BigInt (ES11) never coexisted
}

JSString JS::Class::Internal::Primitive<JSCompoundAttribute>::ToString(FOR_PHASE(phase))
{
	return ObjectToString(JSValue(value) WITH_PHASE(phase));
}

JSObject JS::Class::Internal::Primitive<JSCompoundAttribute>::ToObject()
{
	JS::Throw(JS::New<InternalError> __LOC()); // not supported outside ES4
}

JSString JS::Class::Internal::Primitive<JSCompoundAttribute>::Typeof()
{
	return u"object";
}

JSString JS::Class::Internal::Primitive<JSCompoundAttribute>::ToStringTag()
{
	return u"Attribute";
}

JSType JS::Class::Internal::Primitive<JSCompoundAttribute>::GetType()
{
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->Attribute_class : nullptr;
}

bool JS::Class::Internal::Primitive<JSCompoundAttribute>::has_value(const std::type_info& type) const
{
	return type == typeid(JSCompoundAttribute);
}

const void * JS::Class::Internal::Primitive<JSCompoundAttribute>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSCompoundAttribute))
		return &value;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSCompoundAttribute>::inspect(std::ostream& out, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	if(value.IsEmpty())
	{
		out << "\33[1mempty\33[m";
	}
	else if(!value.Enabled)
	{
		out << "\33[93mfalse\33[m";
	}
	else
	{
		bool started = false;
		for(auto namespace_ : value.Namespaces)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[92m";
			PrintString(out, namespace_->uri);
			out << "\33[m";
		}
		if(value.Explicit)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mexplicit\33[m";
		}
		if(value.Enumerable)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1menumerable\33[m";
		}
		if(value.Dynamic)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mdynamic\33[m";
		}

		switch(value.Category)
		{
		case JS::Runtime::MemberModifier::None:
			break;
		case JS::Runtime::MemberModifier::Static:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mstatic\33[m";
			break;
		case JS::Runtime::MemberModifier::Virtual:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mvirtual\33[m";
			break;
		case JS::Runtime::MemberModifier::Final:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mfinal\33[m";
			break;
		//case JS::Runtime::MemberModifier::Constructor:
		case JS::Runtime::MemberModifier::Abstract:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mabstract\33[m";
			break;
		}

		switch(value.Override)
		{
		case JS::Runtime::OverrideModifier::None:
			break;
		case JS::Runtime::OverrideModifier::True:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1moverride\33[m";
			break;
		case JS::Runtime::OverrideModifier::False:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1moverride\33[m(\33[93mfalse\33[m)";
			break;
		case JS::Runtime::OverrideModifier::Undefined:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1moverride\33[m(\33[37mundefined\33[m)";
			break;
		}
		if(value.Prototype)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1m[prototype\33[m";
		}
		if(value.Unused)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1munused\33[m";
		}
		if(value.Native)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mnative\33[m";
		}
	}
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSCompoundAttribute>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("compound-attribute")
		// TODO
		->CloseXMLElement();
}
#endif

//// Primitive<JSTypeApplication>

JSValue JS::Class::Internal::Primitive<JSTypeApplication>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return JSValue(value);
}

bool JS::Class::Internal::Primitive<JSTypeApplication>::SameValue(const JSValue& other) const
{
	if(!other.has_value<JSTypeApplication>())
	{
		return false;
	}
	auto other_method = other.get_if<JSTypeApplication>();
	return value.parametrized_type == other_method->parametrized_type
		&& value.parameters == other_method->parameters;
}

JSValue JS::Class::Internal::Primitive<JSTypeApplication>::ToPrimitive([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return ToString(__ES4_ONLY(phase));
}

JSBoolean JS::Class::Internal::Primitive<JSTypeApplication>::ToBoolean()
{
	return true;
}

JSNumber JS::Class::Internal::Primitive<JSTypeApplication>::ToNumber(FOR_PHASE(phase))
{
	return ::JS::ToNumber(ToString(__ES4_ONLY(phase)) WITH_PHASE(phase));
}

JSBigInt JS::Class::Internal::Primitive<JSTypeApplication>::ToBigInt()
{
	JS::Throw(JS::New<InternalError> __LOC()); // generic types (ES4) and BigInt (ES11) never coexisted
}

JSString JS::Class::Internal::Primitive<JSTypeApplication>::ToString(FOR_PHASE(phase))
{
	// ActionScript behavior for Vector.<T>
	return u"[class " + GetTypeNameString(value) + u"]";
}

JSObject JS::Class::Internal::Primitive<JSTypeApplication>::ToObject()
{
	JS::Throw(JS::New<InternalError> __LOC()); // not supported outside ES4
}

JSString JS::Class::Internal::Primitive<JSTypeApplication>::Typeof()
{
	// ActionScript returns "object" for all classes
	return u"function";
}

JSString JS::Class::Internal::Primitive<JSTypeApplication>::ToStringTag()
{
	return GetTypeNameString(value);
}

JSType JS::Class::Internal::Primitive<JSTypeApplication>::GetType()
{
	return IsVersionES4() && JS::Intrinsics() != nullptr ? JS::Intrinsics()->Class_class : nullptr;
}

bool JS::Class::Internal::Primitive<JSTypeApplication>::has_value(const std::type_info& type) const
{
	return type == typeid(JSTypeApplication);
}

const void * JS::Class::Internal::Primitive<JSTypeApplication>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSTypeApplication))
		return &value;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSTypeApplication>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSTypeApplication>::inspect_value(out, value, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSTypeApplication>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("type-application")
		// TODO
		->CloseXMLElement();
}
#endif
#endif

//// IntermediateValue

bool JS::Class::Internal::IntermediateValue::SameValue([[maybe_unused]] const JSValue& other) const
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JSValue JS::Class::Internal::IntermediateValue::ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	return GetValue().ToPrimitive(hint WITH_PHASE(phase));
}

JSBoolean JS::Class::Internal::IntermediateValue::ToBoolean()
{
	return GetValue().ToBoolean();
}

JSNumber JS::Class::Internal::IntermediateValue::ToNumber(FOR_PHASE(phase))
{
	return GetValue().ToNumber(__ES4_ONLY(phase));
}

JSValue JS::Class::Internal::IntermediateValue::ToNumeric(FOR_PHASE(phase))
{
	return GetValue().ToNumeric(__ES4_ONLY(phase));
}

JSBigInt JS::Class::Internal::IntermediateValue::ToBigInt()
{
	return GetValue().ToNumber(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run));
}

JSString JS::Class::Internal::IntermediateValue::ToString(FOR_PHASE(phase))
{
	return GetValue().ToString(__ES4_ONLY(phase));
}

JSObject JS::Class::Internal::IntermediateValue::ToObject()
{
	return GetValue().ToObject();
}

JSString JS::Class::Internal::IntermediateValue::Typeof()
{
	return GetValue().Typeof();
}

JSString JS::Class::Internal::IntermediateValue::ToStringTag()
{
	return GetValue().ToStringTag();
}

#if ENABLE_ES4
JSType JS::Class::Internal::IntermediateValue::GetType()
{
	JS::Throw(JS::New<InternalError> __LOC());
}
#endif

JSString JS::Class::Internal::IntermediateValue::ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel)
{
	return GetValue().ToXMLString(AncestorNamespaces, IndentLevel);
}

JSXML JS::Class::Internal::IntermediateValue::ToXML()
{
	return GetValue().ToXML();
}

JSXMLList JS::Class::Internal::IntermediateValue::ToXMLList()
{
	return GetValue().ToXMLList();
}

JSAttributeName JS::Class::Internal::IntermediateValue::ToAttributeName()
{
	return GetValue().ToAttributeName();
}

JSValue JS::Class::Internal::IntermediateValue::ToXMLName()
{
	return GetValue().ToXMLName();
}

void JS::Class::Internal::IntermediateValue::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	// TODO
	GetValue().inspect(out, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::IntermediateValue::serialize([[maybe_unused]] JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	JS::Throw(JS::New<InternalError> __LOC());
}
#endif

//// Primitive<JSAttributeName>

JSValue JS::Class::Internal::Primitive<JSAttributeName>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC(u"expression evaluates to attribute name"));
}

JSString JS::Class::Internal::Primitive<JSAttributeName>::ToString(FOR_PHASE(phase))
{
	return value.Name.ToString();
}

JSAttributeName JS::Class::Internal::Primitive<JSAttributeName>::ToAttributeName()
{
	return value;
}

JSValue JS::Class::Internal::Primitive<JSAttributeName>::ToXMLName()
{
	return JSValue(value);
}

bool JS::Class::Internal::Primitive<JSAttributeName>::has_value(const std::type_info& type) const
{
	return type == typeid(JSAttributeName);
}

const void * JS::Class::Internal::Primitive<JSAttributeName>::get_if(const std::type_info& type) const
{
	if(type == typeid(JSAttributeName))
		return &value;
	else
		return nullptr;
}

void JS::Class::Internal::Primitive<JSAttributeName>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSAttributeName>::inspect_value(out, value, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JSAttributeName>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("attribute-name")
		->AppendText(value.Name.ToString())
		->CloseXMLElement();
}
#endif

#if ENABLE_ES4
//// DeclaredType

JS::Type::MetaType::DeclaredType::DeclaredType(JSClass class_)
	: content(new DeclaredTypeContent<JSClass>(class_))
{
}

JS::Type::MetaType::DeclaredType::DeclaredType(JSClass class_, std::vector<JSDeclaredType> parameter_types)
	: content(new DeclaredTypeContent<JSClass>(class_, parameter_types))
{
}

JS::Type::MetaType::DeclaredType::DeclaredType(JSInterface interface)
	: content(new DeclaredTypeContent<JSInterface>(interface))
{
}

JS::Type::MetaType::DeclaredType::DeclaredType(JSInterface interface, std::vector<JSDeclaredType> parameter_types)
	: content(new DeclaredTypeContent<JSInterface>(interface, parameter_types))
{
}

JS::Type::MetaType::DeclaredType::DeclaredType(JSTypeApplication type_application)
	: content(new DeclaredTypeContent<JSClass>(type_application.parametrized_type, type_application.parameters))
{
}

JS::Type::MetaType::DeclaredType::DeclaredType(JSType type)
	: content(nullptr)
{
	std::visit([this](auto&& type)
	{
		using T = std::decay_t<decltype(type)>;
		if constexpr(std::is_same_v<T, JSClass>)
		{
			content = new DeclaredTypeContent<JSClass>(type);
		}
		else if constexpr(std::is_same_v<T, JSTypeApplication>)
		{
			content = new DeclaredTypeContent<JSClass>(type.parametrized_type, type.parameters);
		}
		else
		{
			static_assert(false);
		}
	}, type);
}

JS::Type::MetaType::DeclaredType::DeclaredType(Array&& array_type)
	: content(new DeclaredTypeContent<Array>(array_type))
{
}

JS::Type::MetaType::DeclaredType::DeclaredType(Union&& union_type)
	: content(new DeclaredTypeContent<Union>(union_type))
{
}

JS::Type::MetaType::DeclaredType::DeclaredType(Record&& record_type)
	: content(new DeclaredTypeContent<Record>(record_type))
{
}

JS::Type::MetaType::DeclaredType::DeclaredType(Function&& function_type)
	: content(new DeclaredTypeContent<Function>(function_type))
{
}

JS::Type::MetaType::DeclaredType::DeclaredType(JSString type_parameter)
	: content(new DeclaredTypeContent<Function::TypeParameter>(type_parameter))
{
}

//// DeclaredTypeContent<JSClass>

JS::Type::MetaType::DeclaredTypeContent<JSClass>::DeclaredTypeContent(JSClass class_, std::vector<JSType> concrete_parameter_types)
	: class_(class_), parameter_types(std::vector<JSDeclaredType>())
{
	for(auto parameter : concrete_parameter_types)
	{
		parameter_types.value().push_back(parameter);
	}
}

JS::Type::MetaType::DeclaredTypeContentBase * JS::Type::MetaType::DeclaredTypeContent<JSClass>::clone() const
{
	return new DeclaredTypeContent(class_);
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JSClass>::Coerce(JSValue value, bool strict)
{
	return class_->Coerce(value, strict);
}

bool JS::Type::MetaType::DeclaredTypeContent<JSClass>::Is(JSValue value)
{
	return class_->Is(value);
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JSClass>::DefaultValue()
{
	return class_->GetDefaultValue();
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JSClass>::ApplyParameters(std::vector<JSDeclaredType> types, [[maybe_unused]] /* TODO */ Nullability nullable)
{
	if(parameter_types.has_value())
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	if(types.size() > class_->type_parameters.size())
	{
		JS::Throw(JS::New<TypeError> __LOC()); // TODO: what would be a correct instance?
	}

	while(types.size() < class_->type_parameters.size())
	{
		types.push_back(Intrinsics()->Object_class); // TODO: faster way?
	}

	return JSDeclaredType(class_, types);
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JSClass>::ReplaceParameters([[maybe_unused]] std::map<JSString, JSDeclaredType> types, [[maybe_unused]] /* TODO */ Nullability nullable)
{
	return JSDeclaredType(class_).WithNullability(nullable);
}

bool JS::Type::MetaType::DeclaredTypeContent<JSClass>::equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const
{
	if(auto other_ = dynamic_cast<DeclaredTypeContent *>(other))
	{
		if(class_ != other_->class_ || parameter_types.has_value() != other_->parameter_types.has_value())
		{
			return false;
		}

		if(parameter_types.has_value())
		{
			if(parameter_types.value().size() != other_->parameter_types.value().size())
			for(size_t index = 0; index < parameter_types.value().size(); index++)
			{
				if(!parameter_types.value()[index].equal(other_->parameter_types.value()[index], parameter_replacements))
				{
					return false;
				}
			}
		}

		return true;
	}
	else
	{
		return false;
	}
}

void JS::Type::MetaType::DeclaredTypeContent<JSClass>::serialize(JS::Runtime::Debugger * debugger)
{
	if(parameter_types.has_value())
	{
		debugger->OpenXMLElement("parametrized-type");
		for(auto parameter_type : parameter_types.value())
		{
			debugger->OpenXMLElement("parameter");
			parameter_type.serialize(debugger);
			debugger->CloseXMLElement();
		}
		JSValue(class_).serialize(debugger);;
		debugger->CloseXMLElement();
	}
	else
	{
		JSValue(class_).serialize(debugger);;
	}
}

//// DeclaredTypeContent<JSInterface>

JS::Type::MetaType::DeclaredTypeContent<JSInterface>::DeclaredTypeContent(JSInterface interface, std::vector<JSType> concrete_parameter_types)
	: interface(interface)
{
	for(auto parameter : concrete_parameter_types)
	{
		parameter_types.value().push_back(parameter);
	}
}

JS::Type::MetaType::DeclaredTypeContentBase * JS::Type::MetaType::DeclaredTypeContent<JSInterface>::clone() const
{
	return new DeclaredTypeContent(interface);
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JSInterface>::Coerce(JSValue value, [[maybe_unused]] bool strict)
{
	// TODO: check if value's class conforms to the interface, is this nominal or structural conformance?
	return value;
}

bool JS::Type::MetaType::DeclaredTypeContent<JSInterface>::Is([[maybe_unused]] JSValue value)
{
	// TODO
	return true;
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JSInterface>::DefaultValue()
{
	return JS_null;
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JSInterface>::ApplyParameters(std::vector<JSDeclaredType> types, [[maybe_unused]] /* TODO */ Nullability nullable)
{
	if(parameter_types.has_value())
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	if(types.size() > interface->type_parameters.size())
	{
		JS::Throw(JS::New<TypeError> __LOC()); // TODO: what would be a correct instance?
	}

	while(types.size() < interface->type_parameters.size())
	{
		types.push_back(Intrinsics()->Object_class); // TODO: faster way?
	}

	return JSDeclaredType(interface, types);
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JSInterface>::ReplaceParameters([[maybe_unused]] std::map<JSString, JSDeclaredType> types, [[maybe_unused]] /* TODO */ Nullability nullable)
{
	return JSDeclaredType(interface).WithNullability(nullable);
}

bool JS::Type::MetaType::DeclaredTypeContent<JSInterface>::equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const
{
	if(auto other_ = dynamic_cast<DeclaredTypeContent *>(other))
	{
		if(interface != other_->interface || parameter_types.has_value() != other_->parameter_types.has_value())
		{
			return false;
		}

		if(parameter_types.has_value())
		{
			if(parameter_types.value().size() != other_->parameter_types.value().size())
			for(size_t index = 0; index < parameter_types.value().size(); index++)
			{
				if(!parameter_types.value()[index].equal(other_->parameter_types.value()[index], parameter_replacements))
				{
					return false;
				}
			}
		}

		return true;
	}
	else
	{
		return false;
	}
}

void JS::Type::MetaType::DeclaredTypeContent<JSInterface>::serialize(JS::Runtime::Debugger * debugger)
{
	if(parameter_types.has_value())
	{
		debugger->OpenXMLElement("parametrized-type");
		for(auto parameter_type : parameter_types.value())
		{
			debugger->OpenXMLElement("parameter");
			parameter_type.serialize(debugger);
			debugger->CloseXMLElement();
		}
		JSValue(interface).serialize(debugger);;
		debugger->CloseXMLElement();
	}
	else
	{
		JSValue(interface).serialize(debugger);;
	}
}

//// DeclaredTypeContent<Array>

JS::Type::MetaType::DeclaredTypeContentBase * JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Array>::clone() const
{
	return new DeclaredTypeContent(*this);
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Array>::Coerce(JSValue value, bool strict)
{
	if(IsNull(value))
	{
		return value;
	}

	auto value_opt = JS::Intrinsics()->Array_class->Coerce(value, strict);
	if(!value_opt.has_value())
	{
		return {};
	}
	value = value_opt.value();
	// check if element types are correct
	for(size_t index = 0; index < fixed_types.size(); index ++)
	{
		auto element = Get(value, ToString(JSNumber(index) RUNTIME_PHASE/*TODO*/));
		auto coerced_element = fixed_types[index].Coerce(element, strict);
		if(!coerced_element.has_value() || element != coerced_element.value())
		{
			return {};
		}
	}
	size_t length = ToNumber(Get(value, u"length") WITH_PHASE(JS::Runtime::ExecutionPhase::Compile));
	if(rest_type.has_value())
	{
		for(size_t index = fixed_types.size(); index < length; index ++)
		{
			auto element = Get(value, ToString(JSNumber(index) RUNTIME_PHASE/*TODO*/));
			auto coerced_element = rest_type.value().Coerce(element, strict);
			if(!coerced_element.has_value() || element != coerced_element.value())
			{
				return {};
			}
		}
	}
	else if(length != fixed_types.size())
	{
		return {};
	}
	return value;
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Array>::Is([[maybe_unused]] /* TODO */ JSValue value)
{
	// TODO
	return true;
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Array>::DefaultValue()
{
	//auto array = JS::New<JS::Class::Array>();
	// TODO
	//return array;
	return JS_null;
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Array>::ApplyParameters([[maybe_unused]] std::vector<JSDeclaredType> types, [[maybe_unused]] Nullability nullable)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Array>::ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable)
{
	std::vector<JSDeclaredType> new_fixed_types;
	std::optional<JSDeclaredType> new_rest_type;

	for(auto type : fixed_types)
	{
		new_fixed_types.push_back(type.ReplaceParameters(types));
	}

	if(rest_type.has_value())
	{
		new_rest_type = rest_type.value().ReplaceParameters(types);
	}

	return JSDeclaredType(Array(new_fixed_types, new_rest_type)).WithNullability(nullable);
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Array>::equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const
{
	if(auto other_ = dynamic_cast<DeclaredTypeContent *>(other))
	{
		if(fixed_types.size() != other_->fixed_types.size())
		{
			return false;
		}

		for(size_t index = 0; index < fixed_types.size(); index++)
		{
			if(!fixed_types[index].equal(other_->fixed_types[index], parameter_replacements))
			{
				return false;
			}
		}

		if(rest_type.has_value() != other_->rest_type.has_value())
		{
			return false;
		}

		if(rest_type.has_value())
		{
			if(!rest_type.value().equal(other_->rest_type.value(), parameter_replacements))
			{
				return false;
			}
		}

		return true;
	}
	else
	{
		return false;
	}
}

void JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Array>::serialize([[maybe_unused]] /* TODO */ JS::Runtime::Debugger * debugger)
{
	// TODO
}

//// DeclaredTypeContent<Union>

JS::Type::MetaType::DeclaredTypeContentBase * JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Union>::clone() const
{
	return new DeclaredTypeContent(*this);
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Union>::Coerce(JSValue value, bool strict)
{
	for(auto type : types)
	{
		auto value_opt = type.Coerce(value, false);
		if(value_opt.has_value())
		{
			return value_opt.value();
		}
	}
	if(strict)
	{
		// check for any range errors
		for(auto type : types)
		{
			type.Coerce(value, true);
		}
	}
	return {};
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Union>::Is(JSValue value)
{
	for(auto type : types)
	{
		if(type.Is(value))
		{
			return true;
		}
	}
	return false;
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Union>::DefaultValue()
{
	for(auto type : types)
	{
		auto value_opt = type.DefaultValue();
		if(value_opt.has_value())
		{
			return value_opt.value();
		}
	}
	return {};
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Union>::ApplyParameters([[maybe_unused]] std::vector<JSDeclaredType> types, [[maybe_unused]] Nullability nullable)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Union>::ReplaceParameters(std::map<JSString, JSDeclaredType> replacement_types, Nullability nullable)
{
	std::vector<JSDeclaredType> new_types;

	for(auto type : types)
	{
		new_types.push_back(type.ReplaceParameters(replacement_types));
	}

	return JSDeclaredType(Union(new_types)).WithNullability(nullable);
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Union>::equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const
{
	if(auto other_ = dynamic_cast<DeclaredTypeContent *>(other))
	{
		if(types.size() != other_->types.size())
		{
			return false;
		}

		for(size_t index = 0; index < types.size(); index ++)
		{
			if(!types[index].equal(other_->types[index], parameter_replacements))
			{
				return false;
			}
		}

		return true;
	}
	else
	{
		return false;
	}
}

void JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Union>::serialize([[maybe_unused]] /* TODO */ JS::Runtime::Debugger * debugger)
{
	// TODO
}

//// DeclaredTypeContent<Record>

JS::Type::MetaType::DeclaredTypeContentBase * JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Record>::clone() const
{
	return new DeclaredTypeContent(*this);
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Record>::Coerce(JSValue value, bool strict)
{
	for(auto field : fields)
	{
		auto element = Get(value, std::get<0>(field));
		auto coerced_element = std::get<1>(field).Coerce(element, strict);
		if(!coerced_element.has_value() || element != coerced_element.value())
		{
			return {};
		}
	}
	return value;
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Record>::Is([[maybe_unused]] JSValue value)
{
	// TODO
	return true;
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Record>::DefaultValue()
{
	// TODO: create object and set each field to the default value
	//return {};
	return JS_null;
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Record>::ApplyParameters([[maybe_unused]] std::vector<JSDeclaredType> types, [[maybe_unused]] Nullability nullable)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Record>::ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable)
{
	std::vector<std::tuple<JSQualifiedName, JSDeclaredType>> new_fields;

	for(auto field : fields)
	{
		new_fields.push_back(std::make_tuple(std::get<0>(field), std::get<1>(field).ReplaceParameters(types)));
	}

	return JSDeclaredType(Record(new_fields)).WithNullability(nullable);
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Record>::equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const
{
	if(auto other_ = dynamic_cast<DeclaredTypeContent *>(other))
	{
		if(fields.size() != other_->fields.size())
		{
			return false;
		}

		for(size_t index = 0; index < fields.size(); index++)
		{
			if(std::get<0>(fields[index]) != std::get<0>(other_->fields[index]))
			{
				return false;
			}
			if(!std::get<1>(fields[index]).equal(std::get<1>(other_->fields[index]), parameter_replacements))
			{
				return false;
			}
		}

		return true;
	}
	else
	{
		return false;
	}
}

void JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Record>::serialize([[maybe_unused]] /* TODO */ JS::Runtime::Debugger * debugger)
{
	// TODO
}

//// DeclaredTypeContent<Function>

JS::Type::MetaType::DeclaredTypeContentBase * JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function>::clone() const
{
	return new DeclaredTypeContent(*this);
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function>::Coerce(JSValue value, [[maybe_unused]] bool strict)
{
	// TODO: type parameters
	std::optional<JSFunction> function = GetFunction(value);
	if(!function.has_value())
	{
		return {};
	}

	std::optional<JS::Runtime::SignatureType> Signature = GetSignature(function.value());
	if(!Signature.has_value())
	{
		return {};
	}

	if(Signature.value().has_rest_parameter != rest_parameter)
	{
		return {};
	}

	if(Signature.value().return_type != result)
	{
		return {};
	}

	if(Signature.value().parameters.size() < parameters.size() + optional_parameters.size())
	{
		return {};
	}

	size_t index;
	for(index = 0; index < parameters.size(); index ++)
	{
		auto parameter = Signature.value().parameters[index].type;
		if(parameter != parameters[index])
		{
			return {};
		}
	}
	for(size_t optional_parameter_index = 0; optional_parameter_index < optional_parameters.size(); index ++, optional_parameter_index ++)
	{
		auto parameter = Signature.value().parameters[index].type;
		if(parameter != optional_parameters[optional_parameter_index])
		{
			return {};
		}
		if(!Signature.value().parameters[index].default_value.has_value())
		{
			return {};
		}
	}

	return value;
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function>::Is([[maybe_unused]] /* TODO */ JSValue value)
{
	// TODO
	return true;
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function>::DefaultValue()
{
	// TODO
	return JS_null;
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function>::ApplyParameters(std::vector<JSDeclaredType> types, Nullability nullable)
{
	if(types.size() > type_parameters.size())
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}

	std::map<JSString, JSDeclaredType> replacement_types;

	for(size_t index = 0; index < type_parameters.size(); index ++)
	{
		if(index < types.size())
		{
			replacement_types.emplace(type_parameters[index], types[index]);
		}
		else
		{
			replacement_types.emplace(type_parameters[index], Intrinsics()->Object_class);
		}
	}

	std::vector<JSDeclaredType> new_parameters;
	std::vector<JSDeclaredType> new_optional_parameters;

	for(auto parameter : parameters)
	{
		new_parameters.push_back(parameter.ReplaceParameters(replacement_types));
	}

	for(auto parameter : optional_parameters)
	{
		new_optional_parameters.push_back(parameter.ReplaceParameters(replacement_types));
	}

	return JSDeclaredType(Function(std::vector<JSString>{}, new_parameters, new_optional_parameters, result.ReplaceParameters(replacement_types), rest_parameter)).WithNullability(nullable);
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function>::ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable)
{
	std::vector<JSDeclaredType> new_parameters;
	std::vector<JSDeclaredType> new_optional_parameters;

	for(auto type_parameter : type_parameters)
	{
		types.erase(type_parameter); // hide and protect name
	}

	for(auto parameter : parameters)
	{
		new_parameters.push_back(parameter.ReplaceParameters(types));
	}

	for(auto parameter : optional_parameters)
	{
		new_optional_parameters.push_back(parameter.ReplaceParameters(types));
	}

	return JSDeclaredType(Function(type_parameters, new_parameters, new_optional_parameters, result.ReplaceParameters(types), rest_parameter)).WithNullability(nullable);
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function>::equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const
{
	if(auto other_ = dynamic_cast<DeclaredTypeContent *>(other))
	{
		if(rest_parameter != other_->rest_parameter)
		{
			return false;
		}

		if(type_parameters.size() != other_->type_parameters.size())
		{
			return false;
		}

		std::map<JSString, JSString> new_parameter_replacements = parameter_replacements;

		for(size_t index = 0; index < type_parameters.size(); index++)
		{
			new_parameter_replacements[type_parameters[index]] = other_->type_parameters[index];
		}

		if(parameters.size() != other_->parameters.size())
		{
			return false;
		}

		for(size_t index = 0; index < parameters.size(); index++)
		{
			if(!parameters[index].equal(other_->parameters[index], new_parameter_replacements))
			{
				return false;
			}
		}

		if(optional_parameters.size() != other_->optional_parameters.size())
		{
			return false;
		}

		for(size_t index = 0; index < optional_parameters.size(); index++)
		{
			if(!optional_parameters[index].equal(other_->optional_parameters[index], new_parameter_replacements))
			{
				return false;
			}
		}

		if(!result.equal(other_->result, new_parameter_replacements))
		{
			return false;
		}

		return true;
	}
	else
	{
		return false;
	}
}

void JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function>::serialize([[maybe_unused]] /* TODO */ JS::Runtime::Debugger * debugger)
{
	// TODO
}

//// DeclaredTypeContent<Function::TypeParameter>

JS::Type::MetaType::DeclaredTypeContentBase * JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function::TypeParameter>::clone() const
{
	return new DeclaredTypeContent(*this);
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function::TypeParameter>::Coerce([[maybe_unused]] /* TODO */ JSValue value, [[maybe_unused]] /* TODO*/ bool strict)
{
	// TODO: check for Function::TypeParameter types
	return {};
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function::TypeParameter>::Is([[maybe_unused]] /* TODO */ JSValue value)
{
	// TODO
	return true;
}

std::optional<JSValue> JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function::TypeParameter>::DefaultValue()
{
	return {};
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function::TypeParameter>::ApplyParameters([[maybe_unused]] std::vector<JSDeclaredType> types, [[maybe_unused]] Nullability nullable)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JSDeclaredType JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function::TypeParameter>::ReplaceParameters(std::map<JSString, JSDeclaredType> types, [[maybe_unused]] Nullability nullable)
{
	auto types_iter = types.find(name);
	if(types_iter != types.end())
	{
		return types_iter->second;
	}
	else
	{
		return JSDeclaredType(name);
	}
}

bool JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function::TypeParameter>::equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const
{
	if(auto other_ = dynamic_cast<DeclaredTypeContent *>(other))
	{
		auto parameter_replacement = parameter_replacements.find(name);
		if(parameter_replacement != parameter_replacements.end())
		{
			return parameter_replacement->second == other_->name;
		}
		else
		{
			return name == other_->name;
		}
	}
	else
	{
		return false;
	}
}

void JS::Type::MetaType::DeclaredTypeContent<JS::Type::MetaType::Function::TypeParameter>::serialize([[maybe_unused]] /* TODO */ JS::Runtime::Debugger * debugger)
{
	// TODO
}
#endif

//// Value

JS::Class::Internal::Value * JS::Type::Value::UnwrapValue(const Value& v)
{
	JS::Class::Internal::Value * u = v.get_content();
	if(auto w = dynamic_cast<JS::Class::Internal::PrimitiveValue *>(u))
	{
		JS::Class::Internal::PrimitiveValue::to_keep_alive.insert(w);
	}
	return u;
}

JS::Type::Value JS::Type::Value::WrapValue(JS::Class::Internal::Value * v)
{
	if(auto u = dynamic_cast<JS::Class::Internal::PrimitiveValue *>(v))
	{
		JS::Class::Internal::PrimitiveValue::to_keep_alive.erase(u);
		return Value(u);
	}
	else if(auto o = dynamic_cast<JS::Class::Object *>(v))
	{
		return Value(o);
	}
	else
	{
		assert(false);
	}
}

void JS::Type::Value::ReleaseValue(const Value& v)
{
	WrapValue(UnwrapValue(v));
}

void JS::Type::Value::ReleaseValue(JS::Class::Internal::Value * v)
{
	WrapValue(v);
}

JS::Type::Value::Value([[maybe_unused]] JS::Class::Internal::Value * value) { assert(false); }

JS::Type::Value::Value() : content(std::make_shared<JS::Class::Internal::Primitive<JS::Type::Internal::Undefined>>()) { }
JS::Type::Value::Value(nullptr_t) : content(std::make_shared<JS::Class::Internal::Primitive<JS::Type::Internal::Null>>()) { }
JS::Type::Value::Value(JSBoolean value) : content(std::make_shared<JS::Class::Internal::Primitive<JSBoolean>>(value)) { }
JS::Type::Value::Value(JSNumber value) : content(std::make_shared<JS::Class::Internal::Primitive<JSNumber>>(value)) { }
JS::Type::Value::Value(JSString value) : content(std::make_shared<JS::Class::Internal::Primitive<JSString>>(value)) { }
JS::Type::Value::Value(const char16_t * value) : content(std::make_shared<JS::Class::Internal::Primitive<JSString>>(value)) { }
JS::Type::Value::Value(JS::Class::Object * value)
{
	if(value == nullptr)
	{
		content = std::make_shared<JS::Class::Internal::Primitive<JS::Type::Internal::Null>>();
	}
	else
	{
		content = gcn::gc_ptr<JS::Class::Object>(value);
	}
}
#if ENABLE_ES4
JS::Type::Value::Value(JSLong value) : content(std::make_shared<JS::Class::Internal::Primitive<JSLong>>(value)) { }
JS::Type::Value::Value(JSUlong value) : content(std::make_shared<JS::Class::Internal::Primitive<JSUlong>>(value)) { }
JS::Type::Value::Value(JSFloat value) : content(std::make_shared<JS::Class::Internal::Primitive<JSFloat>>(value)) { }
#if ENABLE_DEC128
JS::Type::Value::Value(JSDecimal value) : content(std::make_shared<JS::Class::Internal::Primitive<JSDecimal>>(value)) { }
#endif
JS::Type::Value::Value(JSChar value) : content(std::make_shared<JS::Class::Internal::Primitive<JSChar>>(value)) { }
//JS::Type::Value::Value(JSMethodClosure value);
//JS::Type::Value::Value(JSCompoundAttribute value);
//JS::Type::Value::Value(JSTypeApplication value);
//JS::Type::Value::Value(JSType type);
#endif
JS::Type::Value::Value(JSSymbol value) : content(std::make_shared<JS::Class::Internal::Primitive<JSSymbol>>(value)) { }
JS::Type::Value::Value(JSBigInt value) : content(std::make_shared<JS::Class::Internal::Primitive<JSBigInt>>(value)) { }
//JS::Type::Value::Value(const Value& other) : content(other.content->clone()) { }
//JS::Type::Value::Value(Value&& other) : content(other.content) { other.content = nullptr; }

JS::Type::Value::Value(JSPropertyKey value)
{
	std::visit([this](auto&& value)
	{
		content = std::make_shared<JS::Class::Internal::Primitive<std::decay_t<decltype(value)>>>(value);
	}, value);
}

JS::Type::Value::Value(JS::Type::AttributeName name) : content(std::make_shared<JS::Class::Internal::Primitive<JS::Type::AttributeName>>(name)) { }

JS::Type::Value::Value(JS::Runtime::LexicalReference reference)
	 : content(std::make_shared<JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>>(reference))
{
}

JS::Type::Value::Value(JS::Runtime::MemberReference reference)
	 : content(std::make_shared<JS::Class::Internal::Primitive<JS::Runtime::MemberReference>>(reference))
{
}

#if ENABLE_ES4
JS::Type::Value::Value(JS::Runtime::BracketReference reference)
	 : content(std::make_shared<JS::Class::Internal::Primitive<JS::Runtime::BracketReference>>(reference))
{
}
#endif

JS::Type::Value::Value(JS::Runtime::SuperReference reference)
	 : content(std::make_shared<JS::Class::Internal::Primitive<JS::Runtime::SuperReference>>(reference))
{
}

JS::Type::Value::Value(JS::Runtime::PrivateReference reference)
	 : content(std::make_shared<JS::Class::Internal::Primitive<JS::Runtime::PrivateReference>>(reference))
{
}

#if ENABLE_ES4
// TODO: move to value.cc
JS::Type::Value::Value(JSMethodClosure value) : content(std::make_shared<JS::Class::Internal::Primitive<JSMethodClosure>>(value))
{
}

JS::Type::Value::Value(JSCompoundAttribute value) : content(std::make_shared<JS::Class::Internal::Primitive<JSCompoundAttribute>>(value))
{
}

JS::Type::Value::Value(JSTypeApplication value) : content(std::make_shared<JS::Class::Internal::Primitive<JSTypeApplication>>(value))
{
}

JS::Type::Value::Value(JSType type)
{
	std::visit([this](auto&& type)
	{
		using T = std::decay_t<decltype(type)>;
		if constexpr(std::is_same_v<T, JSClass>)
		{
			content = type;
		}
		else if constexpr(std::is_same_v<T, JSTypeApplication>)
		{
			content = std::make_shared<JS::Class::Internal::Primitive<JSTypeApplication>>(type);
		}
		else
		{
			static_assert(false);
		}
	}, type);
}
#endif

JSValue JS::Type::Value::CreateReference(JSIdentifierReference identifier_reference, JS::Runtime::ReferenceContext cxt)
{
	return get_content()->CreateReference(identifier_reference, cxt);
}

JSValue JS::Type::Value::CreateBracketReference(std::vector<JSValue> arguments, JS::Runtime::ReferenceContext cxt)
{
	return get_content()->CreateBracketReference(arguments, cxt);
}

JSValue JS::Type::Value::ResolvePropertyKey(JSValue property_key)
{
	return get_content()->ResolvePropertyKey(property_key);
}

JSValue JS::Type::Value::ResolveMethod(JS::Runtime::MemberReference& reference)
{
	return get_content()->ResolveMethod(reference);
}

#if ENABLE_ES4
JSValue JS::Type::Value::ResolveMethod(JS::Runtime::BracketReference& reference)
{
	return get_content()->ResolveMethod(reference);
}
#endif

JSNumber JS::Type::Value::ToInteger(FOR_PHASE(phase))
{
	auto value = ToNumber(__ES4_ONLY(phase));
	switch(std::fpclassify(value))
	{
	case FP_NAN:
		return 0.0;
	case FP_ZERO:
	case FP_INFINITE:
		return value;
	default:
		return trunc(value);
	}
}

int32_t JS::Type::Value::ToInt32(FOR_PHASE(phase))
{
	auto value = ToNumber(__ES4_ONLY(phase));
	switch(std::fpclassify(value))
	{
	case FP_NAN:
	case FP_ZERO:
	case FP_INFINITE:
		return 0;
	default:
		return (int32_t)trunc(value);
	}
}

uint32_t JS::Type::Value::ToUint32(FOR_PHASE(phase))
{
	return (uint32_t)ToInteger(__ES4_ONLY(phase));
}

#if ENABLE_ES4
int64_t JS::Type::Value::ToInt64(FOR_PHASE(phase))
{
	auto value = ToNumber(__ES4_ONLY(phase));
	switch(std::fpclassify(value))
	{
	case FP_NAN:
	case FP_ZERO:
	case FP_INFINITE:
		return 0;
	default:
		return (int64_t)trunc(value);
	}
}

uint64_t JS::Type::Value::ToUint64(FOR_PHASE(phase))
{
	return (uint64_t)ToInteger(__ES4_ONLY(phase));
}
#endif

uint8_t JS::Type::Value::ToUint8Clamp()
{
	auto value = ToNumber(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run));
	switch(std::fpclassify(value))
	{
	case FP_NAN:
	case FP_ZERO:
		return 0;
	case FP_INFINITE:
		return std::signbit(value) ? 0 : 255;
	}

	fenv_t env;
	fegetenv(&env);
	fesetround(FE_TONEAREST);
	double intval = nearbyint(value);
	fesetenv(&env);
	if(intval < 0)
	{
		return 0;
	}
	else if(intval > 255)
	{
		return 255;
	}
	else
	{
		return (uint8_t)intval;
	}
}

#if ENABLE_ES4
JSValue JS::Type::Value::Coerce(JSDeclaredType type)
{
	auto result = type.Coerce(*this);
	if(result.has_value())
		return result.value();
	else
		JS::Throw(JS::New<TypeError> __LOC());
}

JSType JS::Type::Value::GetType()
{
	return get_content()->GetType();
}

JSClass JS::Type::Value::GetClass()
{
	return GetClassForType(GetType());
}
#endif

bool JS::Type::Value::Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	return get_content()->Define(property_key, property WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
}

#if ENABLE_ES4
JSBigInt JS::Type::Value::ES4NumberToBigInt(JSValue arg)
{
	if(auto longval = GetLong(arg))
	{
		return JSBigInt(*longval);
	}
	else if(auto ulongval = GetUlong(arg))
	{
		return JSBigInt(*ulongval);
	}
	else
	{
		return JSBigInt(JS::ToNumber(arg RUNTIME_PHASE));
	}
}

std::optional<JSValue> JS::Type::Value::ES4BigIntToLong(JSBigInt arg, bool ulong)
{
	if(INT64_MAX < arg && arg <= UINT64_MAX)
	{
		return JSValue(JSUlong(arg.get_ui()));
	}
	else if(INT64_MIN <= arg && arg < 0)
	{
		return JSValue(JSLong(-arg.get_ui()));
	}
	else if(0 <= arg && arg <= INT64_MAX)
	{
		if(ulong)
		{
			return JSValue(JSUlong(arg.get_ui()));
		}
		else
		{
			return JSValue(JSLong(arg.get_ui()));
		}
	}
	return {};
}
#endif

JSValue JS::Type::Value::unaryMinus(JSValue x AND_PHASE(phase))
{
	if(auto number = GetNumber(x))
	{
		return -*number;
	}
	else if(auto bigint = GetBigInt(x))
	{
		return JSBigInt(-*bigint);
	}
#if ENABLE_ES4
	else if(auto longval = GetLong(x))
	{
		if(*longval == INT64_MIN)
		{
			return JSValue(JSUlong(JSUlong(INT64_MAX) + 1));
		}
		else
		{
			return JSValue(JSLong(-*longval));
		}
	}
	else if(auto ulongval = GetUlong(x))
	{
		if(*ulongval == 0)
		{
			return JSValue(JSUlong(0));
		}
		else if(*ulongval <= -JSUlong(INT64_MIN))
		{
			return JSValue(JSLong(-*ulongval));
		}
		else
		{
			return JSNumber(-*ulongval);
		}
	}
	else if(auto floatval = GetFloat(x))
	{
		return JSValue(JSFloat(-*floatval));
	}
#endif
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

JSValue JS::Type::Value::bitwiseNOT(JSValue x AND_PHASE(phase))
{
	if(auto number = GetNumber(x))
	{
		return JSNumber(~JS::ToInt32(*number RUNTIME_PHASE));
	}
	else if(auto bigint = GetBigInt(x))
	{
		return JSBigInt(~*bigint);
	}
#if ENABLE_ES4
	else if(auto longval = GetLong(x))
	{
		return JSValue(JSLong(~*longval));
	}
	else if(auto ulongval = GetUlong(x))
	{
		return JSNumber(~*ulongval);
	}
	else if(auto floatval = GetFloat(x))
	{
		return JSNumber(~JS::ToInt32(*number RUNTIME_PHASE));
	}
#endif
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

JSValue JS::Type::Value::exponentiate(JSValue x, JSValue y)
{
	if(CurrentVersion < Version::ES7)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	if(Version::ES11 <= CurrentVersion)
	{
		if(!SameType(x, y))
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}

		if(auto x_bigint = GetBigInt(x))
		{
			auto y_bigint = GetBigInt(y);
			assert(y_bigint.has_value());

			if(sgn(*y_bigint) < 0)
			{
				JS::Throw(JS::New<RangeError> __LOC(u"raising bigint to negative power"));
			}
			// supported by GMP
			/*else if(sgn(*x_bigint) == 0 && sgn(*y_bigint) == 0)
			{
				return 1_mpz;
			}*/
			else if(*x_bigint == 1_mpz)
			{
				return 1_mpz;
			}
			else if(!y_bigint->fits_ulong_p())
			{
				JS::Throw(JS::New<RangeError> __LOC(u"exponent outside supported range"));
			}
			else
			{
				JSBigInt result;
				mpz_pow_ui(result.get_mpz_t(), x_bigint->get_mpz_t(), y_bigint->get_ui());
				return result;
			}
		}
	}

	auto x_number = GetNumber(x);
	assert(x_number.has_value());
	auto y_number = GetNumber(y);
	assert(y_number.has_value());
	return pow(*x_number, *y_number);
}

JSValue JS::Type::Value::multiply(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if((GetLong(x) || GetUlong(x) || GetLong(y) || GetUlong(y))
		&& IsInteger(x) && IsInteger(y))
		{
			JSBigInt x_bigint = ES4NumberToBigInt(x);
			JSBigInt y_bigint = ES4NumberToBigInt(y);

			auto result = ES4BigIntToLong(x_bigint * y_bigint, GetUlong(x) || GetUlong(y));

			if(result.has_value())
			{
				return result.value();
			}
		}

		return JS::ToNumber(x RUNTIME_PHASE) * JS::ToNumber(y RUNTIME_PHASE);
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());
				return JSBigInt(*x_bigint * *y_bigint);
			}
		}
		auto x_number = GetNumber(x);
		assert(x_number.has_value());
		auto y_number = GetNumber(y);
		assert(y_number.has_value());
		return *x_number * *y_number;
	}
}

JSValue JS::Type::Value::divide(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if((GetLong(x) || GetUlong(x) || GetLong(y) || GetUlong(y))
		&& IsInteger(x) && IsInteger(y) && !IsZero(y))
		{
			JSBigInt x_bigint = ES4NumberToBigInt(x);
			JSBigInt y_bigint = ES4NumberToBigInt(y);

			mpq_class ratresult {x_bigint, y_bigint};
			ratresult.canonicalize();
			if(ratresult.get_den() == 1_mpz)
			{
				mpz_class intresult = ratresult.get_num();
				if(INT64_MAX < intresult && intresult <= UINT64_MAX)
				{
					return JSValue(JSUlong(intresult.get_ui()));
				}
				else if(INT64_MIN <= intresult && intresult < 0)
				{
					return JSValue(JSLong(-intresult.get_ui()));
				}
				else if(0 <= intresult && intresult <= INT64_MAX)
				{
					if(GetUlong(x) || GetUlong(y))
					{
						return JSValue(JSUlong(intresult.get_ui()));
					}
					else
					{
						return JSValue(JSLong(intresult.get_ui()));
					}
				}
				else
				{
					return intresult;
				}
			}

			JSNumber numresult = ratresult.get_d();
			JSNumber rounded = nearbyint(numresult); // TODO: round to nearest
			if(INT64_MIN <= rounded && numresult < -0x20000000000000)
			{
				// does not fit int-in-double or ulong
				return JSValue(JSLong(rounded));
			}
			else if(INT64_MAX < rounded && rounded <= UINT64_MAX)
			{
				// does not fit int-in-double or long
				return JSValue(JSUlong(rounded));
			}
			else if(0x20000000000000 < numresult && rounded <= INT64_MAX)
			{
				if(GetUlong(x) || GetUlong(y))
				{
					return JSValue(JSUlong(rounded));
				}
				else
				{
					return JSValue(JSLong(rounded));
				}
			}
			else
			{
				return numresult;
			}
		}
		return JS::ToNumber(x RUNTIME_PHASE) / JS::ToNumber(y RUNTIME_PHASE);
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());
				if(sgn(*y_bigint) == 0)
				{
					JS::Throw(JS::New<RangeError> __LOC(u"division by zero"));
				}
				return JSBigInt(*x_bigint / *y_bigint); // tdiv
			}
		}
		auto x_number = GetNumber(x);
		assert(x_number.has_value());
		auto y_number = GetNumber(y);
		assert(y_number.has_value());
		return *x_number / *y_number;
	}
}

JSValue JS::Type::Value::remainder(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if((GetLong(x) || GetUlong(x) || GetLong(y) || GetUlong(y))
		&& IsInteger(x) && IsInteger(y) && !IsZero(y))
		{
			JSBigInt x_bigint = ES4NumberToBigInt(x);
			JSBigInt y_bigint = ES4NumberToBigInt(y);

			auto result = ES4BigIntToLong(x_bigint % y_bigint, GetUlong(x) || GetUlong(y));

			if(result.has_value())
			{
				return result.value();
			}
		}
		return JS::ToNumber(x RUNTIME_PHASE) / JS::ToNumber(y RUNTIME_PHASE);
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());
				if(sgn(*y_bigint) == 0)
				{
					JS::Throw(JS::New<RangeError> __LOC(u"division by zero"));
				}
				return JSBigInt(*x_bigint % *y_bigint); // tdiv
			}
		}
		auto x_number = GetNumber(x);
		assert(x_number.has_value());
		auto y_number = GetNumber(y);
		assert(y_number.has_value());
		return fmod(*x_number, *y_number);
	}
}

JSValue JS::Type::Value::add(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if((GetLong(x) || GetUlong(x) || GetLong(y) || GetUlong(y))
		&& IsInteger(x) && IsInteger(y))
		{
			JSBigInt x_bigint = ES4NumberToBigInt(x);
			JSBigInt y_bigint = ES4NumberToBigInt(y);

			auto result = ES4BigIntToLong(x_bigint + y_bigint, GetUlong(x) || GetUlong(y));

			if(result.has_value())
			{
				return result.value();
			}
		}

		return JS::ToNumber(x RUNTIME_PHASE) + JS::ToNumber(y RUNTIME_PHASE);
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());
				return JSBigInt(*x_bigint + *y_bigint);
			}
		}
		auto x_number = GetNumber(x);
		assert(x_number.has_value());
		auto y_number = GetNumber(y);
		assert(y_number.has_value());
		return *x_number + *y_number;
	}
}

JSValue JS::Type::Value::subtract(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if((GetLong(x) || GetUlong(x) || GetLong(y) || GetUlong(y))
		&& IsInteger(x) && IsInteger(y))
		{
			JSBigInt x_bigint = ES4NumberToBigInt(x);
			JSBigInt y_bigint = ES4NumberToBigInt(y);

			auto result = ES4BigIntToLong(x_bigint - y_bigint, GetUlong(x) || GetUlong(y));

			if(result.has_value())
			{
				return result.value();
			}
		}

		return JS::ToNumber(x RUNTIME_PHASE) - JS::ToNumber(y RUNTIME_PHASE);
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());
				return JSBigInt(*x_bigint - *y_bigint);
			}
		}
		auto x_number = GetNumber(x);
		assert(x_number.has_value());
		auto y_number = GetNumber(y);
		assert(y_number.has_value());
		return *x_number - *y_number;
	}
}

JSValue JS::Type::Value::leftShift(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		auto y_integer = JS::ToUint32(y RUNTIME_PHASE);
		if(auto x_longval = GetLong(x))
		{
			return JSValue(*x_longval << (y_integer & 0x3F));
		}
		else if(auto x_ulongval = GetUlong(x))
		{
			return JSValue(*x_ulongval << (y_integer & 0x3F));
		}
		else
		{
			auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
			return JSNumber(x_integer << (y_integer & 0x1F));
		}
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());

				JSBigInt result;

				if(sgn(*y_bigint) < 0)
				{
					if(!JSBigInt(abs(*y_bigint)).fits_ulong_p())
					{
						JS::Throw(JS::New<RangeError> __LOC(u"exponent outside supported range"));
					}

					mpz_fdiv_q_2exp(result.get_mpz_t(), x_bigint->get_mpz_t(), y_bigint->get_ui());
				}
				else
				{
					if(!y_bigint->fits_ulong_p())
					{
						JS::Throw(JS::New<RangeError> __LOC(u"exponent outside supported range"));
					}

					mpz_mul_2exp(result.get_mpz_t(), x_bigint->get_mpz_t(), y_bigint->get_ui());
				}

				return result;
			}
		}

		auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
		auto y_integer = JS::ToUint32(y RUNTIME_PHASE);
		return JSNumber(x_integer << (y_integer & 0x1F));
	}
}

JSValue JS::Type::Value::signedRightShift(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		auto y_integer = JS::ToUint32(y RUNTIME_PHASE);
		if(auto x_longval = GetLong(x))
		{
			return JSValue(JSLong(*x_longval) >> (y_integer & 0x3F));
		}
		else if(auto x_ulongval = GetUlong(x))
		{
			return JSValue(JSLong(*x_ulongval) >> (y_integer & 0x3F));
		}
		else
		{
			auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
			return JSNumber(x_integer >> (y_integer & 0x1F));
		}
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());

				JSBigInt result;

				if(sgn(*y_bigint) < 0)
				{
					if(!JSBigInt(abs(*y_bigint)).fits_ulong_p())
					{
						JS::Throw(JS::New<RangeError> __LOC(u"exponent outside supported range"));
					}

					mpz_mul_2exp(result.get_mpz_t(), x_bigint->get_mpz_t(), y_bigint->get_ui());
				}
				else
				{
					if(!y_bigint->fits_ulong_p())
					{
						JS::Throw(JS::New<RangeError> __LOC(u"exponent outside supported range"));
					}

					mpz_fdiv_q_2exp(result.get_mpz_t(), x_bigint->get_mpz_t(), y_bigint->get_ui());
				}

				return result;
			}
		}

		auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
		auto y_integer = JS::ToUint32(y RUNTIME_PHASE);
		return JSNumber(x_integer >> (y_integer & 0x1F));
	}
}

JSValue JS::Type::Value::unsignedRightShift(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		auto y_integer = JS::ToUint32(y RUNTIME_PHASE);
		if(auto x_longval = GetLong(x))
		{
			return JSValue(JSUlong(*x_longval) >> (y_integer & 0x3F));
		}
		else if(auto x_ulongval = GetUlong(x))
		{
			return JSValue(JSUlong(*x_ulongval) >> (y_integer & 0x3F));
		}
		else
		{
			auto x_integer = JS::ToUint32(x RUNTIME_PHASE);
			return JSNumber(x_integer >> (y_integer & 0x1F));
		}
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
		}

		auto x_integer = JS::ToUint32(x RUNTIME_PHASE);
		auto y_integer = JS::ToUint32(y RUNTIME_PHASE);
		return JSNumber(x_integer >> (y_integer & 0x1F));
	}
}

JS::ComparisonResult JS::Type::Value::compare(JSValue x, JSValue y AND_PHASE(phase))
{
	mpq_class x_float;
	std::optional<bool> x_infinite_sign;
#if ENABLE_ES4
	if(GetNumber(x) || GetFloat(x))
#else
	if(GetNumber(x))
#endif
	{
		auto x_number = JS::ToNumber(x WITH_PHASE(phase));
		switch(std::fpclassify(x_number))
		{
		case FP_NAN:
			return ComparisonResult::Unordered;
		case FP_INFINITE:
			x_infinite_sign = std::signbit(x_number) != 0;
			break;
		default:
			x_float = x_number;
			break;
		}
	}
#if ENABLE_ES4
	else if(auto x_longval = GetLong(x))
	{
		x_float = *x_longval;
	}
	else if(auto x_ulongval = GetUlong(x))
	{
		x_float = *x_ulongval;
	}
#endif
	else if(auto x_bigint = GetBigInt(x))
	{
		x_float = *x_bigint;
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	mpq_class y_float;
#if ENABLE_ES4
	if(GetNumber(y) || GetFloat(y))
#else
	if(GetNumber(y))
#endif
	{
		auto y_number = JS::ToNumber(y WITH_PHASE(phase));
		switch(std::fpclassify(y_number))
		{
		case FP_NAN:
			return ComparisonResult::Unordered;
		case FP_INFINITE:
			if(std::signbit(y_number) != 0)
			{
				if(x_infinite_sign.value_or(false))
				{
					return ComparisonResult::Equal;
				}
				else
				{
					return ComparisonResult::Greater;
				}
			}
			else
			{
				if(x_infinite_sign.value_or(true))
				{
					return ComparisonResult::Equal;
				}
				else
				{
					return ComparisonResult::Less;
				}
			}
		default:
			y_float = y_number;
			break;
		}
	}
#if ENABLE_ES4
	else if(auto y_longval = GetLong(y))
	{
		y_float = *y_longval;
	}
	else if(auto y_ulongval = GetUlong(y))
	{
		y_float = *y_ulongval;
	}
#endif
	else if(auto y_bigint = GetBigInt(y))
	{
		y_float = *y_bigint;
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	int c = cmp(x_float, y_float);
	if(c < 0)
	{
		return ComparisonResult::Less;
	}
	else if(c > 0)
	{
		return ComparisonResult::Greater;
	}
	else
	{
		return ComparisonResult::Equal;
	}
}

bool JS::Type::Value::sameValue([[maybe_unused]] /* TODO */ JSValue x, [[maybe_unused]] /* TODO */ JSValue y)
{
	// TODO
	return false;
}

bool JS::Type::Value::sameValueZero([[maybe_unused]] /* TODO */ JSValue x, [[maybe_unused]] /* TODO */ JSValue y)
{
	// TODO
	return false;
}

JSValue JS::Type::Value::bitwiseAND(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(GetLong(x) || GetUlong(x) || GetLong(y) || GetUlong(y))
		{
			auto x_integer = JS::ToInt64(x RUNTIME_PHASE);
			auto y_integer = JS::ToInt64(y RUNTIME_PHASE);
			if(GetUlong(x) || GetUlong(y))
			{
				return JSValue(JSUlong(x_integer & y_integer));
			}
			else
			{
				return JSValue(JSLong(x_integer & y_integer));
			}
		}
		else
		{
			auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
			auto y_integer = JS::ToInt32(y RUNTIME_PHASE);
			return JSNumber(x_integer & y_integer);
		}
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());
				return JSBigInt(*x_bigint & *y_bigint);
			}
		}

		auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
		auto y_integer = JS::ToInt32(y RUNTIME_PHASE);
		return JSNumber(x_integer & y_integer);
	}
}

JSValue JS::Type::Value::bitwiseXOR(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(GetLong(x) || GetUlong(x) || GetLong(y) || GetUlong(y))
		{
			auto x_integer = JS::ToInt64(x RUNTIME_PHASE);
			auto y_integer = JS::ToInt64(y RUNTIME_PHASE);
			if(GetUlong(x) || GetUlong(y))
			{
				return JSValue(JSUlong(x_integer ^ y_integer));
			}
			else
			{
				return JSValue(JSLong(x_integer ^ y_integer));
			}
		}
		else
		{
			auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
			auto y_integer = JS::ToInt32(y RUNTIME_PHASE);
			return JSNumber(x_integer ^ y_integer);
		}
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());
				return JSBigInt(*x_bigint ^ *y_bigint);
			}
		}

		auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
		auto y_integer = JS::ToInt32(y RUNTIME_PHASE);
		return JSNumber(x_integer ^ y_integer);
	}
}

JSValue JS::Type::Value::bitwiseOR(JSValue x, JSValue y AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(GetLong(x) || GetUlong(x) || GetLong(y) || GetUlong(y))
		{
			auto x_integer = JS::ToInt64(x RUNTIME_PHASE);
			auto y_integer = JS::ToInt64(y RUNTIME_PHASE);
			if(GetUlong(x) || GetUlong(y))
			{
				return JSValue(JSUlong(x_integer | y_integer));
			}
			else
			{
				return JSValue(JSLong(x_integer | y_integer));
			}
		}
		else
		{
			auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
			auto y_integer = JS::ToInt32(y RUNTIME_PHASE);
			return JSNumber(x_integer | y_integer);
		}
	}
	else
#endif
	{
		if(Version::ES11 <= CurrentVersion)
		{
			if(!SameType(x, y))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			if(auto x_bigint = GetBigInt(x))
			{
				auto y_bigint = GetBigInt(y);
				assert(y_bigint.has_value());
				return JSBigInt(*x_bigint | *y_bigint);
			}
		}

		auto x_integer = JS::ToInt32(x RUNTIME_PHASE);
		auto y_integer = JS::ToInt32(y RUNTIME_PHASE);
		return JSNumber(x_integer | y_integer);
	}
}

// TODO: JSClass and JSType should also be inspectable

[[noreturn]] void JS::Throw(JS::Public::JSValue throwable)
{
	throw JS::Type::Value::UnwrapValue(throwable);
}

