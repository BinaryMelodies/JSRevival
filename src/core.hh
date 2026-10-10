#ifndef _OBJECT_HH
#define _OBJECT_HH

/* Types and functions required for the object model */

#include <atomic>
#include <cstddef>
#include <gc_cpp.h>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "runtime.hh"

namespace JS
{
	namespace Runtime
	{
		// ES6
		class SymbolInstance : public gc_cleanup
		{
		public:
			static std::map<JSString, std::vector<JSSymbol>> allocated_symbols;
			static void RegisterSymbol(JSSymbol symbol);

			const std::optional<JSString> Description;

			SymbolInstance() : Description()
			{
				RegisterSymbol(this);
			}
			SymbolInstance(JSString description) : Description(description)
			{
				RegisterSymbol(this);
			}
			SymbolInstance(const SymbolInstance&) = delete;
		};
	}

	namespace Class
	{
		/** @brief Represents the abstract interface to all reference types
		 *
		 * ECMAScript defines several internal slots (properties and methods) that must be implemented
		 * by all objects. The class hierarchy of JSRevival is such that entities are
		 * classified into classes according to their specialized internal layout and
		 * behavior of the internal slots. All objects must descend from JS::Class::Object
		 * and their classes can implement these functions in any way that conforms to the
		 * standards.
		 *
		 * There are three main concrete subclasses of the JS::Class::Object class: OrdinaryObject
		 * that implements all the methods according to the behavior of ECMAScript ordinary objects,
		 * and Class::Proxy for the proxy exotic objects. Additionally, the Node.js external values
		 * are represented as a subclass Class::External.
		 *
		 * This class implements the most common methods of JS::Class::Internal::Value in a way that is shared
		 * by almost all reference classes.
		 */
		class Object : public JS::Class::Internal::Value
		{
		public:
			JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
			bool SameValue(const JSValue& other) const override;
			JSBoolean ToBoolean() override;
			JSNumber ToNumber(FOR_PHASE(phase)) override;
			JSBigInt ToBigInt() override;
			JSString ToString(FOR_PHASE(phase)) override;
			JSObject ToObject() override;
			JSString Typeof() override;
			JSString ToStringTag() override;

			JSValue GetPrivate(JSPrivateName name) override;
			void SetPrivate(JSPrivateName name, JSValue value) override;

			JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
			bool Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;

			JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
			JSXML ToXML() override;
			JSXMLList ToXMLList() override;
			JSAttributeName ToAttributeName() override;
			JSValue ToXMLName() override;

			void PutValue(JSValue value) override;

			bool has_value(const std::type_info& type) const override;
			const void * get_if(const std::type_info& type) const override;
			void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
#if ENABLE_ES4
			void serialize_slots(JS::Runtime::Debugger * debugger, JSClass limit);
#endif
			void serialize_property(JS::Runtime::Debugger * debugger, JSPropertyKey property_key, JSProperty property_value AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses));
			void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif

			/** @brief Result of the Object.prototype.valueOf operation
			 *
			 * Up to ECMAScript 5, host objects were allowed to be converted to wrapper objects, and the valueOf operator
			 * could be used to retrieve their values.
			 *
			 * The default implementation returns the same object as it was invoked on.
			 *
			 * @return The result of the valueOf operation
			 */
			virtual Object * ValueOf();
		};

		/** @brief Represents a fully populated object with properties and default internal methods
		 *
		 * A concrete subclass of the Object class.
		 *
		 * Almost all ECMAScript objects have a similar structure, with a map of property keys
		 * to properties and a set of methods of similar functionality. This class corresponds
		 * to what ECMAScript refers to as an *ordinary object*.
		 *
		 * Any object instance that has the properties of an ECMAScript ordinary object and
		 * method implementations that are similar to those of an ordinary object should be
		 * subclasses of OrdinaryObject instead of Object. In practice, this means
		 * that almost all *exotic objects* are instances of subclasses of this class.
		 *
		 * The Netscape ECMAScript 4 proposal calls this type a "binding object"
		 */
		class OrdinaryObject : public JS::Class::Object
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			// internal slots
			JS::Runtime::Bindings<JSProperty> properties;

			std::set<uint32_t> integer_index_properties;
			std::vector<JSString> string_properties;
			std::vector<JSSymbol> symbol_properties;

			// outside ES4, this must be an Object, an empty value corresponds to an ECMAScript null value
			std::optional<JSValue> Prototype;
			bool Extensible = true;

			/** @brief Introduced in ECMAScript 13, objects can have private elements that are handled separately from other properties */
			std::map<JS::Runtime::PrivateName *, JS::Runtime::PrivateElement> PrivateElements;

#if ENABLE_ES4
			JSType Type;
			std::map<JSInstanceVariable, std::optional<JSValue>> Slots;

			JSType GetType() override;
			std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> FindSlot(JSInstanceVariable id) override;
#endif

			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			std::optional<JSProperty> GetProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
			bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) override;

			JSValue GetPrivate(JSPrivateName name) override;
			void SetPrivate(JSPrivateName name, JSValue value) override;

#if ENABLE_ES4
			bool Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
#endif

			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;

			static bool ValidatePropertyDescriptor(bool extensible, JSPropertyDescriptor descriptor, std::optional<JSProperty> current_property);
			static std::optional<JSProperty> ValidateAndApplyPropertyDescriptor(bool extensible, JSPropertyDescriptor descriptor, std::optional<JSProperty> current_property_option);

			bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) override;

			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;

			Invocability GetInvocability() override;
			JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;

		protected:
#if ENABLE_ES4
			friend class JS::Class::Class;
#endif
			void AddPropertyKey(JSPropertyKey property_key AND_NAMESPACES(namespaces));

		public:
#if ENABLE_ES4
			void add_slot(JSClass class_, JSString property_key, std::set<JSNamespace> namespaces, JSValue value);
#endif

		public:
			std::optional<JSValue> GetPrototypeOf() override;
			bool SetPrototypeOf(JSValue new_prototype) override;
			bool IsExtensible() override;
			bool PreventExtensions() override;

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
			JSString GetClassString() override;
		};

		class ImmutablePrototypeObject : public OrdinaryObject
		{
		public:
			bool SetPrototypeOf(JSValue new_prototype) override;
		};

#if ENABLE_ES4
		class Class : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;

			JS::Runtime::Realm * Realm;

			// internal slots
			std::optional<JSType> Super;
			JS::Runtime::Bindings<JSInstanceProperty> InstanceMembers; // Netscape calls this instanceProperties
			std::vector<JSString> instance_member_names;
			//JSBoolean complete; // only needed for incremental compilation
			std::optional<JSValue> ClassPrototype;
			JSString Name; // not in ECMA
			JSNamespace PrivateNamespace;
			JSBoolean Dynamic;
			JSBoolean Final;
			JSFunction Constructor;
			JSBoolean CallsSuperConstructor;
			std::vector<JSString> type_parameters;

			Class() { }
			Class(JSString name, std::optional<JSType> super, std::optional<JSValue> prototype, JSBoolean dynamic, JSBoolean final, JS::Runtime::Realm * realm);

			Invocability GetInvocability() override;
#if ENABLE_ES4
			std::optional<JS::Runtime::SignatureType> GetSignature() override;
#endif
			JS::Runtime::Realm * GetFunctionRealm() override;
			JSString GetClassString() override;
			JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			bool Begin_Call(JSValue this_argument, std::vector<JSValue> arguments) override;
			void CreateInstanceFields(gcn::gc_ptr<JS::Class::OrdinaryObject> this_argument);
			static inline void AssignType(gcn::gc_ptr<JS::Class::OrdinaryObject> object, gcn::gc_ptr<Class> class_)
			{
				if(class_ != nullptr)
				{
					object->Type = class_;
					class_->CreateInstanceFields(object);
				}
			}
			JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;

			template <typename T = Class::OrdinaryObject>
				JSValue ConstructInstanceOf(std::vector<JSValue> arguments)
			{
				gcn::gc_ptr<T> this_argument = gcn::make_gc<T>();
				this_argument->Extensible = Dynamic;
				this_argument->Prototype = ClassPrototype.value_or(JS_null);
				AssignType(this_argument, JSClass(this));
				Init(this_argument, arguments);
				return this_argument;
			}

			virtual void Init(JSValue this_argument, std::vector<JSValue> arguments); // trivial initializer
			virtual JSBoolean Is(JSValue object); // overloaded by subclasses of Number
			virtual std::optional<JSValue> Coerce(JSValue object, bool strict = true);
			virtual JSString GetTypeofString();
			virtual JS::Runtime::TypeHint GetDefaultHint(); // Netscape only
			virtual std::optional<JSValue> GetDefaultValue();

			// TODO: override methods

			std::optional<std::variant<JSProperty, JSInstanceVariable>> GetClassOwnProperty(JSPropertyKey property_key, std::set<JSNamespace> namespaces, JS::AccessSet accesses);
			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;

			std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> FindSlot(JSInstanceVariable id) override;

			bool SetPrototypeOf(JSValue new_prototype) override;
			bool IsExtensible() override;
			bool PreventExtensions() override;

			bool HasOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;

			static JSObject GetCurrentThisValue();
			static JSObject GetCurrentThisValue(std::shared_ptr<JS::Runtime::EnvironmentRecord> environment);

			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
			bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) override;

			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;

			bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			/*std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) override;*/

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;

			std::optional<JSInstanceProperty> GetInstanceProperty(JSPropertyKey property_key, std::set<JSNamespace> namespaces, AccessSet accesses);
			std::optional<JSInstanceProperty> GetBaseInstanceProperty(JSPropertyKey property_key, std::set<JSNamespace> namespaces, AccessSet accesses);
			std::optional<JSInstanceProperty> SearchForOverrides(JSPropertyKey property_key, std::set<JSNamespace> namespaces, AccessSet accesses);
			std::optional<JSInstanceProperty> GetDerivedInstanceProperty(JSPropertyKey property_key, std::set<JSNamespace> namespaces, AccessSet accesses);

			JSBoolean IsAncestor(JSClass subclass)
			{
				while(true)
				{
					if(this == subclass)
					{
						return true;
					}
					else if(!subclass->Super.has_value())
					{
						return false;
					}
					else
					{
						subclass = GetClassForType(subclass->Super.value());
					}
				}
			}

			static JSBoolean OrdinaryIs(JSValue object, JSClass class_);
			static std::optional<JSValue> OrdinaryCoerce(JSValue object, JSClass class_);

			static JSValue OrdinaryConstruct(JSClass class_, std::vector<JSValue> arguments, JSFunction new_target);
			static void TrivialInit(JSClass class_, JSValue this_argument, std::vector<JSValue> arguments);
			//void Init(JSValue this_argument, std::vector<JSValue> arguments) override;
		};

		// TODO: experimental
		class Interface : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;

			// internal slots
			std::vector<JSType> Implements;
			JS::Runtime::Bindings<JSInstanceProperty> InstanceMembers;
			JSString Name; // not in ECMA
			std::vector<JSString> type_parameters;
		};
#endif

		/** @brief ECMAScript function object type
		 *
		 * Represents an ordinary ECMAScript function object, storing the parse tree of a function from a parsed script.
		 */
		class Function : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			enum class FunctionKindType
			{
				// any non-generator non-class-constructor function
				Normal,
				// used to mark that the function is the constructor of an ES6 class
				ClassConstructor,
				// used for generators, removed in ES11
				Generator,
				// ES8, removed in ES11
				Async,
				// ES9, removed in ES11
				AsyncGenerator,
			};

			enum class ConstructorKindType
			{
				NonConstructor,
				Base,
				Derived,
			};

			enum class ThisModeType
			{
				Lexical, // corresponds to ES4 Static
				Strict, // corresponds to ES4 Instance (and Constructor)
				Global_Checked, // corresponds to ES4 Prototype
				Global, // corresponds to ES4 Unchecked

				Static = Lexical, // ES4 plainFunction
				Instance = Strict, // includes ES4 instanceFunction and constructorFunction
				Prototype = Global_Checked,
				Unchecked = Global,
			};

			JS::Runtime::Realm * Realm;

#if ENABLE_ES4
			std::optional<JS::Runtime::SignatureType> Signature;
#endif

			bool is_arrow = false;
			bool is_generator = false;
			bool is_async = false;

			JS::Runtime::ScriptOrModuleRecord * ScriptOrModule;
			std::shared_ptr<JS::Runtime::PrivateEnvironmentRecord> PrivateEnvironment;
			std::shared_ptr<JS::Runtime::EnvironmentRecord>        Environment;
			FunctionKindType FunctionKind;
			//JS::ParseNode::ParseNode * FormalParameters; // part of Code
			JS::ParseNode::FunctionalElement * Code;

			ConstructorKindType ConstructorKind;
			ConstructorKindType GetConstructorKind();
			Invocability GetInvocability() override;
#if ENABLE_ES4
			std::optional<JS::Runtime::SignatureType> GetSignature() override;
#endif

			JSObject HomeObject;
			JSString SourceText;
			ThisModeType ThisMode;
			bool Strict;

			// ESNext
			std::vector<JS::Runtime::ClassFieldDefinition> Fields;
			std::map<JSPrivateName, JS::Runtime::PrivateElement> PrivateMethods;

			JSBindingName ClassFieldInitializerName; // TODO: do we actually use this?
#if ENABLE_ES4
			std::set<JSNamespace> OpenNamespaces;
			gcn::gc_ptr<Class> ClassBeingConstructed;
#endif

			Function(
				JS::Runtime::Realm * Realm,
				JSString name,
				uint32_t length,
				JS::ParseNode::FunctionalElement * Code,
				ThisModeType ThisMode,
				std::shared_ptr<JS::Runtime::EnvironmentRecord> Environment,
				std::shared_ptr<JS::Runtime::PrivateEnvironmentRecord> PrivateEnvironment
				AND_NAMESPACES(OpenNamespaces),
				FunctionKindType FunctionKind = FunctionKindType::Normal,
				ConstructorKindType ConstructorKind = ConstructorKindType::Base);

			JS::Runtime::Realm * GetFunctionRealm() override;
			JSString GetClassString() override;

			JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			bool Begin_Call(JSValue this_argument, std::vector<JSValue> arguments) override;
			JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
		private:
			gcn::gc_ptr<JS::Class::Generator> CreateGenerator();
			void Begin_EvaluateBody(JSValue this_argument, std::vector<JSValue> arguments);
			void Begin_Construct(std::vector<JSValue> arguments, JSFunction new_target);
		public:
			//JS::Runtime::Realm * GetFunctionRealm() override;

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;

			virtual JS::Runtime::ExecutionContext * PrepareForOrdinaryCall(JSFunction new_target);
			virtual JSValue OrdinaryCallBindThis(JS::Runtime::ExecutionContext * callee_context, JSValue this_argument);
			//virtual JSValue OrdinaryCallEvaluateBody(std::vector<JSValue> arguments);
			virtual void FunctionDeclarationInstantiation(std::vector<JSValue> arguments);
			virtual JS::Interpreter::State<std::optional<JSValue>> CompileBody(JS::Interpreter::State<> state, std::vector<JSValue> arguments);

			void InitializeInstanceElements(JSObject this_argument);
			//JS::Interpreter::State<> Compile_InitializeInstanceElements(JS::Interpreter::State<JSObject> initial_state);
			static JS::Interpreter::State<JSObject> Compile_InitializeInstanceElements(JS::Interpreter::State<JSOrdinaryFunction, JSObject> initial_state);
		};

		/** @brief Boolean wrapper object type
		 *
		 * Unlike JSBoolean, this is used for Boolean object references
		 */
		class Boolean : public OrdinaryObject // TODO
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			JSBoolean BooleanData;
			Boolean(JSBoolean value) : BooleanData(value) { }

			JSString GetClassString() override;
			JSXML ToXML() override;
			JSXMLList ToXMLList() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		/** @brief Number wrapper object type
		 *
		 * Unlike JSNumber, this is used for Number object references
		 */
		class Number : public OrdinaryObject // TODO
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			JSNumber NumberData;
			Number(JSNumber value) : NumberData(value) { }

			JSString GetClassString() override;
			JSXML ToXML() override;
			JSXMLList ToXMLList() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		/** @brief String wrapper object type
		 *
		 * Unlike JSString, this is used for String object references
		 */
		class String : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			JSString StringData;

			String(JSString value)
				: StringData(value)
			{
				// this never gets called in ES4.2003
				Define(
					u"length",
					JSProperty::MakeData(JSNumber(value.size()), /* Writable = */ false, /* Enumerable = */ false, /* Configurable = */ false)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS);
			}

			bool is_string_index(JSString value, uint32_t& result);

			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
			std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) override;

			JSString GetClassString() override;
			JSXML ToXML() override;
			JSXMLList ToXMLList() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;
		};

		/** @brief Symbol wrapper object type
		 *
		 * Introduced in ECMAScript 6
		 *
		 * Unlike JSSymbol, this is used for Symbol object references
		 */
		class Symbol : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			JSSymbol SymbolData;
			Symbol(JSSymbol value) : SymbolData(value) { }

			//JSString GetClassString() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		/** @brief BigInt wrapper object type
		 *
		 * Introduced in ECMAScript 11
		 *
		 * Unlike JSBigInt, this is used for BigInt object references
		 */
		class BigInt : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			JSBigInt BigIntData;
			BigInt(JSBigInt value) : BigIntData(value) { }

			//JSString GetClassString() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		/** @brief ECMAScript Date objects */
		class Date : public OrdinaryObject // TODO
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			JSNumber DateValue;
			Date(int64_t value) : DateValue(value)
			{
#if ENABLE_ES4
				if(IsVersionES4())
				{
					// TODO: set type
				}
#endif
			}
			// TODO

			JSString GetClassString() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES3
		/** @brief ECMAScript Error objects
		 *
		 * Introduced in ECMAScript 3
		 *
		 * This is the root class for all Error objects.
		 */
		class Error : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			std::vector<JS::Parser::Location> SourceLocation;

			Error(std::optional<JSString> message, std::optional<JSValue> cause = std::optional<JSValue>{} __ES4_ONLY(, std::optional<JSObject JS::Runtime::Intrinsics::*> using_prototype = std::optional<JSObject JS::Runtime::Intrinsics::*>{}));

#if ENABLE_ES4
			/*Error(std::optional<JSString> message, std::optional<JSObject JS::Runtime::Intrinsics::*> using_prototype)
				: Error(message, std::optional<JSValue>(), using_prototype)
			{
			}*/
#endif

			JSString GetClassString() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		/** @brief ECMAScript arrays */
		class Array : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			// TODO: enable sparse arrays alongside dense arrays
			enum class Layout
			{
				Dense,
				Sparse,
			};

		private:
			Layout layout;

		public:
			static Layout DefaultLayout;
			// private
			std::vector<JSProperty> elements;

			void init(std::vector<JSValue> values)
			{
				set_length(JSPropertyDescriptor().withValue(JSNumber(values.size())));
				for(uintptr_t i = 0; i < values.size(); i++)
				{
					elements[i].SetValue(values[i]);
				}
			}

			Array(uint32_t length)
				: Array(length, DefaultLayout)
			{
			}

			Array(uint32_t length, Layout layout);

			Array()
				: Array(0)
			{
			}

			Array(std::vector<JSValue> values)
				: Array(values.size())
			{
				for(uintptr_t i = 0; i < values.size(); i++)
				{
					elements[i].SetValue(values[i]);
				}
			}

			bool set_length_property(JSPropertyDescriptor descriptor);
			bool set_length(JSPropertyDescriptor descriptor);
			bool set_length(uint32_t length)
			{
				return set_length(JSPropertyDescriptor().withValue(JSNumber(length)));
			}

			uint32_t get_length();

			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;

			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;

			JSString GetClassString() override;
			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;
		};

		/** @brief ECMAScript regular expression objects
		 *
		 * Introduced in ECMAScript 3
		 */
		class RegExp : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			// internal slots
			void * RegExpMatcher; // TODO
			JSString OriginalSource;
			std::set<char16_t> OriginalFlags; // TODO: global, ignoreCase, multiline
			// properties
			JSValue lastIndex;

			RegExp()
			{
#if ENABLE_ES4
				if(IsVersionES4())
				{
					// TODO: set type
				}
#endif
				Define(u"lastIndex",
					JSProperty::MakeImmutableInternal(lastIndex, false)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS);
			}

			JSString GetClassString() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		/** @brief A singleton type to represent built-in objects with a specific class string
		 *
		 * Only used for the global object, Math and JSON
		 */
		class Singleton : public OrdinaryObject
		{
		public:
			JSString Class;
			Singleton(JSString Class) : Class(Class) { }

			JSString GetClassString() override;
		};

#if ENABLE_ES4
		/** @brief ECMAScript 4 package object
		 *
		 * Introduced in the Netscape proposal
		 *
		 * Replaces the global object from previous ECMAScript versions, packages can be used to separate and share name bindings
		 */
		class Package : public JS::Class::OrdinaryObject
		{
		public:
			/* Attempt to unify the package semantics from JScript .NET, ActionScript 3 and the Netscape proposeal for ECMAScript 4
			 * goals:
			 * - each package statement can create a new package object
			 * - the package object becomes part of the lexical scope inside the package statement
			 * - packages can have locally defined members as well as imported members
			 * - imported members are not explicitly exposed as actual members (unlike the ES4 proposal)
			 * - when an `import A.B.C;` statement occurs, we need to be able to access it as either `C` or `A.B.C` (JSN and AS3)
			 * - a package can be defined spread across multiple source files (AS3 behavior)
			 *
			 * To solve all of these, a package object has the following properties:
			 * - A package object can be accessed through lexical lookup or through membership lookup
			 * - When loading a package, a central store (in JS::Host) is used
			 * - When used in a lexical scope, a package keeps track of which subpackages are actually imported
			 * - Packages can be Transient (only used as intermediary elements in a membership lookup), Closed (fully defined by
			 *   the source) or Open (composed of entities defined in multiple places)
			 * - Open packages are defined in separate files, but the resulting package object has to be unique
			 *
			 * When A is a package, the lookup A.B goes through the following steps:
			 * - Check if package A has a defined member B
			 * - A step to check imported names that is not invoked, only needed to when looking up unqualified (as in no "A." prefix) identifiers
			 * - Check if there is an import in the current scope that is either A.B or contains A.B as its head in a full path
			 * - If A.B is currently imported, load it from the current central store (not the object itself)
			 * - If A.B is not important but it is the head of an import path, create a temporary empty transient package to provide
			 *   access to it
			 */

			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;

			struct ImportDir
			{
			public:
				std::map<JSString, std::shared_ptr<ImportDir>> Imports;
				// the package has been imported, as opposed to being part of a path that is imported
				// for example, `import A.B.C;` will set A.B.C to alive, but not A or A.B
				bool Alive;
				bool Open = false;
			};

			enum class Type
			{
				/* package used only to access member packages (JSN/AS3) */
				Transient,
				/* package used only to access non-package members (AS3) */
				Open,
				/* fully populated package object (JSN/ES4) */
				Closed,
			};
			Type PackageType = Type::Closed;

			// internal slots
			JSString Name;
			std::optional<std::vector<JSString>> Path;
			JSNamespace InternalNamespace;

			// bindings coming from imports, with the associated package object
			JS::Runtime::Bindings<JSPackage> imported_bindings;
			// which packages/objects are actually imported
			ImportDir import_dir;

//				JS::Runtime::ScriptOrModuleRecord * ScriptOrModule;

			Package(JSString Name = u"", Type PackageType = Type::Closed, std::optional<std::vector<JSString>> Path = {});

			bool HasActualProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase));

			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
			bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) override;
			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;
			bool Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;

			// make accessing the package name valid
			void ActivateImportedPackage(std::vector<JSString> package_name, bool open = false);
			JSPackage LookupPackage(std::vector<JSString> package_name);

			void Import(JSPackage from_package, JSPropertyKey key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses));
		};
#endif

		// ES6
		/** @brief Module namespace exotic object */
		class ModuleNamespace : public OrdinaryObject
		{
		public:
			JS::Runtime::ModuleRecord * Module;
			std::set<JSString> Exports;

			ModuleNamespace(JS::Runtime::ModuleRecord * Module, std::set<JSString> Exports)
				: Module(Module), Exports(Exports)
			{
			}

			bool IsExtensible() override;
			bool PreventExtensions() override;
			std::optional<JSValue> GetPrototypeOf() override;
			bool SetPrototypeOf(JSValue new_prototype) override;
			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
			bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) override;
			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;
			bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) override;
			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES6
		class Proxy : public JS::Class::Object
		{
		public:
			JSObject ProxyHandler;
			JSObject ProxyTarget;

			Proxy(JSObject ProxyTarget, JSObject ProxyHandler)
				: ProxyHandler(ProxyHandler), ProxyTarget(ProxyTarget)
			{
			}

			void ValidateNonRevokedProxy();

#if ENABLE_ES4
			JSType GetType() override;
#endif
#if 0
#if ENABLE_ES4
			virtual JSClass GetClass();
#endif
#endif
#if ENABLE_ES4
			std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> FindSlot(JSInstanceVariable id) override;
#endif
#if 0
			virtual JSValue CreateReference(JSIdentifierReference identifier_reference, JS::Runtime::ReferenceContext cxt);
			virtual JSValue CreateBracketReference(std::vector<JSValue> arguments, JS::Runtime::ReferenceContext cxt);
			virtual JSValue ResolvePropertyKey(JSValue property_key);
			virtual JSValue ResolveMethod(JS::Runtime::MemberReference& reference);
#if ENABLE_ES4
			virtual JSValue ResolveMethod(JS::Runtime::BracketReference& reference);
#endif
#endif
			bool IsExtensible() override;
			bool PreventExtensions() override;
			std::optional<JSValue> GetPrototypeOf() override;
			bool SetPrototypeOf(JSValue new_prototype) override;
#if 0
			JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase));
#endif
			JSValue DefaultValue(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			std::optional<JSProperty> GetProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
#if 0
			virtual bool HasOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses));
#endif
			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
#if 0
#if ENABLE_ES4
			virtual JSValue GetBracket(std::vector<JSValue> arguments, JSValue receiver, Intent intent AND_CLASS(limit) AND_PHASE(phase));
#endif
#endif
			bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) override;
#if 0
#if ENABLE_ES4
			virtual bool SetBracket(std::vector<JSValue> arguments, JSValue value, JSValue receiver AND_CLASS(limit));
#endif
#endif
			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;
#if 0
#if ENABLE_ES4
			virtual bool DeleteBracket(std::vector<JSValue> arguments AND_CLASS(limit));
#endif
#endif
			bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
#if 0
			virtual bool Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses));
#endif
			std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) override;
			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
			static bool IsArray(JSObject object);
			JSString GetClassString() override;
#if 0
			virtual JSString GetClassSlot();
			virtual JSValue NewObject();
#endif
			Invocability GetInvocability() override;
			JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
#if 0
			bool Begin_Call(JSValue this_argument, std::vector<JSValue> arguments) override;
#endif
			JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
#if 0
#if ENABLE_ES4
			virtual std::optional<JS::Runtime::SignatureType> GetSignature();
#endif
			virtual JS::Runtime::Realm * GetFunctionRealm(); // TODO: Proxy objects may have one as well
#endif
		};

		// ES6
		class Map : public OrdinaryObject
		{
		public:
			// internal slots
			std::map<JSValue, JSValue> MapData;
			// private data
			std::vector<JSValue> insertion_order;

			// TODO

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES6
		class Set : public OrdinaryObject
		{
		public:
			// internal slots
			std::set<JSValue> SetData;
			// private data
			std::vector<JSValue> insertion_order;

			// TODO

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES6
		class WeakMap : public OrdinaryObject
		{
		public:
			// internal slots
			std::map<JSValue, gcn::weak_gc_ptr<JSObjectValue>> WeakMapData;
			// private data
			std::vector<JSValue> insertion_order;

			// TODO

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES6
		class WeakSet : public OrdinaryObject
		{
		public:
			// internal slots
			std::set<gcn::weak_gc_ptr<JSObjectValue>> WeakSetData;
			// private data
			std::vector<JSValue> insertion_order;

			// TODO

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES6 (and later)
		class AbstractArrayBuffer : public OrdinaryObject
		{
		public:
			virtual bool IsFixedLength() const = 0;
			virtual uintptr_t ByteLength() const = 0;
			virtual bool IsDetached() const = 0;

			virtual void GetBytesFromBuffer(uintptr_t byteIndex, std::vector<std::byte>& rawValue, uintptr_t elementSize, bool isTypedArray, bool isOrdered) = 0;
			virtual void SetBytesInBuffer(uintptr_t byteIndex, std::vector<std::byte>& rawValue, uintptr_t elementSize, bool isTypedArray, bool isOrdered) = 0;
		};

		class ArrayBuffer : public AbstractArrayBuffer
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			std::shared_ptr<std::vector<std::byte>> ArrayBufferData;

			// TODO: ArrayBufferDetachKey

		protected:
			ArrayBuffer(uintptr_t byteLength)
				: ArrayBufferData(std::make_shared<std::vector<std::byte>>(byteLength, std::byte{0}))
			{
			}

		public:
			uintptr_t ByteLength() const override;
			bool IsDetached() const override;

			void GetBytesFromBuffer(uintptr_t byteIndex, std::vector<std::byte>& rawValue, uintptr_t elementSize, [[maybe_unused]] bool isTypedArray, [[maybe_unused]] bool isOrdered) override;
			void SetBytesInBuffer(uintptr_t byteIndex, std::vector<std::byte>& rawValue, uintptr_t elementSize, [[maybe_unused]] bool isTypedArray, [[maybe_unused]] bool isOrdered) override;
		};

		class FixedArrayBuffer : public ArrayBuffer
		{
		public:
			FixedArrayBuffer(uintptr_t byteLength)
				: ArrayBuffer(byteLength)
			{
			}

			bool IsFixedLength() const override;

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		class GrowableArrayBuffer : public ArrayBuffer
		{
		public:
			uintptr_t ArrayBufferMaxByteLength;
			GrowableArrayBuffer(uintptr_t byteLength, uintptr_t maxByteLength);

			bool IsFixedLength() const override;

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES8
		class SharedArrayBuffer : public AbstractArrayBuffer
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			struct AtomicArray
			{
				// TODO
				std::byte * data;
				const uintptr_t length;
				AtomicArray(uintptr_t length) : data(new std::byte[length]), length(length) { }
				~AtomicArray() { delete data; }
				uintptr_t size() const { return length; }
			};

			std::shared_ptr<AtomicArray> ArrayBufferData;

		protected:
			SharedArrayBuffer(uintptr_t byteLength)
				: ArrayBufferData(std::make_shared<AtomicArray>(byteLength))
			{
			}

		public:
			bool IsDetached() const override;

			void GetBytesFromBuffer([[maybe_unused]] uintptr_t byteIndex, [[maybe_unused]] std::vector<std::byte>& rawValue, [[maybe_unused]] uintptr_t elementSize, [[maybe_unused]] bool isTypedArray, [[maybe_unused]] bool isOrdered) override;

			void SetBytesInBuffer([[maybe_unused]] uintptr_t byteIndex, [[maybe_unused]] std::vector<std::byte>& rawValue, [[maybe_unused]] uintptr_t elementSize, [[maybe_unused]] bool isTypedArray, [[maybe_unused]] bool isOrdered) override;
		};

		class FixedSharedArrayBuffer : public SharedArrayBuffer
		{
		public:
			FixedSharedArrayBuffer(uintptr_t byteLength)
				: SharedArrayBuffer(byteLength)
			{
			}

			bool IsFixedLength() const override;
			uintptr_t ByteLength() const override;

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		class GrowableSharedArrayBuffer : public SharedArrayBuffer
		{
		public:
			uintptr_t ArrayBufferByteLengthData;

			GrowableSharedArrayBuffer(uintptr_t byteLength, uintptr_t maxByteLength);

			bool IsFixedLength() const override;
			uintptr_t ByteLength() const override;

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES6
		class DataView : public OrdinaryObject
		{
		public:
			void * DataView; // TODO
			gcn::gc_ptr<ArrayBuffer> ViewedArrayBuffer; // TODO
			uintptr_t ByteLength;
			uintptr_t ByteOffset;

			// TODO

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES6
		class Promise;

		class XMLBase : public OrdinaryObject
		{
		public:
			JSValue ConvertToPropertyKey(JSValue property_key AND_PHASE(phase)) override;
			JSValue CreateReference(JSIdentifierReference identifier_reference, JS::Runtime::ReferenceContext cxt) override;
			JSValue ResolvePropertyKey(JSValue property_key) override;

			JSString Typeof() override;
		};

		class XML : public XMLBase
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			enum class ClassType
			{
				Element,
				Attribute,
				Comment,
				ProcessingInstruction,
				Text,
			};

			std::vector<gcn::gc_ptr<XML>> children;
			ClassType Class = ClassType::Text;
			std::optional<JSString> Value;
			std::optional<::JS::Type::QName> Name;
			gcn::gc_ptr<XML> Parent = nullptr;
			std::map<::JS::Type::QName, gcn::gc_ptr<XML>> Attributes;
			std::map<JSString, JSNamespace> InScopeNamespaces;

			// E4X
			std::optional<JSNamespace> DefaultNamespace;

			// returns "XML"
			JSString GetClassSlot() override;
			JSString GetClassString() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;

			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
			// TODO: more methods

			JSValue ResolveMethod(JS::Runtime::MemberReference& reference) override;
#if ENABLE_ES4
			JSValue ResolveMethod(JS::Runtime::BracketReference& reference) override;
#endif

			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;

			JSString ToString(FOR_PHASE(phase)) override;
			JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
			JSXML ToXML() override;
			JSXMLList ToXMLList() override;

			bool HasSimpleContent()
			{
				if(Class == ClassType::Comment || Class == ClassType::ProcessingInstruction)
				{
					return false;
				}
				for(auto child : children)
				{
					if(child->Class == ClassType::Element)
					{
						return false;
					}
				}
				return true;
			}

			// TODO: trim spaces
		};

		class XMLList : public XMLBase
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			std::vector<gcn::gc_ptr<XML>> children;
			JSObject TargetObject = nullptr;
			JSValue TargetProperty = JS_null;

			// returns "XML"
			JSString GetClassString() override;
			// returns "XMLList"
			JSString GetClassSlot() override;

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
			// TODO

			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;

			JSValue ResolveMethod(JS::Runtime::MemberReference& reference) override;
#if ENABLE_ES4
			JSValue ResolveMethod(JS::Runtime::BracketReference& reference) override;
#endif

			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;

			JSString ToString(FOR_PHASE(phase)) override;
			JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
			JSXML ToXML() override;
			JSXMLList ToXMLList() override;

			bool HasSimpleContent()
			{
				for(auto child : children)
				{
					if(child->Class == XML::ClassType::Element)
					{
						return false;
					}
				}
				return true;
			}

			void Append(JSValue value);
		};

		class QName : public OrdinaryObject, public ::JS::Type::QName
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

#if ENABLE_ES4
			// ES4.2008 Name class field
			std::optional<JSNamespace> qualifier;
#endif

			QName(const ::JS::Type::QName& qname)
				: ::JS::Type::QName(qname)
			{
				Define(
					u"localName",
					JSProperty::MakeImmutableInternal(localName, false)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS);

				Define(
					u"uri",
					JSProperty::MakeImmutableInternal(uri, false)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS);

#if ENABLE_ES4
				Define(
					u"qualifier",
					JSProperty::MakeImmutableInternal(qualifier, false)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS);
#endif
			}

			QName(JSString uri, JSString localName, std::optional<JSString> prefix = {} __ES4_ONLY(, std::optional<JSNamespace> qualifier = {}))
				: QName(::JS::Type::QName(uri, localName, prefix))
			{
#if ENABLE_ES4
				this->qualifier = qualifier;
#endif
			}

			QName(JSString localName)
				: QName(::JS::Type::QName(localName))
			{
			}

			QName()
				: QName(::JS::Type::QName())
			{
			}

			/*bool operator ==(const QName& other) const
			{
				return Prefix == other.Prefix && uri == other.uri && localName == other.localName;
			}

			bool operator <(const QName& other) const
			{
				return Prefix < other.Prefix
					|| (Prefix == other.Prefix && uri < other.uri)
					|| (Prefix == other.Prefix && uri == other.uri && localName < other.localName);
			}*/

			JSAttributeName ToAttributeName() override;
			JSValue ToXMLName() override;

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES4
		class Namespace : public OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

#if ENABLE_ES4
			static std::map<JSString, JSNamespace> named_namespaces; // used outside E4X mode
#endif

#if ENABLE_ES4
			// used for debugging
			static std::vector<JSNamespace> all_allocated_namespaces;
			static std::map<JSString, std::vector<JSNamespace>> allocated_namespaces;
			static void RegisterNamespace(JSNamespace namespace_);
#endif

			// properties
			std::optional<JSString> prefix;
			JSString uri; // also called "name"

#if ENABLE_ES4
		protected:
#endif
			Namespace(std::optional<JSString> prefix, JSString name __ES4_ONLY(, bool this_is_public)) : prefix(prefix), uri(name)
			{
#if ENABLE_ES4
				if(IsVersionES4() || this_is_public) // the public namespace is created before anything else is set up, but it is only used in ES4 mode
				{
					if(this_is_public)
					{
						JS_public = gcn::gc_ptr<JS::Class::Namespace>(this); // to make sure the properties are properly added
					}
					else
					{
						RegisterNamespace(gcn::gc_ptr<JS::Class::Namespace>(this));
					}
					Extensible = false;
				}
#endif
				Define(
					u"prefix",
					JSProperty::MakeImmutableInternal(this->prefix, false)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS);

				Define(
					u"uri",
					JSProperty::MakeImmutableInternal(this->uri, false)
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS);
			}

		public:
#if ENABLE_ES4
			Namespace(std::optional<JSString> prefix, JSString name) : Namespace(prefix, name, false)
			{
			}
#endif

			Namespace(JSString name) : Namespace(std::optional<JSString>{}, name)
			{
			}

			Namespace() : Namespace(u"")
			{
			}

			Namespace(gcn::gc_ptr<Namespace> other) : Namespace(other->prefix, other->uri)
			{
			}

			Namespace(Namespace * other) : Namespace(other->prefix, other->uri)
			{
			}
		};
	}

	namespace Type
	{
#if ENABLE_ES4
		namespace Internal
		{
			// ES4
			// since the identifier JS_public is created before anything else is set up, some methods need to be hijacked to make it work properly
			class PublicNamespace : public JS::Class::Namespace
			{
			public:
				PublicNamespace() : Namespace(std::optional<JSString>{}, u"public", true)
				{
				}

				std::optional<JSValue> GetPrototypeOf() override;
				JSType GetType() override;
			};
		}
#endif
	}

	namespace Class
	{
		class EvalError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			EvalError(JSString message = u"") : Error(message, {} __ES4_ONLY(, IntrinsicPrototype)) { }
		};

		class RangeError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			RangeError(JSString message = u"") : Error(message, {} __ES4_ONLY(, IntrinsicPrototype)) { }
		};

		class ReferenceError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			ReferenceError(JSString message = u"") : Error(message, {} __ES4_ONLY(, IntrinsicPrototype)) { }
		};

		class SyntaxError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			SyntaxError(JSString message = u"") : Error(message, {} __ES4_ONLY(, IntrinsicPrototype)) { }
		};

		class TypeError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			TypeError(JSString message = u"") : Error(message, {} __ES4_ONLY(, IntrinsicPrototype)) { }
		};

		class URIError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			URIError(JSString message = u"") : Error(message, {} __ES4_ONLY(, IntrinsicPrototype)) { }
		};

#if ENABLE_ES4
		class ArgumentError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;

			ArgumentError(JSString message = u"") : Error(message, {}, IntrinsicPrototype) { }
		};

		class AttributeError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;

			AttributeError(JSString message = u"") : Error(message, {}, IntrinsicPrototype) { }
		};

		class ConstantError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;

			ConstantError(JSString message = u"") : Error(message, {}, IntrinsicPrototype) { }
		};

		class DefinitionError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;

			DefinitionError(JSString message = u"") : Error(message, {}, IntrinsicPrototype) { }
		};

		class UninitializedError : public JS::Class::Error
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;

			UninitializedError(JSString message = u"") : Error(message, {}, IntrinsicPrototype) { }
		};
#endif

		class InternalError : public JS::Class::Error
		{
		public:
			InternalError(JSString message = u"") : Error(message) { }
		};

		// ES5
		class Arguments : public JS::Class::OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;
#if ENABLE_ES4
			static JSClass JS::Runtime::Intrinsics::* IntrinsicClass;
#endif

			struct Mapping
			{
				// the actual name of the variable
				JSString Name;
				// whether the simulated property is enumerable
				bool Enumerable;
				// whether the simulated property is configurable
				bool Configurable;
			};

			// simulates a ParameterMap object according to ECMAScript
			std::vector<std::optional<Mapping>> parameter_map;
			std::shared_ptr<JS::Runtime::EnvironmentRecord> environment;

			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
			bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) override;
			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;
			bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;

			//std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) override;
			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;

			JSString GetClassString() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES5
		class BoundFunction : public JS::Class::OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			// TODO: incorporate Object class
			JSFunction BoundTargetFunction;
			JSValue BoundThis;
			std::vector<JSValue> BoundArguments;
			// TODO

			//Class::Function::ConstructorKindType GetConstructorKind(); // TODO: is this needed?
			Invocability GetInvocability() override;
#if ENABLE_ES4
			std::optional<JS::Runtime::SignatureType> GetSignature() override;
#endif
			JSString GetClassString() override;

			JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;

			JS::Runtime::Realm * GetFunctionRealm() override;

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};

		// ES6
		class DefaultConstructor : public JS::Class::Function
		{
		public:
			DefaultConstructor(
				JS::Runtime::Realm * Realm,
				JSString name,
				ConstructorKindType ConstructorKind = ConstructorKindType::Base)
				: Function(Realm, name, 0,
					static_cast<JS::ParseNode::FunctionalElement *>(nullptr),
					ThisModeType::Lexical, // not used
					nullptr,
					nullptr
					PUBLIC_NAMESPACES,
					FunctionKindType::Normal,
					ConstructorKind // might be reassigned during initialization
				)
			{
			}

			JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
		private:
			static JSValue Execute(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));
		};

		// ES6
		class TypedArray : public Class::OrdinaryObject
		{
		public:
			// internal slots
			enum class ElementType
			{
				Int8,
				Uint8,
				Uint8Clamped,
				Int16,
				Uint16,
				Int32,
				Uint32,
				BigInt64, // ES11
				BigUint64, // ES11
				Float16, // ES16
				Float32,
				Float64,
			};
			ElementType TypedArrayName;
			gcn::gc_ptr<JS::Class::AbstractArrayBuffer> ViewedArrayBuffer;
			uintptr_t ByteLength;
			uintptr_t ByteOffset;
			// AUTO if missing
			std::optional<uintptr_t> ArrayLength;

			static inline constexpr uintptr_t ElementSize(ElementType type)
			{
				switch(type)
				{
				case ElementType::Int8:
				case ElementType::Uint8:
				case ElementType::Uint8Clamped:
					return 1;
				case ElementType::Int16:
				case ElementType::Uint16:
					return 2;
				case ElementType::Int32:
				case ElementType::Uint32:
					return 4;
				case ElementType::BigInt64:
				case ElementType::BigUint64:
					if(Version::ES11 <= CurrentVersion)
					{
						return 8;
					}
					else
					{
						return 0;
					}
					break;
				case ElementType::Float16:
					if(Version::ES16 <= CurrentVersion)
					{
						return 2;
					}
					else
					{
						return 0;
					}
					break;
				case ElementType::Float32:
					return 4;
				case ElementType::Float64:
					return 8;
				default:
					return 0;
				}
			}

			static inline constexpr bool IsContentTypeBigInt(ElementType type)
			{
				return type == ElementType::BigInt64 || type == ElementType::BigUint64;
			}

			static inline JSString ArrayName(ElementType type)
			{
				switch(type)
				{
				case ElementType::Int8:
					return u"Int8Array";
				case ElementType::Uint8:
					return u"Uint8Array";
				case ElementType::Uint8Clamped:
					return u"Uint8ClampedArray";
				case ElementType::Int16:
					return u"Int16Array";
				case ElementType::Uint16:
					return u"Uint16Array";
				case ElementType::Int32:
					return u"Int32Array";
				case ElementType::Uint32:
					return u"Uint32Array";
				case ElementType::BigInt64:
					if(Version::ES11 <= CurrentVersion)
					{
						return u"BigInt64Array";
					}
					else
					{
						return u"";
					}
					break;
				case ElementType::BigUint64:
					if(Version::ES11 <= CurrentVersion)
					{
						return u"BigUint64Array";
					}
					else
					{
						return u"";
					}
					break;
				case ElementType::Float16:
					if(Version::ES16 <= CurrentVersion)
					{
						return u"Float16Array";
					}
					else
					{
						return u"";
					}
					break;
				case ElementType::Float32:
					return u"Float32Array";
				case ElementType::Float64:
					return u"Float64Array";
				default:
					return u"";
				}
			}

			TypedArray(ElementType TypedArrayName)
				: TypedArrayName(TypedArrayName), ViewedArrayBuffer(nullptr), ByteLength(0), ByteOffset(0), ArrayLength(0)
			{
			}

			TypedArray(ElementType TypedArrayName, uintptr_t length);

			TypedArray(ElementType TypedArrayName, JS::Class::ArrayBuffer * ViewedArrayBuffer)
				: TypedArrayName(TypedArrayName),
				ViewedArrayBuffer(ViewedArrayBuffer),
				ByteLength(ViewedArrayBuffer ? ViewedArrayBuffer->ByteLength() : 0),
				ByteOffset(0),
				ArrayLength(ViewedArrayBuffer ? ViewedArrayBuffer->ByteLength() / ElementSize(TypedArrayName) : 0)
			{
			}

			TypedArray(ElementType TypedArrayName, JS::Class::ArrayBuffer * ViewedArrayBuffer, uintptr_t ByteOffset, uintptr_t ByteLength)
				: TypedArrayName(TypedArrayName), ViewedArrayBuffer(ViewedArrayBuffer), ByteLength(ByteLength), ByteOffset(ByteOffset), ArrayLength(ByteLength / ElementSize(TypedArrayName))
			{
			}

			static uint8_t RawBytesToUint8(std::vector<std::byte> raw_bytes);
			static uint16_t RawBytesToUint16(std::vector<std::byte> raw_bytes, bool is_little_endian);
			static uint32_t RawBytesToUint32(std::vector<std::byte> raw_bytes, bool is_little_endian);
			static uint64_t RawBytesToUint64(std::vector<std::byte> raw_bytes, bool is_little_endian);

			static float16_t Uint16ToFloat16(uint16_t value);
			static float32_t Uint32ToFloat32(uint32_t value);
			static float64_t Uint64ToFloat64(uint64_t value);

			static JSValue RawBytesToNumeric(ElementType type, std::vector<std::byte> raw_bytes, bool is_little_endian);

			static void Uint8ToRawBytes(std::vector<std::byte>& raw_bytes, uint8_t value);
			static void Uint16ToRawBytes(std::vector<std::byte>& raw_bytes, uint16_t value, bool is_little_endian);
			static void Uint32ToRawBytes(std::vector<std::byte>& raw_bytes, uint32_t value, bool is_little_endian);
			static void Uint64ToRawBytes(std::vector<std::byte>& raw_bytes, uint64_t value, bool is_little_endian);

			static uint16_t Float16ToUint16(float16_t value);
			static uint32_t Float32ToUint32(float32_t value);
			static uint64_t Float64ToUint64(float64_t value);

			static void NumericToRawBytes(ElementType type, std::vector<std::byte>& raw_bytes, JSValue value, bool is_little_endian);

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;

			bool IsFixedLength();

			std::optional<uintptr_t> IsValidIntegerIndex(JSNumber index)
			{
				if(!ViewedArrayBuffer->IsDetached()
				&& index >= 0.0)
				{
					uintptr_t integer_index = index;
					if(integer_index == index && integer_index <= ArrayLength)
					{
						return integer_index;
					}
				}
				return {};
			}

			std::optional<JSValue> GetElement(JSNumber index)
			{
				if(auto index_option = IsValidIntegerIndex(index))
				{
					const uintptr_t elementSize = ElementSize(TypedArrayName);
					std::vector<std::byte> raw_bytes(elementSize);
					ViewedArrayBuffer->GetBytesFromBuffer(index_option.value() * elementSize, raw_bytes, elementSize, true, false);
					return RawBytesToNumeric(TypedArrayName, raw_bytes, GetSurroundingAgent()->LittleEndian);
				}
				else
				{
					return {};
				}
			}

			void SetElement(JSNumber index, JSValue value)
			{
				JSValue numValue;

				if(IsContentTypeBigInt(TypedArrayName))
				{
					numValue = JS::ToBigInt(value);
				}
				else
				{
					numValue = JS::ToNumber(value RUNTIME_PHASE);
				}

				if(auto index_option = IsValidIntegerIndex(index))
				{
					const uintptr_t elementSize = ElementSize(TypedArrayName);
					std::vector<std::byte> raw_bytes(elementSize);
					NumericToRawBytes(TypedArrayName, raw_bytes, numValue, GetSurroundingAgent()->LittleEndian);
					ViewedArrayBuffer->SetBytesInBuffer(index_option.value() * elementSize, raw_bytes, elementSize, true, false);
				}
			}

			bool PreventExtensions() override;
			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
			bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) override;
			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;
			bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) override;
			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;
		};

		// ES6
		class Generator;
		class ArrayIterator;
		// ESNext
		class AsyncFromSyncIterator;

		// Node API
		class External : public JS::Class::Object
		{
		public:
			// behaves like a frozen object with null prototype
			void * data;
			void * finalize;
			void * hint;

			External(void * data, void * finalize, void * hint)
				: data(data), finalize(finalize), hint(hint)
			{
			}

			~External() override;

#if ENABLE_ES4
			JSType GetType() override;
			std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> FindSlot(JSInstanceVariable id) override;
#endif
			bool IsExtensible() override;
			bool PreventExtensions() override;
			std::optional<JSValue> GetPrototypeOf() override;
			bool SetPrototypeOf(JSValue new_prototype) override;
			std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			std::optional<JSProperty> GetProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
			bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) override;
			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;
			bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) override;
			std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override;
			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
			JSString GetClassString() override;
			Invocability GetInvocability() override;
			JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;

			JSValue DefaultValue(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
		};
	}

	template <class C, typename ... Args>
		gcn::gc_ptr<C> New(Args ... args)
	{
		auto object = gcn::make_gc<C>(args...);
		JS::Runtime::Intrinsics * intrinsics = JS::Intrinsics();
		object->Prototype = intrinsics ? intrinsics->*C::IntrinsicPrototype : nullptr;
		object->Extensible = true;
#if ENABLE_ES4
		if constexpr(HasIntrinsicClass<C>::Value)
		{
			if(IsVersionES4() && intrinsics)
			{
				JS::Class::Class::AssignType(object, intrinsics->*C::IntrinsicClass);
			}
		}
#endif
		return object;
	}

	template <class C, typename ... Args>
		gcn::gc_ptr<C> NewWithPrototype(std::optional<JSValue> prototype, Args ... args)
	{
		auto object = gcn::make_gc<C>(args...);
#if ENABLE_ES4
		JS::Runtime::Intrinsics * intrinsics = JS::Intrinsics();
#endif
		object->Prototype = prototype;
		object->Extensible = true;
#if ENABLE_ES4
		if constexpr(HasIntrinsicClass<C>::Value)
		{
			if(IsVersionES4() && intrinsics)
			{
				JS::Class::Class::AssignType(object, intrinsics->*C::IntrinsicClass);
			}
		}
#endif
		return object;
	}

#if ENABLE_ES4
	template <class C, typename ... Args>
		gcn::gc_ptr<C> NewWithClass(JSClass class_, Args ... args)
	{
		auto object = gcn::make_gc<C>(args...);
		JS::Runtime::Intrinsics * intrinsics = JS::Intrinsics();
		object->Prototype = intrinsics ? intrinsics->*C::IntrinsicPrototype : nullptr;
		object->Extensible = true;
		JS::Class::Class::AssignType(object, class_);
		return object;
	}

	template <class C, typename ... Args>
		gcn::gc_ptr<C> NewWithClassAndPrototype(JSClass class_, std::optional<JSValue> prototype, Args ... args)
	{
		auto object = gcn::make_gc<C>(args...);
		object->Prototype = prototype;
		object->Extensible = true;
		object->Type = class_;
		JS::Class::Class::AssignType(object, class_);
		return object;
	}
#endif

	template <typename T>
		JSObject GetPrototypeFromConstructor(JSFunction constructor, T JS::Runtime::Intrinsics::* default_prototype)
	{
		if(auto proto = GetNonNullObject(JS::Get(constructor, u"prototype")))
		{
			return *proto;
		}
		else
		{
			return constructor.GetFunctionRealm()->Intrinsics.*default_prototype;
		}
	}

	template <typename T>
		struct value_convertible;

	template <>
		struct value_convertible<JSValue>
	{
		static JSValue convert(JSValue value) { return value; }
	};

	template <>
		struct value_convertible<JSPropertyKey>
	{
		static JSPropertyKey convert(JSValue value)
		{
			return assert_property_key(value);
		}
	};

	template <typename T = JSValue>
		std::vector<T> CreateListFromArrayLike(JSValue arraylike_value)
	{
		auto arraylike_option = GetNonNullObject(arraylike_value);
		if(!arraylike_option.has_value())
		{
			JS::Throw(JS::New<JS::Class::TypeError> __LOC("expected array-like object, received non-object"));
		}
		auto arraylike = arraylike_option.value();
		auto len = JS::ToLength(JS::Get(arraylike, u"length"));
		std::vector<T> list;
		for(uint64_t index = 0; index < len; index++)
		{
			auto indexName = JS::ToString(JSNumber(index) RUNTIME_PHASE);
			auto next = JS::Get(arraylike, indexName);
			list.push_back(value_convertible<T>::convert(next));
		}
		return list;
	}

	JSFunction GetSpeciesConstructor(JSObject object, JSFunction default_value);

	static inline JS::Public::JSValue assert_singlet(std::vector<JS::Public::JSValue> arguments)
	{
		if(arguments.size() != 1)
		{
#if ENABLE_ES4
			if(JS::IsVersionES4())
			{
				JS::Throw(JS::New<JS::Class::ArgumentError> __LOC(u"argument count mismatch"));
			}
			else
#endif
			{
				JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"argument count mismatch"));
			}
		}
		return arguments[0];
	}
}

//JS::Public::JSFunction::operator JS::Class::Function *() { return dynamic_cast<JS::Class::Function *>(it); }
//JS::Public::JSFunction::operator Class::OrdinaryObject *() { return dynamic_cast<JS::Class::OrdinaryObject *>(it); }

#endif // _OBJECT_HH
