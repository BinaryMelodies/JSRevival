
#include <bit>
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
#include "iterator.hh"
#include "promise.hh"
#include "runtime.hh"
#include "environment.hh"
#include "modules.hh"
#include "parser.hh" // required to check character types
#include "ast.hh" // required for Functional related behavior
#include "builtins.hh"
#include "debugger.hh"

#include <string>
#include <unicode/uchar.h>
#include <variant>
#include <vector>

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

//// Abstract::Value

#if ENABLE_ES4
JSClass JS::Class::Internal::Value::GetClass()
{
	return GetClassForType(GetType());
}
#endif

JSValue JS::Class::Internal::Value::CreateReference(JSIdentifierReference identifier_reference, JS::Runtime::ReferenceContext cxt)
{
	if(identifier_reference.Attribute)
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}

	JSValue object_reference;

	if(CurrentVersion <= Version::ES3)
	{
		object_reference = ToObject();
	}
	else
	{
		object_reference = GetValue(Intent::Read WITH_PHASE(cxt.Phase)); // convert back to primitive value or object reference
	}

	if(Version::ESNEXT <= CurrentVersion && identifier_reference.Private)
	{
		return JS::Runtime::PrivateReference{
			object_reference,
			JS::Runtime::PrivateEnvironmentRecord::ResolvePrivateIdentifier(GetCurrentExecutionContext()->PrivateEnvironment, identifier_reference.GetName())
		};
	};

	return JS::Runtime::MemberReference{
		object_reference,
		identifier_reference.PropertyKey,
		object_reference,
		cxt.Strict
		WITH_NAMESPACES(identifier_reference.GetNamespaces(cxt))
		WITH_CLASS(GetClass())
	};
}

JSValue JS::Class::Internal::Value::CreateBracketReference(std::vector<JSValue> arguments, JS::Runtime::ReferenceContext cxt)
{
	JSValue object_reference;

	if(CurrentVersion <= Version::ES3)
	{
		object_reference = ToObject();
	}
	else
	{
		object_reference = GetValue(Intent::Read WITH_PHASE(cxt.Phase)); // convert back to primitive value or object reference
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		return JS::Runtime::BracketReference{
			object_reference,
			arguments,
			object_reference,
			cxt.Strict
			WITH_CLASS(GetClass())
		};
	}
	else
#endif
	{
		JSValue index = assert_singlet(arguments);

		return JS::Runtime::MemberReference{
			object_reference,
			ConvertToPropertyKey(index WITH_PHASE(cxt.Phase)),
			object_reference,
			cxt.Strict
			PUBLIC_NAMESPACES
			WITH_CLASS(GetClass())
		};
	}
}

JSValue JS::Class::Internal::Value::ResolvePropertyKey(JSValue property_key)
{
	// in ES16+, the referenced name has not been converted to string/symbol yet
	if(CurrentVersion < Version::ES16)
	{
		return property_key;
	}
	else
	{
		return property_key.ToPropertyKey(VIA_RUNTIME_PHASE);
	}
}

static bool toPrimitiveVia(JSValue object, std::u16string method_name, JSValue& result AND_PHASE(phase))
{
	auto method = JS::GetFunction(JS::Get(object, method_name));
	if(!method)
	{
		return false;
	}
	JSValue return_value = JS::Call(method.value(), object, std::vector<JSValue>() WITH_PHASE(phase));
	if(JS::GetNonNullObject(return_value))
	{
		return false;
	}
	else
	{
		result = return_value;
		return true;
	}
}

// TODO: move
JSValue JS::Class::Object::ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	JSValue result;

#if ENABLE_ES4
	if(IsVersionES4() && hint == JS::Runtime::TypeHint::Default)
	{
		hint = GetClass()->GetDefaultHint();
	}
#endif

	if(CurrentVersion >= Version::ES6)
	{
		if(auto ToPrimitive = GetFunction(JS::Get(this, JS::Symbols::toPrimitive)))
		{
			std::u16string hint_string;

			switch(hint)
			{
			case JS::Runtime::TypeHint::String:
				hint_string = u"string";
				break;
			case JS::Runtime::TypeHint::Number:
				hint_string = u"number";
				break;
			case JS::Runtime::TypeHint::Default:
				hint_string = u"default";
				break;
			}

			// TODO: Class::Symbol and Class::Date are special
			result = JS::Call(ToPrimitive.value(), this, std::vector<JSValue> { hint_string } WITH_PHASE(phase));
			if(GetNonNullObject(result))
			{
				JS::Throw(JS::New<TypeError> __LOC("toPrimitive returned non-primitive"));
			}
			else
			{
				return result;
			}
		}

		// overriding by host objects is not allowed in these versions
		return Object::DefaultValue(hint WITH_PHASE(phase));
	}
	else
	{
		return DefaultValue(hint WITH_PHASE(phase));
	}
}

JSValue JS::Class::Internal::Value::DefaultValue(JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	JSValue self = GetValue(Intent::Read WITH_PHASE(phase)); // convert back to primitive value or object reference
	JSValue result;

	switch(hint)
	{
	case JS::Runtime::TypeHint::String:
		if(toPrimitiveVia(self, u"toString", result WITH_PHASE(phase)))
		{
			return result;
		}
		if(toPrimitiveVia(self, u"valueOf", result WITH_PHASE(phase)))
		{
			return result;
		}
		break;
	case JS::Runtime::TypeHint::Number:
	case JS::Runtime::TypeHint::Default:
		if(toPrimitiveVia(self, u"valueOf", result WITH_PHASE(phase)))
		{
			return result;
		}
		if(toPrimitiveVia(self, u"toString", result WITH_PHASE(phase)))
		{
			return result;
		}
		break;
	}
	JS::Throw(JS::New<TypeError> __LOC(u"unable to convert to primitive value, valueOf and toString both failed"));
}

bool JS::Class::Internal::Value::HasOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(GetClass()->GetBaseInstanceProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
		{
			return true;
		}
	}
#endif

	return GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)).has_value();
}

JSValue JS::Class::Internal::Value::ConvertToPropertyKey(JSValue property_key AND_PHASE(phase))
{
	if(CurrentVersion < Version::ES6)
	{
		return property_key.ToString(VIA_PHASE(phase));
	}
	else
	{
		return property_key.ToPropertyKey(VIA_PHASE(phase));
	}
}

#if ENABLE_ES4
JSValue JS::Class::Internal::Value::GetBracket(std::vector<JSValue> arguments, JSValue receiver, Intent intent AND_CLASS(limit) AND_PHASE(phase))
{
	JSValue index = assert_singlet(arguments);
	return Get(ConvertToPropertyKey(index WITH_PHASE(phase)), receiver, intent PUBLIC_NAMESPACES WITH_CLASS(limit) NO_ENVIRONMENT WITH_PHASE(phase));
}
#endif

#if ENABLE_ES4
bool JS::Class::Internal::Value::SetBracket(std::vector<JSValue> arguments, JSValue value, JSValue receiver AND_CLASS(limit))
{
	JSValue index = assert_singlet(arguments);
	return Set(ConvertToPropertyKey(index RUNTIME_PHASE), value, receiver PUBLIC_NAMESPACES WITH_CLASS(limit) NO_ENVIRONMENT);
}
#endif

#if ENABLE_ES4
bool JS::Class::Internal::Value::DeleteBracket(std::vector<JSValue> arguments AND_CLASS(limit))
{
	JSValue index = assert_singlet(arguments);
	return Delete(ConvertToPropertyKey(index RUNTIME_PHASE) PUBLIC_NAMESPACES WITH_CLASS(limit) WITH_FLAG(false));
}
#endif

// TODO: move
JSValue JS::Class::Object::GetPrivate([[maybe_unused]] JSPrivateName name)
{
	JS::Throw(JS::New<TypeError> __LOC("exotic object has no private fields"));
}

// TODO: move
void JS::Class::Object::SetPrivate([[maybe_unused]] JSPrivateName name, [[maybe_unused]] JSValue value)
{
	JS::Throw(JS::New<TypeError> __LOC("exotic object has no private fields"));
}

// TODO: make it a member of Abstract::Object instead
JSValue JS::Class::Internal::Value::NewObject()
{
	return JS::ToObject(GetValue(Intent::Read RUNTIME_PHASE));
}

// TODO: move
bool JS::Class::Object::Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	JSPropertyDescriptor descriptor;

	switch(property.PropertyType)
	{
#if ENABLE_ES4
	case JSProperty::Type::Variable:
	case JSProperty::Type::Getter:
	case JSProperty::Type::Setter:
//	case JSProperty::Type::Alias:
		// exotic objects do not suport variable/getter/setter properties
		return false;
#endif
	case JSProperty::Type::Data:
		descriptor.Value = property.GetValue();
		descriptor.Writable = property.IsWritable();
		break;
	case JSProperty::Type::Accessor:
		descriptor.Get = property.Get;
		descriptor.Set = property.Set;
		break;
	default:
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal property descriptor type"));
	}
	descriptor.Enumerable = property.Enumerable;
	descriptor.Configurable = property.Configurable;
	return DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
}

JSString JS::Class::Internal::Value::GetClassSlot()
{
	return GetClassString(); // usually these two are identical
}

JSValue JS::Class::Internal::Value::ResolveMethod()
{
	return GetValue(Intent::Call RUNTIME_PHASE);
}

JSValue JS::Class::Internal::Value::ResolveMethod(JS::Runtime::MemberReference& reference)
{
	return Get(reference.ReferencedName, reference.ThisValue, Intent::Call WITH_NAMESPACES(reference.Namespaces) WITH_CLASS(reference.Limit) NO_ENVIRONMENT RUNTIME_PHASE);
}

#if ENABLE_ES4
JSValue JS::Class::Internal::Value::ResolveMethod(JS::Runtime::BracketReference& reference)
{
	return GetBracket(reference.Arguments, reference.ThisValue, Intent::Call WITH_CLASS(reference.Limit) RUNTIME_PHASE);
}
#endif

bool JS::Class::Internal::Value::Begin_Call(JSValue this_argument, std::vector<JSValue> arguments)
{
	GetCurrentExecutionContext()->Action_Push<JSValue>(Call(this_argument, arguments RUNTIME_PHASE));
	return false; // call not deferred
}

#if ENABLE_ES4
std::optional<JS::Runtime::SignatureType> JS::Class::Internal::Value::GetSignature()
{
	JS::Throw(JS::New<TypeError> __LOC("non-function has no signature"));
}
#endif

JS::Runtime::Realm * JS::Class::Internal::Value::GetFunctionRealm()
{
	JS::Throw(JS::New<TypeError> __LOC("non-function has no realm"));
}

bool JS::Class::Internal::Value::DeleteReference()
{
	return true;
}

bool JS::Class::Internal::Value::SameValueZero(const JSValue& other) const
{
	return SameValue(other);
}

bool JS::Class::Internal::Value::IsStrictlyEqual(const JSValue& other) const
{
	return SameValue(other);
}

JSValue JS::Class::Internal::Value::ToNumeric(FOR_PHASE(phase))
{
	auto primitive = ToPrimitive(JS::Runtime::TypeHint::Number WITH_PHASE(phase));
	// TODO: make sure this does not result in an infinite recursion
	return primitive.ToNumeric(VIA_PHASE(phase));
}

//// Object

JSValue JS::Class::Object::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return this;
}

void JS::Class::Object::PutValue([[maybe_unused]] JSValue value)
{
	JS::Throw(JS::New<ReferenceError> __LOC("cannot assign to object reference"));
}

bool JS::Class::Object::SameValue(const JSValue& other) const
{
	// TODO: external, XML
	return JSObject(const_cast<Object *>(this)) == GetObjectOrNull(other).value_or(nullptr);
}

JSBoolean JS::Class::Object::ToBoolean()
{
	return true;
}

JSNumber JS::Class::Object::ToNumber(FOR_PHASE(phase))
{
	return ToPrimitive(JS::Runtime::TypeHint::Number WITH_PHASE(phase)).ToNumber(__ES4_ONLY(phase));
}

JSBigInt JS::Class::Object::ToBigInt()
{
	return ToPrimitive(JS::Runtime::TypeHint::Number RUNTIME_PHASE).ToBigInt();
}

JSString JS::Class::Object::ToString(FOR_PHASE(phase))
{
	return ToPrimitive(JS::Runtime::TypeHint::String WITH_PHASE(phase)).ToString(VIA_PHASE(phase));
}

// TODO: move
JSString JS::Class::XML::ToString(FOR_PHASE(phase))
{
	if(Class == JS::Class::XML::ClassType::Attribute || Class == JS::Class::XML::ClassType::Text)
	{
		return Value.value_or(u""); // not mentioned explicitly how to convert a missing element to string
	}
	else if(HasSimpleContent())
	{
		JSString result;
		for(auto child : children)
		{
			if(child->Class == JS::Class::XML::ClassType::Comment || child->Class == JS::Class::XML::ClassType::ProcessingInstruction)
			{
				continue;
			}
			result += JSValue(JSObject(child)).ToString(VIA_PHASE(phase));
		}
		return result;
	}
	else
	{
		return ToXMLString({}, 0);
	}
}

// TODO: move
JSString JS::Class::XMLList::ToString(FOR_PHASE(phase))
{
	if(HasSimpleContent())
	{
		JSString result;
		for(auto child : children)
		{
			if(child->Class == JS::Class::XML::ClassType::Comment || child->Class == JS::Class::XML::ClassType::ProcessingInstruction)
			{
				continue;
			}
			result += JSValue(JSObject(child)).ToString(VIA_PHASE(phase));
		}
		return result;
	}
	else
	{
		return ToXMLString({}, 0);
	}
	// TODO
}

JSObject JS::Class::Object::ToObject()
{
	return JSObject(this);
}

JSString JS::Class::Object::Typeof()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return GetClass()->GetTypeofString();
	}
#endif
	//if(dynamic_cast<JSFunction>(reference))
	if(GetInvocability() != JS::Invocability::NonCallable)
	{
		return u"function";
	}
	return u"object";
}

// TODO: move
JSString JS::Class::XMLBase::Typeof()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return GetClass()->GetTypeofString();
	}
	else
#endif
	{
		return u"xml";
	}
}

JSString JS::Class::Object::ToStringTag()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return GetTypeNameString(GetType());
	}
#endif
	JSString builtin_tag = GetClassString(); // Proxy may throw error according to the specification, so we must start here
	if(CurrentVersion >= Version::ES6)
	{
		if(auto tag = GetString(JS::Get(this, JS::Symbols::toStringTag)))
		{
			return tag.value();
		}
	}
	return builtin_tag;
}

// TODO: move to XML
static inline JSNamespace include_namespace(std::map<JSString, JSNamespace>& Namespaces, std::map<JSString, JSNamespace> AncestorNamespaces, JS::Type::QName qname)
{
	auto namespace_ = qname.GetNamespace(Namespaces);
	if(!namespace_->prefix.has_value())
	{
		std::set<JSString> prefixes;
		for(auto ns : Namespaces)
		{
			if(ns.second->prefix.has_value())
			{
				prefixes.insert(ns.second->prefix.value());
			}
		}
		for(auto ns : AncestorNamespaces)
		{
			if(ns.second->prefix.has_value())
			{
				prefixes.insert(ns.second->prefix.value());
			}
		}

		if(prefixes.find(u"") == prefixes.end())
		{
			namespace_ = JS::New<JS::Class::Namespace>(u"", namespace_->uri);
			Namespaces[namespace_->uri] = namespace_;
		}
		else
		{
			for(size_t index = 0;; index++)
			{
				std::ostringstream oss;
				oss << "_" << index;
				JSString prefix;
				for(auto c : oss.str())
				{
					prefix += char16_t(c);
				}
				if(prefixes.find(prefix) == prefixes.end())
				{
					namespace_ = JS::New<JS::Class::Namespace>(prefix, namespace_->uri);
					Namespaces[namespace_->uri] = namespace_;
					break;
				}
			}
		}
	}
	return namespace_;
}

static inline void include_namespace(std::map<JSString, JSNamespace> Namespaces, std::map<JSString, JSNamespace> AncestorNamespaces, JSNamespace namespace_)
{
	if(!namespace_->prefix.has_value())
	{
		std::set<JSString> prefixes;
		for(auto ns : Namespaces)
		{
			if(ns.second->prefix.has_value())
			{
				prefixes.insert(ns.second->prefix.value());
			}
		}
		for(auto ns : AncestorNamespaces)
		{
			if(ns.second->prefix.has_value())
			{
				prefixes.insert(ns.second->prefix.value());
			}
		}
		if(prefixes.find(u"") == prefixes.end())
		{
			namespace_->prefix = u"";
		}
		else
		{
			for(size_t index = 0;; index++)
			{
				std::ostringstream oss;
				oss << "_" << index;
				JSString prefix;
				for(auto c : oss.str())
				{
					prefix += char16_t(c);
				}
				if(prefixes.find(prefix) == prefixes.end())
				{
					namespace_->prefix = prefix;
					break;
				}
			}
		}
	}
}

JSString JS::Class::Object::ToXMLString([[maybe_unused]] std::map<JSString, JSNamespace> AncestorNamespaces, [[maybe_unused]] uint32_t IndentLevel)
{
	return JSValue(ToString(VIA_RUNTIME_PHASE)).ToXMLString({}, 0);
}

// TODO: move
JSString JS::Class::XML::ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel)
{
	bool prettyPrinting = JS::Get(JS::Intrinsics()->XML, u"prettyPrinting") == true;
	JSString result;
	if(prettyPrinting && IndentLevel > 0)
	{
		result = std::u16string(IndentLevel, u' ');
	}
	switch(Class)
	{
	case JS::Class::XML::ClassType::Text:
		{
			JSString value = Value.value_or(u"");
			if(prettyPrinting)
			{
				// TODO: remove leading and trailing whitespaces
			}
			return result + JS::EscapeElementValue(value);
		}
	case JS::Class::XML::ClassType::Attribute:
		return result + JS::EscapeElementValue(Value.value_or(u""));
	case JS::Class::XML::ClassType::Comment:
		return result + u"<!--" + Value.value_or(u"") + u"-->";
	case JS::Class::XML::ClassType::ProcessingInstruction:
		// TODO: what if Name is null?
		return result + u"<?" + Name->localName + u" " + Value.value_or(u"") + u"?>";
	case JS::Class::XML::ClassType::Element:
	default:
		{
			std::map<JSString, JSNamespace> NamespaceDeclarations;
			for(auto ns_entry : InScopeNamespaces)
			{
				if(AncestorNamespaces.find(ns_entry.first) == AncestorNamespaces.end())
				{
					NamespaceDeclarations[ns_entry.first] = JS::New<JS::Class::Namespace>(ns_entry.second);
				}
			}

			// TODO: Name must not be nullptr
			auto namespace_ = include_namespace(NamespaceDeclarations, AncestorNamespaces, *Name);

			for(auto attribute_entry : Attributes)
			{
				include_namespace(NamespaceDeclarations, AncestorNamespaces, attribute_entry.first);
			}

			result += u"<";
			if(namespace_->prefix.value() != u"")
			{
				result += namespace_->prefix.value() + u":";
			}
			result += Name->localName;

			for(auto attribute_entry : Attributes)
			{
				assert(attribute_entry.second->Class == JS::Class::XML::ClassType::Attribute);

				result += u" ";
				auto namespace_ = include_namespace(NamespaceDeclarations, AncestorNamespaces, attribute_entry.first);
				if(namespace_->prefix.value() != u"")
				{
					result += namespace_->prefix.value() + u":";
				}
				result += attribute_entry.second->Name->localName;
				result += u"=\"" + JS::EscapeAttributeValue(attribute_entry.second->Value.value_or(u"")) + u"\"";
			}
			for(auto namespace_ : NamespaceDeclarations)
			{
				result += u" xmlns";
				include_namespace(NamespaceDeclarations, AncestorNamespaces, namespace_.second);
				if(namespace_.second->prefix.value() != u"")
				{
					result += u":" + namespace_.second->prefix.value();
				}
				result += u"=\"" + JS::EscapeAttributeValue(namespace_.second->uri) + u"\"";
			}

			if(children.size() == 0)
			{
				result += u"/>";
				return result;
			}

			result += u">";

			bool indentChildren = children.size() > 1;
			if(children.size() == 1)
			{
				if(children[0]->Class != JS::Class::XML::ClassType::Text)
				{
					indentChildren = true;
				}
			}

			uint32_t nextIndentLevel = 0;
			if(prettyPrinting && indentChildren)
			{
				nextIndentLevel = IndentLevel + GetNumber(JS::Get(JS::Intrinsics()->XML, u"prettyIndent")).value(); // TODO: must be number
			}

			std::map<JSString, JSNamespace> AllNamespaces = AncestorNamespaces;
			AllNamespaces.insert(NamespaceDeclarations.begin(), NamespaceDeclarations.end());

			for(auto child : children)
			{
				if(prettyPrinting && indentChildren)
				{
					result += u"\n";
				}
				result += JSValue(JSObject(child)).ToXMLString(AllNamespaces, nextIndentLevel);
			}

			if(prettyPrinting && indentChildren)
			{
				result += u"\n" + std::u16string(IndentLevel, u' ');
			}

			result += u"</";
			if(namespace_->prefix.value() != u"")
			{
				result += namespace_->prefix.value() + u":";
			}
			result += Name->localName;
			result += u">";
			return result;
		}
	}
}

// TODO: move
JSString JS::Class::XMLList::ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, [[maybe_unused]] uint32_t IndentLevel)
{
	bool prettyPrinting = JS::Get(JS::Intrinsics()->XML, u"prettyPrinting") == true;
	JSString result;
	bool started = false;
	for(auto child : children)
	{
		if(prettyPrinting && started)
		{
			result += u"\n";
		}
		started = true;
		result += JSValue(JSObject(child)).ToXMLString(AncestorNamespaces);
	}
	return result;
}

JSXML JS::Class::Object::ToXML()
{
	JS::Throw(JS::New<TypeError> __LOC("unable to convert object to XML"));
}

// TODO: move
JSXML JS::Class::Boolean::ToXML()
{
	return JSValue(BooleanData).ToXML();
}

// TODO: move
JSXML JS::Class::Number::ToXML()
{
	return JSValue(NumberData).ToXML();
}

// TODO: move
JSXML JS::Class::String::ToXML()
{
	return JSValue(StringData).ToXML();
}

// TODO: move
JSXML JS::Class::XML::ToXML()
{
	return this;
}

// TODO: Move
JSXML JS::Class::XMLList::ToXML()
{
	// TODO
	return nullptr;
}

JSXMLList JS::Class::Object::ToXMLList()
{
	JS::Throw(JS::New<TypeError> __LOC("unable to convert object to XMLList"));
}

// TODO: move
JSXMLList JS::Class::Boolean::ToXMLList()
{
	return JSValue(BooleanData).ToXMLList();
}

// TODO: move
JSXMLList JS::Class::Number::ToXMLList()
{
	return JSValue(NumberData).ToXMLList();
}

// TODO: move
JSXMLList JS::Class::String::ToXMLList()
{
	return JSValue(StringData).ToXMLList();
}

// TODO: move
JSXMLList JS::Class::XML::ToXMLList()
{
	// TODO
	return nullptr;
}

// TODO: move
JSXMLList JS::Class::XMLList::ToXMLList()
{
	return this;
}

JSAttributeName JS::Class::Object::ToAttributeName()
{
	if(auto qname = dynamic_cast<JS::Class::QName *>(this))
	{
		return JSAttributeName(*qname);
	}
	else
	{
		return JSValue(ToString(VIA_RUNTIME_PHASE)).ToAttributeName();
	}
}

// TODO: move
JSAttributeName JS::Class::QName::ToAttributeName()
{
	return JSAttributeName(*this);
}

JSValue JS::Class::Object::ToXMLName()
{
	return JSValue(ToString(VIA_RUNTIME_PHASE)).ToXMLName();
}

// TODO: move
JSValue JS::Class::QName::ToXMLName()
{
	return JSObject(this);
}

JS::Class::Object * JS::Class::Object::ValueOf()
{
	return this;
}

bool JS::Class::Object::has_value(const std::type_info& type) const
{
	return type == typeid(JSObject);
}

const void * JS::Class::Object::get_if(const std::type_info& type) const
{
	if(type == typeid(JSObject))
		return this;
	else
		return nullptr;
}

void JS::Class::Object::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JSObject>::inspect_value(out, JSObject(this), depth, visited, access);
}

#if ENABLE_DEBUG
#if ENABLE_ES4
void JS::Class::Object::serialize_slots(JS::Runtime::Debugger * debugger, JSClass limit)
{
	if(auto object = dynamic_cast<JS::Class::OrdinaryObject *>(this)) // TODO: what about exotic objects?
	{
		if(limit == nullptr)
			return;

		if(limit->Super.has_value())
		{
			serialize_slots(debugger, GetClassForType(limit->Super.value()));
		}

		// sort keys
		std::set<JSPropertyKey, JS::Runtime::Debugger::PropertyKeyLess> property_keys;
		for(auto property_pair : limit->InstanceMembers.bindings)
		{
			property_keys.insert(property_pair.first);
		}

		for(auto property_key : property_keys)
		{
			for(auto& property_entry : limit->InstanceMembers.bindings[property_key])
			{
				if(auto instance_variable = dynamic_cast<JSInstanceVariable>(*property_entry.value))
				{
					std::set<JSNamespace> namespaces;
					bool read = false, write = false;
					for(auto access : property_entry.keys)
					{
						namespaces.insert(std::get<0>(access));
						switch(std::get<1>(access))
						{
						case JS::Access::Read:
							read = true;
							break;
						case JS::Access::Write:
							write = true;
							break;
						}
					}
					JS::AccessSet accesses = JS::AccessSet::Read;
					if(!write)
					{
						if(!read)
						{
							JS::Throw(JS::New<InternalError> __LOC());
						}

						accesses = JS::AccessSet::Read;
					}
					else if(!read)
					{
						accesses = JS::AccessSet::Write;
					}
					else
					{
						accesses = JS::AccessSet::ReadWrite;
					}

					// (access is always ReadWrite)
					debugger->OpenXMLElement("field");

					std::ostringstream oss;
					PrintString(oss, std::get<JSString>(property_key));
					debugger->AddXMLAttribute("name", oss.str());

					std::set<JSNamespace, JS::Runtime::Debugger::NamespaceLess> namespace_set;
					for(auto namespace_ : namespaces) // TODO: more efficiently?
					{
						namespace_set.insert(namespace_);
					}

					std::string namespace_ids;
					bool started = false;
					for(auto namespace_ : namespace_set)
					{
						if(started)
							namespace_ids += " ";
						else
							started = true;
						namespace_ids = debugger->GetNamespaceId(namespace_);
					}
					debugger->AddXMLAttribute("namespaces", namespace_ids);

					switch(accesses)
					{
					case JS::AccessSet::Read:
						debugger->AddXMLAttribute("access", "read");
						break;
					case JS::AccessSet::Write:
						debugger->AddXMLAttribute("access", "write");
						break;
					case JS::AccessSet::ReadWrite:
						debugger->AddXMLAttribute("access", "read write");
						break;
					}

					if(object->Slots[instance_variable].has_value())
					{
						object->Slots[instance_variable].value().serialize(debugger);
					}
					debugger->CloseXMLElement();
				}
			}
		}
	}
}
#endif

void JS::Class::Object::serialize_property(JS::Runtime::Debugger * debugger, JSPropertyKey property_key, JSProperty property_value AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
#if 0
	bool is_alias = false;
	if(property_value.PropertyType == JSProperty::Type::Alias)
	{
		is_alias = true;
		property_value = property_value.ResolveProperty();
	}
#endif

// TODO: remove
std::visit([debugger](auto&& property_key)
{
	using T = std::decay_t<decltype(property_key)>;
	if constexpr(std::is_same_v<T, JSString>)
	{
		std::cerr << "Serializing ";
		PrintString(std::cerr, property_key);
		std::cerr << std::endl;
	}
	else if constexpr(std::is_same_v<T, JSSymbol>)
	{
		std::cerr << "Serializing symbol" << std::endl;
	}
	else
	{
		static_assert(false);
	}
}, property_key);

	switch(property_value.PropertyType)
	{
	case JSProperty::Type::Data:
		debugger->OpenXMLElement("property");
		debugger->AddXMLAttribute("writable", property_value.IsWritable() ? "true" : "false");
		property_value.GetValue().serialize(debugger);
		break;
#if ENABLE_ES4
	case JSProperty::Type::Variable:
		if(!IsVersionES4())
			return;
		debugger->OpenXMLElement("singleton");
		debugger->AddXMLAttribute("writable", property_value.IsWritable() ? "true" : "false");
		if(property_value.IsInitialized())
		{
			debugger->OpenXMLElement("value");
			property_value.GetValue().serialize(debugger);
			debugger->CloseXMLElement();
		}
		debugger->OpenXMLElement("type");
		property_value.Type->serialize(debugger);
		debugger->CloseXMLElement();
		break;
#endif
	case JSProperty::Type::Accessor:
		if(Version::ES5 > CurrentVersion)
			return;
		debugger->OpenXMLElement("accessor");
		if(property_value.Get != nullptr)
		{
			debugger->OpenXMLElement("get");
			property_value.Get.serialize(debugger);
			debugger->CloseXMLElement();
		}
		if(property_value.Set != nullptr)
		{
			debugger->OpenXMLElement("set");
			property_value.Set.serialize(debugger);
			debugger->CloseXMLElement();
		}
		break;
#if ENABLE_ES4
	case JSProperty::Type::Getter:
		if(!IsVersionES4())
			return;
		debugger->OpenXMLElement("getter");
		// TODO
		break;
	case JSProperty::Type::Setter:
		if(!IsVersionES4())
			return;
		debugger->OpenXMLElement("setter");
		// TODO
		break;
	case JSProperty::Type::Forbidden:
		if(!IsVersionES4())
			return;
		debugger->OpenXMLElement("forbidden");
		// TODO
		break;
#endif
	}

#if 0
	if(is_alias)
	{
		debugger->AddXMLAttribute("alias", "true");
	}
#endif

	std::visit([debugger](auto&& property_key)
	{
		using T = std::decay_t<decltype(property_key)>;
		if constexpr(std::is_same_v<T, JSString>)
		{
			debugger->AddXMLAttribute("name", property_key);
		}
		else if constexpr(std::is_same_v<T, JSSymbol>)
		{
			debugger->AddXMLAttribute("name-symbol", debugger->LookupSymbol(property_key));
		}
		else
		{
			static_assert(false);
		}
	}, property_key);

	debugger->AddXMLAttribute("enumerable", property_value.Enumerable ? "true" : "false");
	debugger->AddXMLAttribute("configurable", property_value.Configurable ? "true" : "false");
#if ENABLE_ES4
	if(IsVersionES4())
	{
		debugger->AddXMLAttribute("explicit", property_value.Explicit ? "true" : "false");

		std::set<JSNamespace, JS::Runtime::Debugger::NamespaceLess> namespace_set;
		for(auto namespace_ : namespaces) // TODO: more efficiently?
		{
			namespace_set.insert(namespace_);
		}

		std::string namespace_ids;
		bool started = false;
		for(auto namespace_ : namespace_set)
		{
			if(started)
				namespace_ids += " ";
			else
				started = true;
			namespace_ids = debugger->LookupNamespace(namespace_);
		}
		debugger->AddXMLAttribute("namespaces", namespace_ids);

		switch(accesses)
		{
		case JS::AccessSet::Read:
			debugger->AddXMLAttribute("access", "read");
			break;
		case JS::AccessSet::Write:
			debugger->AddXMLAttribute("access", "write");
			break;
		case JS::AccessSet::ReadWrite:
			debugger->AddXMLAttribute("access", "read write");
			break;
		}
	}
#endif

	debugger->CloseXMLElement();
}

void JS::Class::Object::serialize(JS::Runtime::Debugger * debugger, bool force_output)
{
	std::string address;

	{
		std::ostringstream oss;
		oss << this;
		address = oss.str();
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(auto namespace_ = std::dynamic_pointer_cast<JS::Class::Namespace>(JSObject(this)))
		{
			auto namespace_id = debugger->LookupNamespace(namespace_);
			debugger
				->OpenXMLElement("namespace")
				->AddXMLAttribute("ref", namespace_id)
				->AddXMLAttribute("_address", address)
				->CloseXMLElement();
			return;
		}
	}
#endif

	std::string element_reference;
	if(debugger->AlreadySerialized(JSObject(this), element_reference) && !force_output)
	{
		debugger
			->OpenXMLElement("object")
			->AddXMLAttribute("ref", element_reference)
			->AddXMLAttribute("_address", address);
		auto label = JS::Runtime::Debugger::GetObjectLabel(JSObject(this));
		if(label.has_value())
		{
			debugger->AddXMLAttribute("label", label.value());
		}
		debugger
			->CloseXMLElement();
	}
	else
	{
		debugger
			->OpenXMLElement("object")
			->AddXMLAttribute("_address", address)
			->AddXMLAttribute("id", element_reference);
		auto label = JS::Runtime::Debugger::GetObjectLabel(JSObject(this));
		if(label.has_value())
		{
			debugger->AddXMLAttribute("label", label.value());
		}

		for(auto slot_pair : GetInternalSlots(true))
		{
			debugger
				->OpenXMLElement("slot")
				->AddXMLAttribute("name", slot_pair.first);
			slot_pair.second.serialize(debugger);
			debugger
				->CloseXMLElement();
		}

#if ENABLE_ES4
		if(IsVersionES4())
		{
			serialize_slots(debugger, GetClass());
		}
#endif

		std::map<JS::Class::Object::PropertyKeyData, JSProperty> properties = ExtractAllProperties();

#if ENABLE_ES4
		if(IsVersionES4())
		{
			std::map<JS::Runtime::Debugger::KeyData, JS::Class::Object::PropertyKeyData> property_map;

			for(auto property_pair : properties)
			{
				auto property_data = property_pair.first;

				// find first namespace according to canonical ordering (NamespaceLess)
				JSNamespace first_namespace = nullptr;
				for(auto namespace_ : property_data.Namespaces)
				{
					if(first_namespace == nullptr)
					{
						first_namespace = namespace_;
					}
					else if(JS::Runtime::Debugger::NamespaceLess()(namespace_, first_namespace))
					{
						first_namespace = namespace_;
					}
				}

				Access first_access = property_data.Accesses == AccessSet::Write ? Access::Write : Access::Read;

				property_map[JS::Runtime::Debugger::KeyData{property_data.PropertyKey, first_namespace, first_access}] = property_data;
			}

			for(auto property_pair : property_map)
			{
				auto data = property_pair.second;
				serialize_property(
					debugger,
					data.PropertyKey,
					properties[data]
					WITH_NAMESPACES(data.Namespaces)
					WITH_ACCESS_SET(data.Accesses));
			}
		}
		else
#endif
		{
			// sort keys
			std::set<JSPropertyKey, JS::Runtime::Debugger::PropertyKeyLess> property_keys;
			for(auto property_pair : properties)
			{
#if ENABLE_ES4
				property_keys.insert(property_pair.first.PropertyKey);
#else
				property_keys.insert(property_pair.first);
#endif
			}

			for(auto property_key : property_keys)
			{
#if ENABLE_ES4
				JSProperty property_value = properties[JS::Class::Object::PropertyKeyData{property_key, {JS_public}, AccessSet::ReadWrite}];
#else
				JSProperty property_value = properties[property_key];
#endif

				serialize_property(debugger, property_key, property_value PUBLIC_NAMESPACES READ_WRITE_ACCESS);
			}
		}

		debugger
			->CloseXMLElement();
	}
}
#endif

//// Function

JS::Type::Function::Function(JSValue value)
	: Value((assert(value.GetInvocability() != JS::Invocability::NonCallable), value))
{
}

#if ENABLE_ES4
JS::Type::Function::Function(gcn::gc_ptr<JS::Class::Class> reference) : Value(reference) { }
#endif
JS::Type::Function::Function(JS::Class::Function * reference) : Value(reference) { }
JS::Type::Function::Function(gcn::gc_ptr<JS::Class::Function> reference) : Value(reference) { }
JS::Type::Function::Function(JS::Class::BoundFunction * reference) : Value(reference) { }
JS::Type::Function::Function(gcn::gc_ptr<JS::Runtime::Builtin::BuiltinFunction> reference) : Value(reference) { }
JS::Type::Function::Function(gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> reference) : Value(reference) { }

#if ENABLE_ES4
JS::Type::Function::Function(JSMethodClosure value) : Value(value) { }
#endif

std::optional<JS::Type::Function> JS::Type::Function::GetFunction(JSValue value)
{
	if(value.GetInvocability() == JS::Invocability::NonCallable)
	{
		return {};
	}
	else
	{
		return Function(value);
	}
}

JS::Type::Function JS::Type::Function::MakeFunction(JSValue value)
{
	if(auto function = GetFunction(value))
	{
		return function.value();
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("not callable"));
	}
}

#if ENABLE_ES4
std::optional<JS::Runtime::SignatureType> JS::Type::Function::GetSignature()
{
	return get_content()->GetSignature();
}
#endif


//// OrdinaryObject

JSObject JS::Runtime::Intrinsics::* JS::Class::OrdinaryObject::IntrinsicPrototype = &JS::Runtime::Intrinsics::Object_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::OrdinaryObject::IntrinsicClass = &JS::Runtime::Intrinsics::Object_class;
#endif

/*JS::Class::OrdinaryObject::Object(JSValue Prototype, bool Extensible)
	: Prototype(Prototype), Extensible(Extensible)
{
#if ENABLE_ES4
	if(IsVersionES4() && JS::Intrinsics() != nullptr) Type = JS::Intrinsics()->Object_class;
#endif
}*/

#if ENABLE_ES4
JSType JS::Class::OrdinaryObject::GetType()
{
	return Type;
}
#endif

std::optional<JSProperty> JS::Class::OrdinaryObject::GetOwnProperty(JSPropertyKey name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	auto result = properties.Get(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	if(result.has_value())
	{
		JSProperty property = *result.value();
#if ENABLE_ES4
		property.direct_access = result.value().get();
#endif
		return property;
	}
	else
	{
		return {};
	}
}

bool JS::Class::OrdinaryObject::HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	auto name = assert_property_key(property_key);

#if ENABLE_ES4
	if(IsVersionES4())
	{
		for(Access access : Accesses(accesses))
		{
			if(GetClass()->GetBaseInstanceProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access))))
			{
				return true;
			}
		}

		// TODO: also for Class instances
		auto meta_has_option = GetClass()->GetBaseInstanceProperty(u"has", {JS_meta}, AccessSet::Read);
		if(meta_has_option.has_value())
		{
			if(auto meta_has = dynamic_cast<JSInstanceFunction>(GetClass()->GetDerivedInstanceProperty(u"has", {JS_meta}, AccessSet::Read).value()))
			{
				if(meta_has->InstanceType == JS::Runtime::InstanceFunction::Type::Method)
				{
					if(auto key_string = GetString(name))
					{
						JSValue key;
						if(namespaces.size() == 1 && *namespaces.begin() != JS_public && Supports_E4X()) // TODO: QName should be replaced with Name, but that is only supported in ES4.2008 // TODO: 'a' in b is called with namespaces == {public}
						{
							// called as ID::STR, otherwise there would be at least public and internal in the namespace set
							key = JS::New<JS::Class::QName>((*namespaces.begin())->uri, key_string.value() __ES4_ONLY(, std::optional<JSString>{}, *namespaces.begin()));
						}
						else
						{
							key = key_string.value();
						}
						return JS::ToBoolean(JS::Call(meta_has->Function, this, {key} WITH_PHASE(phase)));
					}
					else
					{
						JS::Throw(JS::New<InternalError> __LOC("setter received non-string"));
					}
				}
			}
		}
	}
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		for(Access access : Accesses(accesses))
		{
			// check read access first then write access
			auto property = GetOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access)));
			if(property.has_value())
			{
				return true;
			}
			if(auto parent = GetNonNullObject(GetPrototypeOf().value_or(JS_undefined)))
			{
				if(parent.value()->HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access) WITH_PHASE(phase))))
				{
					return true;
				}
			}
		}
	}
	else
#endif
	if(CurrentVersion == Version::ES5)
	{
		return GetProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)).has_value();
	}
	else
	{
		auto property = GetOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		if(property.has_value())
		{
			return true;
		}
		if(auto parent = GetNonNullObject(GetPrototypeOf().value_or(JS_undefined)))
		{
			if(parent.value()->HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase)))
			{
				return true;
			}
		}
	}

	return false;
}

std::optional<JSProperty> JS::Class::OrdinaryObject::GetProperty(JSPropertyKey name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	// only used for ECMAScript 5

	// we check if the current object in the prototype chain has the named property
	if(auto property_option = GetOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS))
	{
		return property_option.value();
	}

	// if the property is not found, we recursively look at the prototype object
	// the recursion is required to satisfy the behavior of proxy objects

	auto parent = GetPrototypeOf();
	if(parent.has_value())
	{
		// if this is not ES4, parent.value() must be an Object
		if(auto parent_object = GetNonNullObject(parent.value()))
		{
			return parent_object.value()->GetProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		}
	}

	return {};
}

JSValue JS::Class::OrdinaryObject::Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	auto name = assert_property_key(property_key);

#if ENABLE_ES4
	if(IsVersionES4() && limit != nullptr && JSValue(this) == receiver)
	{
		// we only check instance properties for the first element in the prototype chain
		// first, check if there are matching instance properties for the class, either actual or corresponding to the super class
		auto property_option = limit->GetBaseInstanceProperty(name, namespaces, AccessSet::Read);
		if(property_option.has_value())
		{
			// if we find a name, return it
			auto property = limit->GetDerivedInstanceProperty(name, namespaces, AccessSet::Read).value();
			return property->GetValue(this WITH_PHASE(phase));
		}
		if(limit != GetClass())
		{
			// if the starting class is not the same as the current class, this was a super access, in which case we do not need to look at dynamic properties
			return {};
		}

		// TODO: also for Class instances
		auto meta_get_option = limit->GetBaseInstanceProperty(u"get", {JS_meta}, AccessSet::Read);
		if(meta_get_option.has_value())
		{
			if(auto meta_get = dynamic_cast<JSInstanceFunction>(limit->GetDerivedInstanceProperty(u"get", {JS_meta}, AccessSet::Read).value()))
			{
				if(meta_get->InstanceType == JS::Runtime::InstanceFunction::Type::Method)
				{
					if(auto key_string = GetString(name))
					{
						JSValue key;
						if(namespaces.size() == 1 && Supports_E4X()) // TODO: QName should be replaced with Name, but that is only supported in ES4.2008
						{
							// called as ID::STR, otherwise there would be at least public and internal in the namespace set
							key = JS::New<JS::Class::QName>((*namespaces.begin())->uri, key_string.value() __ES4_ONLY(, std::optional<JSString>{}, *namespaces.begin()));
						}
						else
						{
							key = key_string.value();
						}
						return JS::Call(meta_get->Function, this, {key} WITH_PHASE(phase));
					}
					else
					{
						JS::Throw(JS::New<InternalError> __LOC("getter received non-string"));
					}
				}
			}
		}
	}
#endif

	std::optional<JSProperty> property_option;

#if 0
#if ENABLE_ES4
	JSObject es4_receiver = this;
#endif
#endif

	if(CurrentVersion == Version::ES5)
	{
		// recursively look for a property
		// (note, this would make sense for ES4 as well, however Class instances must have their own GetClassProperty call)
		property_option = GetProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS);
	}
#if 0
#if ENABLE_ES4
	else if(IsVersionES4())
	{
		while(true)
		{
			property_option = es4_receiver->GetOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS);
			if(property_option.has_value())
				break;

			es4_receiver = GetNonNullObject(es4_receiver->GetPrototypeOf().value_or(JS_undefined)).value_or(nullptr);
			if(es4_receiver == nullptr)
				break;
		}
	}
#endif
#endif
	else
	{
		// look for a property here, use recursion if not found
		property_option = GetOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS);
	}

	// we check if the current object in the prototype chain has the named property
	if(property_option.has_value())
	{
		auto property = property_option.value();
		switch(property.PropertyType)
		{
		case JSProperty::Type::Data:
			if(property.IsInitialized())
			{
				return property.GetValue();
			}
			else
			{
				JS::Throw(
#if ENABLE_ES4
					IsVersionES4() ? JS::New<InternalError> __LOC(u"uninitialized dynamic variable") :
#endif
						JS::New<InternalError> __LOC(u"uninitialized data property"));
			}
			break;
#if ENABLE_ES4
		case JSProperty::Type::Variable:
			// variable properties need to be accessed and modified by address
			if(property.direct_access == nullptr)
			{
				JS::Throw(JS::New<UninitializedError> __LOC());
			}
			if(!property.direct_access->IsInitialized())
			{
				property.direct_access->Initialize(JS::Runtime::ExecutionPhase::Run);
			}
			if(!property.direct_access->IsInitialized())
			{
				JS::Throw(JS::New<UninitializedError> __LOC());
			}
			return property.direct_access->GetValue();
#endif
		case JSProperty::Type::Accessor:
			if(CurrentVersion >= Version::ES5)
			{
				if(property.Get != nullptr)
				{
					// ES5 accessor properties get the receiver object as their "this" value
					return property.Get.Call(receiver, std::vector<JSValue>{} WITH_PHASE(phase));
				}
				else
				{
					return JS_undefined;
				}
			}
			// ignore accessors in these versions
			break;
#if ENABLE_ES4
		case JSProperty::Type::Getter:
			if(IsVersionES4())
			{
				// ES4 static getter methods do not get a "this" operand
				return property.Get.Call(JS_undefined, std::vector<JSValue>{} WITH_PHASE(phase));
			}
			// ignore getters in these versions
			break;
		case JSProperty::Type::Setter:
			if(IsVersionES4())
			{
				// ES4 static setter methods cannot occur for a JS::Access::Read lookup
				JS::Throw(JS::New<InternalError> __LOC(u"setter method on read lookup"));
			}
			// ignore setters in these versions
			break;
		case JSProperty::Type::Forbidden:
			if(IsVersionES4())
			{
				JS::Throw(JS::New<InternalError> __LOC(u"property outside region shadowed by inner block"));
			}
			// ignore
			break;
#endif
		}
	}

#if 0
//#if ENABLE_ES4
	if(IsVersionES4() || CurrentVersion == Version::ES5)
//#else
#endif
	if(CurrentVersion == Version::ES5)
//#endif
	{
		// no need for recursion, we already looked at the entire prototype chain
		return JS_undefined;
	}

	// if the property is not found, we recursively look at the prototype object
	// the recursion is required to satisfy the behavior of proxy objects

	auto parent = GetPrototypeOf();
	if(parent.has_value())
	{
		// if this is not ES4, parent.value() must be an Object

		// TODO: the ES4.2003 spec does not look at instance properties of the prototype object (WITH_CLASS(nullptr))
		// this is incompatible with earlier versions (for example, the Array.length property is now an instance variable)
		// and has been changed in ES4.2008, so it is likely a bug or at least an unintended breakage
		return parent.value().Get(property_key, receiver, intent WITH_NAMESPACES(namespaces) WITH_CLASS(parent.value().GetClass()) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
	}

	return JS_undefined;
}

bool JS::Class::OrdinaryObject::Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put)
{
	auto name = assert_property_key(property_key);

	// ES1-3 and ES5 first verify if this property can actually be assigned

	if(CurrentVersion <= Version::ES3)
	{
		auto property_option = GetOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS);

		// we check if the current object in the prototype chain has the named property
		if(property_option.has_value() && property_option.value().PropertyType == JSProperty::Type::Data)
		{
			// ES1-3 only recognize data accessors
			// a read-only property can block creating a new property by writing
			if(!property_option.value().IsWritable())
			{
				return false;
			}
		}
		else
		{
			// check if we can put a value in the parent
			auto parent = GetPrototypeOf();
			if(parent.has_value())
			{
				if(!parent.value().Set(name, value, receiver WITH_NAMESPACES(namespaces) WITH_CLASS(nullptr) NO_ENVIRONMENT, /* check_can_put = */ true))
				{
					return false;
				}
			}
		}

		if(check_can_put)
		{
			return true;
		}

		if(property_option.has_value() && property_option.value().PropertyType == JSProperty::Type::Data)
		{
			// the property exists and it is writable, so we set the value
			return DefineOwnProperty(name, JSPropertyDescriptor().withValue(value) WITH_NAMESPACES(namespaces) READ_WRITE_ACCESS);
		}
		else
		{
			// no property has been found, so a new property will be created
			return DefineOwnProperty(name, JSPropertyDescriptor().withValue(value).withWritable(true).withEnumerable(true).withConfigurable(true) WITH_NAMESPACES(namespaces) READ_WRITE_ACCESS);
		}
	}
#if ENABLE_ES4
	else if(IsVersionES4())
	{
		if(limit != nullptr && JSValue(this) == receiver)
		{
			// we only check instance properties for the first element in the prototype chain
			// first, check if there are matching instance properties for the class, either actual or corresponding to the super class
			auto property_option = limit->GetBaseInstanceProperty(name, namespaces, AccessSet::Write);
			if(property_option.has_value())
			{
				// if we find a name, assign to it
				auto property = limit->GetDerivedInstanceProperty(name, namespaces, AccessSet::Write).value();
				if(!check_can_put) // not needed
				{
					property->SetValue(this, value);
				}
				return true;
			}
			if(limit != GetClass())
			{
				// if the starting class is not the same as the current class, this was a super access, in which case we do not need to look at dynamic properties
				return false;
			}

		// TODO: also for Class instances
			auto meta_set_option = limit->GetBaseInstanceProperty(u"set", {JS_meta}, AccessSet::Read);
			if(meta_set_option.has_value())
			{
				if(auto meta_set = dynamic_cast<JSInstanceFunction>(limit->GetDerivedInstanceProperty(u"set", {JS_meta}, AccessSet::Read).value()))
				{
					if(meta_set->InstanceType == JS::Runtime::InstanceFunction::Type::Method)
					{
						if(auto key_string = GetString(name))
						{
							JSValue key;
							if(namespaces.size() == 1 && Supports_E4X()) // TODO: QName should be replaced with Name, but that is only supported in ES4.2008
							{
								// called as ID::STR, otherwise there would be at least public and internal in the namespace set
								key = JS::New<JS::Class::QName>((*namespaces.begin())->uri, key_string.value() __ES4_ONLY(, std::optional<JSString>{}, *namespaces.begin()));
							}
							else
							{
								key = key_string.value();
							}
							JS::Call(meta_set->Function, this, {key, value} RUNTIME_PHASE);
							return true; // TODO: what should we return here?
						}
						else
						{
							JS::Throw(JS::New<InternalError> __LOC("setter received non-string"));
						}
					}
				}
			}
		}

		// we look at the own properties of the current object
		auto property_option = GetOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS);
		if(property_option.has_value())
		{
			auto property = property_option.value();
			switch(property.PropertyType)
			{
			case JSProperty::Type::Data:
				if(!property.IsWritable())
				{
					return false;
				}
				if(check_can_put) // not needed
				{
					return true;
				}
				// the property exists and it is writable, so we set the value
				return DefineOwnProperty(name, JSPropertyDescriptor().withValue(value) WITH_NAMESPACES(namespaces) READ_WRITE_ACCESS);
			case JSProperty::Type::Variable:
				if(!property.IsWritable())
				{
					JS::Throw(JS::New<ReferenceError> __LOC());
				}
				if(check_can_put) // not needed
				{
					return true;
				}
				// variable properties need to be accessed and modified by address
				if(property.direct_access == nullptr)
				{
					JS::Throw(JS::New<UninitializedError> __LOC());
				}
				property.direct_access->SetValue(value);
				return true;
			case JSProperty::Type::Accessor:
				// Note: this should not occur, but other versions would return false in a similar situation
				return false;
			case JSProperty::Type::Getter:
				// ES4 static getter methods cannot occur for a JS::Access::Write lookup
				JS::Throw(JS::New<InternalError> __LOC("getter method on write lookup"));
			case JSProperty::Type::Setter:
				// ES4 static setter methods do not get a "this" operand
				JS::Call(property.Set, JS_undefined, {value} RUNTIME_PHASE);
				return true;
			case JSProperty::Type::Forbidden:
				JS::Throw(JS::New<InternalError> __LOC(u"property outside region shadowed by inner block"));
			}
		}

		// no property has been found, so a new property will be created

		// this is only possible if the public namespace is included
		if(namespaces.find(JS_public) == namespaces.end())
		{
			return false;
		}

		// unlike other versions, we do not check to see if the prototype objects has a read-only member with the same name
		// instead, we must check if there are no names already present with read access
		if(limit->GetBaseInstanceProperty(name, {JS_public}, AccessSet::Read)
		|| GetOwnProperty(name, {JS_public}, AccessSet::Read))
		{
			return false;
		}

		if(check_can_put) // not needed
		{
			return true;
		}

		return DefineOwnProperty(name, JSPropertyDescriptor().withValue(value).withWritable(true).withEnumerable(true).withConfigurable(true) WITH_NAMESPACES(namespaces) READ_WRITE_ACCESS);
	}
#endif
	else if(CurrentVersion == Version::ES5)
	{
		// first we look at the properties of the current object in the prototype chain
		auto property_option = GetOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS);

		// we check if the current object in the prototype chain has the named property
		if(property_option.has_value())
		{
			auto property = property_option.value();
			switch(property.PropertyType)
			{
			case JSProperty::Type::Data:
				// a read-only property can block creating a new property by writing
				if(!property.IsWritable())
				{
					return false;
				}
				// writing the property must be done on the receiver object, which is handled later
				break;
			case JSProperty::Type::Accessor:
				if(property.Set == nullptr)
				{
					return false;
				}
				else if(check_can_put)
				{
					return true;
				}
				// only apply accessor later
				break;
#if ENABLE_ES4
			default:
				// ignore
				break;
#endif
			}
		}
		else
		{
			// first we will check the entire prototype chain

			auto parent = GetPrototypeOf();
			if(parent.has_value())
			{
				auto parent_object = GetNonNullObject(parent.value());
				if(!parent_object.has_value())
				{
					JS::Throw(JS::New<InternalError> __LOC("primitive type found in prototype chain"));
				}

				if(auto parent_property_option = parent_object.value()->GetProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS))
				{
					switch(parent_property_option.value().PropertyType)
					{
					case JSProperty::Type::Data:
						if(!parent_property_option.value().IsWritable())
						{
							return false;
						}
						break;
					case JSProperty::Type::Accessor:
						if(parent_property_option.value().Set == nullptr)
						{
							return false;
						}
						else if(check_can_put)
						{
							return true;
						}
						break;
#if ENABLE_ES4
					default:
						// ignore
						break;
#endif
					}
				}
			}
		}

		// ES5 checks extensibility here, ES6+ checks it during the DefineOwnProperty call
		if(!Extensible)
		{
			return false;
		}

		// these versions have a CanPut property
		if(check_can_put)
		{
			return true;
		}

		// at this point, no member of the prototype chain recognizes this property name
		// we now look at the actual receiver object and see what type of assignment is possible

		auto receiver_object_option = GetNonNullObject(receiver);
		if(!receiver_object_option.has_value())
		{
			// the receiver is a primitive type, no assignment is possible
			// this branch can occur if the receiver was converted to an Object before calling the [[Set]] method
			// note that this circumstance is only meaningful in ECMAScript 5 and later when accessor properties were introduced

			// ECMAScript 4 will also not end up here since the setter for primitive objects is defined in subclasses of PrimitiveValue
			return false;
		}

		if(property_option.has_value() && property_option.value().PropertyType == JSProperty::Type::Data)
		{
			// ES5 checks the own property here again, and assigns to it if it exists
			return DefineOwnProperty(name, JSPropertyDescriptor().withValue(value) WITH_NAMESPACES(namespaces) READ_WRITE_ACCESS);
		}

		property_option = GetProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS);
		if(property_option.has_value() && property_option.value().PropertyType == JSProperty::Type::Accessor)
		{
			auto set = property_option.value().Set;
			assert(set != nullptr);
			JS::Call(set, receiver, {value} RUNTIME_PHASE);
			return true;
		}

		// no property has been found, so a new property will be created

		return DefineOwnProperty(name, JSPropertyDescriptor().withValue(value).withWritable(true).withEnumerable(true).withConfigurable(true) WITH_NAMESPACES(namespaces) READ_WRITE_ACCESS);
	}
	else
	{
		// first we look at the properties of the current object in the prototype chain
		auto property_option = GetOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS);

		// we check if the current object in the prototype chain has the named property
		if(property_option.has_value())
		{
			auto property = property_option.value();
			switch(property.PropertyType)
			{
			case JSProperty::Type::Data:
				// a read-only property can block creating a new property by writing
				if(!property.IsWritable())
				{
					return false;
				}
				// writing the property must be done on the receiver object, which is handled later
				break;
			case JSProperty::Type::Accessor:
				if(property.Set != nullptr)
				{
					if(!check_can_put) // technically this condition is not used
					{
						// ES5 accessor properties get the receiver object as their "this" value
						JS::Call(property.Set, receiver, {value} RUNTIME_PHASE);
					}
					// only apply accessor later
					return true;
				}
				else
				{
					return false;
				}
#if ENABLE_ES4
			default:
				// ignore
				break;
#endif
			}
		}
		else
		{
			// if the property is not found, we recursively look at the prototype object
			// the recursion is required to satisfy the behavior of proxy objects
			// this is handled differently for ECMAScript 5

			auto parent = GetPrototypeOf();
			if(parent.has_value())
			{
				// needs to use parent Set call to make sure proxy objects work
				// if this is not ES4, parent.value() must be an Object
				return parent.value().Set(name, value, receiver WITH_NAMESPACES(namespaces) WITH_CLASS(nullptr) NO_ENVIRONMENT, check_can_put);
			}
		}

		// at this point, no member of the prototype chain recognizes this property name
		// we now look at the actual receiver object and see what type of assignment is possible

		auto receiver_object_option = GetNonNullObject(receiver);
		if(!receiver_object_option.has_value())
		{
			// the receiver is a primitive type, no assignment is possible
			// this branch can occur if the receiver was converted to an Object before calling the [[Set]] method
			// note that this circumstance is only meaningful in ECMAScript 5 and later when accessor properties were introduced

			// ECMAScript 4 will also not end up here since the setter for primitive objects is defined in subclasses of PrimitiveValue
			return false;
		}
		auto receiver_object = receiver_object_option.value();

		// obtain the actual property, if there is one
		if(JSValue(this) != receiver)
		{
			property_option = receiver_object->GetOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS);
		}

		if(property_option.has_value())
		{
			auto property = property_option.value();
			switch(property.PropertyType)
			{
			case JSProperty::Type::Data:
				if(!property.IsWritable())
				{
					return false;
				}
				if(check_can_put) // not needed
				{
					return true;
				}
				// the property exists and it is writable, so we set the value
				return receiver_object->DefineOwnProperty(name, JSPropertyDescriptor().withValue(value) WITH_NAMESPACES(namespaces) READ_WRITE_ACCESS);
			case JSProperty::Type::Accessor:
				// it is not possible to reassign an accessor property to a data property

				// note that this branch will only be taken once we have already looked at all the elements of the prototype chain
				// and we did not find the named property
				// this is only possible in a super call if the current variable already has an accessor member with this name
				return false;
#if ENABLE_ES4
			default:
				// ignore
				break;
#endif
			}
		}

		if(check_can_put) // not needed
		{
			return true;
		}

		// no property has been found, so a new property will be created

		return receiver_object->DefineOwnProperty(name, JSPropertyDescriptor().withValue(value).withWritable(true).withEnumerable(true).withConfigurable(true) WITH_NAMESPACES(namespaces) READ_WRITE_ACCESS);
	}
}

JSValue JS::Class::OrdinaryObject::GetPrivate(JSPrivateName name)
{
	auto it = PrivateElements.find(name);
	if(it == PrivateElements.end())
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
	switch(it->second.Kind)
	{
	case JSPrivateElement::ElementKind::Field:
	case JSPrivateElement::ElementKind::Method:
		return it->second.Value.value();
	case JSPrivateElement::ElementKind::Accessor:
		// TODO
		return it->second.Get.value();
	}
	JS::Throw(JS::New<InternalError> __LOC());
}

void JS::Class::OrdinaryObject::SetPrivate(JSPrivateName name, JSValue value)
{
	auto it = PrivateElements.find(name);
	if(it == PrivateElements.end())
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
	switch(it->second.Kind)
	{
	case JSPrivateElement::ElementKind::Field:
		it->second.Value = value;
		break;
	case JSPrivateElement::ElementKind::Method:
		JS::Throw(JS::New<TypeError> __LOC());
	case JSPrivateElement::ElementKind::Accessor:
		if(!it->second.Set.has_value())
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
		JS::Call(it->second.Set.value(), this, {value} RUNTIME_PHASE);
		break;
	default:
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

bool JS::Class::OrdinaryObject::Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
	auto name = assert_property_key(property_key);

#if ENABLE_ES4
	if(IsVersionES4() && limit != nullptr)
	{
		if(limit->GetBaseInstanceProperty(name, namespaces, AccessSet::Write))
		{
			return false;
		}
		if(limit != GetClass())
		{
			return false;
		}

		// TODO: also for Class instances
		auto meta_delete_option = GetClass()->GetBaseInstanceProperty(u"delete", {JS_meta}, AccessSet::Read);
		if(meta_delete_option.has_value())
		{
			if(auto meta_delete = dynamic_cast<JSInstanceFunction>(limit->GetDerivedInstanceProperty(u"delete", {JS_meta}, AccessSet::Read).value()))
			{
				if(meta_delete->InstanceType == JS::Runtime::InstanceFunction::Type::Method)
				{
					if(auto key_string = GetString(name))
					{
						JSValue key;
						if(namespaces.size() == 1 && Supports_E4X()) // TODO: QName should be replaced with Name, but that is only supported in ES4.2008
						{
							// called as ID::STR, otherwise there would be at least public and internal in the namespace set
							key = JS::New<JS::Class::QName>((*namespaces.begin())->uri, key_string.value() __ES4_ONLY(, std::optional<JSString>{}, *namespaces.begin()));
						}
						else
						{
							key = key_string.value();
						}
						return JS::ToBoolean(JS::Call(meta_delete->Function, this, {key} RUNTIME_PHASE));
					}
					else
					{
						JS::Throw(JS::New<InternalError> __LOC("setter received non-string"));
					}
				}
			}
		}
	}
#endif

	if(auto property_option = GetOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS))
	{
		auto property = property_option.value();
		if(property.Configurable)
		{
			// remove it from the bindings
			properties.Delete(name WITH_NAMESPACES(namespaces));

			// remove it from the property name list
			std::visit([this WITH_NAMESPACES(&namespaces)](auto&& value)
			{
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, JSString>)
				{
#if ENABLE_ES4
					if(!IsVersionES4())
					{
						namespaces = {JS_public};
					}
					if(namespaces.find(JS_public) != namespaces.end())
					{
						uint32_t index;
						if(is_array_index(value, index))
						{
							integer_index_properties.erase(index);
							return;
						}
						string_properties.erase(std::find(string_properties.begin(), string_properties.end(), value));
					}
#else
					uint32_t index;
					if(is_array_index(value, index))
					{
						integer_index_properties.erase(index);
					}
					else
					{
						string_properties.erase(std::find(string_properties.begin(), string_properties.end(), value));
					}
#endif
				}
				else if constexpr (std::is_same_v<T, JSSymbol>)
				{
					symbol_properties.erase(std::find(symbol_properties.begin(), symbol_properties.end(), value));
				}
				else
				{
					static_assert(false);
				}
			}, name);
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		return true;
	}
}

#if ENABLE_ES4
bool JS::Class::OrdinaryObject::Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	JSPropertyDescriptor descriptor;
	JS::AccessSet actual_accesses = JS::AccessSet::ReadWrite;

	switch(property.PropertyType)
	{
	case JSProperty::Type::Variable:
		actual_accesses = JS::AccessSet::ReadWrite;
		break;
	case JSProperty::Type::Getter:
		actual_accesses = JS::AccessSet::Read;
		break;
	case JSProperty::Type::Setter:
		actual_accesses = JS::AccessSet::Write;
		break;
	default:
		break;
	}

	switch(property.PropertyType)
	{
	case JSProperty::Type::Variable:
	case JSProperty::Type::Getter:
	case JSProperty::Type::Setter:
//	case JSProperty::Type::Alias:
		if(GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(actual_accesses)))
		{
			return false;
		}

		AddPropertyKey(property_key WITH_NAMESPACES(namespaces));
		properties.Set(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), property);
		return true;
	default:
		return JS::Class::Object::Define(property_key, property WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
}
#endif

void JS::Class::OrdinaryObject::AddPropertyKey(JSPropertyKey property_key AND_NAMESPACES(namespaces))
{
	std::visit([this WITH_NAMESPACES(&namespaces)](auto&& value)
	{
		using T = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<T, JSString>)
		{
#if ENABLE_ES4
			if(!IsVersionES4())
			{
				namespaces = {JS_public};
			}
			if(namespaces.find(JS_public) != namespaces.end())
			{
				uint32_t index;
				if(is_array_index(value, index))
				{
					integer_index_properties.insert(index);
					return;
				}
				string_properties.push_back(value);
			}
#else
			uint32_t index;
			if(is_array_index(value, index))
			{
				integer_index_properties.insert(index);
			}
			else
			{
				string_properties.push_back(value);
			}
#endif
		}
		else if constexpr (std::is_same_v<T, JSSymbol>)
		{
			symbol_properties.push_back(value);
		}
		else
		{
			static_assert(false);
		}
	}, property_key);
}

bool JS::Class::OrdinaryObject::ValidatePropertyDescriptor(bool extensible, JSPropertyDescriptor descriptor, std::optional<JSProperty> current_property)
{
	if(current_property.has_value())
	{
		bool configurable = current_property.value().Configurable;

		if(descriptor.Configurable)
		{
			if(!configurable)
			{
				if(descriptor.Configurable.value())
				{
					return false;
				}
			}
		}

		if(descriptor.Enumerable)
		{
			if(!configurable)
			{
				if(current_property.value().Enumerable != descriptor.Enumerable.value())
				{
					return false;
				}
			}
		}

		if(descriptor.is_data_descriptor())
		{
			if(current_property.value().PropertyType == JSProperty::Type::Accessor)
			{
				if(!configurable)
				{
					return false;
				}
			}
			else
			{
				bool writable = current_property.value().IsWritable();

				if(descriptor.Writable)
				{
					if(!configurable && !writable)
					{
						return false;
					}
				}

				if(descriptor.Value)
				{
					if(!configurable)
					{
						if(!writable)
						{
							if(!JS::SameValue(current_property.value().GetValue(), descriptor.Value.value()))
							{
								return false;
							}
						}
					}
				}
			}
		}
		else if(descriptor.is_accessor_descriptor())
		{
			if(current_property.value().PropertyType != JSProperty::Type::Accessor)
			{
				if(!configurable)
				{
					return false;
				}
			}
			else
			{
				if(descriptor.Get)
				{
					if(!configurable)
					{
						if(!JS::SameValue(current_property.value().Get, descriptor.Get.value()))
						{
							return false;
						}
					}
				}

				if(descriptor.Set)
				{
					if(!configurable)
					{
						if(!JS::SameValue(current_property.value().Set, descriptor.Set.value()))
						{
							return false;
						}
					}
				}
			}
		}

		return true;
	}
	else
	{
		return extensible;
	}
}

std::optional<JSProperty> JS::Class::OrdinaryObject::ValidateAndApplyPropertyDescriptor(bool extensible, JSPropertyDescriptor descriptor, std::optional<JSProperty> current_property_option)
{
	if(current_property_option.has_value())
	{
		auto current_property = current_property_option.value();
		bool configurable = current_property.Configurable;

		if(descriptor.Configurable)
		{
			if(!configurable)
			{
				if(descriptor.Configurable.value())
				{
					return {};
				}
			}
			else
			{
				current_property.Configurable = descriptor.Configurable.value();
			}
		}

		if(descriptor.Enumerable)
		{
			if(!configurable)
			{
				if(current_property.Enumerable != descriptor.Enumerable.value())
				{
					return {};
				}
			}
			else
			{
				current_property.Enumerable = descriptor.Enumerable.value();
			}
		}

		if(descriptor.is_data_descriptor())
		{
			if(current_property.PropertyType == JSProperty::Type::Accessor)
			{
				if(!configurable)
				{
					return {};
				}
				else
				{
					current_property.ConvertToData();
				}

				if(descriptor.Writable)
				{
					current_property.SetWritable(descriptor.Writable.value());
				}

				if(descriptor.Value)
				{
					current_property.SetValue(descriptor.Value.value()); // modifies internal property immediately
				}
			}
			else
			{
				bool writable = current_property.IsWritable();

				if(descriptor.Writable)
				{
					if(!configurable && !writable)
					{
						return {};
					}

					current_property.SetWritable(descriptor.Writable.value());
				}

				if(descriptor.Value)
				{
					if(!configurable)
					{
						if(!writable)
						{
							if(!JS::SameValue(current_property.GetValue(), descriptor.Value.value()))
							{
								return {};
							}
						}
					}

					current_property.SetValue(descriptor.Value.value()); // modifies internal property immediately
				}
			}
		}
		else if(descriptor.is_accessor_descriptor())
		{
			if(current_property.PropertyType != JSProperty::Type::Accessor)
			{
				if(!configurable)
				{
					return {};
				}
				else
				{
					current_property.ConvertToAccessor();
				}

				if(descriptor.Get)
				{
					if(descriptor.Get.value() == nullptr)
					{
						current_property.Get = {};
					}
					else
					{
						current_property.Get = descriptor.Get.value();
					}
				}

				if(descriptor.Set)
				{
					if(descriptor.Set.value() == nullptr)
					{
						current_property.Set = {};
					}
					else
					{
						current_property.Set = descriptor.Set.value();
					}
				}
			}
			else
			{
				if(descriptor.Get)
				{
					if(!configurable)
					{
						if(!JS::SameValue(current_property.Get, descriptor.Get.value()))
						{
							return {};
						}
					}
					else
					{
						current_property.Get = descriptor.Get.value();
					}
				}

				if(descriptor.Set)
				{
					if(!configurable)
					{
						if(!JS::SameValue(current_property.Set, descriptor.Set.value()))
						{
							return {};
						}
					}
					else
					{
						if(descriptor.Set)
						{
							current_property.Set = descriptor.Set.value();
						}
					}
				}
			}
		}

		return current_property;
	}
	else
	{
		if(!extensible)
		{
			return {};
		}
		else
		{
			return JSProperty(descriptor);
		}
	}
}

bool JS::Class::OrdinaryObject::DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	auto current_property = GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));

	auto updated_property = ValidateAndApplyPropertyDescriptor(IsExtensible(), descriptor, current_property);

	if(updated_property.has_value())
	{
		if(!current_property.has_value())
		{
			AddPropertyKey(property_key WITH_NAMESPACES(namespaces));
		}

		properties.Set(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), updated_property.value());

		return true;
	}
	else
	{
		return false;
	}
}

#if ENABLE_ES4
void JS::Class::OrdinaryObject::add_slot(JSClass class_, JSString property_key, std::set<JSNamespace> namespaces, JSValue value)
{
	dynamic_cast<JSInstanceVariable>(class_->GetDerivedInstanceProperty(property_key, namespaces READ_WRITE_ACCESS).value())->InitValue(this, value);
}
#endif

std::vector<JSPropertyKey> JS::Class::OrdinaryObject::OwnPropertyKeys(FOR_CLASS(limit))
{
	std::vector<JSPropertyKey> property_keys;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(limit != nullptr)
		{
			if(limit->Super.has_value())
			{
				property_keys = OwnPropertyKeys(GetClassForType(limit->Super.value()));
			}

			property_keys.insert(property_keys.end(), limit->instance_member_names.begin(), limit->instance_member_names.end());
		}

		if(limit != GetClass())
		{
			return property_keys;
		}
	}
#endif
	for(auto integer_index_property : integer_index_properties)
	{
		property_keys.push_back(JS::ToString(JSNumber(integer_index_property) RUNTIME_PHASE));
	}
	for(auto string_property : string_properties)
	{
		property_keys.push_back(string_property);
	}
	for(auto symbol_property : symbol_properties)
	{
		property_keys.push_back(symbol_property);
	}
	return property_keys;
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::OrdinaryObject::ExtractAllProperties()
{
	std::map<PropertyKeyData, JSProperty> result;
	for(auto iterator = properties.begin(); iterator != properties.end(); ++iterator)
	{
#if ENABLE_ES4
		PropertyKeyData data;
		data.PropertyKey = iterator.get_name();
		if(IsVersionES4())
		{
			bool read = false, write = false;
			for(auto pair : iterator.get_access())
			{
				data.Namespaces.insert(std::get<0>(pair));
				switch(std::get<1>(pair))
				{
				case Access::Read:
					read = true;
					break;
				case Access::Write:
					write = true;
					break;
				}
			}

			if(!write)
			{
				data.Accesses = AccessSet::Read;
			}
			else if(!read)
			{
				data.Accesses = AccessSet::Write;
			}
			else
			{
				data.Accesses = AccessSet::ReadWrite;
			}
		}
		else
		{
			data.Namespaces = {JS_public};
			data.Accesses = AccessSet::ReadWrite;
		}
		result[data] = **iterator;
#else
		result[iterator.get_name()] = **iterator;
#endif
	}
	return result;
}

#if ENABLE_ES4
std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> JS::Class::OrdinaryObject::FindSlot(JSInstanceVariable id)
{
	auto iter = Slots.find(id);
	if(iter != Slots.end())
	{
		return std::make_optional(iter);
	}
	else
	{
		return {};
	}
}
#endif

JSString JS::Class::OrdinaryObject::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"Object";
}

std::optional<JSValue> JS::Class::OrdinaryObject::GetPrototypeOf()
{
	return Prototype;
}

bool JS::Class::OrdinaryObject::SetPrototypeOf(JSValue new_prototype)
{
#if ENABLE_ES4
	if(!IsVersionES4() && !GetObjectOrNull(new_prototype))
#else
	if(!GetObjectOrNull(new_prototype))
#endif
	{
		JS::Throw(JS::New<InternalError> __LOC("prototype must be object or null"));
	}

	if(JS::SameValue(Prototype.value_or(JS_null), new_prototype))
	{
		return true;
	}
	else if(!Extensible)
	{
		return false;
	}

	// ES4 has no setting the prototype, so we can check only Object prototypes
	for(JSObject link = GetNonNullObject(new_prototype).value_or(nullptr); link != nullptr; link = GetNonNullObject(link->GetPrototypeOf().value_or(JS_undefined)).value_or(nullptr))
	{
		if(JS::SameValue(this, link))
		{
			// circular prototype chain
			return false;
		}

		// the specification requires link->GetPrototypeOf to return its Class::OrdinaryObject::Prototype slot, this is as close as we can get
		if(!std::dynamic_pointer_cast<Object>(link))
		{
			break;
		}
	}

#if ENABLE_ES4
	if(!IsVersionES4() && IsNull(new_prototype))
#else
	if(IsNull(new_prototype))
#endif
	{
		Prototype = {};
	}
	else
	{
		Prototype = new_prototype;
	}
	return true;
}

bool JS::Class::OrdinaryObject::IsExtensible()
{
	return Extensible;
}

bool JS::Class::OrdinaryObject::PreventExtensions()
{
	Extensible = false;
	return true;
}

std::map<JSString, JSValue> JS::Class::OrdinaryObject::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots;
#if ENABLE_ES4
	if(IsVersionES4() && !unified && Prototype.has_value())
	{
		slots[u"Archetype"] = Prototype.value();
	}
	else if(!IsVersionES4() || Prototype.has_value())
#endif
	{
		slots[u"Prototype"] = Prototype.value_or(JS_null);
	}
#if ENABLE_ES4
	if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
	if(CurrentVersion < Version::ES6)
#endif
	{
		slots[u"Class"] = GetClassSlot();
	}
	if(unified || CurrentVersion >= Version::ES5)
	{
		slots[u"Extensible"] = Extensible;
	}
#if ENABLE_ES4
	else if(IsVersionES4())
	{
		slots[u"Sealed"] = !Extensible;
	}
#endif
#if ENABLE_ES4
	if(IsVersionES4())
	{
		bool skip_type_slot = false;
		auto type = GetType();
		if(auto class_indirect_pointer = std::get_if<JSClass>(&type))
		{
			if(*class_indirect_pointer == nullptr)
			{
				skip_type_slot = true; // TODO: this should not happen
			}
		}
		if(!skip_type_slot)
		{
			slots[u"Type"] = JSValue(GetType());
		}
	}
#endif
	// TODO: move to XML?
	/*if(Supports_E4X() && DefaultNamespace.has_value())
	{
		slots[u"DefaultNamespace"] = DefaultNamespace.value();
	}*/
	return slots;
}

JS::Invocability JS::Class::OrdinaryObject::GetInvocability()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		// TODO: also for Class instances
		if(auto limit = GetClass())
		{
			auto meta_invoke_option = limit->GetBaseInstanceProperty(u"invoke", {JS_meta}, AccessSet::Read);
			if(meta_invoke_option.has_value())
			{
				return Invocability::NonConstructor;
			}
		}
	}
#endif

	return Invocability::NonCallable;
}

JSValue JS::Class::OrdinaryObject::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		// TODO: also for Class instances
		if(auto limit = GetClass())
		{
			auto meta_invoke_option = limit->GetBaseInstanceProperty(u"invoke", {JS_meta}, AccessSet::Read);
			if(meta_invoke_option.has_value())
			{
				if(auto meta_invoke = dynamic_cast<JSInstanceFunction>(limit->GetDerivedInstanceProperty(u"invoke", {JS_meta}, AccessSet::Read).value()))
				{
					if(meta_invoke->InstanceType == JS::Runtime::InstanceFunction::Type::Method)
					{
						return JS::Call(meta_invoke->Function, this, arguments WITH_PHASE(phase));
					}
				}
			}
		}
	}
#endif

	JS::Throw(JS::New<TypeError> __LOC("not callable"));
}

JSValue JS::Class::OrdinaryObject::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC("not constructable"));
}

//// ImmutablePrototypeObject

bool JS::Class::ImmutablePrototypeObject::SetPrototypeOf(JSValue new_prototype)
{
	return JS::SameValue(GetPrototypeOf().value_or(nullptr), new_prototype);
}

//// Class

#if ENABLE_ES4
JSObject JS::Runtime::Intrinsics::* JS::Class::Class::IntrinsicPrototype = &JS::Runtime::Intrinsics::Class_prototype;
JSClass JS::Runtime::Intrinsics::* JS::Class::Class::IntrinsicClass = &JS::Runtime::Intrinsics::Class_class;

JS::Class::Class::Class(JSString name, std::optional<JSType> super, std::optional<JSValue> prototype, bool dynamic, bool final, JS::Runtime::Realm * realm)
	: Name(name), Super(super), ClassPrototype(prototype), Dynamic(dynamic), Final(final)
{
	assert(!super.has_value() || JS::GetClassForType(super.value()) != nullptr);
	Realm = realm != nullptr ? realm : GetCurrentRealm();
}

std::optional<JS::Runtime::SignatureType> JS::Class::Class::GetSignature()
{
	return {};
}

JS::Runtime::Realm * JS::Class::Class::GetFunctionRealm()
{
	return Realm;
}

JSString JS::Class::Class::GetClassString()
{
	JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
}

JS::Invocability JS::Class::Class::GetInvocability()
{
	return Invocability::Constructor;
}

JSValue JS::Class::Class::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	if(arguments.size() != 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else
	{
		auto result = Coerce(arguments[0]);
		if(!result.has_value())
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
		return result.value();
	}
}

bool JS::Class::Class::Begin_Call(JSValue this_argument, std::vector<JSValue> arguments)
{
	// simulate creation and execution of a new execution context by pushing the result onto the stack
	GetCurrentExecutionContext()->Action_Push<JSValue>(Call(this_argument, arguments RUNTIME_PHASE));
	return false; // call not deferred
}

void JS::Class::Class::CreateInstanceFields(gcn::gc_ptr<JS::Class::OrdinaryObject> this_argument)
{
	if(Super.has_value())
	{
		GetClassForType(Super.value())->CreateInstanceFields(this_argument);
	}

	for(auto instance_member_iter = InstanceMembers.begin();
		instance_member_iter != InstanceMembers.end();
		++instance_member_iter)
	{
		if(auto instance_variable = dynamic_cast<JSInstanceVariable>(**instance_member_iter))
		{
			if(this_argument->Slots.find(instance_variable) == this_argument->Slots.end())
			{
				// some built-in constructors already assign to these fields, we must not overwrite them
				this_argument->Slots[instance_variable] = instance_variable->DefaultValue;
			}

			std::set<JSNamespace> namespaces;
			for(auto namespace_access_set : instance_member_iter.get_access())
			{
				namespaces.insert(std::get<0>(namespace_access_set));
			}
			//this_argument->AddPropertyKey(instance_member_iter.get_name(), namespaces);
		}
	}
}

JSValue JS::Class::Class::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(phase == JS::Runtime::ExecutionPhase::Compile)
	{
		JS::Throw(JS::New<ConstantError> __LOC("custom class constructor call at compile time"));
	}

	return ConstructInstanceOf<JS::Class::OrdinaryObject>(arguments);
}

void JS::Class::Class::Init(JSValue this_argument, std::vector<JSValue> arguments)
{
	if(Constructor != nullptr)
	{
		// If a call to the constructor of the superclass is necessary, it will happen right after Block::SetupContent

		Constructor.Call(this_argument, arguments RUNTIME_PHASE);
	}
	else if(Super.has_value())
	{
		GetClassForType(Super.value())->Init(this_argument, arguments);
	}
	else
	{
		// trivial initializer

		if(arguments.size() != 0)
		{
			JS::Throw(JS::New<ArgumentError> __LOC());
		}
		// do nothing
	}
}

std::optional<std::variant<JSProperty, JSInstanceVariable>> JS::Class::Class::GetClassOwnProperty(JSPropertyKey name, std::set<JSNamespace> namespaces, JS::AccessSet accesses)
{
	auto property = OrdinaryObject::GetOwnProperty(name, namespaces, accesses);
	if(property.has_value())
	{
		return property.value();
	}
	else
	{
		auto property = GetInstanceProperty(name, namespaces, accesses);
		if(property.has_value())
		{
			if(auto variable = dynamic_cast<JSInstanceVariable>(property.value()))
			{
				return variable;
			}
		}
		if(Super.has_value())
		{
			return GetClassForType(Super.value())->GetClassOwnProperty(name, namespaces, accesses);
		}
		else
		{
			return {};
		}
	}
}

std::optional<JSProperty> JS::Class::Class::GetOwnProperty(JSPropertyKey name, std::set<JSNamespace> namespaces, JS::AccessSet accesses)
{
	auto property = GetClassOwnProperty(name, namespaces, accesses);
	if(property.has_value())
	{
		return std::visit([](auto&& property) -> std::optional<JSProperty>
		{
			using T = std::decay_t<decltype(property)>;
			if constexpr(std::is_same_v<T, JSProperty>)
			{
				return property;
			}
			else if constexpr(std::is_same_v<T, JSInstanceVariable>)
			{
				JS::Throw(JS::New<ReferenceError> __LOC("cannot read instance property without supplying an instance"));
			}
			else
			{
				static_assert(false);
			}
		}, property.value());
	}
	else
	{
		return {};
	}
}

std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> JS::Class::Class::FindSlot([[maybe_unused]] JSInstanceVariable id)
{
	return {};
}

bool JS::Class::Class::SetPrototypeOf([[maybe_unused]] JSValue new_prototype)
{
	return false;
}

bool JS::Class::Class::IsExtensible()
{
	return false;
}

bool JS::Class::Class::PreventExtensions()
{
	return true;
}

bool JS::Class::Class::HasOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(GetClass()->GetBaseInstanceProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
		{
			return true;
		}
	}
#endif

	return GetClassOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)).has_value();
}

bool JS::Class::Class::HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	auto name = assert_property_key(property_key);

	for(Access access : Accesses(accesses))
	{
		if(GetClass()->GetBaseInstanceProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access))))
		{
			return true;
		}
	}

	for(Access access : Accesses(accesses))
	{
		// check read access first then write access
		// changed from Object
		auto property = GetClassOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access)));
		if(property.has_value())
		{
			return true;
		}
		if(auto parent = GetNonNullObject(GetPrototypeOf().value_or(JS_undefined)))
		{
			if(parent.value()->HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access) WITH_PHASE(phase))))
			{
				return true;
			}
		}
	}

	return false;
}

JSObject JS::Class::Class::GetCurrentThisValue()
{
	auto env = GetCurrentExecutionContext()->GetThisEnvironment();
	if(auto function_env = std::dynamic_pointer_cast<JS::Runtime::FunctionEnvironmentRecord>(env))
	{
		auto this_value = function_env->GetThisBinding();
		if(function_env->FunctionObject->ThisMode != JS::Class::Function::ThisModeType::Instance) // same as Strict
		{
			JS::Throw(JS::New<ReferenceError> __LOC());
		}

		if(auto this_object_pointer = this_value.get_if<JSObject>())
		{
			return *this_object_pointer;
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
	}
	else
	{
		JS::Throw(JS::New<ReferenceError> __LOC());
	}
}

JSObject JS::Class::Class::GetCurrentThisValue(std::shared_ptr<JS::Runtime::EnvironmentRecord> environment)
{
	auto env = GetCurrentExecutionContext()->GetThisEnvironment(environment);
	if(auto function_env = std::dynamic_pointer_cast<JS::Runtime::FunctionEnvironmentRecord>(env))
	{
		auto this_value = function_env->GetThisBinding();
		if(function_env->FunctionObject->ThisMode != JS::Class::Function::ThisModeType::Instance) // same as Strict
		{
			JS::Throw(JS::New<ReferenceError> __LOC());
		}

		if(auto this_object_pointer = this_value.get_if<JSObject>())
		{
			return *this_object_pointer;
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
	}
	else
	{
		JS::Throw(JS::New<ReferenceError> __LOC());
	}
}

JSValue JS::Class::Class::Get(JSValue property_key, JSValue receiver, [[maybe_unused]] Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	auto name = assert_property_key(property_key);

	if(limit != nullptr)
	{
		auto property_option = limit->GetBaseInstanceProperty(name, namespaces, AccessSet::Read);
		if(property_option.has_value())
		{
			auto property = limit->GetDerivedInstanceProperty(name, namespaces, AccessSet::Read).value();
			return property->GetValue(this WITH_PHASE(phase));
		}
		if(limit != GetClass())
		{
			return {};
		}
	}

	std::optional<std::variant<JSProperty, JSInstanceVariable>> property_option;
	JSObject current = JSObject(this);
	while(true)
	{
		if(JSClass class_ = std::dynamic_pointer_cast<JS::Class::Class>(current))
			property_option = class_->GetClassOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS);
		else
			property_option = current->GetOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS);

		if(property_option.has_value())
			break;

		// in this case, this is a lexical lookup, and we are searching instance properties in the class, so the prototype attributes are not required
		if(environment != nullptr)
			break;

		// if environment is not provided, it is a member lookup, call the prototype object
		current = GetNonNullObject(current->GetPrototypeOf().value_or(JS_undefined)).value_or(nullptr);
		if(current == nullptr)
			break;
	}

	// changed from Object
	// we can reach this point either through lexical lookup (i.e. identifier declared inside a class) or membership lookup, as part of a prototype chain
	// we check if the current class, either the one lexically enclosing a reference, or as part of a prototype chain, has the named property
	if(auto property_option = GetClassOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS))
	{
		auto property = property_option.value();

		return std::visit([this, name, receiver, namespaces, environment, phase](auto&& property)
		{
			using T = std::decay_t<decltype(property)>;
			if constexpr (std::is_same_v<T, JSProperty>)
			{
				switch(property.PropertyType)
				{
				case JSProperty::Type::Dynamic:
					if(property.IsInitialized())
					{
						return property.GetValue();
					}
					else
					{
						JS::Throw(JS::New<UninitializedError> __LOC());
					}
					break;
				case JSProperty::Type::Variable:
					// variable properties need to be accessed and modified by address
					if(property.direct_access == nullptr)
					{
						JS::Throw(JS::New<UninitializedError> __LOC());
					}
					if(!property.direct_access->IsInitialized())
					{
						property.direct_access->Initialize(JS::Runtime::ExecutionPhase::Run);
					}
					if(!property.direct_access->IsInitialized())
					{
						JS::Throw(JS::New<UninitializedError> __LOC());
					}
					return property.direct_access->GetValue();
				case JSProperty::Type::Accessor:
				case JSProperty::Type::Setter:
					JS::Throw(JS::New<InternalError> __LOC());
				case JSProperty::Type::Getter:
					return property.Get.Call(JS_undefined, std::vector<JSValue>{} WITH_PHASE(phase));
				case JSProperty::Type::Forbidden:
					JS::Throw(JS::New<InternalError> __LOC(u"property outside region shadowed by inner block"));
				}
				return JS_undefined;
			}
			else if constexpr (std::is_same_v<T, JSInstanceVariable>)
			{
				if(environment == nullptr)
				{
					JS::Throw(JS::New<ReferenceError> __LOC(u"cannot read instance property without supplying an instance"));
				}
				auto this_object = GetCurrentThisValue(environment);
				auto property = this_object->GetClass()->GetDerivedInstanceProperty(name, namespaces, AccessSet::Read).value();
				return property->GetValue(this_object WITH_PHASE(phase));
			}
			else
			{
				static_assert(false);
			}
		}, property);
	}

#if 0
	if(environment != nullptr)
	{
		// in this case, this is a lexical lookup, and we are searching instance properties in the class, so the prototype attributes are not required
		return JS_undefined;
	}

	// if environment is not provided, it is a member lookup, call the prototype object

	auto parent = GetPrototypeOf();
	if(parent.has_value())
	{
		// if this is not ES4, parent.value() must be an Object
		return parent.value().Get(property_key, receiver, intent WITH_NAMESPACES(namespaces) WITH_CLASS(nullptr) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
	}
#endif

	return JS_undefined;
}

bool JS::Class::Class::Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put)
{
	auto name = assert_property_key(property_key);

	auto instance_property_option = limit->GetBaseInstanceProperty(name, namespaces, AccessSet::Write);
	if(instance_property_option.has_value())
	{
		auto instance_property = limit->GetDerivedInstanceProperty(name, namespaces, AccessSet::Write).value();
		if(!check_can_put)
		{
			instance_property->SetValue(this, value);
		}
		return true;
	}
	if(limit != GetClass())
	{
		return false;
	}

	// changed from Object
	// we can reach this point either through lexical lookup (i.e. identifier declared inside a class) or membership lookup, as part of a prototype chain
	// we check if the current class, either the one lexically enclosing a reference, or as part of a prototype chain, has the named property
	if(auto property_option = GetClassOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS))
	{
		auto property = property_option.value();

		return std::visit([this, name, value, receiver, namespaces, environment, check_can_put](auto&& property)
		{
			using T = std::decay_t<decltype(property)>;
			if constexpr (std::is_same_v<T, JSProperty>)
			{
				switch(property.PropertyType)
				{
				case JSProperty::Type::Dynamic:
					if(!property.IsWritable())
					{
						return false;
					}
					if(check_can_put)
					{
						return true;
					}
					return DefineOwnProperty(name, JSPropertyDescriptor().withValue(value) WITH_NAMESPACES(namespaces) WRITE_ACCESS);
				case JSProperty::Type::Variable:
					if(!property.IsWritable())
					{
						JS::Throw(JS::New<ReferenceError> __LOC());
					}
					if(check_can_put)
					{
						return true;
					}
					// variable properties need to be accessed and modified by address
					if(property.direct_access == nullptr)
					{
						JS::Throw(JS::New<UninitializedError> __LOC());
					}
					property.direct_access->SetValue(value);
					return true;
				case JSProperty::Type::Accessor:
				case JSProperty::Type::Getter:
					JS::Throw(JS::New<InternalError> __LOC());
				case JSProperty::Type::Setter:
					if(!check_can_put)
					{
						property.Set.Call(JS_undefined, std::vector<JSValue>{ value } RUNTIME_PHASE);
					}
					return true;
				case JSProperty::Type::Forbidden:
					JS::Throw(JS::New<InternalError> __LOC(u"property outside region shadowed by inner block"));
				}
				JS::Throw(JS::New<InternalError> __LOC());
			}
			else if constexpr (std::is_same_v<T, JSInstanceVariable>)
			{
				if(environment == nullptr)
				{
					JS::Throw(JS::New<ReferenceError> __LOC(u"cannot write instance property without supplying an instance"));
				}
				if(check_can_put)
				{
					return true;
				}
				auto this_object = GetCurrentThisValue(environment);
				auto property = this_object->GetClass()->GetDerivedInstanceProperty(name, namespaces, AccessSet::Write).value();
				property->SetValue(this_object, value);
				return true;
			}
			else
			{
				static_assert(false);
			}
		}, property);
	}

	// classes are sealed

	return false;
}

bool JS::Class::Class::Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
	auto name = assert_property_key(property_key);

	// TODO
	return false;
}

bool JS::Class::Class::DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	// TODO
	//return false;
	return OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
}

std::map<JSString, JSValue> JS::Class::Class::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	slots[u"Super"] = JSObject(Super.has_value() ? GetClassForType(Super.value()) /* TODO: more detailed description for type application? */ : nullptr);
	if(unified)
	{
		slots[u"ClassPrototype"] = ClassPrototype.value_or(JS_null);
	}
	else
	{
		slots[u"Prototype"] = ClassPrototype.value_or(JS_null);
	}
	//slots[u"Complete"] = Complete;
	slots[u"Name"] = Name;
	slots[u"TypeofString"] = GetTypeofString();
	slots[u"PrivateNamespace"] = PrivateNamespace;
	slots[u"Dynamic"] = Dynamic;
	slots[u"Final"] = Final;
	if(GetDefaultValue().has_value())
	{
		slots[u"DefaultValue"] = GetDefaultValue().value();
	}
	switch(GetDefaultHint())
	{
	case JS::Runtime::TypeHint::Default:
	case JS::Runtime::TypeHint::Number:
	default:
		slots[u"DefaultHint"] = u"hintNumber";
		break;
	case JS::Runtime::TypeHint::String:
		slots[u"DefaultHint"] = u"hintString";
		break;
	}
	return slots;
}

std::optional<JSInstanceProperty> JS::Class::Class::GetInstanceProperty(JSPropertyKey name, std::set<JSNamespace> namespaces, AccessSet accesses)
{
	auto result = InstanceMembers.Get(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	if(result.has_value())
	{
		return *result.value();
	}
	else
	{
		return {};
	}
}

std::optional<JSInstanceProperty> JS::Class::Class::GetBaseInstanceProperty(JSPropertyKey name, std::set<JSNamespace> namespaces, AccessSet accesses)
{
	if(Super.has_value())
	{
		auto property = GetClassForType(Super.value())->GetBaseInstanceProperty(name, namespaces, accesses);
		if(property.has_value())
		{
			return property;
		}
	}

	auto property = GetInstanceProperty(name, namespaces, accesses);
	if(property.has_value())
	{
		return property.value();
	}
	else
	{
		return {};
	}
}

std::optional<JSInstanceProperty> JS::Class::Class::SearchForOverrides(JSPropertyKey name, std::set<JSNamespace> namespaces, AccessSet accesses)
{
	std::optional<JSInstanceProperty> result;
	if(Super.has_value())
	{
		for(auto namespace_ : namespaces)
		{
			auto property = GetClassForType(Super.value())->GetBaseInstanceProperty(name, {namespace_}, accesses);
			if(!result.has_value())
			{
				result = property;
			}
			else if(property.has_value() && result.value() != property.value())
			{
				JS::Throw(JS::New<DefinitionError> __LOC()); // overriding two names of separate namespaces
			}
		}
	}
	return result;
}

std::optional<JSInstanceProperty> JS::Class::Class::GetDerivedInstanceProperty(JSPropertyKey name, std::set<JSNamespace> namespaces, AccessSet accesses)
{
	auto property = GetInstanceProperty(name, namespaces, accesses);
	if(property.has_value())
	{
		return property;
	}
	else if(Super.has_value())
	{
		return GetClassForType(Super.value())->GetDerivedInstanceProperty(name, namespaces, accesses);
	}
	else
	{
		return {};
	}
}

JSBoolean JS::Class::Class::OrdinaryIs(JSValue object, JSClass class_)
{
	return class_->IsAncestor(GetClassForType(object.GetType()));
}

JSBoolean JS::Class::Class::Is(JSValue object)
{
	return OrdinaryIs(object, JSClass(this));
}

std::optional<JSValue> JS::Class::Class::OrdinaryCoerce(JSValue object, JSClass class_)
{
	if(IsNull(object) || class_->Is(object))
		return object;
	else
		return {};
}

std::optional<JSValue> JS::Class::Class::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	return OrdinaryCoerce(object, JSClass(this));
}

JSString JS::Class::Class::GetTypeofString()
{
	return u"object";
}

JS::Runtime::TypeHint JS::Class::Class::GetDefaultHint()
{
	return JS::Runtime::TypeHint::Number;
}

std::optional<JSValue> JS::Class::Class::GetDefaultValue()
{
	return JS_null;
}

JSValue JS::Class::Class::OrdinaryConstruct(JSClass class_, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target)
{
	gcn::gc_ptr<JS::Class::OrdinaryObject> this_argument;
	if(class_->ClassPrototype)
	{
		this_argument = JS::NewWithPrototype<JS::Class::OrdinaryObject>(class_->ClassPrototype);
	}
	else
	{
		this_argument = JS::New<JS::Class::OrdinaryObject>();
	}
	this_argument->Extensible = class_->Dynamic;
	AssignType(this_argument, class_);
	class_->Init(this_argument, arguments);
	return this_argument;
}

void JS::Class::Class::TrivialInit([[maybe_unused]] JSClass class_, [[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments)
{
	if(arguments.size() != 0)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	// do nothing
}
#endif

//// Class::Function

JSObject JS::Runtime::Intrinsics::* JS::Class::Function::IntrinsicPrototype = &JS::Runtime::Intrinsics::Function_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::Function::IntrinsicClass = &JS::Runtime::Intrinsics::Function_class;
#endif

JSObject JS::Runtime::Intrinsics::* JS::Class::BoundFunction::IntrinsicPrototype = &JS::Runtime::Intrinsics::Function_prototype;

JS::Class::Function::Function(
	JS::Runtime::Realm * Realm,
	JSString name,
	uint32_t length,
	JS::ParseNode::FunctionalElement * Code,
	ThisModeType ThisMode,
	std::shared_ptr<JS::Runtime::EnvironmentRecord> Environment,
	std::shared_ptr<JS::Runtime::PrivateEnvironmentRecord> PrivateEnvironment
	AND_NAMESPACES(OpenNamespaces),
	FunctionKindType FunctionKind,
	ConstructorKindType ConstructorKind)
		: Code(Code),
			ThisMode(ThisMode),
			Environment(Environment),
			PrivateEnvironment(PrivateEnvironment),
			FunctionKind(FunctionKind)
			WITH_NAMESPACES(OpenNamespaces(OpenNamespaces)),
		ConstructorKind(ConstructorKind),
		Realm(Realm)
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		//add_slot(JS::Intrinsics()->Function_class, u"name" PUBLIC_NAMESPACES, name);
		add_slot(JS::Intrinsics()->Function_class, u"length" PUBLIC_NAMESPACES, JSNumber(length));
	}
	else
#endif
	{
		if(Version::ES6 <= CurrentVersion)
		{
			Define(u"name",
				JSProperty::MakeData(name, /* Writable = */ false, /* Enumerable = */ false, /* Configurable = */ true)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS);
		}

		Define(
			u"length",
			JSProperty::MakeData(JSNumber(length), /* Writable = */ false, /* Enumerable = */ false, /* Configurable = */ Version::ES6 <= CurrentVersion)
			PUBLIC_NAMESPACES
			READ_WRITE_ACCESS);
	}
}

JS::Runtime::Realm * JS::Class::Function::GetFunctionRealm()
{
	return Realm;
}

JSString JS::Class::Function::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"Function";
}


JS::Class::Function::ConstructorKindType JS::Class::Function::GetConstructorKind()
{
	return ConstructorKind;
}

JS::Invocability JS::Class::Function::GetInvocability()
{
	//return IsConstructor() ? Invocability::Constructor : Invocability::NonConstructor;
	return GetConstructorKind() != ConstructorKindType::NonConstructor ? Invocability::Constructor : Invocability::NonConstructor;
}

#if ENABLE_ES4
std::optional<JS::Runtime::SignatureType> JS::Class::Function::GetSignature()
{
	return Signature;
}
#endif

#if 0
JSValue JS::Class::Function::Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4() && phase == JS::Runtime::ExecutionPhase::Compile)
	{
		JS::Throw(JS::New<ConstantError> __LOC());
	}
#endif

#if 0
	if(Version::ES8 <= CurrentVersion && is_async) // TODO: async generators
	{
		JS::Runtime::PromiseCapability promiseCapability { JS::Intrinsics()->Promise };
		bool rejected = false;
		try
		{
			Begin_Call(this_argument, arguments); // call not deferred
		}
		catch(JS::Runtime::ThrownValue * thrown_value)
		{
			JS::Call(promiseCapability.Reject, JS_undefined, {thrown_value->Value});
			delete thrown_value;
			rejected = true;
		}
		catch(JS::Class::Error * error)
		{
			JS::Call(promiseCapability.Reject, JS_undefined, {error});
			delete thrown_value;
			rejected = true;
		}

		if(!rejected)
		{
			JS::Runtime::ExecutionContext * async_context = new JS::Runtime::ExecutionContext(GetCurrentExecutionContext());
			async_context->code = GetCurrentExecutionContext()->code;
			JS::Runtime::ExecutionContext::GetContextStack().push_back(async_context);
			JSValue result = JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
			JS::Runtime::ExecutionContext::GetContextStack().pop_back();
			GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			return result;
		}

		return promiseCapability.Promise;
	}
#endif

	switch(FunctionKind)
	{
	case FunctionKindType::Normal:
	case FunctionKindType::ClassConstructor:
		{
std::cerr << "N\n";
			Begin_EvaluateBody(this_argument, arguments);
			JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			JSValue result = JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
			GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			return result;
		}
	case FunctionKindType::Generator:
std::cerr << "G\n";
		//if(Version::ES6 <= CurrentVersion)
		{
			JS::Class::Generator * generator = CreateGenerator();
			GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			return generator;
		}
	case FunctionKindType::Async:
std::cerr << "A\n";
		//if(Version::ES8 <= CurrentVersion)
		{
#if 0
			JS::Runtime::PromiseCapability promiseCapability { JS::Intrinsics()->Promise };
			bool rejected = false;
			try
			{
				Begin_EvaluateBody(this_argument, arguments);
			}
			catch(JS::Runtime::ThrownValue * thrown_value)
			{
				JS::Call(promiseCapability.Reject, JS_undefined, {thrown_value->Value});
				delete thrown_value;
				rejected = true;
			}
			catch(JS::Class::Error * error)
			{
				JS::Call(promiseCapability.Reject, JS_undefined, {error});
				delete thrown_value;
				rejected = true;
			}

			if(!rejected)
			{
				JS::Runtime::ExecutionContext * async_context = new JS::Runtime::ExecutionContext(GetCurrentExecutionContext());
				async_context->code = GetCurrentExecutionContext()->code;
				JS::Runtime::ExecutionContext::GetContextStack().push_back(async_context);
				JSValue result = JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
				JS::Runtime::ExecutionContext::GetContextStack().pop_back();
				GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
				return result;
			}

			return promiseCapability.Promise;
#endif
			assert(false);
		}
	case FunctionKindType::AsyncGenerator:
std::cerr << "AG\n";
		//if(Version::ES9 <= CurrentVersion)
		{
			// TODO
			assert(false);
		}
	default:
		assert(false);
	}
}

bool JS::Class::Function::Begin_Call(JSValue this_argument, std::vector<JSValue> arguments)
{
	switch(FunctionKind)
	{
	case FunctionKindType::Normal:
	case FunctionKindType::ClassConstructor:
		Begin_EvaluateBody(this_argument, arguments);
		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
		// execution will now continue in function body
		return true; // call deferred
	default:
		{
			auto result = Call(this_argument, arguments RUNTIME_PHASE);
			JS::Runtime::ExecutionContext::GetContextStack().back()->Action_Push<JSValue>(result);
			return false; // call not deferred
		}
	}
}
#endif

JSValue JS::Class::Function::Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4() && phase == JS::Runtime::ExecutionPhase::Compile)
	{
		JS::Throw(JS::New<ConstantError> __LOC());
	}
#endif

#if 0
	if(Version::ES8 <= CurrentVersion && is_async) // TODO: async generators
	{
		JS::Runtime::PromiseCapability promiseCapability { JS::Intrinsics()->Promise };
		bool rejected = false;
		try
		{
			Begin_Call(this_argument, arguments); // call not deferred
		}
		catch(JS::Runtime::ThrownValue * thrown_value)
		{
			JS::Call(promiseCapability.Reject, JS_undefined, {thrown_value->Value});
			delete thrown_value;
			rejected = true;
		}
		catch(JS::Class::Error * error)
		{
			JS::Call(promiseCapability.Reject, JS_undefined, {error});
			delete thrown_value;
			rejected = true;
		}

		if(!rejected)
		{
			JS::Runtime::ExecutionContext * async_context = new JS::Runtime::ExecutionContext(GetCurrentExecutionContext());
			async_context->code = GetCurrentExecutionContext()->code;
			JS::Runtime::ExecutionContext::GetContextStack().push_back(async_context);
			JSValue result = JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
			JS::Runtime::ExecutionContext::GetContextStack().pop_back();
			GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			return result;
		}

		return promiseCapability.Promise;
	}
#endif

#if 0
	Begin_EvaluateBody(this_argument, arguments);

	if(Version::ES6 <= CurrentVersion && is_generator)
	{
		JS::Class::Generator * generator = CreateGenerator();
		GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
		return generator;
	}
	else
	{
		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
		JSValue result = JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
		GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
		return result;
	}
#endif
	switch(FunctionKind)
	{
	case FunctionKindType::Normal:
	case FunctionKindType::ClassConstructor:
		{
			Begin_EvaluateBody(this_argument, arguments);
			JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			JSValue result = JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
			GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			return result;
		}
	case FunctionKindType::Generator:
		//if(Version::ES6 <= CurrentVersion)
		{
			Begin_EvaluateBody(this_argument, arguments);
			gcn::gc_ptr<JS::Class::Generator> generator = CreateGenerator();
			GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			return generator;
		}
	case FunctionKindType::Async:
		//if(Version::ES8 <= CurrentVersion)
		{
			// AsyncBlockStart

			JS::Runtime::PromiseCapability<JS::Class::Promise> promiseCapability { JS::Intrinsics()->Promise };
			bool rejected = false;
			try
			{
				Begin_EvaluateBody(this_argument, arguments);
			}
			catch(JS::Class::Internal::Value * thrown_value)
			{
				// TODO: roll back to original execution context
				JS::Call(promiseCapability.Reject, JS_undefined, {JSValue::WrapValue(thrown_value)} RUNTIME_PHASE);
				rejected = true;
			}

			if(!rejected)
			{
				JS::Runtime::ExecutionContext::GetContextStack().back()->promiseCapability = promiseCapability;
				JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

				JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
				GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			}

			return promiseCapability.Promise;
		}
	case FunctionKindType::AsyncGenerator:
		//if(Version::ES9 <= CurrentVersion)
		{
			// TODO
			assert(false);
		}
	default:
		assert(false);
	}
}

bool JS::Class::Function::Begin_Call(JSValue this_argument, std::vector<JSValue> arguments)
{
	switch(FunctionKind)
	{
	case FunctionKindType::Normal:
	case FunctionKindType::ClassConstructor:
		Begin_EvaluateBody(this_argument, arguments);
		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
		// execution will now continue in function body
		return true; // call deferred
	default:
		{
			auto result = Call(this_argument, arguments RUNTIME_PHASE);
			JS::Runtime::ExecutionContext::GetContextStack().back()->Action_Push<JSValue>(result);
			return false; // call not deferred
		}
	}

#if 0
	if(Version::ES6 <= CurrentVersion && is_generator)
	{
		// TODO: async
		JS::Class::Generator * generator = CreateGenerator();
		JS::Runtime::ExecutionContext::GetContextStack().back()->Action_Push<JSValue>(generator);
		return false; // call not deferred
	}
#if 0
	else if(Version::ES8 <= CurrentVersion && is_async)
	{
		// do nothing here
		// TODO
	}
#endif
	else
	{
		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
		// execution will now continue in function body
		return true; // call deferred
	}
#endif
}

void JS::Class::Function::Begin_EvaluateBody(JSValue this_argument, std::vector<JSValue> arguments)
{
	auto callee_context = PrepareForOrdinaryCall(nullptr);
	if(Version::ES6 <= CurrentVersion && FunctionKind == FunctionKindType::ClassConstructor)
	{
		JS::Throw(JS::New<TypeError> __LOC()); // created in new context
	}

	JS::Interpreter::Code * code = new JS::Interpreter::Code();
	JS::Interpreter::State<> state = code->Begin();
	JS::Interpreter::FunctionRange<> function_range = JS::Interpreter::Code::BeginFunctionRange(state, is_arrow, is_generator, is_async);
	OrdinaryCallBindThis(callee_context, this_argument);
	JS::Interpreter::State<std::optional<JSValue>> state1 = CompileBody(state, arguments);
	[[maybe_unused]] JS::Interpreter::State<JSValue> state2 = function_range.EndFunctionRange(state1);
#if ENABLE_ES4
	if(IsVersionES4() && ClassBeingConstructed != nullptr)
	{
		// this is a constructor, replace the return value with this_argument
		state2 = state2.Pop().Push<JSValue>(this_argument);
	}
#endif

	JS::Runtime::ExecutionContext::GetContextStack().back()->code = code;
}

gcn::gc_ptr<JS::Class::Generator> JS::Class::Function::CreateGenerator()
{
	gcn::gc_ptr<JS::Class::Generator> generator =
		JS::NewWithPrototype<JS::Class::Generator>(
			JS::GetPrototypeFromConstructor(JSFunction::MakeFunction(this), &JS::Runtime::Intrinsics::GeneratorPrototype),
			JS::Class::Generator::StateType::SuspendedStart,
			JS::Runtime::ExecutionContext::GetContextStack().back());
	JS::Runtime::ExecutionContext::GetContextStack().back()->Generator = generator;
	JS::Runtime::ExecutionContext::GetContextStack().pop_back();
	return generator;
}

JSValue JS::Class::Function::Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
#if ENABLE_ES4
	if(phase == JS::Runtime::ExecutionPhase::Compile)
	{
		JS::Throw(JS::New<ConstantError> __LOC("prototype constructor call at compile time"));
	}
#endif

	Begin_Construct(arguments, new_target);
	//auto cxt = JS::Runtime::ExecutionContext::GetContextStack().back();

	JSValue result = JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
	GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
	return result;
}

void JS::Class::Function::Begin_Construct(std::vector<JSValue> arguments, JSFunction new_target)
{
	gcn::gc_ptr<JS::Class::OrdinaryObject> this_argument = nullptr;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JSValue proto;
		switch(ThisMode)
		{
		case ThisModeType::Unchecked: // Global
		case ThisModeType::Prototype: // Global_Checked
			proto = JS::Get(this, u"prototype");
			if(IsNull(proto) || IsUndefined(proto))
			{
				proto = GetFunctionRealm()->Intrinsics.Object_prototype;
			}
			else if(!GetNonNullObject(proto) || (*GetNonNullObject(proto))->GetClass() != GetFunctionRealm()->Intrinsics.Object_class)
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}

			this_argument = JS::NewWithPrototype<JS::Class::OrdinaryObject>(proto);
			JS::Class::Class::AssignType(this_argument, GetFunctionRealm()->Intrinsics.Object_class);
			break;

		case ThisModeType::Static: // Lexical
		case ThisModeType::Instance: // Strict
			JS::Throw(JS::New<TypeError> __LOC());
		}
	}
	else
#endif
	if(Version::ES6 <= CurrentVersion && ConstructorKind == ConstructorKindType::NonConstructor)
	{
		JS::Throw(JS::New<TypeError> __LOC()); // [[Constructor]] method not implemented
	}
	else if(CurrentVersion < Version::ES6 || ConstructorKind == ConstructorKindType::Base)
	{
		JSValue proto;
		if(CurrentVersion < Version::ES6)
		{
			proto = JS::Get(this, u"prototype");
		}
		else
		{
			proto = JS::Get(new_target, u"prototype");
		}

		if(IsPrimitive(proto))
		{
			proto = GetFunctionRealm()->Intrinsics.Object_prototype;
			// TODO: the language of ES1 seems to claim this is Object.prototype, whatever its current state is, however Object.prototype is supposed to be read-only
		}

		this_argument = JS::NewWithPrototype<JS::Class::OrdinaryObject>(proto);
	}

	auto callee_context = PrepareForOrdinaryCall(new_target);
	JS::Interpreter::Code * code = new JS::Interpreter::Code();
	JS::Interpreter::State<> state = code->Begin();
	JS::Interpreter::FunctionRange<> function_range = JS::Interpreter::Code::BeginFunctionRange(state, is_arrow, is_generator, is_async);
	if(CurrentVersion < Version::ES6 || ConstructorKind == ConstructorKindType::Base)
	{
		OrdinaryCallBindThis(callee_context, this_argument);
		if(Version::ESNEXT <= CurrentVersion)
		{
			state = Compile_InitializeInstanceElements(state.Push<JSObject>(this_argument).Push<JSOrdinaryFunction>(this)).Pop();
		}
	}

	auto constructor_env = callee_context->LexicalEnvironment;
	JS::Interpreter::State<std::optional<JSValue>> state1 = CompileBody(state, arguments);
	JS::Interpreter::State<JSValue> state2 = function_range.EndFunctionRange(state1);
	// at the end of the function range, convert the return value
	state2 = state2.Apply(std::function<JSValue(JSValue)>([this, this_argument, constructor_env](JSValue result) -> JSValue
	{
		if(!IsPrimitive(result))
		{
			return result;
		}
		else if(CurrentVersion < Version::ES6 || ConstructorKind == ConstructorKindType::Base)
		{
			return this_argument;
		}
		else if(!IsUndefined(result))
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
		else
		{
			return constructor_env->GetThisBinding();
		}
	}));

	JS::Runtime::ExecutionContext::GetContextStack().back()->code = code;
	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
	JS::Runtime::ExecutionContext::GetContextStack()[JS::Runtime::ExecutionContext::GetContextStack().size() - 2]->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;

	// execution will now continue in function body
}

std::map<JSString, JSValue> JS::Class::Function::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	if(Version::ES3 <= CurrentVersion)
	{
		if(Version::ES6 <= CurrentVersion || unified)
		{
			slots[u"Environment"] = JS_null; // TODO
		}
#if ENABLE_ES4
		else if(IsVersionES4())
		{
			slots[u"Env"] = JS_null; // TODO
		}
#endif
		else
		{
			slots[u"Scope"] = JS_null; // TODO
		}
	}

	if(Version::ES13 <= CurrentVersion)
	{
		slots[u"PrivateEnvironment"] = JS_null; // TODO
	}

	if(Version::ES5 <= CurrentVersion)
	{
		if(Version::ES6 <= CurrentVersion || unified)
		{
			slots[u"FormalParameters"] = JS_null; // TODO: ParseNode
		}
		else
		{
			slots[u"FormalParameters"] = JS_null; // TODO: list of Strings
		}
	}

	if(Version::ES6 <= CurrentVersion && CurrentVersion < Version::ES11)
	{
		switch(FunctionKind)
		{
		case FunctionKindType::Async:
			if(Version::ES8 <= CurrentVersion)
			{
				slots[u"FunctionKind"] = u"async";
				break;
			}
			// fallthrough
		case FunctionKindType::Normal:
			slots[u"FunctionKind"] = u"normal";
			break;
		case FunctionKindType::ClassConstructor:
			slots[u"FunctionKind"] = u"classConstructor";
			break;
		case FunctionKindType::AsyncGenerator:
			if(Version::ES9 <= CurrentVersion)
			{
				slots[u"FunctionKind"] = u"async generator";
				break;
			}
			// fallthrough
		case FunctionKindType::Generator:
			slots[u"FunctionKind"] = u"generator";
			break;
		}
		// replaced by [[IsClassConstructor]]
	}

	if(Version::ES5 <= CurrentVersion)
	{
		if(Version::ES6 <= CurrentVersion || unified)
		{
			slots[u"ECMAScriptCode"] = JS_null; // TODO
		}
		else
		{
			slots[u"Code"] = JS_null; // TODO
		}
	}

	if(Version::ES6 <= CurrentVersion)
	{
		switch(ConstructorKind)
		{
		case ConstructorKindType::NonConstructor:
			break;
		case ConstructorKindType::Base:
			slots[u"ConstructorKind"] = u"base";
			break;
		case ConstructorKindType::Derived:
			slots[u"ConstructorKind"] = u"derived";
			break;
		}

		slots[u"Realm"] = JS_null; // TODO
	}

	if(Version::ES7 <= CurrentVersion)
	{
		slots[u"ScriptOrModule"] = JS_null; // TODO
	}

	if(Version::ES6 <= CurrentVersion)
	{
		switch(ThisMode)
		{
		case ThisModeType::Lexical:
			slots[u"ThisMode"] = u"lexical";
			break;
		case ThisModeType::Strict:
			slots[u"ThisMode"] = u"strict";
			break;
		case ThisModeType::Global:
			slots[u"ThisMode"] = u"global";
			break;
		case ThisModeType::Global_Checked:
			// cannot appear outside ES4 mode
			break;
		}

		slots[u"Strict"] = Strict;
		slots[u"HomeObject"] = HomeObject;
	}

	if(Version::ES10 <= CurrentVersion)
	{
		slots[u"SourceText"] = SourceText;
	}

	if(Version::ES13 <= CurrentVersion)
	{
		slots[u"Fields"] = JS_null; // TODO
		slots[u"PrivateMethods"] = JS_null; // TODO
		slots[u"ClassFieldInitializer"] = JS_null; // TODO
	}

	if(Version::ES11 <= CurrentVersion)
	{
		// replaces [[FunctionKind]]
		slots[u"IsClassConstructor"] = FunctionKind == FunctionKindType::ClassConstructor;
	}

	return slots;
}

JS::Runtime::ExecutionContext * JS::Class::Function::PrepareForOrdinaryCall(JSFunction new_target)
{
	auto callee_context = new JS::Runtime::ExecutionContext();
	callee_context->Function = JSFunction::MakeFunction(this);
	callee_context->Realm = Realm;
	callee_context->ScriptOrModule = ScriptOrModule;
	auto localEnv = std::make_shared<JS::Runtime::FunctionEnvironmentRecord>(gcn::gc_ptr<JS::Class::Function>(this), new_target);
	callee_context->LexicalEnvironment = localEnv;
	callee_context->VariableEnvironment = localEnv;
	if(Version::ESNEXT <= CurrentVersion)
	{
		callee_context->PrivateEnvironment = PrivateEnvironment;
	}

	if(JS::Runtime::ExecutionContext::GetContextStack().size() > 0
	&& JS::Runtime::ExecutionContext::GetContextStack().back()->state == JS::Runtime::ExecutionContext::ExecutionState::Executing)
	{
		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;
	}
	JS::Runtime::ExecutionContext::GetContextStack().push_back(callee_context);
	return callee_context;
}

JSValue JS::Class::Function::OrdinaryCallBindThis(JS::Runtime::ExecutionContext * callee_context, JSValue this_argument)
{
	switch(ThisMode)
	{
	case ThisModeType::Lexical: // Static
#if ENABLE_ES4
		if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
		if(CurrentVersion < Version::ES6)
#endif
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		return JS_undefined;
	case ThisModeType::Strict: // Instance
		break;
	case ThisModeType::Global: // Unchecked
	case ThisModeType::Global_Checked: // Prototype
#if ENABLE_ES4
		if(IsVersionES4())
		{
			if(IsPrimitive(this_argument))
			{
				this_argument = callee_context->GetPackage();
			}
		}
		else
#endif
		if(IsUndefined(this_argument) || IsNull(this_argument))
		{
			this_argument = Realm->GlobalObject;
		}
		else
		{
			this_argument = JS::ToObject(this_argument);
		}
		break;
	}
	return callee_context->LexicalEnvironment->BindThisValue(this_argument);
}

void JS::Class::Function::FunctionDeclarationInstantiation(std::vector<JSValue> arguments)
{
	auto ThisMode = Code->GetThisMode();

	// TODO: this is only ES6+
	std::multiset<JSString> parameter_names;
	bool has_duplicates = false;
	bool has_parameter_expressions = false;
	for(auto parameter : Code->GetArguments())
	{
		has_duplicates |= parameter->CollectBoundNames(parameter_names);
		has_parameter_expressions |= parameter->ContainsInitialization();
	}
	bool simple_parameter_list = Code->IsSimple();

	bool arguments_object_needed;

#if ENABLE_ES4
	if(IsVersionES4())
	{
		arguments_object_needed = ThisMode == JS::Class::Function::ThisModeType::Unchecked;
	}
	else
#endif
	{
		arguments_object_needed =
			ThisMode != JS::Class::Function::ThisModeType::Lexical
			&& parameter_names.count(u"arguments") == 0; // TODO: lexical and function names will also shadow this
	}

	if(Code->parameter_evaluation_environment_needed)
	{
		GetCurrentExecutionContext()->LexicalEnvironment = std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);
	}

#if ENABLE_ES4
	bool lexical_binding = IsVersionES4() && ThisMode != JS::Class::Function::ThisModeType::Unchecked;
	if(lexical_binding)
	{
		for(auto argument : Code->GetArguments())
		{
			JS::ParseNode::Identifier * identifier;
			if(auto assignment = dynamic_cast<JS::ParseNode::AssignmentExpression *>(argument))
			{
				argument = assignment->target;
			}
			identifier = dynamic_cast<JS::ParseNode::Identifier *>(argument);
			if(!identifier)
			{
				if(auto rest_expression = dynamic_cast<JS::ParseNode::RestExpression *>(argument))
				{
					identifier = dynamic_cast<JS::ParseNode::Identifier *>(rest_expression->expression);
					if(!identifier)
					{
						JS::Throw(JS::New<InternalError> __LOC());
					}
				}
				else
				{
					JS::Throw(JS::New<InternalError> __LOC());
				}
			}
			GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
				identifier->StringValue
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS,
				JSProperty::MakeVariable(
					std::optional<JSValue>{},
					/* Writable = */ !identifier->is_const,
					true,
					false,
					identifier->type.has_value() ? identifier->type.value()->EvaluateType() : Intrinsics()->Object_class));
		}
	}
	else
#endif
	for(auto parameter_name : parameter_names)
	{
		if(has_duplicates || CurrentVersion < Version::ES5)
		{
			GetCurrentExecutionContext()->LexicalEnvironment->CreateHoistedBinding(parameter_name, JS_undefined);
		}
		else
		{
			GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(parameter_name PUBLIC_NAMESPACES READ_WRITE_ACCESS,
				JSBinding::MakeMutable({}, /* Deletable = */ false));
		}
	}

	// leave as nullptr if the arguments object is non-mapped
	gcn::gc_ptr<JS::Class::Arguments> mapped_arguments_object = nullptr;
	if(arguments_object_needed)
	{
		gcn::gc_ptr<JS::Class::OrdinaryObject> arguments_object;
#if ENABLE_ES4
		if(IsVersionES4())
		{
			arguments_object = mapped_arguments_object = JS::NewWithPrototype<JS::Class::Arguments>(Intrinsics()->Array_prototype);
		}
		else
#endif
		if(CurrentVersion < Version::ES5 || (!Code->strict && simple_parameter_list))
		{
			arguments_object = mapped_arguments_object = JS::New<JS::Class::Arguments>();
		}
		else
		{
			arguments_object = JS::New<JS::Class::OrdinaryObject>();
		}

		for(size_t index = mapped_arguments_object
				? Code->GetArguments().size() // only supplemental arguments should be stored unmapped
				: 0; // all arguments should be stored unmapped
			index < arguments.size();
			index++)
		{
			arguments_object->DefineOwnProperty(
				JS::ToString(JSNumber(index) RUNTIME_PHASE),
				JSPropertyDescriptor().withValue(arguments[index]).withWritable(true).withEnumerable(true).withConfigurable(true)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS);
		}

#if ENABLE_ES4
		if(IsVersionES4())
		{
			arguments_object->add_slot(JS::Intrinsics()->Array_class, u"length", {JS::Intrinsics()->Array_private}, JSNumber(arguments.size()));
		}
		else
#endif
		{
			arguments_object->DefineOwnProperty(
				u"length",
				JSPropertyDescriptor().withValue(JSNumber(arguments.size())).withWritable(true).withEnumerable(false).withConfigurable(true)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS);
		}

		if(Version::ES6 <= CurrentVersion)
		{
			arguments_object->DefineOwnProperty(
				JS::Symbols::iterator,
				JSPropertyDescriptor().withValue(JS::Intrinsics()->Array_prototype_values).withWritable(true).withEnumerable(false).withConfigurable(true)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS);
		}
		arguments_object->DefineOwnProperty(
			u"callee",
			JSPropertyDescriptor().withValue(this).withWritable(true).withEnumerable(false).withConfigurable(true)
			PUBLIC_NAMESPACES
			READ_WRITE_ACCESS);

		if(mapped_arguments_object)
		{
			auto env = GetCurrentExecutionContext()->LexicalEnvironment;

			mapped_arguments_object->environment = env;

			for(uint32_t index = 0; index < Code->GetArguments().size(); index ++)
			{
				auto parameter = static_cast<JS::ParseNode::Identifier *>(Code->GetArguments()[index]);
				JSString name = parameter->StringValue;

				mapped_arguments_object->parameter_map.push_back(
					std::make_optional(JS::Class::Arguments::Mapping{/* Name = */ name, /* Enumerable = */ true, /* Configurable = */ true}));
				mapped_arguments_object->integer_index_properties.insert(index);
			}
		}

#if ENABLE_ES4
		if(!IsVersionES4() && Code->strict)
#else
		if(Code->strict)
#endif
		{
			GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(u"arguments" PUBLIC_NAMESPACES READ_WRITE_ACCESS,
				JSBinding::MakeImmutable(arguments_object, /* Strict = */ false));
		}
		else
		{
			GetCurrentExecutionContext()->LexicalEnvironment->CreateHoistedBinding(u"arguments", arguments_object);
		}
	}

	JS::Runtime::IteratorBindingContext binding_context;
	if(!(has_duplicates || CurrentVersion < Version::ES5))
	{
		binding_context.environment = GetCurrentExecutionContext()->LexicalEnvironment;
	}

	JS::Runtime::ListIteratorBindingData iterator_binding_data;
	iterator_binding_data.argument_iterator = arguments.begin();
	iterator_binding_data.argument_end = arguments.end();
	binding_context.binding_data = &iterator_binding_data;

#if ENABLE_ES4
	binding_context.exact_match_required = IsVersionES4() && !arguments_object_needed;
#endif

	for(auto parameter : Code->GetArguments())
	{
		parameter->EvaluateIteratorBinding(binding_context);
	}

#if 0
	if(mapped_arguments_object)
	{
		for(uint32_t index = 0; index < Code->GetArguments().size(); index ++)
		{
			auto parameter = static_cast<JS::ParseNode::Identifier *>(Code->GetArguments()[index]);
			JSString name = parameter->StringValue;

			mapped_arguments_object->Define(
				JS::ToString(JSNumber(index) RUNTIME_PHASE),
				JSProperty::MakeAlias(
					std::dynamic_pointer_cast<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment)
						->bindings.Get(name PUBLIC_NAMESPACES READ_WRITE_ACCESS).value(),
					/* Writable = */ true,
					/* Enumerable = */ IsVersionES4() ? false : true,
					/* Configurable = */ IsVersionES4() ? false : true,
					/* Explicit = */ false)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS);
		}
	}
#endif

#if ENABLE_ES4
	if(binding_context.exact_match_required && iterator_binding_data.argument_iterator != arguments.end())
	{
		JS::Throw(JS::New<ArgumentError> __LOC(u"more arguments than parameters were supplied"));
	}
#endif

	if(Code->parameter_closure_environment_needed)
	{
		GetCurrentExecutionContext()->VariableEnvironment = std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);
		Code->body->CreateBindings(JS::Runtime::EnvironmentMode::Hoisted);
		for(auto parameter_name : parameter_names)
		{
			auto binding = GetCurrentExecutionContext()->LexicalEnvironment->GetBindingValue(JSIdentifierReference(parameter_name) PUBLIC_NAMESPACES RUNTIME_PHASE);
			GetCurrentExecutionContext()->VariableEnvironment->CreateHoistedBinding(parameter_name, binding);
			// TODO: these values should not be redefined by those in the function body (except for functions)
		}
	}
	else
	{
		GetCurrentExecutionContext()->VariableEnvironment = GetCurrentExecutionContext()->LexicalEnvironment;
		Code->body->CreateBindings(JS::Runtime::EnvironmentMode::Hoisted);
	}

	if(!Code->strict)
	{
		GetCurrentExecutionContext()->LexicalEnvironment = std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->VariableEnvironment);
	}
}

JS::Interpreter::State<std::optional<JSValue>> JS::Class::Function::CompileBody(JS::Interpreter::State<> state, std::vector<JSValue> arguments)
{
	if(auto statements = dynamic_cast<JS::ParseNode::Block *>(Code->body))
	{
		ProcessDirectives(statements->statements);
	}
	else if(dynamic_cast<JS::ParseNode::Empty *>(Code->body))
	{
		JS::Throw(JS::New<InternalError> __LOC()); // TODO: abstract method called
	}

	FunctionDeclarationInstantiation(arguments);

	return Code->body->CompileStatement(state);
}

void JS::Class::Function::InitializeInstanceElements(JSObject this_argument)
{
	gcn::gc_ptr<JS::Class::OrdinaryObject> this_object = std::dynamic_pointer_cast<JS::Class::OrdinaryObject>(this_argument);
	if(!this_object)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	for(auto method : PrivateMethods)
	{
		this_object->PrivateElements[method.first] = method.second;
	}

	for(auto& field_record : Fields)
	{
		JSValue value = JS_undefined;
		if(field_record.Initialization != nullptr)
		{
			//value = field_record.Initializer->Initialize(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run));
			value = JS::Call(field_record.Initialization, this_object, std::vector<JSValue>{} RUNTIME_PHASE);
		}

		std::visit([this_object, value](auto&& name)
		{
			using T = std::decay_t<decltype(name)>;
			if constexpr(std::is_same_v<T, JSPropertyKey>)
			{
				if(!this_object->DefineOwnProperty(
					name,
					JSPropertyDescriptor()
						.withValue(value)
						.withWritable(true)
						.withEnumerable(true)
						.withConfigurable(true)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS))
				{
					JS::Throw(JS::New<TypeError> __LOC());
				}
			}
			else if constexpr(std::is_same_v<T, JSPrivateName>)
			{
				this_object->PrivateElements[name] = JSPrivateElement{
					JSPrivateElement::ElementKind::Field,
					value,
					{},
					{}
				};
			}
			else
			{
				static_assert(false);
			}
		}, field_record.Name);
	}
}

JS::Interpreter::State<JSObject> JS::Class::Function::Compile_InitializeInstanceElements(JS::Interpreter::State<JSOrdinaryFunction, JSObject> initial_state)
{
	struct runcontext
	{
		std::vector<JS::Runtime::ClassFieldDefinition>::iterator * field_record_iterator;
	};

	JS::Interpreter::State<runcontext, JSOrdinaryFunction, JSObject> state = initial_state.Obtain(std::function<runcontext(JSOrdinaryFunction, JSObject)>([](JSOrdinaryFunction function, JSObject this_argument) -> runcontext
	{
		gcn::gc_ptr<JS::Class::OrdinaryObject> this_object = std::dynamic_pointer_cast<JS::Class::OrdinaryObject>(this_argument);
		if(!this_object)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}

		for(auto method : function->PrivateMethods)
		{
			this_object->PrivateElements[method.first] = method.second;
		}

		runcontext context;
		context.field_record_iterator = new (GC) std::vector<JS::Runtime::ClassFieldDefinition>::iterator(function->Fields.begin());
		return context;
	}));

	size_t loop_start = state.self->instruction_sequence.size();

	state.Obtain(std::function<JSValue(runcontext, JSOrdinaryFunction)>([](runcontext context, JSOrdinaryFunction function) -> JSValue
	{
		return *context.field_record_iterator != function->Fields.end();
	}));

	JS::Interpreter::Instruction_JumpIfNot * jump = new JS::Interpreter::Instruction_JumpIfNot(0); // target will be supplied later

	state = state.Append<>(jump);

	state = state.Obtain(std::function<void(runcontext, JSOrdinaryFunction, JSObject)>([](runcontext context, [[maybe_unused]] JSOrdinaryFunction _1, JSObject this_argument)
	{
		auto& field_record = **context.field_record_iterator;

		JS::Interpreter::State<JSValue, JSObject> state1;
		if(field_record.Initialization != nullptr)
		{
			JS::Begin_Call(field_record.Initialization, this_argument, std::vector<JSValue>{}); // TODO
		}
		else
		{
			// we need to emulate a call without creating a new execution context, and the call would push an optional value on stack after it is completed
			// we can only do this by hiding it from the type checking
			GetCurrentExecutionContext()->Action_Push<JSValue>(JS_undefined);
		}
	})).WaitForReturn().Obtain(std::function<JSObject(JSValue, runcontext, JSOrdinaryFunction, JSObject)>([]([[maybe_unused]] JSValue _1, [[maybe_unused]] runcontext _2, [[maybe_unused]] JSOrdinaryFunction _3, JSObject this_argument) -> JSObject
	{
		return this_argument;
	})).Obtain(std::function<void(JSObject, JSValue, runcontext)>([](JSObject this_argument, JSValue value, runcontext context)
	{
		std::visit([this_argument, value](auto&& name)
		{
			using T = std::decay_t<decltype(name)>;
			if constexpr(std::is_same_v<T, JSPropertyKey>)
			{
				if(!this_argument->DefineOwnProperty(
					name,
					JSPropertyDescriptor()
						.withValue(value)
						.withWritable(true)
						.withEnumerable(true)
						.withConfigurable(true)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS))
				{
					JS::Throw(JS::New<TypeError> __LOC());
				}
			}
			else if constexpr(std::is_same_v<T, JSPrivateName>)
			{
				gcn::gc_ptr<JS::Class::OrdinaryObject> this_object = std::dynamic_pointer_cast<JS::Class::OrdinaryObject>(this_argument);
				if(!this_object)
				{
					JS::Throw(JS::New<InternalError> __LOC());
				}

				this_object->PrivateElements[name] = JSPrivateElement{
					JSPrivateElement::ElementKind::Field,
					value,
					{},
					{}
				};
			}
			else
			{
				static_assert(false);
			}
		}, (*context.field_record_iterator)->Name);
		++ *context.field_record_iterator;
	})).Pop().Pop();

	state = state.Append<>(new JS::Interpreter::Instruction_Jump(loop_start));

	jump->target = state.self->instruction_sequence.size();

	return state.Pop().Pop();
}

//// Class::Boolean

JSObject JS::Runtime::Intrinsics::* JS::Class::Boolean::IntrinsicPrototype = &JS::Runtime::Intrinsics::Boolean_prototype;

JSString JS::Class::Boolean::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"Boolean";
}

std::map<JSString, JSValue> JS::Class::Boolean::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	if(unified || CurrentVersion >= Version::ES6)
	{
		slots[u"BooleanData"] = BooleanData;
	}
	else if(CurrentVersion >= Version::ES5)
	{
		slots[u"PrimitiveValue"] = BooleanData;
	}
	else
	{
		slots[u"Value"] = BooleanData;
	}
	return slots;
}

//// Class::Number

JSObject JS::Runtime::Intrinsics::* JS::Class::Number::IntrinsicPrototype = &JS::Runtime::Intrinsics::Number_prototype;

JSString JS::Class::Number::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"Number";
}

std::map<JSString, JSValue> JS::Class::Number::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	if(unified || CurrentVersion >= Version::ES6)
	{
		slots[u"NumberData"] = NumberData;
	}
	else if(CurrentVersion >= Version::ES5)
	{
		slots[u"PrimitiveValue"] = NumberData;
	}
	else
	{
		slots[u"Value"] = NumberData;
	}
	return slots;
}

//// Class::String

JSObject JS::Runtime::Intrinsics::* JS::Class::String::IntrinsicPrototype = &JS::Runtime::Intrinsics::String_prototype;

bool JS::Class::String::is_string_index(JSString string, uint32_t& result)
{
	size_t index;
	if(string == u"-0")
	{
		return false;
	}
	JSNumber value = JSValue(string).ToNumber(VIA_RUNTIME_PHASE);
	if(!JS::SameValue(JS::ToString(value RUNTIME_PHASE), string))
	{
		return false;
	}
	switch(std::fpclassify(value))
	{
	case FP_NAN:
	case FP_INFINITE:
		return false;
	}
	if(floor(fabs(value)) != fabs(value))
	{
		return false;
	}
	if(value < 0)
	{
		return false;
	}
	index = (size_t)value;
	if(index >= StringData.size())
	{
		return false;
	}
	result = index;
	return true;
}

std::optional<JSProperty> JS::Class::String::GetOwnProperty(JSPropertyKey name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	auto property = OrdinaryObject::GetOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	if(property)
	{
		return property;
	}
#if ENABLE_ES4
	if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
	{
		if(auto string_property = std::get_if<JSString>(&name))
		{
			uint32_t index;
			if(!is_string_index(*string_property, index))
			{
				return {};
			}
			return JSProperty::MakeData(std::u16string(1, StringData[index]), /* Writable = */ false, true, false);
		}
	}
	return {};
}

bool JS::Class::String::HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	auto name = assert_property_key(property_key);

	if(auto property_option = std::get_if<JSString>(&name))
	{
#if ENABLE_ES4
		if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
		{
			uint32_t index;
			if(is_string_index(*property_option, index))
			{
				return true;
			}
		}
	}
	return OrdinaryObject::HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
}

std::vector<JSPropertyKey> JS::Class::String::OwnPropertyKeys(FOR_CLASS(limit))
{
	std::vector<JSPropertyKey> property_keys;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		assert(false); // String objects are not created in ES4
	}
#endif

	for(uint32_t index = 0; index < StringData.size(); index++)
	{
		property_keys.push_back(JS::ToString(JSNumber(index) RUNTIME_PHASE));
	}
	for(auto integer_index_property : integer_index_properties)
	{
		if(integer_index_property < StringData.size())
			continue;
		property_keys.push_back(JS::ToString(JSNumber(integer_index_property) RUNTIME_PHASE));
	}
	for(auto string_property : string_properties)
	{
		property_keys.push_back(string_property);
	}
	for(auto symbol_property : symbol_properties)
	{
		property_keys.push_back(symbol_property);
	}
	return property_keys;
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::String::ExtractAllProperties()
{
	std::map<JS::Class::Object::PropertyKeyData, JSProperty> result = OrdinaryObject::ExtractAllProperties();
	for(uint32_t index = 0; index < StringData.size(); index++)
	{
		PropertyKeyData data;
#if ENABLE_ES4
		data.PropertyKey = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
		data.Namespaces = {JS_public};
		data.Accesses = AccessSet::ReadWrite;
#else
		data = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
#endif
		result[data] = JSProperty::MakeData(std::u16string(1, StringData[index]), /* Writable = */ false, true, false);
	}
	return result;
}

JSString JS::Class::String::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"String";
}

std::map<JSString, JSValue> JS::Class::String::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	if(unified || CurrentVersion >= Version::ES6)
	{
		slots[u"StringData"] = StringData;
	}
	else if(CurrentVersion >= Version::ES5)
	{
		slots[u"PrimitiveValue"] = StringData;
	}
	else
	{
		slots[u"Value"] = StringData;
	}
	return slots;
}

//// Class::Symbol

JSObject JS::Runtime::Intrinsics::* JS::Class::Symbol::IntrinsicPrototype = &JS::Runtime::Intrinsics::Symbol_prototype;

std::map<JSString, JSValue> JS::Class::Symbol::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	slots[u"SymbolData"] = SymbolData;
	return slots;
}

//// Class::BigInt

JSObject JS::Runtime::Intrinsics::* JS::Class::BigInt::IntrinsicPrototype = &JS::Runtime::Intrinsics::BigInt_prototype;

std::map<JSString, JSValue> JS::Class::BigInt::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	slots[u"BigIntData"] = BigIntData;
	return slots;
}

//// Date

JSObject JS::Runtime::Intrinsics::* JS::Class::Date::IntrinsicPrototype = &JS::Runtime::Intrinsics::Date_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::Date::IntrinsicClass = &JS::Runtime::Intrinsics::Date_class;
#endif

JSString JS::Class::Date::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"Date";
}

std::map<JSString, JSValue> JS::Class::Date::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	if(unified || CurrentVersion >= Version::ES6)
	{
		slots[u"DateValue"] = DateValue;
	}
	else if(CurrentVersion >= Version::ES5)
	{
		slots[u"PrimitiveValue"] = DateValue;
	}
#if ENABLE_ES4
	else if(IsVersionES4())
	{
		slots[u"TimeValue"] = DateValue;
	}
#endif
	else
	{
		slots[u"Value"] = DateValue;
	}
	return slots;
}

//// Error

JSObject JS::Runtime::Intrinsics::* JS::Class::Error::IntrinsicPrototype = &JS::Runtime::Intrinsics::Error_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::Error::IntrinsicClass = &JS::Runtime::Intrinsics::Error_class;
#endif

JS::Class::Error::Error(std::optional<JSString> message, std::optional<JSValue> cause __ES4_ONLY(, std::optional<JSObject JS::Runtime::Intrinsics::*> using_prototype))
	: SourceLocation(GetSurroundingAgent()->GetCurrentLocationStack())
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(Intrinsics()->Error_class != nullptr) // must be set later
		{
			std::optional<JSObject> prototype_option;
			if(using_prototype.has_value())
			{
				prototype_option = Intrinsics()->*using_prototype.value();
			}
			else
			{
				prototype_option = Intrinsics()->Error_prototype;
			}

			if(prototype_option.has_value())
			{
				if(GetNonNullObject(prototype_option.value()))
				{
					add_slot(Intrinsics()->Error_class, u"name" PUBLIC_NAMESPACES, JS::Get(prototype_option.value(), u"name"));
				}
			}
		}
	}
#endif

	if(message.has_value())
	{
#if ENABLE_ES4
		if(IsVersionES4())
		{
			if(Intrinsics()->Error_class != nullptr) // must be set later
			{
				add_slot(Intrinsics()->Error_class, u"message" PUBLIC_NAMESPACES, message.value());
			}
		}
		else
#endif
		{
			Define(
				u"message",
				JSProperty::MakeData(message.value(), /* Writable = */ true, false, true)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS);
		}
	}
	if(cause.has_value() && Version::ES13 <= CurrentVersion)
	{
		Define(u"cause",
			JSProperty::MakeData(cause.value(), /* Writable = */ true, false, true)
			PUBLIC_NAMESPACES
			READ_WRITE_ACCESS);
	}
}

JSString JS::Class::Error::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"Error";
}

std::map<JSString, JSValue> JS::Class::Error::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	if(!unified && Version::ES6 <= CurrentVersion)
	{
		slots[u"ErrorData"] = JS_undefined;
	}
	return slots;
}

//// Array

JSObject JS::Runtime::Intrinsics::* JS::Class::Array::IntrinsicPrototype = &JS::Runtime::Intrinsics::Array_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::Array::IntrinsicClass = &JS::Runtime::Intrinsics::Array_class;
#endif

JS::Class::Array::Layout JS::Class::Array::DefaultLayout = JS::Class::Array::Layout::Dense;

JS::Class::Array::Array(uint32_t length, Layout layout)
	: elements(layout == Layout::Dense ? length : 0, JSProperty::MakeData(JS_undefined, /* Writable = */ true, /* Enumerable = */ true, /* Configurable = */ true)), layout(layout)
{
	for(uint32_t index = 0; index < length; index++)
	{
		integer_index_properties.insert(index);
	}
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(Intrinsics()->Array_class != nullptr) // must be set later
		{
			add_slot(Intrinsics()->Array_class, u"length", {Intrinsics()->Array_private}, JSNumber(length));
		}
	}
	else
#endif
	{
		OrdinaryObject::DefineOwnProperty(
			u"length",
			JSPropertyDescriptor().withValue(JSNumber(length)).withWritable(true).withEnumerable(false).withConfigurable(false)
			PUBLIC_NAMESPACES
			READ_WRITE_ACCESS);
	}
}

bool JS::Class::Array::set_length_property(JSPropertyDescriptor descriptor)
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(descriptor.Value.has_value())
		{
			GetClass()->GetDerivedInstanceProperty(u"length", {JS::Intrinsics()->Array_private} WRITE_ACCESS).value()->SetValue(this, descriptor.Value.value());
		}
		return true;
	}
	else
#endif
	{
		return OrdinaryObject::DefineOwnProperty(u"length", descriptor PUBLIC_NAMESPACES WRITE_ACCESS);
	}
}

bool JS::Class::Array::set_length(JSPropertyDescriptor descriptor)
{
	if(descriptor.Value.has_value())
	{
		uint32_t new_length;

		new_length = descriptor.Value.value().ToUint32(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run));
		if(new_length != descriptor.Value.value().ToNumber(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run)))
		{
			JS::Throw(JS::New<RangeError> __LOC());
		}

		uint32_t old_length = get_length();

		if(new_length >= old_length)
		{
			if(!set_length_property(descriptor))
			{
				return false;
			}

			if(layout == Layout::Dense && new_length != elements.size())
			{
				for(uint32_t index = elements.size(); index < new_length; index++)
				{
					integer_index_properties.insert(index);
				}
				elements.resize(new_length, JSProperty::MakeData(JS_undefined, /* Writable = */ true, true, true));
			}
			return true;
		}

#if ENABLE_ES4
		if(!IsVersionES4())
#endif
		{
			auto property = GetOwnProperty(u"length" PUBLIC_NAMESPACES WRITE_ACCESS).value();
			if(!property.IsWritable())
			{
				return false;
			}
		}

		while(new_length < old_length)
		{
			if(!Delete(JS::ToString(JSNumber(old_length - 1) RUNTIME_PHASE) PUBLIC_NAMESPACES WITH_CLASS(GetClass()) WITH_FLAG(false)))
			{
				set_length_property(descriptor.withValue(JSNumber(old_length)));
				return false;
			}
			integer_index_properties.erase(old_length - 1);
			old_length --;
		}

		elements.resize(new_length);
	}

	return set_length_property(descriptor);
}

#if 0
JSProperty JS::Class::Array::get_length_property()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		auto property = GetClass()->GetDerivedInstanceProperty(u"length", {JS::Intrinsics()->Array_private} WRITE_ACCESS).value();
		JSValue value = property->GetValue(this);
		return JSProperty::MakeData(value, property->Writable, property->Enumerable, /* Configurable = */ false);
	}
	else
#endif
	{
		return OrdinaryObject::GetOwnProperty(u"length" PUBLIC_NAMESPACES READ_ACCESS);
	}
}
#endif

uint32_t JS::Class::Array::get_length()
{
	switch(layout)
	{
	case Layout::Dense:
		return elements.size();
	case Layout::Sparse:
#if ENABLE_ES4
		if(IsVersionES4())
		{
			return JS::ToUint32(GetClass()->GetDerivedInstanceProperty(u"length", {JS::Intrinsics()->Array_private} WRITE_ACCESS).value()->GetValue(this) RUNTIME_PHASE);
		}
		else
#endif
		{
			return JS::ToUint32(OrdinaryObject::GetOwnProperty(u"length" PUBLIC_NAMESPACES READ_ACCESS).value().GetValue() RUNTIME_PHASE);
		}
	default:
		return 0;
	}
}

bool JS::Class::Array::Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
	auto name = assert_property_key(property_key);

#if ENABLE_ES4
	if(IsVersionES4() && limit != nullptr)
	{
		if(limit->GetBaseInstanceProperty(name, namespaces, AccessSet::Write))
		{
			return false;
		}
		if(limit != GetClass())
		{
			return false;
		}
	}
#endif

#if ENABLE_ES4
	if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
	{
		if(layout == Layout::Dense)
		{
			if(auto property_option = std::get_if<JSString>(&name))
			{
				uint32_t index;
				if(is_array_index(*property_option, index))
				{
					if(index < get_length())
					{
						// we cannot literally delete the array element
						elements[index] = JSProperty::MakeData(JS_undefined, /* Writable = */ true, true, true);
						return true;
					}
				}
			}
		}
	}
	return OrdinaryObject::Delete(name WITH_NAMESPACES(namespaces) WITH_CLASS(nullptr) WITH_FLAG(lexical)); // set limit to null so we only delete dynamic properties in ES4
}

// note: this is not overloaded in the specification, but it's more convenient for us to access the elements inside an array
std::optional<JSProperty> JS::Class::Array::GetOwnProperty(JSPropertyKey name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
#if ENABLE_ES4
	if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
	{
		if(layout == Layout::Dense)
		{
			if(auto property_option = std::get_if<JSString>(&name))
			{
				uint32_t index;
				if(is_array_index(*property_option, index))
				{
					if(index < elements.size())
					{
						JSProperty property = elements[index];
#if ENABLE_ES4
						property.direct_access = &elements[index];
#endif
						return property;
					}
				}
			}
		}
	}
	return OrdinaryObject::GetOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
}

bool JS::Class::Array::DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(auto property_option = std::get_if<JSString>(&property_key))
	{
#if ENABLE_ES4
		if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
		{
			uint32_t index;
			if(*property_option == u"length")
			{
				return set_length(descriptor);
			}
			if(is_array_index(*property_option, index))
			{
#if ENABLE_ES4
				if(!IsVersionES4())
#endif
				{
					JSProperty property = *GetOwnProperty(u"length" PUBLIC_NAMESPACES READ_ACCESS);
					if(index >= elements.size())
					{
						if(!property.IsWritable())
						{
							return false;
						}
					}
				}

				if(layout == Layout::Sparse)
				{
					uint32_t length;
#if ENABLE_ES4
					if(IsVersionES4())
					{
						length = get_length();
					}
					else
#endif
					{
						auto lengthDesc = OrdinaryObject::GetOwnProperty(u"length" PUBLIC_NAMESPACES READ_ACCESS);
						length = JS::ToUint32(GetNumber(lengthDesc.value().GetValue()).value() RUNTIME_PHASE);
						if(index >= length && !lengthDesc.value().IsWritable())
						{
							return false;
						}
					}

					if(!OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
					{
						return false;
					}

					if(index >= length)
					{
						set_length(JSPropertyDescriptor().withValue(JSNumber(index + 1)));
					}

					return true;
				}
				else
				{
					if(index >= get_length())
					{
						if(!set_length(JSPropertyDescriptor().withValue(JSNumber(index + 1))))
						{
							return false;
						}
					}

					auto updated_property = ValidateAndApplyPropertyDescriptor(IsExtensible(), descriptor, elements[index]);

					if(updated_property.has_value())
					{
						elements[index] = updated_property.value();

						return true;
					}
					else
					{
						// should not happen if index >= elements.size() was true before
						return false;
					}
				}
			}
		}
	}
	return OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
}

JSString JS::Class::Array::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"Array";
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::Array::ExtractAllProperties()
{
	std::map<JS::Class::Object::PropertyKeyData, JSProperty> result = OrdinaryObject::ExtractAllProperties();
	if(layout == Layout::Dense)
	{
		for(uint32_t index = 0; index < elements.size(); index++)
		{
			PropertyKeyData data;
#if ENABLE_ES4
			data.PropertyKey = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
			data.Namespaces = {JS_public};
			data.Accesses = AccessSet::ReadWrite;
#else
			data = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
#endif
			result[data] = elements[index];
		}
	}
	return result;
}

//// RegExp

JSObject JS::Runtime::Intrinsics::* JS::Class::RegExp::IntrinsicPrototype = &JS::Runtime::Intrinsics::RegExp_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::RegExp::IntrinsicClass = &JS::Runtime::Intrinsics::RegExp_class;
#endif

JSString JS::Class::RegExp::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"RegExp";
}

std::map<JSString, JSValue> JS::Class::RegExp::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	// TODO: ES3 has [[Match]], ES6 has [[RegExpMatcher]]
	if(unified || CurrentVersion >= Version::ES6)
	{
		slots[u"OriginalSource"] = OriginalSource;
		// TODO: store OriginalFlags as a string
	}
#if ENABLE_ES4
	else if(IsVersionES4())
	{
		slots[u"Source"] = OriginalSource;
		// TODO: other slots
	}
#endif
	// note: in previous versions, these were properties
	return slots;
}

//// Singleton

JSString JS::Class::Singleton::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
	else
#endif
	if(CurrentVersion < Version::ES6)
	{
		return Class;
	}
	else
	{
		return OrdinaryObject::GetClassString(); // obtained from [Symbols.toStringTag]
	}
}

//// Package

#if ENABLE_ES4
JSObject JS::Runtime::Intrinsics::* JS::Class::Package::IntrinsicPrototype = &JS::Runtime::Intrinsics::Object_prototype;
JSClass JS::Runtime::Intrinsics::* JS::Class::Package::IntrinsicClass = &JS::Runtime::Intrinsics::Package_class;

JS::Class::Package::Package(JSString Name, Type PackageType, std::optional<std::vector<JSString>> Path)
	:
		Name(Name),
		InternalNamespace(new JS::Class::Namespace(u"internal")),
		PackageType(PackageType),
		Path(Path)
{
}

// return only non-imported properties
bool JS::Class::Package::HasActualProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	if(OrdinaryObject::GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)).has_value())
	{
		return true;
	}
	else
	{
		return Prototype.value_or(JS_undefined).cast_object<JS::Class::Object>()->HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
	}
}

std::optional<JSProperty> JS::Class::Package::GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	auto result = OrdinaryObject::GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	if(result.has_value())
	{
		return result;
	}

#if 0
	// if it is not defined, refer to the imports

	auto package = imported_bindings.Get(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	if(package.has_value())
	{
		return (**package)->GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
#endif
	else
	{
		return {};
	}
}

bool JS::Class::Package::HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	auto name = assert_property_key(property_key);

	for(Access access : Accesses(accesses))
	{
		if(GetClass()->GetBaseInstanceProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access))))
		{
			return true;
		}
	}

	for(Access access : Accesses(accesses))
	{
		// check read access first then write access
		auto property = GetOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access)));
		if(property.has_value())
		{
			return true;
		}
		// check imports
		auto package = imported_bindings.Get(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		if(package.has_value())
		{
			return true;
		}
		// check imported packages
		if(auto name_string = GetString(name))
		{
			if(import_dir.Imports.find(name_string.value()) != import_dir.Imports.end())
			{
				return true;
			}
		}
		if(auto parent = GetNonNullObject(GetPrototypeOf().value_or(JS_undefined)))
		{
			if(parent.value()->HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(AsAccessSet(access) WITH_PHASE(phase))))
			{
				return true;
			}
		}
	}

	return false;
}

JSValue JS::Class::Package::Get(JSValue property_key, [[maybe_unused]] JSValue receiver, [[maybe_unused]] Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	auto name = assert_property_key(property_key);

	if(limit != nullptr)
	{
		// we only check instance properties for the first element in the prototype chain
		// first, check if there are matching instance properties for the class, either actual or corresponding to the super class
		auto property_option = limit->GetBaseInstanceProperty(name, namespaces, AccessSet::Read);
		if(property_option.has_value())
		{
			// if we find a name, return it
			auto property = limit->GetDerivedInstanceProperty(name, namespaces, AccessSet::Read).value();
			return property->GetValue(this WITH_PHASE(phase));
		}
		if(limit != GetClass())
		{
			// if the starting class is not the same as the current class, this was a super access, in which case we do not need to look at dynamic properties
			return {};
		}
	}

	std::optional<JSProperty> property_option;
	JSObject es4_receiver = JSObject(this);

	property_option = GetOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS);
	if(!property_option.has_value())
	{
		if(environment != nullptr)
		{
			// lexical lookup, check imports
			auto package = imported_bindings.Get(name WITH_NAMESPACES(namespaces) READ_ACCESS);
			if(package.has_value())
			{
				property_option = (**package)->GetOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS);
				es4_receiver = JSObject(**package);
			}

			// check fully qualified imported packages
			if(!property_option.has_value())
			{
				if(auto name_string = GetString(name))
				{
					auto package = GetCurrentExecutionContext()->GetPackage()->LookupPackage(std::vector<JSString>{name_string.value()});
					if(package != nullptr)
					{
						return package;
					}

					auto iter = import_dir.Imports.find(name_string.value());
					if(iter != import_dir.Imports.end())
					{
						return
							iter->second->Alive
								? JS::Host->GetES4Package({}, name_string.value(), iter->second->Open).Package // the actual package
								: JS::New<Package>(name_string.value(), Type::Transient, std::make_optional(std::vector<JSString>{name_string.value()})); // a transient phantom
					}
				}
			}
		}
		else if(GetCurrentExecutionContext()->LexicalEnvironment != nullptr) // do not attempt to look up member packages
		{
			// member lookup, check members
			if(Path.has_value())
			{
				if(auto name_string = GetString(name))
				{
					std::vector<JSString> member_path = Path.value();
					member_path.push_back(name_string.value());
					auto package = GetCurrentExecutionContext()->GetPackage()->LookupPackage(member_path);
					if(package != nullptr)
					{
						return package;
					}
				}
			}
		}
	}

	if(!property_option.has_value())
	{
		es4_receiver = GetNonNullObject(es4_receiver->GetPrototypeOf().value_or(JS_undefined)).value_or(nullptr);
		if(es4_receiver != nullptr)
		{
			while(true)
			{
				property_option = es4_receiver->GetOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS);
				if(property_option.has_value())
					break;

				es4_receiver = GetNonNullObject(es4_receiver->GetPrototypeOf().value_or(JS_undefined)).value_or(nullptr);
				if(es4_receiver == nullptr)
					break;
			}
		}
	}

	// we check if the current object in the prototype chain has the named property
	if(property_option.has_value())
	{
		auto property = property_option.value();
		switch(property.PropertyType)
		{
		case JSProperty::Type::Data:
			if(property.IsInitialized())
			{
				return property.GetValue();
			}
			else
			{
				JS::Throw(JS::New<InternalError> __LOC(u"uninitialized dynamic variable"));
			}
			break;
#if ENABLE_ES4
		case JSProperty::Type::Variable:
			// variable properties need to be accessed and modified by address
			if(property.direct_access == nullptr)
			{
				JS::Throw(JS::New<UninitializedError> __LOC());
			}
			if(!property.direct_access->IsInitialized())
			{
				property.direct_access->Initialize(JS::Runtime::ExecutionPhase::Run);
			}
			if(!property.direct_access->IsInitialized())
			{
				JS::Throw(JS::New<UninitializedError> __LOC());
			}
			return property.direct_access->GetValue();
#endif
		case JSProperty::Type::Accessor:
			// ignore accessors in these versions
			break;
#if ENABLE_ES4
		case JSProperty::Type::Getter:
			if(IsVersionES4())
			{
				// ES4 static getter methods do not get a "this" operand
				return property.Get.Call(JS_undefined, std::vector<JSValue>{} WITH_PHASE(phase));
			}
			// ignore getters in these versions
			break;
		case JSProperty::Type::Setter:
			if(IsVersionES4())
			{
				// ES4 static setter methods cannot occur for a JS::Access::Read lookup
				JS::Throw(JS::New<InternalError> __LOC(u"setter method on read lookup"));
			}
			// ignore setters in these versions
			break;
		case JSProperty::Type::Forbidden:
			if(IsVersionES4())
			{
				JS::Throw(JS::New<InternalError> __LOC(u"property outside region shadowed by inner block"));
			}
			// ignore
			break;
#endif
		}
	}

	return JS_undefined;
}

bool JS::Class::Package::Delete(JSValue name_value AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
	auto property_key = assert_property_key(name_value);
	if(limit == nullptr || limit == GetClass())
	{
		if(auto package = imported_bindings.Get(property_key WITH_NAMESPACES(namespaces) WRITE_ACCESS))
		{
			auto property = (**package)->GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WRITE_ACCESS);
			if(!property.has_value() || property.value().Configurable)
			{
				// if it is imported, remove the binding

				imported_bindings.Delete(property_key WITH_NAMESPACES(namespaces));

				// remove it from the property name list
				std::visit([this WITH_NAMESPACES(&namespaces)](auto&& value)
				{
					using T = std::decay_t<decltype(value)>;
					if constexpr (std::is_same_v<T, JSString>)
					{
						if(!IsVersionES4())
						{
							namespaces = {JS_public};
						}
						if(namespaces.find(JS_public) != namespaces.end())
						{
							uint32_t index;
							if(is_array_index(value, index))
							{
								integer_index_properties.erase(index);
								return;
							}
							string_properties.erase(std::find(string_properties.begin(), string_properties.end(), value));
						}
					}
					else if constexpr (std::is_same_v<T, JSSymbol>)
					{
						symbol_properties.erase(std::find(symbol_properties.begin(), symbol_properties.end(), value));
					}
					else
					{
						static_assert(false);
					}
				}, property_key);

				return true;
			}
			else
			{
				return false;
			}
		}
	}
	return OrdinaryObject::Delete(name_value WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_FLAG(lexical));
}

bool JS::Class::Package::Set(JSValue name_value, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put)
{
	auto property_key = assert_property_key(name_value);
	if(receiver == this && (limit == nullptr || limit == GetClass()) && environment != nullptr)
	{
		if(auto package = imported_bindings.Get(property_key WITH_NAMESPACES(namespaces) WRITE_ACCESS))
		{
			// if it is imported, set the value there
			return (**package)->Set(name_value, value, receiver WITH_NAMESPACES(namespaces) WITH_CLASS((**package)->GetClass()) NO_ENVIRONMENT, check_can_put);
		}
	}
	return OrdinaryObject::Set(name_value, value, receiver WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment), check_can_put);
}

bool JS::Class::Package::DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(descriptor.Value.has_value()
	&& !descriptor.Writable.has_value()
	&& !descriptor.Enumerable.has_value()
	&& !descriptor.Configurable.has_value())
	{
		// this is just a Set operation
		if(auto package = imported_bindings.Get(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
		{
			return (**package)->DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		}
	}

	if(!OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		return false;
	}

	// if definition was successful, remove import
	if(imported_bindings.Get(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		imported_bindings.Delete(property_key WITH_NAMESPACES(namespaces));
	}

	return true;
}

bool JS::Class::Package::Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	// a copy of Class::OrdinaryObject::Define
	JSPropertyDescriptor descriptor;
	JS::AccessSet actual_accesses = JS::AccessSet::ReadWrite;

	switch(property.PropertyType)
	{
	case JSProperty::Type::Variable:
		actual_accesses = JS::AccessSet::ReadWrite;
		break;
	case JSProperty::Type::Getter:
		actual_accesses = JS::AccessSet::Read;
		break;
	case JSProperty::Type::Setter:
		actual_accesses = JS::AccessSet::Write;
		break;
	default:
		break;
	}

	switch(property.PropertyType)
	{
	case JSProperty::Type::Variable:
	case JSProperty::Type::Getter:
	case JSProperty::Type::Setter:
		if(OrdinaryObject::GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(actual_accesses)))
		{
			return false;
		}

		AddPropertyKey(property_key WITH_NAMESPACES(namespaces));
		properties.Set(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), property);
		break;
	default:
		{
			// a copy of Object::Define
			JSPropertyDescriptor descriptor;

			switch(property.PropertyType)
			{
			case JSProperty::Type::Data:
				descriptor.Value = property.GetValue();
				descriptor.Writable = property.IsWritable();
				break;
			case JSProperty::Type::Accessor:
				descriptor.Get = property.Get;
				descriptor.Set = property.Set;
				break;
			default:
				JS::Throw(JS::New<InternalError> __LOC(u"abnormal property descriptor type"));
			}
			descriptor.Enumerable = property.Enumerable;
			descriptor.Configurable = property.Configurable;

			// use OrdinaryObject::GetOwnProperty
			auto current_property = OrdinaryObject::GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));

			auto updated_property = ValidateAndApplyPropertyDescriptor(IsExtensible(), descriptor, current_property);

			if(updated_property.has_value())
			{
				if(!current_property.has_value())
				{
					AddPropertyKey(property_key WITH_NAMESPACES(namespaces));
				}

				properties.Set(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), updated_property.value());
			}
			else
			{
				return false;
			}
		}
	}

#if 0
	if(!OrdinaryObject::Define(property_key, property WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		return false;
	}
#endif

	// if definition was successful, remove import
	if(imported_bindings.Get(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		imported_bindings.Delete(property_key WITH_NAMESPACES(namespaces));
	}

	return true;
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::Package::ExtractAllProperties()
{
	std::map<PropertyKeyData, JSProperty> result = OrdinaryObject::ExtractAllProperties();
	for(auto iterator = imported_bindings.begin(); iterator != imported_bindings.end(); ++iterator)
	{
		PropertyKeyData data;
		data.PropertyKey = iterator.get_name();
		bool read = false, write = false;
		for(auto pair : iterator.get_access())
		{
			data.Namespaces.insert(std::get<0>(pair));
			switch(std::get<1>(pair))
			{
			case Access::Read:
				read = true;
				break;
			case Access::Write:
				write = true;
				break;
			}
		}

		if(!write)
		{
			data.Accesses = AccessSet::Read;
		}
		else if(!read)
		{
			data.Accesses = AccessSet::Write;
		}
		else
		{
			data.Accesses = AccessSet::ReadWrite;
		}

		if(result.find(data) == result.end()) // ignore overshadowed imports
		{
			auto property = (**iterator)->GetOwnProperty(data.PropertyKey WITH_NAMESPACES(data.Namespaces) WITH_ACCESS_SET(data.Accesses));
			if(property.has_value()) // ignore missing imports
			{
				result[data] = property.value();
			}
		}
	}
	return result;
}

void JS::Class::Package::ActivateImportedPackage(std::vector<JSString> package_name, bool open)
{
	ImportDir * dir = &import_dir;

	for(size_t level = 0;
		level < package_name.size();
		level ++)
	{
		auto name_part = package_name[level];
		auto iter = dir->Imports.find(name_part);
		if(iter != dir->Imports.end())
		{
			dir = iter->second.get();
		}
		else
		{
			auto import = std::make_shared<ImportDir>();
			dir->Imports[name_part] = import;
			dir = import.get();
		}
	}

	dir->Alive = true;
	dir->Open = open;
}

JSPackage JS::Class::Package::LookupPackage(std::vector<JSString> package_name)
{
	ImportDir * dir = &import_dir;

	for(size_t level = 0;
		level < package_name.size();
		level ++)
	{
		auto name_part = package_name[level];
		auto iter = dir->Imports.find(name_part);
		if(iter == dir->Imports.end())
		{
			return nullptr;
		}
		else
		{
			dir = iter->second.get();
		}
	}

	// if the package has been imported, return the actual package using the host lookup tables
	// if the package was only used as an intermediate element in an import statement (such as `intermediate` in the statement `import intermediate.actual`), return an empty transient package object instead
	return
		dir->Alive
			? JS::Host->GetES4Package({}, package_name, dir->Open).Package // the actual package
			: JS::New<Package>(package_name.back(), Type::Transient, std::make_optional(package_name)); // a transient phantom
}

void JS::Class::Package::Import(JSPackage from_package, JSPropertyKey key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	imported_bindings.Set(key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), from_package);
}
#endif

//// ModuleNamespace

bool JS::Class::ModuleNamespace::IsExtensible()
{
	return false;
}

bool JS::Class::ModuleNamespace::PreventExtensions()
{
	return true;
}

std::optional<JSValue> JS::Class::ModuleNamespace::GetPrototypeOf()
{
	return {};
}

bool JS::Class::ModuleNamespace::SetPrototypeOf(JSValue new_prototype)
{
	return IsNull(new_prototype);
}

std::optional<JSProperty> JS::Class::ModuleNamespace::GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(auto key_pointer = std::get_if<JSString>(&property_key))
	{
		auto exports_iterator = std::find(Exports.begin(), Exports.end(), *key_pointer);
		if(exports_iterator == Exports.end())
		{
			return {};
		}
		else
		{
			return JSProperty::MakeData(
				Get(property_key, this, Intent::Read WITH_NAMESPACES(namespaces) WITH_CLASS(nullptr) WITH_ENVIRONMENT(nullptr) RUNTIME_PHASE),
				/* Writable = */ true,
				/* Enumerable = */ true,
				/* Configurable = */ false);
		}
	}
	else
	{
		return OrdinaryObject::GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
}

bool JS::Class::ModuleNamespace::HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	if(auto key_option = GetString(property_key))
	{
		auto exports_iterator = std::find(Exports.begin(), Exports.end(), *key_option);
		return exports_iterator != Exports.end();
	}
	else
	{
		return OrdinaryObject::HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
	}
}

JSValue JS::Class::ModuleNamespace::Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	if(auto key_option = GetString(property_key))
	{
		auto exports_iterator = std::find(Exports.begin(), Exports.end(), *key_option);
		if(exports_iterator == Exports.end())
		{
			return JS_undefined;
		}

		auto binding = Module->ResolveExport(*key_option);
		if(auto record_pointer = std::get_if<JS::Runtime::ModuleRecord::ResolvedBinding>(&binding))
		{
			auto record = *record_pointer;
			auto targetModule = record.Module;
			if(!record.BindingName.has_value())
			{
				return targetModule->GetModuleNamespace();
			}
			else
			{
				auto binding_name = record.BindingName.value();
				auto targetEnv = targetModule->Environment;
				if(targetModule->Environment == nullptr)
				{
					JS::Throw(JS::New<ReferenceError> __LOC());
				}
				if(!targetEnv->HasBinding(JSIdentifierReference(binding_name) PUBLIC_NAMESPACES READ_WRITE_ACCESS RUNTIME_PHASE))
				{
					JS::Throw(JS::New<ReferenceError> __LOC());
				}
				return targetEnv->GetBindingValue(JSIdentifierReference(binding_name) PUBLIC_NAMESPACES RUNTIME_PHASE);
			}
		}

		JS::Throw(JS::New<InternalError> __LOC());
	}
	else if(auto key_symbol_option = GetSymbol(property_key))
	{
		return OrdinaryObject::Get(key_symbol_option.value(), receiver, intent WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

bool JS::Class::ModuleNamespace::Set([[maybe_unused]] JSValue property_key, [[maybe_unused]] JSValue value, [[maybe_unused]] JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), [[maybe_unused]] bool check_can_put)
{
	return false;
}

bool JS::Class::ModuleNamespace::Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
	if(auto key_option = GetString(property_key))
	{
		auto exports_iterator = std::find(Exports.begin(), Exports.end(), *key_option);
		return exports_iterator == Exports.end();
	}
	else
	{
		return OrdinaryObject::Delete(property_key WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_FLAG(lexical));
	}
}

bool JS::Class::ModuleNamespace::DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if([[maybe_unused]] auto key_pointer = std::get_if<JSString>(&property_key))
	{
		auto current = GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		if(current.has_value())
		{
			return
				!descriptor.Configurable.value_or(false)
				&& descriptor.Enumerable.value_or(true)
				&& !descriptor.is_accessor_descriptor()
				&& descriptor.Writable.value_or(true)
				&& (!descriptor.Value.has_value() || JS::SameValue(descriptor.Value.value(), current.value().GetValue()));
		}
		else
		{
			return {};
		}
	}
	else
	{
		return OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
}

std::vector<JSPropertyKey> JS::Class::ModuleNamespace::OwnPropertyKeys(FOR_CLASS(limit))
{
	std::vector<JSPropertyKey> ownKeys;
	for(auto export_ : Exports) // sorted lexicographically
	{
		ownKeys.push_back(export_);
	}
	auto symbolKeys = OrdinaryObject::OwnPropertyKeys(VIA_CLASS(limit));
	ownKeys.insert(ownKeys.end(), symbolKeys.begin(), symbolKeys.end());
	return ownKeys;
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::ModuleNamespace::ExtractAllProperties()
{
	std::map<PropertyKeyData, JSProperty> result = OrdinaryObject::ExtractAllProperties();
	for(auto export_ : Exports)
	{
#if ENABLE_ES4
		PropertyKeyData data;
		data.PropertyKey = export_;
		data.Namespaces = {JS_public};
		data.Accesses = AccessSet::ReadWrite;
		result[data] = GetOwnProperty(export_ PUBLIC_NAMESPACES READ_WRITE_ACCESS).value();
#else
		result[export_] = GetOwnProperty(export_ PUBLIC_NAMESPACES READ_WRITE_ACCESS).value();
#endif
	}
	return result;
}

std::map<JSString, JSValue> JS::Class::ModuleNamespace::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	// TODO: Module, Exports
	return slots;
}

//// Proxy

void JS::Class::Proxy::ValidateNonRevokedProxy()
{
	if(ProxyTarget == nullptr)
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy revoked"));
	}
	if(ProxyHandler == nullptr)
	{
		JS::Throw(JS::New<InternalError> __LOC("proxy handler missing"));
	}
}

#if ENABLE_ES4
JSType JS::Class::Proxy::GetType()
{
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC("proxy used in ES4 mode"));
	}
	return nullptr;
}

std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> JS::Class::Proxy::FindSlot([[maybe_unused]] JSInstanceVariable id)
{
	return {};
}
#endif

bool JS::Class::Proxy::IsExtensible()
{
	ValidateNonRevokedProxy();
	if(CurrentVersion < Version::ES6)
	{
		return ProxyTarget->IsExtensible();
	}

	// TODO: untested
	auto trap_option = JS::GetMethod(ProxyHandler, u"isExtensible");
	if(!trap_option.has_value())
	{
		return ProxyTarget->IsExtensible();
	}
	auto trap = trap_option.value();
	auto boolean_trap_result = JS::ToBoolean(JS::Call(trap, ProxyHandler, {ProxyTarget} RUNTIME_PHASE));
	auto target_result = ProxyTarget->IsExtensible();
	if(boolean_trap_result != target_result)
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy handler gave incompatible result for isExtensible"));
	}
	return boolean_trap_result;
}

bool JS::Class::Proxy::PreventExtensions()
{
	ValidateNonRevokedProxy();
	if(CurrentVersion < Version::ES6)
	{
		return ProxyTarget->PreventExtensions();
	}

	// TODO: untested
	auto trap_option = JS::GetMethod(ProxyHandler, u"preventExtensions");
	if(!trap_option.has_value())
	{
		return ProxyTarget->PreventExtensions();
	}
	auto trap = trap_option.value();
	auto boolean_trap_result = JS::ToBoolean(JS::Call(trap, ProxyHandler, {ProxyTarget} RUNTIME_PHASE));
	if(boolean_trap_result)
	{
		if(ProxyTarget->IsExtensible())
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy handler reported success for preventExtensions but target is still extensible"));
		}
	}
	return boolean_trap_result;
}

std::optional<JSValue> JS::Class::Proxy::GetPrototypeOf()
{
	ValidateNonRevokedProxy();
	if(CurrentVersion < Version::ES6)
	{
		return ProxyTarget->GetPrototypeOf();
	}

	auto trap_option = JS::GetMethod(ProxyHandler, u"getPrototypeOf");
	if(!trap_option.has_value())
	{
		return ProxyTarget->GetPrototypeOf();
	}
	auto trap = trap_option.value();
	auto handler_proto_option = GetObjectOrNull(JS::Call(trap, ProxyHandler, {ProxyTarget} RUNTIME_PHASE));
	if(!handler_proto_option.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy handler gave invalid value for getPrototypeOf: expected object or null"));
	}
	auto handler_proto = handler_proto_option.value();
	if(ProxyTarget->IsExtensible())
	{
		return handler_proto;
	}
	auto target_proto_option = GetObjectOrNull(ProxyTarget->GetPrototypeOf().value_or(JS_null));
	if(!target_proto_option.has_value())
	{
		JS::Throw(JS::New<InternalError> __LOC("[[GetPrototypeOf]] returned non-object/non-null"));
	}
	auto target_proto = target_proto_option.value();
	if(!JS::SameValue(handler_proto, target_proto))
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy handler gave invalid value for getPrototypeOf: expected to be the same as target prototype"));
	}
	if(target_proto == nullptr)
	{
		return {};
	}
	else
	{
		return target_proto;
	}
}

bool JS::Class::Proxy::SetPrototypeOf(JSValue new_prototype)
{
	ValidateNonRevokedProxy();
	if(CurrentVersion < Version::ES6)
	{
		return ProxyTarget->SetPrototypeOf(new_prototype);
	}

	// TODO: untested
	auto trap_option = JS::GetMethod(ProxyHandler, u"setPrototypeOf");
	if(!trap_option.has_value())
	{
		return ProxyTarget->SetPrototypeOf(new_prototype);
	}
	auto trap = trap_option.value();
	auto boolean_trap_result = JS::ToBoolean(JS::Call(trap, ProxyHandler, {ProxyTarget, new_prototype} RUNTIME_PHASE));
	if(!boolean_trap_result)
	{
		return false;
	}
	else if(ProxyTarget->IsExtensible())
	{
		return true;
	}
	auto target_proto = ProxyTarget->GetPrototypeOf().value_or(JS_null);
	if(!JS::SameValue(new_prototype, target_proto))
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy handler attempted to change prototype in non-extensible object"));
	}
	return true;
}

JSValue JS::Class::Proxy::DefaultValue(JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	ValidateNonRevokedProxy();
	if(CurrentVersion >= Version::ES6)
	{
		// cannot be overridden
		return ProxyTarget->ToPrimitive(hint WITH_PHASE(phase));
	}

	auto trap_option = JS::GetMethod(ProxyHandler, u"getOwnPropertyDescriptor");
	if(!trap_option.has_value())
	{
		return ProxyTarget->DefaultValue(hint WITH_PHASE(phase));
	}
	auto trap = trap_option.value();

	std::u16string hint_string;

	switch(hint)
	{
	case JS::Runtime::TypeHint::String:
		hint_string = u"string";
		break;
	case JS::Runtime::TypeHint::Number:
		hint_string = u"number";
		break;
	case JS::Runtime::TypeHint::Default:
		hint_string = u"default";
		break;
	}

	auto trap_result_obj = JS::Call(trap, ProxyHandler, {ProxyTarget, hint_string} RUNTIME_PHASE); // always called at runtime phase
	if(GetNonNullObject(trap_result_obj))
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy toPrimitive returned non-primitive"));
	}
	else
	{
		return trap_result_obj;
	}
}

std::optional<JSProperty> JS::Class::Proxy::GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	ValidateNonRevokedProxy();
	if(CurrentVersion < Version::ES5)
	{
		return ProxyTarget->GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}

	auto trap_option = JS::GetMethod(ProxyHandler, u"getOwnPropertyDescriptor");
	if(!trap_option.has_value())
	{
		return ProxyTarget->GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
	auto trap = trap_option.value();
	auto trap_result_obj = JS::Call(trap, ProxyHandler, {ProxyTarget, property_key} RUNTIME_PHASE);
	if(IsUndefined(trap_result_obj))
	{
		auto target_desc = ProxyTarget->GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		if(!target_desc.has_value())
		{
			return {};
		}
		else if(!target_desc.value().Configurable)
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy getOwnPropertyDescriptor attempted to hide non-configurable descriptor"));
		}
		else if(!ProxyTarget->IsExtensible())
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy getOwnPropertyDescriptor attempted to hide descriptor in non-extensible object"));
		}
		else
		{
			return {};
		}
	}
	else if(JS::GetNonNullObject(trap_result_obj))
	{
		auto target_desc = ProxyTarget->GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		bool extensible_target = ProxyTarget->IsExtensible();
		JSProperty result_desc = JS::ToPropertyDescriptor(trap_result_obj);
		auto valid = JS::Class::OrdinaryObject::ValidateAndApplyPropertyDescriptor(extensible_target, JSPropertyDescriptor(result_desc), target_desc);
		if(!valid.has_value())
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy getOwnPropertyDescriptor provided incompatible descriptor with target"));
		}
		if(!result_desc.Configurable)
		{
			if(!target_desc.has_value() || target_desc.value().Configurable)
			{
				JS::Throw(JS::New<TypeError> __LOC("proxy getOwnPropertyDescriptor attempted to make descriptor non-configurable"));
			}
			if(result_desc.PropertyType == JSProperty::Type::Data
			&& !result_desc.IsWritable() && target_desc.value().IsWritable())
			{
				JS::Throw(JS::New<TypeError> __LOC("proxy getOwnPropertyDescriptor attempted to make descriptor non-writable"));
			}
		}
		return result_desc;
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy getOwnPropertyDescriptor returned invalid value"));
	}
}

std::optional<JSProperty> JS::Class::Proxy::GetProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(CurrentVersion != Version::ES5)
	{
		JS::Throw(JS::New<InternalError> __LOC("proxy getPropertyDescriptor got invoked outside ECMAScript 5 mode"));
	}

	ValidateNonRevokedProxy();
	auto trap_option = JS::GetMethod(ProxyHandler, u"getPropertyDescriptor");
	if(!trap_option.has_value())
	{
		return ProxyTarget->GetProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
	auto trap = trap_option.value();
	auto trap_result_obj = JS::Call(trap, ProxyHandler, {ProxyTarget, property_key} RUNTIME_PHASE);
	if(IsUndefined(trap_result_obj))
	{
		auto target_desc = ProxyTarget->GetProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		if(!target_desc.has_value())
		{
			return {};
		}
		else if(!target_desc.value().Configurable)
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy getPropertyDescriptor attempted to hide non-configurable descriptor"));
		}
		else if(!ProxyTarget->IsExtensible())
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy getPropertyDescriptor attempted to hide descriptor in non-extensible object"));
		}
		else
		{
			return {};
		}
	}
	else if(JS::GetNonNullObject(trap_result_obj))
	{
		auto target_desc = ProxyTarget->GetProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		bool extensible_target = ProxyTarget->IsExtensible();
		JSProperty result_desc = JS::ToPropertyDescriptor(trap_result_obj);
		auto valid = JS::Class::OrdinaryObject::ValidateAndApplyPropertyDescriptor(extensible_target, JSPropertyDescriptor(result_desc), target_desc);
		if(!valid.has_value())
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy getPropertyDescriptor provided incompatible descriptor with target"));
		}
		if(!result_desc.Configurable)
		{
			if(!target_desc.has_value() || target_desc.value().Configurable)
			{
				JS::Throw(JS::New<TypeError> __LOC("proxy getPropertyDescriptor attempted to make descriptor non-configurable"));
			}
			if(result_desc.PropertyType == JSProperty::Type::Data
			&& !result_desc.IsWritable() && target_desc.value().IsWritable())
			{
				JS::Throw(JS::New<TypeError> __LOC("proxy getPropertyDescriptor attempted to make descriptor non-writable"));
			}
		}
		return result_desc;
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy getPropertyDescriptor returned invalid value"));
	}
}

bool JS::Class::Proxy::HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	auto name = assert_property_key(property_key);

	ValidateNonRevokedProxy();

	// TODO: ES4 would likely need to call the class instance method meta::has, after looking through the instance properties

	auto trap_option = JS::GetMethod(ProxyHandler, JSString(u"has"));
	if(!trap_option.has_value())
	{
		return ProxyTarget->HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
	}
	auto trap = trap_option.value();
	auto boolean_trap_result = JS::ToBoolean(JS::Call(trap, ProxyHandler, {ProxyTarget, property_key} WITH_PHASE(phase)));
	if(!boolean_trap_result)
	{
		auto target_descriptor = ProxyTarget->GetOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		if(target_descriptor.has_value())
		{
			if(!target_descriptor.value().Configurable)
			{
				JS::Throw(JS::New<TypeError> __LOC("proxy 'has' attempted to hide non-configurable property value"));
			}
			else if(!ProxyTarget->IsExtensible())
			{
				JS::Throw(JS::New<TypeError> __LOC("proxy 'has' attempted to hide property value in non-extensible object"));
			}
		}
	}
	return boolean_trap_result;
}

JSValue JS::Class::Proxy::Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	// TODO: can we have XML/XMLList as a proxy target? then property_key could be other than String/Symbol, however GetOwnProperty is also checked which is not defined for XML/XMLList
	auto name = assert_property_key(property_key);

	ValidateNonRevokedProxy();

	// TODO: ES4 would likely need to call the class instance method meta::get, after looking through the instance properties

	auto trap_option = JS::GetMethod(ProxyHandler, JSString(u"get"));
	if(!trap_option.has_value())
	{
		return ProxyTarget->Get(property_key, receiver, intent WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
	}
	auto trap = trap_option.value();
	JSValue trap_result;
	if(CurrentVersion < Version::ES6)
	{
		// pre-ES6 extensions: no receiver
		trap_result = JS::Call(trap, ProxyHandler, {ProxyTarget, property_key} WITH_PHASE(phase));
	}
	else
	{
		trap_result = JS::Call(trap, ProxyHandler, {ProxyTarget, property_key, receiver} WITH_PHASE(phase));
	}
	auto target_descriptor = ProxyTarget->GetOwnProperty(name WITH_NAMESPACES(namespaces) READ_ACCESS);
	if(target_descriptor.has_value() && !target_descriptor.value().Configurable)
	{
		switch(target_descriptor.value().PropertyType)
		{
		case JSProperty::Type::Data:
			if(!target_descriptor.value().IsWritable()
			&& !JS::SameValue(trap_result, target_descriptor.value().GetValue()))
			{
				JS::Throw(JS::New<TypeError> __LOC("proxy getter attempted to overwrite non-writable property value"));
			}
			break;
		case JSProperty::Type::Accessor:
			if(target_descriptor.value().Get != nullptr
			&& !IsUndefined(trap_result))
			{
				JS::Throw(JS::New<TypeError> __LOC("proxy getter attempted to overwrite non-writable property value"));
			}
			break;
#if ENABLE_ES4
		default:
			// ignored
			break;
#endif
		}
	}
	return trap_result;
}

bool JS::Class::Proxy::Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put)
{
	auto name = assert_property_key(property_key);

	ValidateNonRevokedProxy();

	if(check_can_put)
	{
#if ENABLE_ES4
		if(Version::ES6 <= CurrentVersion || IsVersionES4())
#else
		if(Version::ES6 <= CurrentVersion)
#endif
		{
			JS::Throw(JS::New<InternalError> __LOC("invalid internal call [[CanPut]]"));
		}

		// TODO: untested, additional method to evaluate [[CanPut]] (called "canSet" here)

		auto trap_option = JS::GetMethod(ProxyHandler, JSString(u"canSet"));
		if(!trap_option.has_value())
		{
			return ProxyTarget->Set(property_key, value, receiver WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment), /* check_can_put = */ true);
		}
		auto trap = trap_option.value();
		auto boolean_trap_result = JS::ToBoolean(JS::Call(trap, ProxyHandler, {ProxyTarget, property_key} RUNTIME_PHASE));
		if(!boolean_trap_result)
		{
			return false;
		}

		auto target_descriptor = ProxyTarget->GetOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS);
		if(target_descriptor.has_value() && !target_descriptor.value().Configurable)
		{
			switch(target_descriptor.value().PropertyType)
			{
			case JSProperty::Type::Data:
				if(!target_descriptor.value().IsWritable())
				{
					// override proxy canSet method
					return false;
				}
				break;
			case JSProperty::Type::Accessor:
				if(target_descriptor.value().Set != nullptr)
				{
					// override proxy canSet method
					return false;
				}
				break;
#if ENABLE_ES4
			default:
				// ignored
				break;
#endif
			}
		}
		return true;
	}
	else
	{
		// TODO: ES4 would likely need to call the class instance method meta::set, after looking through the instance properties
		auto trap_option = JS::GetMethod(ProxyHandler, JSString(u"set"));
		if(!trap_option.has_value())
		{
			return ProxyTarget->Set(property_key, value, receiver WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment), /* check_can_put = */ false);
		}
		auto trap = trap_option.value();
		// TODO: pre-ES6 there was no receiver
		bool boolean_trap_result;
		if(CurrentVersion < Version::ES6)
		{
			// pre-ES6 extensions: no receiver
			// TODO: ES5 had a trap handling flag, probably not necessary
			boolean_trap_result = JS::ToBoolean(JS::Call(trap, ProxyHandler, {ProxyTarget, property_key, value} RUNTIME_PHASE));
		}
		else
		{
			boolean_trap_result = JS::ToBoolean(JS::Call(trap, ProxyHandler, {ProxyTarget, property_key, value, receiver} RUNTIME_PHASE));
		}
		if(!boolean_trap_result)
		{
			return false;
		}

		auto target_descriptor = ProxyTarget->GetOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS);
		if(target_descriptor.has_value() && !target_descriptor.value().Configurable)
		{
			switch(target_descriptor.value().PropertyType)
			{
			case JSProperty::Type::Data:
				if(!target_descriptor.value().IsWritable()
				&& !JS::SameValue(value, target_descriptor.value().GetValue()))
				{
					JS::Throw(JS::New<TypeError> __LOC("proxy setter attempted to overwrite non-writable property value"));
				}
				break;
			case JSProperty::Type::Accessor:
				if(target_descriptor.value().Set != nullptr)
				{
					JS::Throw(JS::New<TypeError> __LOC("proxy setter attempted to overwrite non-writable property value"));
				}
				break;
#if ENABLE_ES4
			default:
				// ignored
				break;
#endif
			}
		}
		return true;
	}
}

bool JS::Class::Proxy::Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
	auto name = assert_property_key(property_key);

	ValidateNonRevokedProxy();

	// TODO: ES4 would likely need to call the class instance method meta::delete, after looking through the instance properties

	auto trap_option = JS::GetMethod(ProxyHandler, JSString(u"deleteProperty"));
	if(!trap_option.has_value())
	{
		return ProxyTarget->Delete(property_key WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_FLAG(lexical));
	}
	auto trap = trap_option.value();

	// TODO: ES5 had a trap handling flag, probably not necessary
	auto boolean_trap_result = JS::ToBoolean(JS::Call(trap, ProxyHandler, {ProxyTarget, property_key} RUNTIME_PHASE));
	if(!boolean_trap_result)
	{
		return false;
	}

	auto target_descriptor = ProxyTarget->GetOwnProperty(name WITH_NAMESPACES(namespaces) WRITE_ACCESS);

	if(!target_descriptor.has_value())
	{
		return true;
	}
	else if(!target_descriptor.value().Configurable || !ProxyTarget->IsExtensible())
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy delete attempted to delete non-deletable property"));
	}
	return true;
}

bool JS::Class::Proxy::DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	ValidateNonRevokedProxy();
	if(CurrentVersion < Version::ES5)
	{
		return ProxyTarget->DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}

	auto trap_option = JS::GetMethod(ProxyHandler, JSString(u"defineProperty"));
	if(!trap_option.has_value())
	{
		return ProxyTarget->DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
	auto trap = trap_option.value();
	auto desc_obj = JS::FromPropertyDescriptor(descriptor);
	auto boolean_trap_result = JS::ToBoolean(JS::Call(trap, ProxyHandler, {ProxyTarget, property_key, desc_obj} RUNTIME_PHASE));
	if(!boolean_trap_result)
	{
		return false;
	}
	auto target_desc_option = ProxyTarget->GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	bool extensible_target = ProxyTarget->IsExtensible();
	bool setting_config_false = !descriptor.Configurable.value_or(true);
	if(target_desc_option.has_value())
	{
		auto target_desc = target_desc_option.value();

		if(!JS::Class::OrdinaryObject::ValidateAndApplyPropertyDescriptor(
			extensible_target,
			descriptor,
			target_desc))
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy attempted to redefine property in incompatible way"));
		}
		
		if(setting_config_false)
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy attempted to redefine property as non-configurable without also affecting target"));
		}

		if(target_desc.PropertyType == JSProperty::Type::Data
		&& !target_desc.Configurable
		&& target_desc.IsWritable()
		&& !descriptor.Writable.value_or(true))
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy attempted to change writable property to non-writable on non-configurable target"));
		}
	}
	else
	{
		if(!extensible_target)
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy attempted to define new property on non-extensible object"));
		}
		else if(setting_config_false)
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy attempted to define new non-configurable property without also adding it to target"));
		}
	}
	return true;
}

std::vector<JSPropertyKey> JS::Class::Proxy::OwnPropertyKeys(FOR_CLASS(limit))
{
	// TODO: untested
	ValidateNonRevokedProxy();
	auto trap_option = JS::GetMethod(ProxyHandler, JSString(u"ownKeys"));
	if(!trap_option.has_value())
	{
		return ProxyTarget->OwnPropertyKeys(VIA_CLASS(limit));
	}
	auto trap = trap_option.value();
	auto trap_result_array = JS::Call(trap, ProxyHandler, {ProxyTarget} RUNTIME_PHASE);
	auto trap_result = JS::CreateListFromArrayLike<JSPropertyKey>(trap_result_array);
	std::set<JSPropertyKey> unchecked_result_keys;
	for(auto key : trap_result)
	{
		if(unchecked_result_keys.find(key) != unchecked_result_keys.end())
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy ownKeys returned duplicate entries"));
		}
		unchecked_result_keys.insert(key);
	}
	bool extensible_target = ProxyTarget->IsExtensible();
	auto target_keys = ProxyTarget->OwnPropertyKeys(VIA_CLASS(limit));
	std::vector<JSPropertyKey> target_configurable_keys;
	std::vector<JSPropertyKey> target_nonconfigurable_keys;
	for(auto key : target_keys)
	{
		auto desc = ProxyTarget->GetOwnProperty(key PUBLIC_NAMESPACES READ_WRITE_ACCESS);
		if(desc.has_value() && !desc.value().Configurable)
		{
			target_nonconfigurable_keys.push_back(key);
		}
		else
		{
			target_configurable_keys.push_back(key);
		}
	}
	if(extensible_target && target_nonconfigurable_keys.empty())
	{
		return trap_result;
	}
	for(auto key : target_nonconfigurable_keys)
	{
		auto result_key_iter = unchecked_result_keys.find(key);
		if(result_key_iter == unchecked_result_keys.end())
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy ownKeys removed non-configurable property"));
		}
		unchecked_result_keys.erase(result_key_iter);
	}
	if(extensible_target)
	{
		return trap_result;
	}
	for(auto key : target_configurable_keys)
	{
		auto result_key_iter = unchecked_result_keys.find(key);
		if(result_key_iter == unchecked_result_keys.end())
		{
			JS::Throw(JS::New<TypeError> __LOC("proxy ownKeys removed property from non-extensible target"));
		}
		unchecked_result_keys.erase(result_key_iter);
	}
	if(!unchecked_result_keys.empty())
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy ownKeys supplied additional properties for non-extensible target"));
	}
	return trap_result;
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::Proxy::ExtractAllProperties()
{
	// this is only used for debugging, but proxy objects have no internal slots
	return {};
}

std::map<JSString, JSValue> JS::Class::Proxy::GetInternalSlots([[maybe_unused]] bool unified)
{
	std::map<JSString, JSValue> slots;
	slots[u"ProxyHandler"] = ProxyHandler;
	slots[u"ProxyTarget"] = ProxyTarget;
	return slots;
}

bool JS::Class::Proxy::IsArray(JSObject object)
{
	for(gcn::gc_ptr<Proxy> proxy = std::dynamic_pointer_cast<Proxy>(object);
		proxy != nullptr;
		proxy = std::dynamic_pointer_cast<Proxy>(proxy->ProxyTarget))
	{
		if(proxy->ProxyTarget == nullptr)
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
		if(proxy->ProxyHandler == nullptr)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
	}
	return bool(std::dynamic_pointer_cast<Array>(object));
}

JSString JS::Class::Proxy::GetClassString()
{
	return IsArray(JSObject(this)) ? u"Array" : u"Object";
}

JS::Invocability JS::Class::Proxy::GetInvocability()
{
	return ProxyTarget != nullptr ? ProxyTarget->GetInvocability() : Invocability::NonCallable;
}

JSValue JS::Class::Proxy::Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	if(GetInvocability() == Invocability::NonCallable)
	{
		JS::Throw(JS::New<TypeError> __LOC("called non-callable object"));
	}

	ValidateNonRevokedProxy();
	auto trap_option = JS::GetMethod(ProxyHandler, u"apply");
	if(!trap_option.has_value())
	{
		return ProxyTarget->Call(this_argument, arguments WITH_PHASE(phase));
	}
	auto trap = trap_option.value();
	auto argArray = JS::New<JS::Class::Array>(arguments);
	return JS::Call(trap, ProxyHandler, {ProxyTarget, this_argument, argArray} WITH_PHASE(phase));
}

JSValue JS::Class::Proxy::Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(GetInvocability() != Invocability::Constructor)
	{
		JS::Throw(JS::New<TypeError> __LOC("called constructor on non-constructable object"));
	}

	ValidateNonRevokedProxy();
	auto trap_option = JS::GetMethod(ProxyHandler, u"construct");
	if(!trap_option.has_value())
	{
		return ProxyTarget->Construct(arguments, new_target WITH_PHASE(phase));
	}
	auto trap = trap_option.value();
	auto argArray = JS::New<JS::Class::Array>(arguments);
	JSValue new_obj;
	if(Version::ES6 <= CurrentVersion)
	{
		new_obj = JS::Call(trap, ProxyHandler, {ProxyTarget, argArray, new_target} WITH_PHASE(phase));
	}
	else
	{
		new_obj = JS::Call(trap, ProxyHandler, {ProxyTarget, argArray} WITH_PHASE(phase));
	}
	if(!GetNonNullObject(new_obj))
	{
		JS::Throw(JS::New<TypeError> __LOC("proxy construct returned non-object result"));
	}
	return new_obj;
}

//// Map

//// Set

//// WeakMap

//// WeakSet

//// AbstractArrayBuffer

//// ArrayBuffer

JSObject JS::Runtime::Intrinsics::* JS::Class::ArrayBuffer::IntrinsicPrototype = &JS::Runtime::Intrinsics::ArrayBuffer_prototype;

uintptr_t JS::Class::ArrayBuffer::ByteLength() const
{
	return ArrayBufferData->size();
}

bool JS::Class::ArrayBuffer::IsDetached() const
{
	return ArrayBufferData != nullptr;
}

void JS::Class::ArrayBuffer::GetBytesFromBuffer(uintptr_t byteIndex, std::vector<std::byte>& rawValue, uintptr_t elementSize, [[maybe_unused]] bool isTypedArray, [[maybe_unused]] bool isOrdered)
{
	rawValue.clear();
	rawValue.insert(rawValue.begin(), ArrayBufferData->begin() + byteIndex, ArrayBufferData->begin() + byteIndex + elementSize);
}

void JS::Class::ArrayBuffer::SetBytesInBuffer(uintptr_t byteIndex, std::vector<std::byte>& rawValue, uintptr_t elementSize, [[maybe_unused]] bool isTypedArray, [[maybe_unused]] bool isOrdered)
{
	std::copy(rawValue.begin(), rawValue.begin() + elementSize, ArrayBufferData->begin() + byteIndex);
}

//// FixedArrayBuffer

bool JS::Class::FixedArrayBuffer::IsFixedLength() const
{
	return true;
}

std::map<JSString, JSValue> JS::Class::FixedArrayBuffer::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	// TODO
	//slots[u"ArrayBufferData"]
	if(ArrayBufferData != nullptr)
	{
		slots[u"ArrayBufferByteLength"] = JSBigInt(ArrayBufferData->size());
	}
	else
	{
		slots[u"ArrayBufferByteLength"] = JS_null;
	}
	//slots[u"ArrayBufferDetachKey"]
	return slots;
}

//// GrowableArrayBuffer

JS::Class::GrowableArrayBuffer::GrowableArrayBuffer(uintptr_t byte_length, uintptr_t max_byte_length)
	: ArrayBuffer(byte_length > max_byte_length ? 0 : byte_length), ArrayBufferMaxByteLength(max_byte_length)
{
	if(byte_length > max_byte_length)
	{
		JS::Throw(JS::New<RangeError> __LOC(u"Initial ArrayBuffer size larger than maximum size"));
	}
}

bool JS::Class::GrowableArrayBuffer::IsFixedLength() const
{
	return false;
}

std::map<JSString, JSValue> JS::Class::GrowableArrayBuffer::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	// TODO
	//slots[u"ArrayBufferData"]
	if(ArrayBufferData != nullptr)
	{
		slots[u"ArrayBufferByteLength"] = JSBigInt(ArrayBufferData->size());
	}
	else
	{
		slots[u"ArrayBufferByteLength"] = JS_null;
	}
	slots[u"ArrayBufferMaxByteLength"] = JSBigInt(ArrayBufferMaxByteLength);
	//slots[u"ArrayBufferDetachKey"]
	return slots;
}

//// SharedArrayBuffer

JSObject JS::Runtime::Intrinsics::* JS::Class::SharedArrayBuffer::IntrinsicPrototype = &JS::Runtime::Intrinsics::SharedArrayBuffer_prototype;

bool JS::Class::SharedArrayBuffer::IsDetached() const
{
	return false;
}

void JS::Class::SharedArrayBuffer::GetBytesFromBuffer([[maybe_unused]] uintptr_t byteIndex, [[maybe_unused]] std::vector<std::byte>& rawValue, [[maybe_unused]] uintptr_t elementSize, [[maybe_unused]] bool isTypedArray, [[maybe_unused]] bool isOrdered)
{
	rawValue.clear();
	// TODO
}

void JS::Class::SharedArrayBuffer::SetBytesInBuffer([[maybe_unused]] uintptr_t byteIndex, [[maybe_unused]] std::vector<std::byte>& rawValue, [[maybe_unused]] uintptr_t elementSize, [[maybe_unused]] bool isTypedArray, [[maybe_unused]] bool isOrdered)
{
	// TODO
}

//// FixedSharedArrayBuffer

bool JS::Class::FixedSharedArrayBuffer::IsFixedLength() const
{
	return true;
}

uintptr_t JS::Class::FixedSharedArrayBuffer::ByteLength() const
{
	return ArrayBufferData->size();
}

std::map<JSString, JSValue> JS::Class::FixedSharedArrayBuffer::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	// TODO
	return slots;
}

//// GrowableSharedArrayBuffer

JS::Class::GrowableSharedArrayBuffer::GrowableSharedArrayBuffer(uintptr_t byte_length, uintptr_t max_byte_length)
	: SharedArrayBuffer(byte_length > max_byte_length ? 0 : byte_length), ArrayBufferByteLengthData(byte_length)
{
	if(byte_length > max_byte_length)
	{
		JS::Throw(JS::New<RangeError> __LOC(u"Initial SharedArrayBuffer size larger than maximum size"));
	}
}

bool JS::Class::GrowableSharedArrayBuffer::IsFixedLength() const
{
	return false;
}

uintptr_t JS::Class::GrowableSharedArrayBuffer::ByteLength() const
{
	return ArrayBufferByteLengthData;
}

std::map<JSString, JSValue> JS::Class::GrowableSharedArrayBuffer::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	// TODO
	return slots;
}

//// DataView

//// Promise

//// Reflect

//// EvalError

JSObject JS::Runtime::Intrinsics::* JS::Class::EvalError::IntrinsicPrototype = &JS::Runtime::Intrinsics::EvalError_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::EvalError::IntrinsicClass = &JS::Runtime::Intrinsics::EvalError_class;
#endif

//// RangeError

JSObject JS::Runtime::Intrinsics::* JS::Class::RangeError::IntrinsicPrototype = &JS::Runtime::Intrinsics::RangeError_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::RangeError::IntrinsicClass = &JS::Runtime::Intrinsics::RangeError_class;
#endif

//// ReferenceError

JSObject JS::Runtime::Intrinsics::* JS::Class::ReferenceError::IntrinsicPrototype = &JS::Runtime::Intrinsics::ReferenceError_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::ReferenceError::IntrinsicClass = &JS::Runtime::Intrinsics::ReferenceError_class;
#endif

//// SyntaxError

JSObject JS::Runtime::Intrinsics::* JS::Class::SyntaxError::IntrinsicPrototype = &JS::Runtime::Intrinsics::SyntaxError_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::SyntaxError::IntrinsicClass = &JS::Runtime::Intrinsics::SyntaxError_class;
#endif

//// TypeError

JSObject JS::Runtime::Intrinsics::* JS::Class::TypeError::IntrinsicPrototype = &JS::Runtime::Intrinsics::TypeError_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::TypeError::IntrinsicClass = &JS::Runtime::Intrinsics::TypeError_class;
#endif

//// URIError

JSObject JS::Runtime::Intrinsics::* JS::Class::URIError::IntrinsicPrototype = &JS::Runtime::Intrinsics::URIError_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::URIError::IntrinsicClass = &JS::Runtime::Intrinsics::URIError_class;
#endif

#if ENABLE_ES4
//// ArgumentError

JSObject JS::Runtime::Intrinsics::* JS::Class::ArgumentError::IntrinsicPrototype = &JS::Runtime::Intrinsics::ArgumentError_prototype;
JSClass JS::Runtime::Intrinsics::* JS::Class::ArgumentError::IntrinsicClass = &JS::Runtime::Intrinsics::ArgumentError_class;

//// AttributeError

JSObject JS::Runtime::Intrinsics::* JS::Class::AttributeError::IntrinsicPrototype = &JS::Runtime::Intrinsics::AttributeError_prototype;
JSClass JS::Runtime::Intrinsics::* JS::Class::AttributeError::IntrinsicClass = &JS::Runtime::Intrinsics::AttributeError_class;

//// ConstantError

JSObject JS::Runtime::Intrinsics::* JS::Class::ConstantError::IntrinsicPrototype = &JS::Runtime::Intrinsics::ConstantError_prototype;
JSClass JS::Runtime::Intrinsics::* JS::Class::ConstantError::IntrinsicClass = &JS::Runtime::Intrinsics::ConstantError_class;

//// DefinitionError

JSObject JS::Runtime::Intrinsics::* JS::Class::DefinitionError::IntrinsicPrototype = &JS::Runtime::Intrinsics::DefinitionError_prototype;
JSClass JS::Runtime::Intrinsics::* JS::Class::DefinitionError::IntrinsicClass = &JS::Runtime::Intrinsics::DefinitionError_class;

//// UninitializedError

JSObject JS::Runtime::Intrinsics::* JS::Class::UninitializedError::IntrinsicPrototype = &JS::Runtime::Intrinsics::UninitializedError_prototype;
JSClass JS::Runtime::Intrinsics::* JS::Class::UninitializedError::IntrinsicClass = &JS::Runtime::Intrinsics::UninitializedError_class;

#endif

//// Arguments

JSObject JS::Runtime::Intrinsics::* JS::Class::Arguments::IntrinsicPrototype = &JS::Runtime::Intrinsics::Object_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::Arguments::IntrinsicClass = &JS::Runtime::Intrinsics::Array_class;
#endif

std::optional<JSProperty> JS::Class::Arguments::GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
#if ENABLE_ES4
	if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
	{
		if(auto property_option = std::get_if<JSString>(&property_key))
		{
			uint32_t index;
			if(is_array_index(*property_option, index))
			{
				if(index < parameter_map.size() && parameter_map[index].has_value())
				{
					auto mapping = parameter_map[index].value();
					return JSProperty::MakeData(
						environment->GetBindingValue(JSIdentifierReference(mapping.Name) PUBLIC_NAMESPACES RUNTIME_PHASE),
						/* Writable = */ true,
						/* Enumerable = */ mapping.Enumerable,
						/* Configurable = */ mapping.Configurable);
				}
			}
		}
	}
	return OrdinaryObject::GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
}

JSValue JS::Class::Arguments::Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(using_environment) AND_PHASE(phase))
{
#if ENABLE_ES4
	if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
	{
		if(auto property_option = GetString(property_key))
		{
			uint32_t index;
			if(is_array_index(*property_option, index))
			{
				if(index < parameter_map.size() && parameter_map[index].has_value())
				{
					auto mapping = parameter_map[index].value();
					return environment->GetBindingValue(JSIdentifierReference(mapping.Name) PUBLIC_NAMESPACES WITH_PHASE(phase));
				}
			}
		}
	}
	return OrdinaryObject::Get(property_key, receiver, intent WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
}

bool JS::Class::Arguments::Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(using_environment), bool check_can_put)
{
	if(this == receiver)
	{
#if ENABLE_ES4
		if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
		{
			if(auto property_option = GetString(property_key))
			{
				uint32_t index;
				if(is_array_index(*property_option, index))
				{
					if(index < parameter_map.size() && parameter_map[index].has_value())
					{
						if(!check_can_put)
						{
							auto mapping = parameter_map[index].value();
							environment->SetMutableBinding(JSIdentifierReference(mapping.Name), value PUBLIC_NAMESPACES);
						}
						return true;
					}
				}
			}
		}
	}
	return OrdinaryObject::Set(property_key, value, receiver WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment), check_can_put);
}

bool JS::Class::Arguments::Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
#if ENABLE_ES4
	if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
	{
		if(auto property_option = GetString(property_key))
		{
			uint32_t index;
			if(is_array_index(*property_option, index))
			{
				if(index < parameter_map.size() && parameter_map[index].has_value())
				{
					auto& mapping = parameter_map[index].value();
					if(!environment->DeleteBinding(mapping.Name PUBLIC_NAMESPACES))
					{
						return false;
					}
					mapping = {};
					return true;
				}
			}
		}
	}

	return OrdinaryObject::Delete(property_key WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_FLAG(lexical));
}

bool JS::Class::Arguments::DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(auto property_option = std::get_if<JSString>(&property_key))
	{
#if ENABLE_ES4
		if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
		{
			uint32_t index;

#if 0
			if(is_array_index(*property_option, index))
			{
				auto property_option = GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
				if(property_option.has_value() && property_option.value().PropertyType == JSProperty::Type::Alias)
				{
					auto resolved_property = property_option.value().ResolveProperty();
					if(descriptor.is_data_descriptor() && descriptor.Writable.has_value() && descriptor.Writable.value() == false)
					{
						if(!descriptor.Value.has_value())
						{
							descriptor = descriptor.withValue(resolved_property.GetValue());
						}
						if(!descriptor.Enumerable.has_value())
						{
							descriptor = descriptor.withEnumerable(resolved_property.Enumerable);
						}
						if(!descriptor.Configurable.has_value())
						{
							descriptor = descriptor.withConfigurable(resolved_property.Configurable);
						}
					}
					else if(descriptor.is_accessor_descriptor())
					{
						if(!descriptor.Enumerable.has_value())
						{
							descriptor = descriptor.withEnumerable(resolved_property.Enumerable);
						}
						if(!descriptor.Configurable.has_value())
						{
							descriptor = descriptor.withConfigurable(resolved_property.Configurable);
						}
					}
					else
					{
						return OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
					}

					if(!resolved_property.Configurable)
					{
						return false;
					}

					Delete(property_key WITH_NAMESPACES(namespaces) WITH_CLASS(nullptr) WITH_FLAG(false));

					return OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
				}
			}
#endif
			if(is_array_index(*property_option, index)
			&& index < parameter_map.size()
			&& parameter_map[index].has_value())
			{
				auto& mapping = parameter_map[index].value();
				if(descriptor.is_data_descriptor() && descriptor.Writable.has_value() && descriptor.Writable.value() == false)
				{
					if(!descriptor.Value.has_value())
					{
						descriptor = descriptor.withValue(
							environment->GetBindingValue(JSIdentifierReference(mapping.Name) PUBLIC_NAMESPACES RUNTIME_PHASE));
					}
					if(!descriptor.Enumerable.has_value())
					{
						descriptor = descriptor.withEnumerable(mapping.Enumerable);
					}
					if(!descriptor.Configurable.has_value())
					{
						descriptor = descriptor.withConfigurable(mapping.Configurable);
					}
				}
				else if(descriptor.is_accessor_descriptor())
				{
					if(!descriptor.Enumerable.has_value())
					{
						descriptor = descriptor.withEnumerable(mapping.Enumerable);
					}
					if(!descriptor.Configurable.has_value())
					{
						descriptor = descriptor.withConfigurable(mapping.Configurable);
					}
				}
				else
				{
					// simulate DefineOwnProperty on the arguments properties
					auto updated_property = ValidateAndApplyPropertyDescriptor(
						IsExtensible(),
						descriptor.withoutValue(),
						JSProperty::MakeData(JS_undefined, /* Writable = */ true, mapping.Enumerable, mapping.Configurable));

					if(!updated_property.has_value())
					{
						return false;
					}

					if(descriptor.Value.has_value())
					{
						environment->SetMutableBinding(JSIdentifierReference(mapping.Name), descriptor.Value.value() PUBLIC_NAMESPACES);
					}
					mapping.Enumerable = updated_property.value().Enumerable;
					mapping.Configurable = updated_property.value().Configurable;
					return true;
				}

				if(!OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
				{
					return false;
				}
				parameter_map[index] = {};
				return true;

				// TODO: integrate the confusingly different ES4 semantics
			}
		}
	}
	return OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::Arguments::ExtractAllProperties()
{
	std::map<JS::Class::Object::PropertyKeyData, JSProperty> result = OrdinaryObject::ExtractAllProperties();
	for(uint32_t index = 0; index < parameter_map.size(); index++)
	{
		if(parameter_map[index].has_value())
		{
			PropertyKeyData data;
#if ENABLE_ES4
			data.PropertyKey = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
			data.Namespaces = {JS_public};
			data.Accesses = AccessSet::ReadWrite;
#else
			data = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
#endif
			auto mapping = parameter_map[index].value();
			result[data] = JSProperty::MakeData(
				environment->GetBindingValue(JSIdentifierReference(mapping.Name) PUBLIC_NAMESPACES RUNTIME_PHASE),
				/* Writable = */ true,
				/* Enumerable = */ mapping.Enumerable,
				/* Configurable = */ mapping.Configurable);
		}
	}
	return result;
}

JSString JS::Class::Arguments::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"Arguments";
}

std::map<JSString, JSValue> JS::Class::Arguments::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	if(Version::ES5 <= CurrentVersion)
	{
#if ENABLE_ES4
		// TODO
#else
		// create a simulated ParameterMap object according to ECMAScript
		auto ParameterMap = JS::New<JS::Class::OrdinaryObject>();
		for(size_t index = 0; index < parameter_map.size(); index++)
		{
			if(parameter_map[index].has_value())
			{
				auto name = parameter_map[index].value().Name;
				ParameterMap->DefineOwnProperty(
					JS::ToString(JSNumber(index) RUNTIME_PHASE),
					JSPropertyDescriptor().withGetter(
						// note: the getter is not actually used
						JS::CreateBuiltinFunction([this, name]([[maybe_unused]] std::optional<Public::JSValue> this_argument, [[maybe_unused]] std::vector<Public::JSValue> arguments, [[maybe_unused]] Public::JSFunction new_target AND_PHASE(phase))
						{
							return environment->GetBindingValue(JSIdentifierReference(name) PUBLIC_NAMESPACES WITH_PHASE(phase));
						}, 0, u"", Intrinsics()->Function_prototype, GetCurrentRealm())
					).withSetter(
						// note: the setter is not actually used
						JS::CreateBuiltinFunction([this, name]([[maybe_unused]] std::optional<Public::JSValue> this_argument, std::vector<Public::JSValue> arguments, [[maybe_unused]] Public::JSFunction new_target AND_PHASE(phase))
						{
							environment->SetMutableBinding(JSIdentifierReference(name), arguments.size() > 0 ? arguments[0] : JS_undefined PUBLIC_NAMESPACES);
							return JS_undefined;
						}, 1, u"", Intrinsics()->Function_prototype, GetCurrentRealm())
					).withEnumerable(false)
					.withConfigurable(true)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS);
			}
		}
		slots[u"ParameterMap"] = ParameterMap;
#endif
	}
	return slots;
}

//// BoundFunction

JSString JS::Class::BoundFunction::GetClassString()
{
	return u"Function";
}

#if ENABLE_ES4
std::optional<JS::Runtime::SignatureType> JS::Class::BoundFunction::GetSignature()
{
	return {};
}
#endif

JS::Invocability JS::Class::BoundFunction::GetInvocability()
{
	return BoundTargetFunction.GetInvocability();
}

JSValue JS::Class::BoundFunction::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	std::vector<JSValue> new_arguments(BoundArguments);
	new_arguments.insert(new_arguments.end(), arguments.begin(), arguments.end());
	return BoundTargetFunction.Call(BoundThis, new_arguments WITH_PHASE(phase));
}

JSValue JS::Class::BoundFunction::Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	std::vector<JSValue> new_arguments(BoundArguments);
	new_arguments.insert(new_arguments.end(), arguments.begin(), arguments.end());

	return BoundTargetFunction.Construct(new_arguments, JS::SameValue(this, new_target) ? BoundTargetFunction : new_target WITH_PHASE(phase));
}

JS::Runtime::Realm * JS::Class::BoundFunction::GetFunctionRealm()
{
	return BoundTargetFunction.GetFunctionRealm();
}

std::map<JSString, JSValue> JS::Class::BoundFunction::GetInternalSlots([[maybe_unused]] bool unified)
{
	std::map<JSString, JSValue> slots;
	slots[u"TargetFunction"] = BoundTargetFunction;
	slots[u"BoundThis"] = BoundThis;
	//slots[u"BoundArguments"] = BoundArguments; // TODO: convert vector to array?
	return slots;
}

//// DefaultConstructor

JSValue JS::Class::DefaultConstructor::Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	JS::Runtime::CallContextCapture context_capture;

	// PrepareForOrdinaryCall
	auto callee_context = new JS::Runtime::ExecutionContext();
	callee_context->Function = JSFunction::MakeFunction(this);
	callee_context->Realm = Realm;
	callee_context->ScriptOrModule = ScriptOrModule;
	JS::Runtime::ExecutionContext::GetContextStack().push_back(callee_context);

	return Execute(this_argument, arguments, nullptr WITH_PHASE(phase));
}

JSValue JS::Class::DefaultConstructor::Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	JS::Runtime::CallContextCapture context_capture;

	// PrepareForOrdinaryCall
	auto callee_context = new JS::Runtime::ExecutionContext();
	callee_context->Function = JSFunction::MakeFunction(this);
	callee_context->Realm = Realm;
	callee_context->ScriptOrModule = ScriptOrModule;
	JS::Runtime::ExecutionContext::GetContextStack().push_back(callee_context);

	return Execute(std::optional<JSValue>{}, arguments, new_target RUNTIME_PHASE);
}

JSValue JS::Class::DefaultConstructor::Execute([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	// ES6
	if(new_target == nullptr)
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}

	gcn::gc_ptr<JS::Class::Function> function = GetCurrentExecutionContext()->Function.cast_object<JS::Class::Function>();
	if(function == nullptr)
	{
		JS::Throw(JS::New<InternalError> __LOC("default constructor executed within non-ordinary function context"));
	}
	JSObject result;

	switch(function->GetConstructorKind())
	{
	case JS::Class::Function::ConstructorKindType::NonConstructor:
		JS::Throw(JS::New<InternalError> __LOC());
	case JS::Class::Function::ConstructorKindType::Derived:
		{
			//if(auto super_call = dynamic_cast<JSFunction>(GetNonNullObject(function->GetPrototypeOf().value_or(JS_null)).value_or(nullptr)))
			if(auto super_call = GetNonNullObject(function->GetPrototypeOf().value_or(JS_null)).value_or(nullptr))
			{
				if(super_call->GetInvocability() != JS::Invocability::Constructor)
				{
					JS::Throw(JS::New<TypeError> __LOC());
				}

				result = GetNonNullObject(super_call->Construct(arguments, new_target WITH_PHASE(phase))).value_or(nullptr);
			}
			else
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
		}
		break;
	case JS::Class::Function::ConstructorKindType::Base:
		{
			auto proto_value = JS::Get(new_target, u"prototype");
			if(auto proto_object = JS::GetNonNullObject(proto_value))
			{
				result = JS::NewWithPrototype<JS::Class::OrdinaryObject>(proto_object.value());
			}
			else
			{
				result = JS::New<JS::Class::OrdinaryObject>();
			}
		}
		break;
	}

	function->InitializeInstanceElements(result);
	return result;
}

//// TypedArray

uint8_t JS::Class::TypedArray::RawBytesToUint8(std::vector<std::byte> raw_bytes)
{
	return *reinterpret_cast<int8_t *>(raw_bytes.data());
}

uint16_t JS::Class::TypedArray::RawBytesToUint16(std::vector<std::byte> raw_bytes, bool is_little_endian)
{
	uint16_t value = *reinterpret_cast<int16_t *>(raw_bytes.data());
	if(is_little_endian != GetSurroundingAgent()->LittleEndian)
	{
		value = std::byteswap(value);
	}
	return value;
}

uint32_t JS::Class::TypedArray::RawBytesToUint32(std::vector<std::byte> raw_bytes, bool is_little_endian)
{
	uint32_t value = *reinterpret_cast<int32_t *>(raw_bytes.data());
	if(is_little_endian != GetSurroundingAgent()->LittleEndian)
	{
		value = std::byteswap(value);
	}
	return value;
}

uint64_t JS::Class::TypedArray::RawBytesToUint64(std::vector<std::byte> raw_bytes, bool is_little_endian)
{
	uint64_t value = *reinterpret_cast<int64_t *>(raw_bytes.data());
	if(is_little_endian != GetSurroundingAgent()->LittleEndian)
	{
		value = std::byteswap(value);
	}
	return value;
}

float16_t JS::Class::TypedArray::Uint16ToFloat16(uint16_t value)
{
	return *reinterpret_cast<float16_t *>(&value);
}

float32_t JS::Class::TypedArray::Uint32ToFloat32(uint32_t value)
{
	return *reinterpret_cast<float32_t *>(&value);
}

float64_t JS::Class::TypedArray::Uint64ToFloat64(uint64_t value)
{
	return *reinterpret_cast<float64_t *>(&value);
}

JSValue JS::Class::TypedArray::RawBytesToNumeric(ElementType type, std::vector<std::byte> raw_bytes, bool is_little_endian)
{
	switch(type)
	{
	case ElementType::Int8:
		return JSNumber(int8_t(RawBytesToUint8(raw_bytes)));
	case ElementType::Uint8:
	case ElementType::Uint8Clamped:
		return JSNumber(RawBytesToUint8(raw_bytes));
	case ElementType::Int16:
		return JSNumber(int16_t(RawBytesToUint16(raw_bytes, is_little_endian)));
	case ElementType::Uint16:
		return JSNumber(RawBytesToUint16(raw_bytes, is_little_endian));
	case ElementType::Int32:
		return JSNumber(int32_t(RawBytesToUint32(raw_bytes, is_little_endian)));
	case ElementType::Uint32:
		return JSNumber(RawBytesToUint32(raw_bytes, is_little_endian));
	case ElementType::BigInt64:
		if(Version::ES11 <= CurrentVersion)
		{
			return JSBigInt(int64_t(RawBytesToUint64(raw_bytes, is_little_endian)));
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC("BigInt64Array not supported before ES11"));
		}
		break;
	case ElementType::BigUint64:
		if(Version::ES11 <= CurrentVersion)
		{
			return JSBigInt(RawBytesToUint64(raw_bytes, is_little_endian));
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC("BigUint64Array not supported before ES11"));
		}
		break;
	case ElementType::Float16:
		if(Version::ES16 <= CurrentVersion)
		{
			return JSNumber(Uint16ToFloat16(RawBytesToUint16(raw_bytes, is_little_endian)));
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC("Float16Array not supported before ES16"));
		}
		break;
	case ElementType::Float32:
		return JSNumber(Uint32ToFloat32(RawBytesToUint32(raw_bytes, is_little_endian)));
	case ElementType::Float64:
		return JSNumber(Uint64ToFloat64(RawBytesToUint64(raw_bytes, is_little_endian)));
	default:
		JS::Throw(JS::New<InternalError> __LOC("aberrant TypedArray type"));
	}
}

void JS::Class::TypedArray::Uint8ToRawBytes(std::vector<std::byte>& raw_bytes, uint8_t value)
{
	*reinterpret_cast<int8_t *>(raw_bytes.data()) = value;
}

void JS::Class::TypedArray::Uint16ToRawBytes(std::vector<std::byte>& raw_bytes, uint16_t value, bool is_little_endian)
{
	if(is_little_endian != GetSurroundingAgent()->LittleEndian)
	{
		value = std::byteswap(value);
	}
	*reinterpret_cast<int16_t *>(raw_bytes.data()) = value;
}

void JS::Class::TypedArray::Uint32ToRawBytes(std::vector<std::byte>& raw_bytes, uint32_t value, bool is_little_endian)
{
	if(is_little_endian != GetSurroundingAgent()->LittleEndian)
	{
		value = std::byteswap(value);
	}
	*reinterpret_cast<int32_t *>(raw_bytes.data()) = value;
}

void JS::Class::TypedArray::Uint64ToRawBytes(std::vector<std::byte>& raw_bytes, uint64_t value, bool is_little_endian)
{
	if(is_little_endian != GetSurroundingAgent()->LittleEndian)
	{
		value = std::byteswap(value);
	}
	*reinterpret_cast<int64_t *>(raw_bytes.data()) = value;
}

uint16_t JS::Class::TypedArray::Float16ToUint16(float16_t value)
{
	return *reinterpret_cast<uint16_t *>(&value);
}

uint32_t JS::Class::TypedArray::Float32ToUint32(float32_t value)
{
	return *reinterpret_cast<uint32_t *>(&value);
}

uint64_t JS::Class::TypedArray::Float64ToUint64(float64_t value)
{
	return *reinterpret_cast<uint64_t *>(&value);
}

void JS::Class::TypedArray::NumericToRawBytes(ElementType type, std::vector<std::byte>& raw_bytes, JSValue value, bool is_little_endian)
{
	fenv_t env;

	switch(type)
	{
	case ElementType::Int8:
		Uint8ToRawBytes(raw_bytes, uint8_t(JS::ToInt8(value)));
		break;
	case ElementType::Uint8:
		Uint8ToRawBytes(raw_bytes, JS::ToUint8(value));
		break;
	case ElementType::Uint8Clamped:
		Uint8ToRawBytes(raw_bytes, JS::ToUint8Clamp(value));
		break;
	case ElementType::Int16:
		Uint16ToRawBytes(raw_bytes, uint16_t(JS::ToInt16(value)), is_little_endian);
		break;
	case ElementType::Uint16:
		Uint16ToRawBytes(raw_bytes, JS::ToUint16(value), is_little_endian);
		break;
	case ElementType::Int32:
		Uint32ToRawBytes(raw_bytes, uint32_t(JS::ToInt32(value RUNTIME_PHASE)), is_little_endian);
		break;
	case ElementType::Uint32:
		Uint32ToRawBytes(raw_bytes, JS::ToUint32(value RUNTIME_PHASE), is_little_endian);
		break;
	case ElementType::BigInt64:
		if(Version::ES11 <= CurrentVersion)
		{
			Uint64ToRawBytes(raw_bytes, uint64_t(JS::ToBigInt64(value)), is_little_endian);
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC("BigInt64Array not supported before ES11"));
		}
		break;
	case ElementType::BigUint64:
		if(Version::ES11 <= CurrentVersion)
		{
			Uint64ToRawBytes(raw_bytes, JS::ToBigUint64(value), is_little_endian);
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC("BigUint64Array not supported before ES11"));
		}
		break;
	case ElementType::Float16:
		if(Version::ES16 <= CurrentVersion)
		{
			fegetenv(&env);
			fesetround(FE_TONEAREST);
			Uint16ToRawBytes(raw_bytes, Float16ToUint16(std::float16_t(JS::ToNumber(value RUNTIME_PHASE))), is_little_endian);
			fesetenv(&env);
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC("Float16Array not supported before ES16"));
		}
		break;
	case ElementType::Float32:
		fegetenv(&env);
		fesetround(FE_TONEAREST);
		Uint32ToRawBytes(raw_bytes, Float32ToUint32(std::float32_t(JS::ToNumber(value RUNTIME_PHASE))), is_little_endian);
		fesetenv(&env);
		break;
	case ElementType::Float64:
		Uint64ToRawBytes(raw_bytes, Float64ToUint64(JS::ToNumber(value RUNTIME_PHASE)), is_little_endian);
		break;
	default:
		JS::Throw(JS::New<InternalError> __LOC("aberrant TypedArray type"));
	}
}

std::map<JSString, JSValue> JS::Class::TypedArray::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	// TODO: ViewedArrayBuffer, ArrayLength, ByteOffset, TypedArrayName
	return slots;
}

JS::Class::TypedArray::TypedArray(ElementType TypedArrayName, uintptr_t length)
	: TypedArrayName(TypedArrayName),
		ViewedArrayBuffer(JS::New<JS::Class::FixedArrayBuffer>(ElementSize(TypedArrayName) * length)),
		ByteLength(ElementSize(TypedArrayName) * length),
		ByteOffset(0),
		ArrayLength(length)
{
}

bool JS::Class::TypedArray::IsFixedLength()
{
	return ArrayLength.has_value() && std::dynamic_pointer_cast<JS::Class::FixedArrayBuffer>(ViewedArrayBuffer);
}

bool JS::Class::TypedArray::PreventExtensions()
{
	return IsFixedLength() && OrdinaryObject::PreventExtensions();
}

std::optional<JSProperty> JS::Class::TypedArray::GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(auto property_string = GetString(property_key))
	{
		if(auto numeric_index = CanonicalNumericIndexString(property_string.value()))
		{
			if(auto value = GetElement(numeric_index.value()))
			{
				return JSPropertyDescriptor().withValue(value.value()).withWritable(true).withEnumerable(true).withConfigurable(true);
			}
			else
			{
				return {};
			}
		}
	}
	return OrdinaryObject::GetOwnProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
}

bool JS::Class::TypedArray::HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	if(auto property_string = GetString(property_key))
	{
		if(auto numeric_index = CanonicalNumericIndexString(property_string.value()))
		{
			return IsValidIntegerIndex(numeric_index.value()).has_value();
		}
	}
	return OrdinaryObject::HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
}

JSValue JS::Class::TypedArray::Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	if(auto property_string = GetString(property_key))
	{
		if(auto numeric_index = CanonicalNumericIndexString(property_string.value()))
		{
			return GetElement(numeric_index.value()).value_or(JS_undefined);
		}
	}
	return OrdinaryObject::Get(property_key, receiver, intent WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
}

bool JS::Class::TypedArray::Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put)
{
	if(auto property_string = GetString(property_key))
	{
		if(auto numeric_index = CanonicalNumericIndexString(property_string.value()))
		{
			if(this == receiver)
			{
				SetElement(numeric_index.value(), value);
				return true;
			}
			else if(!IsValidIntegerIndex(numeric_index.value()))
			{
				return true;
			}
		}
	}
	return OrdinaryObject::Set(property_key, value, receiver WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment), check_can_put);
}

bool JS::Class::TypedArray::Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
	if(auto property_string = GetString(property_key))
	{
		if(auto numeric_index = CanonicalNumericIndexString(property_string.value()))
		{
			return !IsValidIntegerIndex(numeric_index.value());
		}
	}
	return OrdinaryObject::Delete(property_key WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_FLAG(lexical));
}

bool JS::Class::TypedArray::DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(auto property_string = GetString(property_key))
	{
		if(auto numeric_index = CanonicalNumericIndexString(property_string.value()))
		{
			if(!descriptor.Configurable.value_or(true)
			|| !descriptor.Enumerable.value_or(true)
			|| descriptor.is_accessor_descriptor()
			|| !descriptor.Writable.value_or(true))
			{
				return false;
			}
			if(descriptor.Value.has_value())
			{
				SetElement(numeric_index.value(), descriptor.Value.value());
			}
			return true;
		}
	}
	return OrdinaryObject::DefineOwnProperty(property_key, descriptor WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
}

std::vector<JSPropertyKey> JS::Class::TypedArray::OwnPropertyKeys(FOR_CLASS(limit))
{
	std::vector<JSPropertyKey> property_keys;
	// TODO: verify buffer
	for(uintptr_t index = 0; index < ArrayLength; index++)
	{
		property_keys.push_back(JS::ToString(JSNumber(index) RUNTIME_PHASE));
	}
	for(auto string_property : string_properties)
	{
		property_keys.push_back(string_property);
	}
	for(auto symbol_property : symbol_properties)
	{
		property_keys.push_back(symbol_property);
	}
	return property_keys;
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::TypedArray::ExtractAllProperties()
{
	std::map<JS::Class::Object::PropertyKeyData, JSProperty> result = OrdinaryObject::ExtractAllProperties();
	// TODO
	return result;
}

//// Generator

std::map<JSString, JSValue> JS::Class::Generator::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	switch(GeneratorState)
	{
	case StateType::SuspendedStart:
		slots[u"GeneratorState"] = u"suspendedStart";
		break;
	case StateType::SuspendedYield:
		slots[u"GeneratorState"] = u"suspendedYield";
		break;
	case StateType::Executing:
		slots[u"GeneratorState"] = u"executing";
		break;
	case StateType::Completed:
		slots[u"GeneratorState"] = u"completed";
		break;
	}
	//slots[u"GeneratorContext"] = GeneratorContext; // TODO
	if(Version::ESNEXT <= CurrentVersion)
	{
		slots[u"GeneratorBrand"] = GeneratorBrand.has_value() ? JSValue(GeneratorBrand.value()) : JS_undefined;
	}
	return slots;
}

JSValue JS::Class::Generator::Resume(JS::Runtime::CompletionRecord completion, std::optional<JSString> brand)
{
	if(GeneratorBrand != brand)
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}

	switch(GeneratorState)
	{
	case StateType::Executing:
		JS::Throw(JS::New<TypeError> __LOC());
	case StateType::SuspendedStart:
		if(completion.Type == JS::Runtime::CompletionRecord::CompletionType::Normal)
		{
			break;
		}
		GeneratorState = StateType::Completed;
		// fallthrough
	case StateType::Completed:
		switch(completion.Type)
		{
		case JS::Runtime::CompletionRecord::CompletionType::Normal:
			return CreateIteratorResultObject(JS_undefined, true);
		case JS::Runtime::CompletionRecord::CompletionType::Return:
			return CreateIteratorResultObject(completion.Value.value(), true);
		case JS::Runtime::CompletionRecord::CompletionType::Throw:
			JS::Throw(completion.Value.value());
		default:
			JS::Throw(JS::New<InternalError> __LOC(u"invalid completion type"));
		}
	case StateType::SuspendedYield:
		break;
	}

	GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;
	GeneratorContext->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
	JS::Runtime::ExecutionContext::GetContextStack().push_back(GeneratorContext);
//	GetCurrentExecutionContext()->Action_JumpToHandler(completion);
//	GetCurrentExecutionContext()->ProcessHandlers();
	GetCurrentExecutionContext()->Action_Push<JS::Runtime::CompletionRecord>(completion);
	return JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
}

//// External

// frozen object with null prototype

JS::Class::External::~External()
{
	if(finalize != nullptr)
	{
		// TODO: call Node API function
	}
}

#if ENABLE_ES4
JSType JS::Class::External::GetType()
{
	return nullptr;
}

std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> JS::Class::External::FindSlot([[maybe_unused]] JSInstanceVariable id)
{
	return {};
}
#endif

bool JS::Class::External::IsExtensible()
{
	return false;
}

bool JS::Class::External::PreventExtensions()
{
	return true;
}

std::optional<JSValue> JS::Class::External::GetPrototypeOf()
{
	return {};
}

bool JS::Class::External::SetPrototypeOf(JSValue new_prototype)
{
	return JS::SameValue(JS_null, new_prototype);
}

std::optional<JSProperty> JS::Class::External::GetOwnProperty([[maybe_unused]] JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	return {};
}

std::optional<JSProperty> JS::Class::External::GetProperty([[maybe_unused]] JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	return {};
}

bool JS::Class::External::HasProperty([[maybe_unused]] JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	return false;
}

JSValue JS::Class::External::Get(JSValue property_key, [[maybe_unused]] JSValue receiver, [[maybe_unused]] Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	assert_property_key(property_key);
	return JS_undefined;
}

bool JS::Class::External::Set(JSValue property_key, [[maybe_unused]] JSValue value, [[maybe_unused]] JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), [[maybe_unused]] bool check_can_put)
{
	assert_property_key(property_key);
	return false;
}

bool JS::Class::External::Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
{
	assert_property_key(property_key);
	return true;
}

bool JS::Class::External::DefineOwnProperty([[maybe_unused]] JSPropertyKey property_key, [[maybe_unused]] JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	return false;
}

std::vector<JSPropertyKey> JS::Class::External::OwnPropertyKeys(FOR_CLASS(limit))
{
	return std::vector<JSPropertyKey>{};
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::External::ExtractAllProperties()
{
	return std::map<JS::Class::Object::PropertyKeyData, JSProperty>();
}

std::map<JSString, JSValue> JS::Class::External::GetInternalSlots([[maybe_unused]] bool unified)
{
	std::map<JSString, JSValue> slots;
	slots[u"Prototype"] = JS_null;
#if ENABLE_ES4
	if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
	if(CurrentVersion < Version::ES6)
#endif
	{
		slots[u"Class"] = GetClassString();
	}
	if(CurrentVersion >= Version::ES5)
	{
		slots[u"Extensible"] = false;
	}
	return slots;
}

JSString JS::Class::External::GetClassString()
{
	return u"external"; // TODO: what is it written as?
}

JS::Invocability JS::Class::External::GetInvocability()
{
	return Invocability::NonCallable;
}

JSValue JS::Class::External::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC("not callable"));
}

JSValue JS::Class::External::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC("not constructable"));
}

JSValue JS::Class::External::DefaultValue([[maybe_unused]] JS::Runtime::TypeHint hint AND_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC());
}

//// ArrayIterator

JSObject JS::Runtime::Intrinsics::* JS::Class::ArrayIterator::IntrinsicPrototype = &JS::Runtime::Intrinsics::ArrayIteratorPrototype;

//// AsyncFromSyncIterator

JSObject JS::Runtime::Intrinsics::* JS::Class::AsyncFromSyncIterator::IntrinsicPrototype = &JS::Runtime::Intrinsics::AsyncFromSyncIteratorPrototype;

//// Type::QName

bool JS::Type::QName::IsXMLName(JSString name)
{
	enum
	{
		Initial,
		Continue,
		Surrogate,
	} State = Initial;

	// NCName
	for(auto c : name)
	{
		// \i except ':'
		if((u'A' <= c && c <= u'Z')
		|| c == u'_'
		|| (u'a' <= c && c <= u'z')
		|| (u'\xC0' <= c && c <= u'\xD6')
		|| (u'\xD8' <= c && c <= u'\xF6')
		|| (u'\xF8' <= c && c <= u'\u02FF')
		|| (u'\u0370' <= c && c <= u'\u037D')
		|| (u'\u037F' <= c && c <= u'\u1FFF')
		|| c == u'\u200C'
		|| c == u'\u200D'
		|| (u'\u2070' <= c && c <= u'\u218F')
		|| (u'\u2C00' <= c && c <= u'\u2FEF')
		|| (u'\u3001' <= c && c <= u'\uD7FF')
		|| (u'\uF900' <= c && c <= u'\uFDCF')
		|| (u'\uFDF0' <= c && c <= u'\uFFFD'))
		{
			if(State == Surrogate)
			{
				return false;
			}
			State = Continue;
		}
		else if(c == u'-'
		|| c == u'.'
		|| (u'0' <= c && c <= u'9')
		|| c == u'\xB7'
		|| (u'\u0300' <= c && c <= u'\u036F')
		|| (u'\u203F' <= c && c <= u'\u2040'))
		{
			if(State != Continue)
			{
				return false;
			}
		}
		else if(0xD800 <= c && c <= 0xDB7F) // only allowed until U'\U000EFFFF'
		{
			if(State == Surrogate)
			{
				return false;
			}
			State = Surrogate;
		}
		else if(0xDC00 <= c && c <= 0xDFFF)
		{
			if(State != Surrogate)
			{
				return false;
			}
			State = Continue;
		}
		else
		{
			return false;
		}
	}

	return State == Continue;
}

bool JS::Type::QName::IsXMLName(const QName& qname)
{
	return IsXMLName(qname.localName);
}

bool JS::Type::QName::IsXMLName(JSValue name)
{
	if(IsUndefined(name))
	{
		return false;
	}
	else if(auto qname = std::dynamic_pointer_cast<JS::Class::QName>(GetNonNullObject(name).value_or(nullptr)))
	{
		return IsXMLName(qname->localName);
	}

	try
	{
		return IsXMLName(JS::ToString(name RUNTIME_PHASE));
	}
	catch(TypeError *)
	{
		return false;
	}
}

JSNamespace JS::Type::QName::GetNamespace(std::map<JSString, JSNamespace> InScopeNamespaces)
{
	if(!uri.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}

	auto iter = InScopeNamespaces.find(uri.value());
	if(iter != InScopeNamespaces.end())
	{
		return iter->second;
	}

	if(Prefix.has_value())
	{
		return JS::New<JS::Class::Namespace>(Prefix.value(), uri.value());
	}
	else
	{
		return JS::New<JS::Class::Namespace>(uri.value());
	}
}

//// XMLBase

JSValue JS::Class::XMLBase::ConvertToPropertyKey(JSValue property_key AND_PHASE(phase))
{
	if((property_key.has_value<JSObject>() && std::dynamic_pointer_cast<QName>(*property_key.get_if<JSObject>()))
	|| property_key.has_value<JS::Type::AttributeName>())
	{
		return property_key;
	}
	else
	{
		return OrdinaryObject::ConvertToPropertyKey(property_key WITH_PHASE(phase));
	}
}

JSValue JS::Class::XMLBase::CreateReference(JSIdentifierReference identifier_reference, JS::Runtime::ReferenceContext cxt)
{
	if(identifier_reference.Attribute || identifier_reference.Namespace.has_value() || (identifier_reference.IsName() && identifier_reference.GetName() == JSIdentifierReference::Wildcard))
	{
		JSValue member;
		JS::Class::QName * qname = JSIdentifierReference{identifier_reference.Namespace, identifier_reference.GetName()}.ToQName();
		if(identifier_reference.Attribute)
		{
			member = JS::Type::AttributeName(*qname);
		}
		else
		{
			member = qname;
		}

		return JS::Runtime::MemberReference{
			this,
			member,
			this,
			cxt.Strict
			WITH_NAMESPACES({})
			WITH_CLASS(GetClass())
		};
	}
	else
	{
		return JS::Runtime::MemberReference{
			this,
			identifier_reference.PropertyKey,
			this,
			cxt.Strict
			WITH_NAMESPACES(identifier_reference.GetNamespaces(cxt))
			WITH_CLASS(GetClass())
		};
	}
}

JSValue JS::Class::XMLBase::ResolvePropertyKey(JSValue property_key)
{
	// E4X allows QName and AttributeName referenced names as well, so we cannot perform ToPropertyKey here
	return property_key;
}

//// XML

JSObject JS::Runtime::Intrinsics::* JS::Class::XML::IntrinsicPrototype = &JS::Runtime::Intrinsics::XML_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::XML::IntrinsicClass = &JS::Runtime::Intrinsics::XML_class;
#endif

JSString JS::Class::XML::GetClassSlot()
{
	switch(Class)
	{
	case ClassType::Element:
		return u"element";
	case ClassType::Attribute:
		return u"attribute";
	case ClassType::Comment:
		return u"comment";
	case ClassType::ProcessingInstruction:
		return u"processing-instruction";
	case ClassType::Text:
		return u"text";
	}
	JS::Throw(JS::New<InternalError> __LOC());
}

JSString JS::Class::XML::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"XML";
}

std::map<JSString, JSValue> JS::Class::XML::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	if(CurrentVersion >= Version::ES6)
	{
		slots[u"Class"] = GetClassSlot(); // according to the specs, this is the [[Class]] value, but SpiderMonkey and Rhino use "XML" as a class tag
	}
	slots[u"Value"] = Value.has_value() ? JSValue(Value.value()) : JS_undefined;
	slots[u"Name"] = Name.has_value() ? JSValue(JS::New<JS::Class::QName>(Name.value())) : JS_null;
	slots[u"Parent"] = Parent;
	auto attributes = JS::New<JS::Class::Array>(Attributes.size());
	uint32_t index = 0;
	for(auto attribute_entry : Attributes)
	{
		attributes->elements[index].SetValue(attribute_entry.second);
		index++;
	}
	slots[u"Attributes"] = attributes;
	auto namespaces = JS::New<JS::Class::Array>(InScopeNamespaces.size());
	index = 0;
	for(auto namespace_entry : InScopeNamespaces)
	{
		namespaces->elements[index].SetValue(namespace_entry.second);
		index++;
	}
	slots[u"InScopeNamespaces"] = namespaces;
	slots[u"Length"] = JSNumber(children.size());
	return slots;
}

bool JS::Class::XML::HasProperty([[maybe_unused]] /* TODO */ JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	// TODO
	return false;
}

JSValue JS::Class::XML::Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	switch(intent)
	{
	case Intent::Read:
		if(auto property_option = GetString(property_key))
		{
#if ENABLE_ES4
			if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
			{
				uint32_t index;
				if(is_array_index(*property_option, index))
				{
					// preform as if converted to ToXMLList and then invoked [[Get]](index)
					if(index < children.size())
					{
						return JSObject(children[index]);
					}
					else
					{
						return OrdinaryObject::Get(property_key, receiver, intent WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
					}
				}
			}
		}

		{
			JSValue xml_name = property_key.ToXMLName(); // must be QName * or JSAttributeName
			gcn::gc_ptr<JS::Class::XMLList> xml_list = JS::New<JS::Class::XMLList>();
			xml_list->TargetObject = JSObject(this);
			xml_list->TargetProperty = xml_name;
			if(auto attribute_name_option = xml_name.get_if<JSAttributeName>())
			{
				auto attribute_name = attribute_name_option.value();
				for(auto attribute_pair : Attributes)
				{
					auto attribute = attribute_pair.second;
					if(
						(attribute_name.Name.localName == JSIdentifierReference::Wildcard
							|| attribute_name.Name.localName == attribute->Name.value().localName)
					&&
						(!attribute_name.Name.uri.has_value()
							|| attribute_name.Name.uri == attribute->Name.value().uri))
					{
						xml_list->Append(attribute);
					}
				}
				return xml_list;
			}
			// this should not fail
			JS::Type::QName qname = *std::dynamic_pointer_cast<JS::Class::QName>(GetNonNullObject(xml_name).value_or(nullptr));

			for(auto element : children)
			{
				if(
					(qname.localName == JSIdentifierReference::Wildcard
						|| (element->Class == ClassType::Element && element->Name.value().localName == qname.localName))
				&&
					(!qname.uri.has_value()
						|| (element->Class == ClassType::Element && element->Name.value().uri == qname.uri)))
				{
					xml_list->Append(element);
				}
			}
			return xml_list;
		}
	case Intent::Call:
		return OrdinaryObject::Get(property_key, receiver, Intent::Call WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
	}
	assert(false);
}

JSValue JS::Class::XML::ResolveMethod(JS::Runtime::MemberReference& reference)
{
	JSValue value = Get(reference.ReferencedName, reference.ThisValue, Intent::Call WITH_NAMESPACES(reference.Namespaces) WITH_CLASS(reference.Limit) NO_ENVIRONMENT RUNTIME_PHASE);
	if(IsUndefined(value) && HasSimpleContent())
	{
		reference.Base = children[0];
		return children[0]->ResolveMethod(reference);
	}
	else
	{
		return value;
	}
}

#if ENABLE_ES4
JSValue JS::Class::XML::ResolveMethod(JS::Runtime::BracketReference& reference)
{
	JSValue value = GetBracket(reference.Arguments, reference.ThisValue, Intent::Call WITH_CLASS(reference.Limit) RUNTIME_PHASE);
	if(IsUndefined(value) && HasSimpleContent())
	{
		reference.Base = children[0];
		return children[0]->ResolveMethod(reference);
	}
	else
	{
		return value;
	}
}
#endif

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::XML::ExtractAllProperties()
{
	std::map<JS::Class::Object::PropertyKeyData, JSProperty> result = OrdinaryObject::ExtractAllProperties();
	for(uint32_t index = 0; index < children.size(); index++)
	{
		PropertyKeyData data;
#if ENABLE_ES4
		data.PropertyKey = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
		data.Namespaces = {JS_public};
		data.Accesses = AccessSet::ReadWrite;
#else
		data = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
#endif
		result[data] = JSProperty::MakeData(JSObject(children[index]), /* Writable = */ false, true, false);
	}
	return result;
}

//// XMLList

JSObject JS::Runtime::Intrinsics::* JS::Class::XMLList::IntrinsicPrototype = &JS::Runtime::Intrinsics::XMLList_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::XMLList::IntrinsicClass = &JS::Runtime::Intrinsics::XMLList_class;
#endif

JSString JS::Class::XMLList::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"XML";
}

JSString JS::Class::XMLList::GetClassSlot()
{
	return u"XMLList";
}

std::map<JSString, JSValue> JS::Class::XMLList::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	if(CurrentVersion >= Version::ES6)
	{
		slots[u"Class"] = u"XMLList";
	}
	slots[u"TargetObject"] = TargetObject;
	slots[u"TargetProperty"] = TargetProperty;
	slots[u"Length"] = JSNumber(children.size());
	return slots;
}

// TODO: XML and XMLList also have some additional properties

JSValue JS::Class::XMLList::Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
{
	switch(intent)
	{
	case Intent::Read:
		if(auto property_option = GetString(property_key))
		{
#if ENABLE_ES4
			if(!IsVersionES4() || namespaces.find(JS_public) != namespaces.end())
#endif
			{
				uint32_t index;
				if(is_array_index(*property_option, index))
				{
					// preform as if converted to ToXMLList and then invoked [[Get]](index)
					if(index < children.size())
					{
						return JSObject(children[index]);
					}
					else
					{
						return OrdinaryObject::Get(property_key, receiver, intent WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
					}
				}
			}
		}

		{
			gcn::gc_ptr<JS::Class::XMLList> xml_list = JS::New<JS::Class::XMLList>();
			xml_list->TargetObject = JSObject(this);
			xml_list->TargetProperty = property_key;
			for(auto element : children)
			{
				if(element->Class == XML::ClassType::Element)
				{
					auto element_property_value = element->Get(property_key, element, intent WITH_NAMESPACES(namespaces) WITH_CLASS(nullptr) WITH_ENVIRONMENT(nullptr) WITH_PHASE(phase));
					if(auto element_property = std::dynamic_pointer_cast<JS::Class::XML>(GetNonNullObject(element_property_value).value_or(nullptr)))
					{
						if(element_property->children.size() > 0)
						{
							xml_list->Append(element_property);
						}
					}
					else
					{
						JS::Throw(JS::New<InternalError> __LOC("XML type error"));
					}
				}
			}
			return xml_list;
		}
	case Intent::Call:
		return OrdinaryObject::Get(property_key, receiver, Intent::Call WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
	}
	assert(false);
}

JSValue JS::Class::XMLList::ResolveMethod(JS::Runtime::MemberReference& reference)
{
	JSValue value = Get(reference.ReferencedName, reference.ThisValue, Intent::Call WITH_NAMESPACES(reference.Namespaces) WITH_CLASS(reference.Limit) NO_ENVIRONMENT RUNTIME_PHASE);
	if(IsUndefined(value) && children.size() == 1)
	{
		reference.Base = children[0];
		return children[0]->ResolveMethod(reference);
	}
	else
	{
		return value;
	}
}

#if ENABLE_ES4
JSValue JS::Class::XMLList::ResolveMethod(JS::Runtime::BracketReference& reference)
{
	JSValue value = GetBracket(reference.Arguments, reference.ThisValue, Intent::Call WITH_CLASS(reference.Limit) RUNTIME_PHASE);
	if(IsUndefined(value) && children.size() == 1)
	{
		reference.Base = children[0];
		return children[0]->ResolveMethod(reference);
	}
	else
	{
		return value;
	}
}
#endif

void JS::Class::XMLList::Append(JSValue value)
{
	auto object = GetNonNullObject(value).value_or(nullptr);
	if(auto xml_list = std::dynamic_pointer_cast<XMLList>(object))
	{
		TargetObject = xml_list->TargetObject;
		TargetProperty = xml_list->TargetProperty;
		children.insert(children.end(), xml_list->children.begin(), xml_list->children.end());
	}
	else if(auto xml = std::dynamic_pointer_cast<XML>(object))
	{
		TargetObject = xml->Parent;
		if(xml->Class == XML::ClassType::ProcessingInstruction)
		{
			TargetProperty = JS_null;
		}
		else if(xml->Name.has_value())
		{
			TargetProperty = JS::New<QName>(xml->Name.value());
		}
		else
		{
			TargetProperty = JS_null;
		}
		children.push_back(xml);
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC("expected XML or XMLList"));
	}
}

std::map<JS::Class::Object::PropertyKeyData, JSProperty> JS::Class::XMLList::ExtractAllProperties()
{
	std::map<JS::Class::Object::PropertyKeyData, JSProperty> result = OrdinaryObject::ExtractAllProperties();
	for(uint32_t index = 0; index < children.size(); index++)
	{
		PropertyKeyData data;
#if ENABLE_ES4
		data.PropertyKey = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
		data.Namespaces = {JS_public};
		data.Accesses = AccessSet::ReadWrite;
#else
		data = JS::ToString(JSNumber(index) COMPILE_TIME_PHASE);
#endif
		result[data] = JSProperty::MakeData(JSObject(children[index]), /* Writable = */ false, true, false);
	}
	return result;
}

//// QName

JSObject JS::Runtime::Intrinsics::* JS::Class::QName::IntrinsicPrototype = &JS::Runtime::Intrinsics::QName_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::QName::IntrinsicClass = &JS::Runtime::Intrinsics::QName_class;
#endif

std::map<JSString, JSValue> JS::Class::QName::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	slots[u"Prefix"] = Prefix.has_value() ? JSValue(Prefix.value()) : JS_undefined;
	return slots;
}

//// Namespace

JSObject JS::Runtime::Intrinsics::* JS::Class::Namespace::IntrinsicPrototype = &JS::Runtime::Intrinsics::Namespace_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Class::Namespace::IntrinsicClass = &JS::Runtime::Intrinsics::Namespace_class;
#endif

#if ENABLE_ES4
std::vector<JSNamespace> JS::Class::Namespace::all_allocated_namespaces;
std::map<JSString, std::vector<JSNamespace>> JS::Class::Namespace::allocated_namespaces;
std::map<JSString, JSNamespace> JS::Class::Namespace::named_namespaces;

void JS::Class::Namespace::RegisterNamespace(JSNamespace namespace_)
{
	auto all_allocated_namespaces_iter = std::find(all_allocated_namespaces.begin(), all_allocated_namespaces.end(), namespace_);
	if(all_allocated_namespaces_iter == all_allocated_namespaces.end())
	{
		all_allocated_namespaces.push_back(namespace_);
	}

	auto allocated_namespaces_iter = allocated_namespaces.find(namespace_->uri);
	if(allocated_namespaces_iter ==  allocated_namespaces.end())
	{
		allocated_namespaces[namespace_->uri] = {};
	}
	auto& allocated_namespace_vector = allocated_namespaces[namespace_->uri];
	auto namespace_iter = std::find(allocated_namespace_vector.begin(), allocated_namespace_vector.end(), namespace_);
	if(namespace_iter == allocated_namespace_vector.end())
	{
		allocated_namespace_vector.push_back(namespace_);
	}
}
#endif

//// PublicNamespace

#if ENABLE_ES4
std::optional<JSValue> JS::Type::Internal::PublicNamespace::GetPrototypeOf()
{
	return JS::Intrinsics()->Namespace_prototype;
}

JSType JS::Type::Internal::PublicNamespace::GetType()
{
	return IsVersionES4() ? JS::Intrinsics()->Namespace_class : JSClass(nullptr);
}
#endif

//// AttributeName

//// InternalError

//// JS

