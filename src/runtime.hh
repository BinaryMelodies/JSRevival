#ifndef _RUNTIME_HH
#define _RUNTIME_HH

/* Structures and routines used by the runtime, as described in the specifications */

#include <cassert>
#include <filesystem>
#include <gc_cpp.h>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <typeinfo>
#include <variant>
#include <vector>

#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "utils.hh"
#include "../out/intrinsics.hh"

namespace JS
{
	using namespace Public;

	namespace Interpreter
	{
		/* Represents a block/loop/try block */
		struct Range
		{
			enum class RangeType
			{
				Block, // restores environment records
				Function, // like Block but also catches return statements
				Try,
				Loop, // while, do-while or for
				Switch,
			};

			RangeType type;
			std::optional<JSString> label; // only for Loop
			size_t handle = 0; // continue for Loop, catch branch for Try
			size_t end = 0; // break for Loop and Switch, finally branch for Try
			// only for functions
			bool is_arrow = false;
			bool is_generator = false;
			bool is_async = false;

			size_t stack_size = 0; // reset to this stack size after leaving
			// values to restore
			JS::Runtime::ExecutionContext * Context = nullptr;
			bool previous_strict_mode = false;
#if ENABLE_ES4
			Version previous_version;
#endif
			std::shared_ptr<JS::Runtime::EnvironmentRecord>        LexicalEnvironment = nullptr;
			std::shared_ptr<JS::Runtime::EnvironmentRecord>        VariableEnvironment = nullptr;
			std::shared_ptr<JS::Runtime::PrivateEnvironmentRecord> PrivateEnvironment = nullptr;

			Range(RangeType type, std::optional<JSString> label = {})
				: type(type), label(label)
#if ENABLE_ES4
					, previous_version(CurrentVersion)
#endif
			{
			}
		};

		class Wrapper
		{
		public:
			virtual ~Wrapper() = default;
			virtual void print_type(std::ostream& out) = 0;
			virtual void print(std::ostream& out) = 0;
		};

		template <typename T>
			class WrapperOf : public Wrapper
		{
		public:
			T value;
			WrapperOf(T value) : value(value) { }
			/*static void print(std::ostream& out, const T& value)
			{
				out << typeid(T).name();
			}*/
			void print_type(std::ostream& out) override
			{
				inspect<T>::inspect_type(out);
			}
			void print(std::ostream& out) override
			{
				std::map<JSObject, std::string> visited;
				inspect<T>::inspect_value(out, value, 5, visited, std::optional<std::string>());
				//print(out, value);
			}
		};
	}

	namespace Runtime
	{
		class PrivateName
		{
		public:
			JSString Description;

			PrivateName(JSString Description) : Description(Description)
			{
				/*std::cerr << "Created new private name ";
				PrintString(std::cerr, Description);
				std::cerr << " -> " << this << std::endl;*/
			}
			PrivateName(const PrivateName&) = delete;
		};

		class Initialization;

		/** Information needed to validate source code before execution
		 *
		 * This information is used for methods that correspond to the Netscape ECMAScript 4 proposal Validation[] stage
		 *
		 * This structure contains the information necessary to setting up certain syntactic structures, such as
		 * what type of declaration statement a variable has (var/let/const), or whether a call is in tail position.
		 */
		struct ValidationContext
		{
		public:
			// ES4: pragma directives will write back here
			// ES4: constructor will access this
			// accesses functions_to_declare
			ValidationContext * CurrentValidationContext;

			// hoisted not allowed, only regional, local and for ES4, class
			EnvironmentMode Mode = EnvironmentMode::Regional;

			bool Strict = false;
#if ENABLE_ES4
			bool is_package_range = true;
			std::set<JSNamespace> OpenNamespaces;
			JSCompoundAttribute Attributes;
			bool superclass_constructor_called = false;

			ValidationContext() : CurrentValidationContext(this) { }
#endif
			// used to find a top-level await statement
			bool is_module_range = true;
			bool has_top_level_await = false;

			std::map<JSString, JS::ParseNode::FunctionDefinition *> functions_to_declare; // collects the last function to declare

			// for declarations
			JS::Runtime::EntityMode entity_mode = JS::Runtime::EntityMode::Unknown;
			JS::ParseNode::DeclarationKind declaration_kind = JS::ParseNode::DeclarationKind::None;

			// for class elements (only valid in class context)
			bool Static = false;
			JS::ParseNode::ParseNode * Initializer = nullptr; // also ES4 constant initializer
			std::optional<JSString> class_name;
			bool is_derived_class = false;

			// to check which calls are in tail position
			bool tail_position = false;

			JS::Runtime::ScriptOrModuleRecord * scriptOrModuleRecord = nullptr;

			bool IsStrict() const
			{
				return Strict;
			}

			std::set<JSNamespace> GetOpenNamespaces() const
			{
#if ENABLE_ES4
				if(IsVersionES4())
				{
					return OpenNamespaces;
				}
				else
				{
					return {JS_public};
				}
#else
				return {};
#endif
			}
		};

		/** Contains the necessary information for evaluating identifier references */
		struct ReferenceContext
		{
		public:
			bool Strict;
#if ENABLE_ES4
			std::set<JSNamespace> OpenNamespaces;
			ExecutionPhase Phase;
#endif
			ReferenceContext(bool Strict
#if ENABLE_ES4
				, std::set<JSNamespace> OpenNamespaces, ExecutionPhase Phase
#endif
			)
				: Strict(Strict)
#if ENABLE_ES4
				, OpenNamespaces(OpenNamespaces), Phase(Phase)
#endif
			{
			}
		};

		struct GeneralBindingContext
		{
		public:
			// used to indicate whether the binding is done via initialization (let/const) or via assignment (var)
			std::optional<std::shared_ptr<JS::Runtime::EnvironmentRecord>> environment;
		};

		/** Contains the information to execute the ECMAScript BindingInitialization operation */
		struct BindingContext : public GeneralBindingContext
		{
		// used to bind values to identifiers, usually in binding patterns
		public:
			// a preevaluated value to be bound to the identifier
			// almost always contains a value (except in iterator and property binding contexts), it can be empty to mark an empty let body
			JSValue Value;

			BindingContext() : GeneralBindingContext() { }
			BindingContext(const GeneralBindingContext& context) : GeneralBindingContext(context) { }
			BindingContext(const BindingContext&) = default;
		};

		class IteratorBindingData;

		/** Contains the information to execute the ECMAScript IteratorBindingInitialization operation */
		struct IteratorBindingContext : public GeneralBindingContext
		{
		public:
			IteratorBindingData * binding_data = nullptr;

#if ENABLE_ES4
			bool exact_match_required = false;
#endif

			IteratorBindingContext() : GeneralBindingContext() { }
			IteratorBindingContext(const GeneralBindingContext& context) : GeneralBindingContext(context) { }
			IteratorBindingContext(const IteratorBindingContext&) = default;
		};

		/** Contains the information to execute the ECMAScript PropertyBindingInitialization and related operations */
		class PropertyBindingData
		{
		public:
			std::set<JSPropertyKey> bound_names;
		};

		/** Contains the information to execute the ECMAScript PropertyBindingInitialization */
		class PropertyBindingContext : public GeneralBindingContext
		{
		public:
			// object for the source of properties
			JSValue Value;

			PropertyBindingData * binding_data;

			PropertyBindingContext() : GeneralBindingContext() { }
			PropertyBindingContext(const GeneralBindingContext& context) : GeneralBindingContext(context) { }
			PropertyBindingContext(const BindingContext& context) : GeneralBindingContext(context), Value(context.Value) { }
			PropertyBindingContext(const PropertyBindingContext&) = default;
		};

		/** Contains the information to execute the ECMAScript KeyedBindingInitialization */
		class KeyedBindingContext : public PropertyBindingContext
		{
		public:
			// the current property key
			JSPropertyKey property_key;

			KeyedBindingContext() : PropertyBindingContext() { }
			KeyedBindingContext(const PropertyBindingContext& context) : PropertyBindingContext(context) { }
			KeyedBindingContext(const BindingContext& context) : PropertyBindingContext(context) { }
			KeyedBindingContext(const KeyedBindingContext&) = default;
		};

		// for ES6 style classes
		struct ClassContext
		{
		public:
			// only valid in class context
			bool is_derived_class = false;
			JSObject ConstructorParent = nullptr;
			JSObject ClassPrototype = nullptr;
			std::optional<JSBindingName> class_name;

			gcn::gc_ptr<JS::Class::Function> * CompiledClass = nullptr;

			// class context specific
			JSObject GetHomeObject(bool Static);

			void DefinePrivateStaticVariable(JSPrivateName name, JS::Runtime::PrivateElement value);
			void DefinePublicStaticVariable(JSPropertyKey name, JSProperty value);

			void DefinePrivateInstanceVariable(JSPrivateName name, JSFunction Initialization);
			void DefinePublicInstanceVariable(JSPropertyKey name, JSFunction Initialization);

			void DefinePrivateMethod(bool Static, JSString name, JSFunction method);
			void DefinePrivateGetter(bool Static, JSString name, JSFunction method);
			void DefinePrivateSetter(bool Static, JSString name, JSFunction method);

			void DefinePublicMethod(bool Static, JSPropertyKey name, JSFunction method);
			void DefinePublicGetter(bool Static, JSPropertyKey name, JSFunction method);
			void DefinePublicSetter(bool Static, JSPropertyKey name, JSFunction method);
		};

		/** Contains a callback for deferred evaluation of a constant initialization, used by ECMAScript 4 */
#if ENABLE_ES4
		class Initialization
		{
		public:
			virtual JSValue Initialize(FOR_PHASE(phase)) = 0;
			static Initialization * Busy;
		};

		class BusyInitializer : public Initialization
		{
		public:
			JSValue Initialize(JS::Runtime::ExecutionPhase phase) override;
		};
#endif

		/* Represents a property value inside an object */
		class Property : public gc_cleanup
		{
		public:
			/* This is a combination of ES5+ object properties, ES6+ declarative environment record entries and ES4 local bindings
			 *
			 * An ES5 object property can be in one of the following states:
			 *   Configurable: boolean - it can be deleted or altered between Data or Accessor property type
			 *   Enumerable: boolean
			 *   Kind: Data or Accessor
			 *   Value (Data only): Value - the content of the property
			 *   Writable (Data only): boolean - whether the contents can be changed
			 *   Get (Accessor only): Function - invoked when reading property value
			 *   Set (Accessor only): Function - invoked when writing property value
			 *
			 * An ES5 binding can be initialized or uninitialized, and one of the following 4 states:
			 *   Mutable, Deletable - name can be altered and deleted
			 *   Mutable - name can be altered but not deleted
			 *   Immutable - name cannot be altered once initialized
			 *   Immutable, strict - attempting to alter name will throw an error, even in non-strict mode
			 *
			 */
			enum class Type
			{
				/* ES5+ Data properties, identified with ES4 DynamicVar local bindings */
				Data,
				/* ES5+ Accessor properties */
				Accessor,
#if ENABLE_ES4
				/* ES4 Variable local bindings */
				Variable,
				/* ES4 Getter local bindings */
				Getter,
				/* ES4 Setter local bindings */
				Setter,
				// /* ES4 alias of another property */
				//Alias,
				/* ES4 not a real element, marks that creating a declaration with this name is not allowed */
				Forbidden, // TODO
				Dynamic = Data,
#endif
			};

			Type PropertyType = Type::Data;

#if ENABLE_ES4
			static inline constexpr AccessSet GetAccessSet(Type type)
			{
				switch(type)
				{
				case Type::Data:
				case Type::Accessor:
				case Type::Variable:
				case Type::Forbidden: // Forbidden can be either
				//case Type::Alias: // Alias cannot be determined
					return AccessSet::ReadWrite;
				case Type::Getter:
					return AccessSet::Read;
				case Type::Setter:
					return AccessSet::Write;
				default:
					assert(false);
				}
			}
#endif

			bool Enumerable = false;
			/* ES1-3 !DontDelete, ES5 Configurable, ES4 sealed for DynamicVar, otherwise false */
			bool Configurable = false;

			// Variable only fields
#if ENABLE_ES4
			/* ES4 explicit */
			bool Explicit = false;
#endif

			// Data
		private:
			/* A property/binding can be either intialized or uninitialized
			 * Uninitialized properties are only accepted for ES6+ lexical bindings
			 * Or ES4 Variables
			 * Initialized properties can also be internal
			 */
			struct Uninitialized { };
			typedef JSValue Initialized;
			typedef class _Internal
			{
			public:
				virtual std::optional<JSValue> Get() = 0;
				virtual bool Set(JSValue value) = 0;
			} * Internal;

			template <typename T>
				class InternalValue : public _Internal
			{
			public:
				T * pointer;
				InternalValue(T * pointer) : pointer(pointer) { }
				std::optional<JSValue> Get() override { return *pointer; }
				bool Set(JSValue value) override { *pointer = value; return true; }
			};

			template <typename T>
				class InternalValue<const T> : public _Internal
			{
			public:
				const T * pointer;
				InternalValue(const T * pointer) : pointer(pointer) { }
				std::optional<JSValue> Get() override { return *pointer; }
				bool Set([[maybe_unused]] JSValue value) override { return false; }
			};

			template <typename T>
				class InternalValue<std::optional<T>> : public _Internal
			{
			public:
				std::optional<T> * pointer;
				InternalValue(std::optional<T> * pointer) : pointer(pointer) { }
				std::optional<JSValue> Get() override { return pointer->value_or(JS_undefined); }
				bool Set([[maybe_unused]] JSValue value) override { return false; } // TODO
			};

			template <typename T>
				class InternalValue<const std::optional<T>> : public _Internal
			{
			public:
				const std::optional<T> * pointer;
				InternalValue(const std::optional<T> * pointer) : pointer(pointer) { }
				std::optional<JSValue> Get() override { return pointer->has_value() ? pointer->value() : JS_undefined; }
				bool Set([[maybe_unused]] JSValue value) override { return false; }
			};

			std::variant<Uninitialized, Initialized, Internal> Value = Uninitialized();
		public:
#if ENABLE_ES4
			/* ES1-3: DontRead, ES4: immutable for Variable, ES5+: Writable; also bindings in declarative environment records can be Strict_Immutable */
			// ES4: Variable only, ES6: Dynamic only
			enum class Mutability
			{
				Mutable,
				Immutable,
				Strict_Immutable,
			};
			Mutability Mutable = Mutability::Mutable;
#else
			bool Writable;
#endif

			// Accessor only
			JSFunction Get;
			JSFunction Set;

#if ENABLE_ES4
			// Variable only
			std::optional<JSDeclaredType> Type;
			Initialization * Initializer = nullptr;

			// used to handle directly modifying Variable properties, only filled in for properties returned by GetOwnProperty
			Property * direct_access = nullptr;

			// Alias only
			//std::shared_ptr<Property> alias_reference = nullptr;

			inline AccessSet GetAccessSet() const
			{
				/*if(PropertyType == Type::Alias)
				{
					return alias_reference->GetAccessSet();
				}
				else*/
				{
					return GetAccessSet(PropertyType);
				}
			}

		public:

			void Initialize(ExecutionPhase phase);
#endif

#if ENABLE_ES4
			static Property MakeUninitializedDynamic(bool Writable, bool Enumerable, bool Configurable); // not yet initialized (only used in environment records)
#endif
			static Property MakeData(JSValue Value, bool Writable, bool Enumerable, bool Configurable AND_FLAG(Explicit = false));
			static Property MakeData(JSValue Value, bool Writable, bool Configurable);
#if ENABLE_ES4
			static Property MakeDynamic(JSValue Value, bool Writable, bool Enumerable, bool Configurable AND_FLAG(Explicit = false))
			{
				return MakeData(Value, Writable, Enumerable, Configurable WITH_FLAG(Explicit));
			}
			static Property MakeDynamic(JSValue Value, bool Writable, bool Configurable)
			{
				return MakeData(Value, Writable, Configurable);
			}
#endif
		private:
			static Property MakeInternal(Internal Value, bool Writable, bool Enumerable);
		public:
			static Property MakeImmutableInternal(Internal Value, bool Enumerable);
			//static Property MakeInternal(JSValue& Value, bool Enumerable);
			template <typename T>
				static Property MakeImmutableInternal(const T& Value, bool Enumerable)
			{
				return MakeInternal(new InternalValue<const T>(&Value), false, Enumerable);
			}
			template <typename T>
				static Property MakeImmutableInternal(const std::optional<T>& Value, bool Enumerable)
			{
				return MakeInternal(new InternalValue<const std::optional<T>>(&Value), false, Enumerable);
			}
#if ENABLE_ES4
			static Property MakeVariable(std::optional<JSValue> value, bool Writable, bool Enumerable, bool Explicit, std::optional<JSDeclaredType> type);
			static Property MakeVariable(Initialization * initializer, bool Writable, bool Enumerable, bool Explicit, std::optional<JSDeclaredType> type);
			static Property MakeGetter(JSFunction Get, bool Enumerable, bool Explicit);
			static Property MakeSetter(JSFunction Set, bool Enumerable, bool Explicit);
			//static Property MakeAlias(std::shared_ptr<JSProperty> reference, bool Writable, bool Enumerable, bool Configurable, bool Explicit);
			static Property MakeForbidden();
#endif
			static Property MakeAccessor(JSFunction Get, JSFunction Set, bool Enumerable, bool Configurable);

#if ENABLE_ES4
			static inline Property MakeMutable(std::optional<JSValue> value, bool Deletable)
			{
				if(value.has_value())
				{
					return MakeDynamic(value.value_or(JS_undefined), /* Writable = */ true, /* Enumerable = */ true, /* Configurable = */ Deletable);
				}
				else
				{
					return MakeUninitializedDynamic(/* Writable = */ true, /* Enumerable = */ true, /* Configurable = */ Deletable);
				}
			}
			static inline Property MakeImmutable(std::optional<JSValue> value, bool Strict)
			{
				if(value.has_value())
				{
					auto var = MakeDynamic(value.value(), /* Writable = */ false, /* Enumerable = */ true, /* Configurable = */ false);
					if(Strict)
					{
						var.Mutable = Mutability::Strict_Immutable;
					}
					return var;
				}
				else
				{
					auto var = MakeUninitializedDynamic(/* Writable = */ false, /* Enumerable = */ true, /* Configurable = */ false);
					if(!Strict)
					{
						var.Mutable = Mutability::Immutable;
					}
					return var;
				}
			}
#endif
			void ConvertToData();
			void ConvertToAccessor();

			bool IsWritable() const;
			void SetWritable(bool Writable);
			bool IsInitialized() const;
			// can only be called on Data (Dynamic) and Variable properties
			JSValue GetValue() const;
			// can only be called on Data (Dynamic) and Variable properties
			bool SetValue(JSValue value);

#if 0
			Property ResolveProperty();
			Property& ResolvePropertyAccess();
#endif
		};

#if !ENABLE_ES4
		class Binding : public gc_cleanup
		{
		public:
			enum class BindingType
			{
				Strict, // and immutable
				Immutable,
				Mutable,
				Deletable, // and mutable
			};

			BindingType type = BindingType::Mutable;
			std::optional<JSValue> value;

			static Binding MakeMutable(std::optional<JSValue> value, bool Deletable);
			static Binding MakeImmutable(std::optional<JSValue> value, bool Strict);

			bool IsInitialized()
			{
				return value.has_value();
			}

			void SetValue(JSValue new_value)
			{
				value = std::make_optional<>(new_value);
			}

			JSValue GetValue()
			{
				return value.value();
			}
		};
#endif

		class PropertyDescriptor : public gc
		{
		public:
			std::optional<JSValue> Value;
			std::optional<bool> Writable;
			std::optional<JSFunction> Get;
			std::optional<JSFunction> Set;
			std::optional<bool> Enumerable;
			std::optional<bool> Configurable;

			PropertyDescriptor() = default;
			explicit PropertyDescriptor(const Property& property);

			PropertyDescriptor withoutValue();
			PropertyDescriptor withValue(JSValue value);
			PropertyDescriptor withWritable(bool writable);
			PropertyDescriptor withEnumerable(bool enumerable);
			PropertyDescriptor withoutEnumerable();
			PropertyDescriptor withConfigurable(bool configurable);
			PropertyDescriptor withoutConfigurable();
			PropertyDescriptor withGetter(JSFunction get);
			PropertyDescriptor withSetter(JSFunction set);

			bool is_accessor_descriptor();
			bool is_data_descriptor();

			// CompletePropertyDescriptor
			operator Property();

			bool is_compatible(Property property);
		};

		class PrivateElement
		{
		public:
			enum class ElementKind
			{
				Field,
				Method,
				Accessor,
			};
			ElementKind Kind;
			std::optional<JSValue> Value;
			std::optional<JSFunction> Get;
			std::optional<JSFunction> Set;
		};

		class ClassFieldDefinition
		{
		public:
			JSBindingName Name;
			JSFunction Initialization;
		};

#if ENABLE_ES4
		class InstanceProperty : public gc_cleanup
		{
		public:
			JSString name;
			std::set<JSNamespace> namespaces;
			bool Final;
			bool Enumerable;
			InstanceProperty(bool Final, bool Enumerable) : Final(Final), Enumerable(Enumerable) { }

			virtual JSValue GetValue(JSValue this_argument, ExecutionPhase phase = ExecutionPhase::Run) = 0;
			virtual void SetValue(JSValue this_argument, JSValue value) = 0;
			virtual AccessSet GetAccessSet() = 0;
		};

		class InstanceVariable : public InstanceProperty
		{
		public:
			std::optional<JSDeclaredType> Type;
			std::optional<JSValue> DefaultValue;
			bool Immutable;
			InstanceVariable(bool Final, bool Enumerable, bool Immutable, std::optional<JSDeclaredType> Type, std::optional<JSValue> DefaultValue)
				: InstanceProperty(Final, Enumerable), Type(Type), DefaultValue(DefaultValue), Immutable(Immutable)
			{
			}

			static inline InstanceVariable * Variable(bool Final, bool Enumerable, bool Immutable, std::optional<JSDeclaredType> Type = {}, std::optional<JSValue> DefaultValue = {})
			{
				return new InstanceVariable(Final, Enumerable, Immutable, Type, DefaultValue);
			}

			JSValue GetValue(JSValue this_argument, ExecutionPhase phase = ExecutionPhase::Run) override;
			void SetValue(JSValue this_argument, JSValue value) override;
			AccessSet GetAccessSet() override;
			// only used when the slot has not been set up yet, to be called in constructors
			void InitValue(JSValue this_argument, JSValue value);
		};

		struct Parameter
		{
			//bool is_const;
			JSDeclaredType type;
			std::optional<JSValue> default_value;
		};

		struct SignatureType
		{
			std::vector<Parameter> parameters;
			bool has_rest_parameter = false;
			std::optional<JSDeclaredType> return_type;
		};

		class InstanceFunction : public InstanceProperty
		{
		public:
			enum class Type
			{
				Method,
				Getter,
				Setter,
			};

			Type InstanceType;
			SignatureType Signature;
			uint64_t Length;
			JSFunction Function;

			InstanceFunction(bool Final, bool Enumerable, Type InstanceType, SignatureType Signature, JSFunction Function)
				: InstanceProperty(Final, Enumerable), InstanceType(InstanceType), Signature(Signature), Function(Function)
			{
			}

			static inline InstanceFunction * Method(bool Final, bool Enumerable, SignatureType Signature, JSFunction Function)
			{
				return new InstanceFunction(Final, Enumerable, Type::Method, Signature, Function);
			}

			static inline InstanceFunction * Getter(bool Final, bool Enumerable, SignatureType Signature, JSFunction Function)
			{
				return new InstanceFunction(Final, Enumerable, Type::Getter, Signature, Function);
			}

			static inline InstanceFunction * Setter(bool Final, bool Enumerable, SignatureType Signature, JSFunction Function)
			{
				return new InstanceFunction(Final, Enumerable, Type::Setter, Signature, Function);
			}

			JSValue GetValue(JSValue this_argument, ExecutionPhase phase = ExecutionPhase::Run) override;
			void SetValue(JSValue this_argument, JSValue value) override;
			AccessSet GetAccessSet() override;
		};
#endif

		class ImportAttribute
		{
		public:
			JSString Key, Value;

			bool operator ==(const ImportAttribute& other) const
			{
				return Key == other.Key && Value == other.Value;
			}
		};

		class ModuleRequestRecord
		{
		public:
			JSString Specifier;
			std::map<JSString, JSString> Attributes;

			ModuleRequestRecord() { }

			ModuleRequestRecord(JSString Specifier, std::vector<ImportAttribute> Attributes = std::vector<ImportAttribute>{})
				: Specifier(Specifier)
			{
				for(auto Attribute : Attributes)
				{
					this->Attributes[Attribute.Key] = Attribute.Value;
				}
			}

			bool operator ==(const ModuleRequestRecord& other) const
			{
				return Specifier == other.Specifier && Attributes == other.Attributes;
			}

			bool operator <(const ModuleRequestRecord& other) const
			{
				return Specifier < other.Specifier
					|| (Specifier == other.Specifier && Attributes < other.Attributes);
			}
		};

		class ModuleRecord;
		class LoadedModuleRequestRecord : public ModuleRequestRecord
		{
		public:
			ModuleRecord * Module;
			LoadedModuleRequestRecord(const ModuleRequestRecord& record, ModuleRecord * Module = nullptr)
				: ModuleRequestRecord(record), Module(Module)
			{
			}
		};

		class Realm : public gc
		{
		public:
			Agent * AgentSignifier;
			Runtime::Intrinsics Intrinsics;
			JSObject GlobalObject;
			std::shared_ptr<JS::Runtime::GlobalEnvironmentRecord> GlobalEnv = nullptr;
			std::vector<void *> TemplateMap; // TODO: type
			std::set<LoadedModuleRequestRecord> LoadedModules;

			Realm(Agent * AgentSignifier, JSObject global_object = nullptr, JSObject global_this = nullptr);
			// roughly the same as ES4 createGlobalObject()
			std::shared_ptr<JS::Runtime::GlobalEnvironmentRecord> InitializeGlobalObject(JSObject global_object = nullptr, JSObject global_this = nullptr);
		};

		class CompletionRecord
		{
		public:
			enum class CompletionType
			{
				Normal,
				Break,
				Continue,
				Return,
				Throw,
			};
			CompletionType Type = CompletionType::Normal;
			std::optional<JSValue> Value;
			std::optional<JSString> Target;

			bool IsAbrupt() const
			{
				return Type != CompletionType::Normal;
			}

			bool HasValue() const
			{
				return Value.has_value();
			}

			static CompletionRecord Normal(std::optional<JSValue> value = {})
			{
				return CompletionRecord{CompletionType::Normal, value, {}};
			}

			static CompletionRecord Break()
			{
				return CompletionRecord{CompletionType::Break, {}, {}};
			}

			static CompletionRecord Break(std::optional<JSString> target)
			{
				return CompletionRecord{CompletionType::Break, {}, target};
			}

			static CompletionRecord Continue()
			{
				return CompletionRecord{CompletionType::Continue, {}, {}};
			}

			static CompletionRecord Continue(std::optional<JSString> target)
			{
				return CompletionRecord{CompletionType::Continue, {}, target};
			}

			static CompletionRecord Return()
			{
				return CompletionRecord{CompletionType::Return, {}, {}};
			}

			static CompletionRecord Return(JSValue value)
			{
				return CompletionRecord{CompletionType::Return, value, {}};
			}

			static CompletionRecord Throw(JSValue value)
			{
				return CompletionRecord{CompletionType::Throw, value, {}};
			}
		};

		class ImportEntry
		{
		public:
			ModuleRequestRecord ModuleRequest;
			// corresponds to NamespaceObject if missing
			std::optional<JSString> ImportName;
			JSString LocalName;

			ImportEntry() { }

			ImportEntry(ModuleRequestRecord ModuleRequest, JSString ImportName, JSString LocalName)
				: ModuleRequest(ModuleRequest), ImportName(ImportName), LocalName(LocalName)
			{
			}

			ImportEntry(ModuleRequestRecord ModuleRequest, JSString LocalName)
				: ModuleRequest(ModuleRequest), ImportName(), LocalName(LocalName)
			{
			}
		};

		class ExportEntry
		{
		public:
			std::optional<JSString> ExportName;
			std::optional<ModuleRequestRecord> ModuleRequest;
			enum class Type
			{
				String,
				Null,
				All,
				AllButDefault,
			};
			Type ImportNameType;
			std::optional<JSString> ImportName; // only set if ImportNameType == Type::String
			std::optional<JSString> LocalName;

			ExportEntry(std::optional<ModuleRequestRecord> ModuleRequest, std::optional<JSString> ImportName, std::optional<JSString> LocalName, std::optional<JSString> ExportName)
				: ExportName(ExportName), ModuleRequest(ModuleRequest), ImportNameType(ImportName.has_value() ? Type::String : Type::Null), ImportName(ImportName), LocalName(LocalName)
			{
			}

			ExportEntry(std::optional<ModuleRequestRecord> ModuleRequest, Type ImportNameType, std::optional<JSString> LocalName, std::optional<JSString> ExportName)
				: ExportName(ExportName), ModuleRequest(ModuleRequest), ImportNameType(ImportNameType), ImportName(), LocalName(LocalName)
			{
				// TODO: ImportNameType should not be String
			}
		};

		class Job : public gc_cleanup
		{
		public:
			// TODO
			virtual void Execute() = 0;
		};

		class JobCallback
		{
		public:
			JSFunction Callback;
			// TODO: any other fields?
		};

		class Agent
		{
		public:
			JSBoolean LittleEndian;
			JSBoolean CanBlock = true; // TODO
			JSBoolean IsLockFree1 = true; // TODO
			JSBoolean IsLockFree2 = true; // TODO
			JSBoolean IsLockFree8 = true; // TODO
			void * CandidateExecution; // TODO
			std::set<JSObject> KeptAlive;
			uint32_t ModuleAsyncEvaluationCount;

			std::vector<ExecutionContext *> ContextStack;
			std::queue<Job *> EnqueuedJobs;

			static thread_local Agent * CurrentAgent;

			Agent();

			uint32_t IncrementModuleAsyncEvaluationCount()
			{
				return ModuleAsyncEvaluationCount ++;
			}

			std::vector<JS::Parser::Location> GetCurrentLocationStack();
		};

#if ENABLE_ES4
		// information about package and the script that loads it
		struct PackageImportRecord
		{
			// if null, package has not been fully loaded (either transient or open)
			PackageRecord * ScriptOrModule;
			// associated package object
			JSPackage Package;

			PackageImportRecord(PackageRecord * ScriptOrModule = nullptr);

			PackageImportRecord(JSPackage Package)
				: ScriptOrModule(nullptr), Package(Package)
			{
			}
		};
#endif

		struct AsynchronousModuleDefinition
		{
			ScriptRecord * Script = nullptr;
			JSString Identifier;
			std::optional<std::vector<JSString>> Dependencies;
			JSValue Factory;

			static AsynchronousModuleDefinition Create(JSValue Identifier, JSValue Dependencies, JSValue Factory);
		};

		class Host
		{
		public:
			virtual JobCallback * MakeJobCallback(JSFunction callback);
			virtual JSValue CallJobCallback(JobCallback * jobCallback, JSValue value, std::vector<JSValue> arguments);
			virtual void EnqueuePromiseJob(Job * job, JS::Runtime::Realm * realm);
			enum class OperationType
			{
				Reject,
				Handle,
			};
			virtual void PromiseRejectionTracker(gcn::gc_ptr<JS::Class::Promise> promise, OperationType operation);

			// ECMAScript 4 packages
#if ENABLE_ES4
			virtual std::vector<JSString> GetES4FullPackageName(std::filesystem::path sourcefilename) = 0;
			virtual bool IsES4PackageOpen(std::filesystem::path sourcefilename, std::vector<JSString> PathName) = 0;

			virtual JSString GetES4PackageName(std::optional<std::filesystem::path> sourcefilename) = 0;
			virtual JSString GetES4PackageName(std::optional<std::filesystem::path> sourcefilename, JSString StringValue) = 0;
			virtual JSString GetES4PackageName(std::optional<std::filesystem::path> sourcefilename, std::vector<JSString> PathName) = 0;

			virtual void SetES4Package(std::filesystem::path sourcefilename, JSString StringValue) = 0;
			virtual void SetES4Package(std::filesystem::path sourcefilename, std::vector<JSString> PathName) = 0;

			// accesses (and possibly creates) a package import record
			virtual PackageImportRecord& GetES4Package(std::optional<std::filesystem::path> sourcefilename, bool open = false) = 0;
			virtual PackageImportRecord& GetES4Package(std::optional<std::filesystem::path> sourcefilename, JSString StringValue, bool open = false) = 0;
			virtual PackageImportRecord& GetES4Package(std::optional<std::filesystem::path> sourcefilename, std::vector<JSString> PathName, bool open = false) = 0;

			// accesses (and if needed, loads) a package
			virtual PackageImportRecord& LocateES4Package(JSString StringValue) = 0;
			virtual PackageImportRecord& LocateES4Package(std::vector<JSString> PathName) = 0;
#endif

			// CommonJS and AMD modules
			/** Creates an environment around the script to bind predefined functions (such as require or define) */
			void SetupWrappers(std::shared_ptr<JS::Runtime::EnvironmentRecord> using_environment = nullptr);
			/** Loads a module using the CommonJS/AMD 'require' statement */
			virtual JSValue LoadRequiredModule(JSString path) = 0;

			virtual void DeclareDefine(AsynchronousModuleDefinition definition) = 0;

			/** Called for identifiers appearing as part of a depency list in a define call */
			virtual void LoadDefinedModuleDependency(JSString identifier) = 0;
			/** Called when the script finished loading, activates the definitions on the global queue */
			virtual void FinishLoadingDefinedModule() = 0;
			/** Activates a loaded module, forcing it to execute */
			virtual void EnableDefinedModule(JSString identifier) = 0;

			// ECMAScript 6 modules
			virtual bool IsSupportedImportAttribute(JSString attribute) = 0;

			bool AllImportAttributesSupported(std::map<JSString, JSString> attributes)
			{
				for(auto attribute : attributes)
				{
					if(!IsSupportedImportAttribute(attribute.first))
					{
						return false;
					}
				}
				return true;
			}

			class Payload
			{
			public:
				virtual ~Payload() = default;
				virtual void NormalCompletion(ModuleRecord * moduleRecord) = 0;
				virtual void ThrowCompletion(JSValue error_result) = 0;
			};

			/** Loads a module file into memory using the ECMAScript 6 'import' statement or dynamic import
			 *
			 * Corresponds to the ECMAScript abstract operation HostLoadImportedModule
			 *
			 * @param referrer_loaded_modules The set of loaded module records corresponding to the script or module record, or if no such
			 *                                is available, the set of loaded module records of the current realm. Replaces the ECMAScript
			 *                                'referrer' argument.
			 * @param moduleRequest A request identifying an import, consisting of a specifier (a string) and a set of attributes (pairs of
			 *                      strings). An identical module request should return the same module object (at least for the same referrer).
			 * @param payload An object representing the action to be taken once loading completes or an error occurs. For an import statement,
			 *                a graph loading state record is provided to keep track of which modules are currently loaded. For a dynamic import,
			 *                a promise capability record shall be invoked. // TODO
			 */
			virtual void LoadImportedModule(std::set<LoadedModuleRequestRecord> * referrer_loaded_modules, ModuleRequestRecord moduleRequest, std::shared_ptr<Payload> payload) = 0;

			/** Corresponds to the ECMAScript abstract operation FinishLoadingImportedModule when the result is a normal completion */
			void FinishLoadingImportedModule_NormalCompletion(std::set<LoadedModuleRequestRecord> * referrer_loaded_modules, ModuleRequestRecord moduleRequest, std::shared_ptr<Payload> payload, ModuleRecord * module_result);

			/** Corresponds to the ECMAScript abstract operation FinishLoadingImportedModule when the result is an abrupt completion */
			void FinishLoadingImportedModule_ThrowCompletion(std::set<LoadedModuleRequestRecord> * referrer_loaded_modules, ModuleRequestRecord moduleRequest, std::shared_ptr<Payload> payload, JSValue error_result);

			JSValue LoadJSONFile(std::filesystem::path host_path);
			JSString LoadTextFile(std::filesystem::path host_path);
		};

		class ContextCapture
		{
		public:
			bool active = true;
			bool previous_strict_mode;
			Version previous_version;

			ContextCapture()
				: previous_strict_mode(JS::StrictMode), previous_version(JS::CurrentVersion)
			{
			}

			ContextCapture(const ContextCapture&) = delete;

			~ContextCapture()
			{
				release();
			}

			void release()
			{
				if(active)
				{
					active = false;
					StrictMode = previous_strict_mode;
					CurrentVersion = previous_version;
				}
			}
		};

		// TODO: is this still needed?
		class CallContextCapture
		{
		public:
			JS::Runtime::ExecutionContext * callerContext;
			bool previous_strict_mode;
			Version previous_version;
			std::shared_ptr<JS::Runtime::EnvironmentRecord>        LexicalEnvironment;
			std::shared_ptr<JS::Runtime::EnvironmentRecord>        VariableEnvironment;
			std::shared_ptr<JS::Runtime::PrivateEnvironmentRecord> PrivateEnvironment;

			CallContextCapture();

			~CallContextCapture()
			{
				release();
			}

			void release();
		};

		/** Handles the internals of a tablelike structure, typically for the properties of an object, bindings of an environment record or for ECMAScript 4 the instance properties of a class */
		template <typename T>
			class Bindings
		{
		public:
#if ENABLE_ES4
			typedef shared_map<std::tuple<JSNamespace, Access>, std::shared_ptr<T>> binding_value_type;
#else
			typedef std::shared_ptr<T> binding_value_type;
#endif

			/** A map from property keys to bindings 
			 *
			 * Before ES6, all bindings are accessed by their names of type string. ES6 introduces symbols as possible property names (though not binding names).
			 * In ES4, property and binding access is done via multinames and access type (read, write, read or write).
			 * Since all multinames have the same name part, we represent this as accessing bindings through their name
			 * and set of potential namespaces and access types.
			 * The same binding might have different namespaces and access types, so we use the shared_map structure to express that they belong
			 * to the same binding.
			 */
			std::map<JSPropertyKey, binding_value_type> bindings;

#if ENABLE_ES4
			struct const_iterator
			{
				const Bindings * self;
				typename std::map<JSPropertyKey, shared_map<std::tuple<JSNamespace, Access>, std::shared_ptr<T>>>::const_iterator key_pair;
				std::optional<typename shared_map<std::tuple<JSNamespace, Access>, std::shared_ptr<T>>::const_iterator> access_pair;

				const_iterator(const Bindings * self, bool end)
					:
						self(self),
						key_pair(
							end
								? self->bindings.end()
								: self->bindings.begin()),
						access_pair(
							end || self->bindings.size() == 0
								? decltype(access_pair)()
								: self->bindings.begin()->second.begin())
				{
				}

				bool operator !=(const const_iterator& other) const
				{
					return access_pair != other.access_pair;
				}

				const_iterator operator ++()
				{
					const_iterator current = *this;
					if(access_pair.has_value())
					{
						++access_pair.value();
						if(access_pair.value() == key_pair->second.end())
						{
							++key_pair;
							if(key_pair != self->bindings.end())
							{
								access_pair = key_pair->second.begin();
							}
							else
							{
								access_pair = decltype(access_pair)(); // we have to make sure that the final iterator has a single value
							}
						}
					}
					return current;
				}

				std::shared_ptr<T> operator *()
				{
					return access_pair.value()->value;
				}

				JSPropertyKey get_name()
				{
					return key_pair->first;
				}

				std::set<std::tuple<JSNamespace, Access>> get_access()
				{
					return access_pair.value()->keys;
				}
			};
#else
			struct const_iterator
			{
				const Bindings * self;
				typename std::map<JSPropertyKey, std::shared_ptr<T>>::const_iterator pair;

				const_iterator(const Bindings * self, bool end)
					:
						self(self),
						pair(
							end
								? self->bindings.end()
								: self->bindings.begin())
				{
				}

				bool operator !=(const const_iterator& other) const
				{
					return pair != other.pair;
				}

				const_iterator operator ++()
				{
					const_iterator current = *this;
					++pair;
					return current;
				}

				std::shared_ptr<T> operator *()
				{
					return pair->second;
				}

				JSPropertyKey get_name()
				{
					return pair->first;
				}
			};
#endif

			std::optional<std::shared_ptr<T>> Get(JSPropertyKey key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses));
			bool Set(JSPropertyKey key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), T value);
			bool Delete(JSPropertyKey key AND_NAMESPACES(namespaces));

			const_iterator begin() const
			{
				return const_iterator(this, false);
			}

			const_iterator end() const
			{
				return const_iterator(this, true);
			}
		};
	}

#if ENABLE_ES4
	std::optional<JS::Runtime::SignatureType> GetSignature(JSFunction callable);
#endif

	template <class C, typename ... Args>
		gcn::gc_ptr<C> New(Args ... args);
}

template <typename T>
	std::optional<std::shared_ptr<T>> JS::Runtime::Bindings<T>::Get(JSPropertyKey key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	using namespace JS::Public::ErrorClasses;

#if ENABLE_ES4
	auto bindings_iter = bindings.find(key);
	if(bindings_iter != bindings.end())
	{
		std::optional<std::shared_ptr<T>> found_value;
		if(IsVersionES4())
		{
			for(JSNamespace namespace_ : namespaces)
			{
				for(Access access : Accesses(accesses))
				{
					auto pair = std::make_tuple(namespace_, access);
					if(!bindings_iter->second.has(pair))
					{
						continue;
					}
					auto new_value = bindings_iter->second.get(pair);
					if(found_value.has_value())
					{
						if(found_value.value() != new_value)
						{
							JS::Throw(JS::New<ReferenceError> __LOC(u"ambiguous access"));
						}
						else
						{
							continue;
						}
					}
					found_value = new_value;
				}
			}
		}
		else if(bindings_iter->second.has(std::make_tuple(JS_public, Access::Read)))
		{
			found_value = bindings_iter->second.get(std::make_tuple(JS_public, Access::Read));
		}

		if(found_value.has_value())
		{
			return found_value.value();
		}
	}
#else
	auto binding = bindings.find(key);
	if(binding != bindings.end())
	{
		return binding->second;
	}
#endif
	return {};
}

template <typename T>
	bool JS::Runtime::Bindings<T>::Set(JSPropertyKey key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), T value)
{
	if(auto reference_option = Get(key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		*reference_option.value() = value;
		return true;
	}

#if ENABLE_ES4
	if(bindings.find(key) == bindings.end())
	{
		bindings[key] = shared_map<std::tuple<JSNamespace, Access>, std::shared_ptr<T>>();
	}

	std::optional<std::tuple<JSNamespace, Access>> first_pair;
	if(IsVersionES4())
	{
		for(JSNamespace namespace_ : namespaces)
		{
			for(Access access : Accesses(accesses))
			{
				auto pair = std::make_tuple(namespace_, access);
				if(first_pair.has_value())
				{
					bindings[key].join(pair, first_pair.value());
				}
				else
				{
					bindings[key].set(pair, std::make_shared<T>(value));
					first_pair = pair;
				}
			}
		}
	}
	else
	{
		bindings[key].set(std::make_tuple(JS_public, Access::Read), std::make_shared<T>(value));
		bindings[key].join(std::make_tuple(JS_public, Access::Write), std::make_tuple(JS_public, Access::Read));
	}
#else
	bindings[key] = std::make_shared<T>(value);
#endif
	return true;
}

template <typename T>
	bool JS::Runtime::Bindings<T>::Delete(JSPropertyKey key AND_NAMESPACES(namespaces))
{
#if ENABLE_ES4
	auto bindings_iter = bindings.find(key);
	if(bindings_iter == bindings.end())
	{
		return false;
	}

	if(IsVersionES4())
	{
		// find a namespace, access pair that exists, delete all entries using that
		for(JSNamespace namespace_ : namespaces)
		{
			auto pair = std::make_tuple(namespace_, Access::Write);
			if(!bindings_iter->second.has(pair))
				continue;

			auto access = *bindings_iter->second.keys(pair).begin();
			bindings_iter->second.clear(access);
			if(bindings_iter->second.size() == 0)
			{
				bindings.erase(bindings_iter);
			}
			return true;
		}
	}
#endif
	bindings.erase(key);
	return true;
}

#include "core.hh" // needed for JS::New

#endif // _RUNTIME_HH
