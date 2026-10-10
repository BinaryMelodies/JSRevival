#ifndef _ENVIRONMENT_HH
#define _ENVIRONMENT_HH

/* Structures related to environment records and references */

#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "runtime.hh"

namespace JS
{
	namespace Runtime
	{

		/** @brief Represents the types of references that may appear on the right hand side of a membership expression
		 * Not all combinations of values are valid, and no ECMAScript version supports all of them
		 * The main types of references are as follows:
		 *  - a string reference (for example myValue.stringReference)
		 *  - (ES6+) a symbol reference (myValue[Symbol("someSymbol")])
		 *  - (E4X, ES4) namespace qualified reference (myValue.namespaceReference::stringReference)
		 *  - (E4X) attribute reference (myValue.@attributeName, myValue.@namespaceReference::attributeName)
		 *  - (E4X) wildcard references (myValue.*, myValue.namespaceReference::*, myValue.*::stringReference, myValue.*::*)
		 *  - (ES13) private reference (myValue.#privateReference)
		 */
		class IdentifierReference
		{
		public:
			/** @brief The namespace an identifier belongs to
			 * The wildcard ('*') namespace is represented by a null pointer, accessible as the constant WildcardNamespace.
			 * If it is missing, the reference contains no namespace information.
			 */
			std::optional<JSNamespace> Namespace;
			/** @brief The property key, a string or symbol (since ECMAScript 4)
			 * The wildcard ('*') property key is represented by a "*" string, accessible as the constant Wildcard.
			 */
			JSPropertyKey PropertyKey;

			/** @brief Set to true if this is a private reference (#identifier), introduced in ES13 */
			bool Private = false;
			/** @brief Set to true if this is an attribute reference (@identifier), E4X only */
			bool Attribute = false;

			static constexpr char16_t Wildcard[] = u"*";
			static constexpr JSNamespace WildcardNamespace = nullptr;

			IdentifierReference()
				: Namespace(), PropertyKey(u""), Private(false), Attribute(false)
			{
			}

			IdentifierReference(const char16_t * Name)
				: Namespace(), PropertyKey(Name), Private(false), Attribute(false)
			{
			}

			IdentifierReference(JSString Name, bool Attribute = false)
				: Namespace(), PropertyKey(Name), Private(false), Attribute(Attribute)
			{
			}

			IdentifierReference(JSNamespace Namespace, JSString Name, bool Attribute = false)
				: Namespace(Namespace), PropertyKey(Name), Private(false), Attribute(Attribute)
			{
			}

			IdentifierReference(std::optional<JSNamespace> Namespace, JSString Name, bool Attribute = false)
				: Namespace(Namespace), PropertyKey(Name), Private(false), Attribute(Attribute)
			{
			}

			IdentifierReference(bool Private, JSString Name)
				: Namespace(), PropertyKey(Name), Private(Private), Attribute(false)
			{
			}

			IdentifierReference(JSPropertyKey PropertyKey)
				: Namespace(), PropertyKey(PropertyKey), Private(false), Attribute(false)
			{
			}

			bool IsName();
			JSString GetName();
			std::set<JSNamespace> GetNamespaces(ReferenceContext cxt);
			std::set<JSNamespace> GetNamespaceSet() { if(Namespace.has_value()) return {Namespace.value()}; else return {}; }
			JSString ToString(FOR_PHASE(phase));
			JS::Class::QName * ToQName();
		};

		// used to represent the possible outcomes of SetMutableBinding
		enum class Result
		{
			Success,
			TypeError,
			ReferenceError,
		};

		/** @brief Abstract base for all environment records.
		 *
		 * In ECMAScript, the current scope is always a chain of embedded environments. Each environment comes with its associated bindings
		 * that can be used to retrieve information about variables.
		 *
		 * At the start of the chain is a global environment that encompasses global declarations and holds the global object.
		 * Each syntactic construct can introduce new environments: blocks create declarative environments (ECMAScript 4 and ECMAScript 6 or later),
		 * functions create function environments, and "with" statements create object environments.
		 *
		 * In ECMAScript 6 and later, a module introduces a module environment on top of the global environment. This is used
		 * to access all the local declarations as well as imported names.
		 *
		 * In ECMAScript 4, packages also introduce global environments, and classes introduce class environments.
		 */
		class EnvironmentRecord : public gc_cleanup, public std::enable_shared_from_this<EnvironmentRecord>
		{
		public:
			std::shared_ptr<EnvironmentRecord> OuterEnvironment;
			EnvironmentRecord(std::shared_ptr<EnvironmentRecord> OuterEnvironment = nullptr) : OuterEnvironment(OuterEnvironment) { }

			/** @brief Checks if a binding exists for the specifies identifier reference.
			 *
			 * Compile phase is used for const lookups only.
			 *
			 * @param reference A fully qualified reference, usually an identifier.
			 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
			 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write binding, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
			 * @param phase ECMAScript 4 only, whether this look up occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
			 * @return True if the reference binding could be found, false otherwise
			 */
			virtual bool HasBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) = 0;

			/** @brief Same as HasBinding but ECMAScript 4 package environments will ignore imported bindings */
			virtual bool HasActualBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase));

			/** @brief Creates a new mutable binding.
			 *
			 * Used for var declarations, corresponds to CreateMutableBinding and InitializeMutableBinding.
			 *
			 * ECMAScript 4 referens to this as defineHoistedVar() and uses it for plain var declarations in non-strict mode.
			 * The resulting local binding is a typeless dynamic variable.
			 *
			 * It is referred to as hoisted binding to describe the declaration hoisting behavior of such declarations,
			 * where the declared variable is accessible even before the textual appearance of the declaration.
			 *
			 * @param name The name of the new binding.
			 * @param value The value of the new binding. Typically set to JS_undefined and a following SetHoistedBinding or SetMutableBinding updates it.
			 */
			virtual void CreateHoistedBinding(JSString name, JSValue value) = 0;

			/** @brief Assigns a value to a binding previously created via CreateHoistedBinding.
			 *
			 * Intended to initialize a var declaration, corresponds to SetMutableBinding.
			 *
			 * In ECMAScript 4, this corresponds to lexicalWrite. It can only be used to assign to typeless dynamic variables.
			 *
			 * @param name The name of the existing binding.
			 * @param value The new value to be assigned to the binding.
			 */
			virtual void SetHoistedBinding(JSString name, JSValue value) = 0;

			/** @brief Assigns a value to a previously created binding.
			 *
			 * Assigns a new value to a variable, corresponds to SetMutableBinding
			 *
			 * In ECMAScript 4, this corresponds to lexicalWrite, and unlike SetHoistedBinding it can be used for arbitrary
			 * variables, not just ones created by CreateHoistedBinding.
			 *
			 * @param reference A fully qualified reference, usually an identifier.
			 * @param value The new value to be assigned to the binding.
			 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
			 */
			virtual Result SetMutableBinding(JSIdentifierReference reference, JSValue value AND_NAMESPACES(namespaces)) = 0;

			/** @brief Creates a new binding, possibly uninitialized.
			 *
			 * Creates a let/const declaration, corresponds to CreateMutableBinding and CreateImmutableBinding in ECMAScript 6
			 *
			 * It is unified with the ECMAScript 4 routine defineSingletonProperty. It is used for const declarations
			 * and strict mode or non-plain var declarations. The created variable is always a typed variable.
			 *
			 * It is referred to as a singleton binding to contrast it with instance bindings that are associated with
			 * class instances.
			 *
			 * @param name The name of the new binding.
			 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
			 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write binding, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
			 * @param binding A prepared binding structure containing the binding type and initial value among others.
			 */
			virtual void CreateSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSBinding binding) = 0;
#if ENABLE_ES4
			/** @brief ECMAScript 4 only, runs the initializer for an already declared const variable.
			 *
			 * @param name The name of an existing binding.
			 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
			 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write binding, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
			 */
			virtual void ExecuteSingletonBindingInitializer(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) = 0;

			/** @brief ECMAScript 4 only, assigns a type to an already declared singleton variable.
			 *
			 * @param name The name of an existing binding.
			 * @param type The type of the variable to be assigned to the binding.
			 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
			 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write binding, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
			 */
			virtual void SetSingletonBindingType(JSString name, JSType type AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) = 0;

			/** @brief ECMAScript 4 only, assigns a value to a (non-const) declared singleton variable, intended for initialization.
			 *
			 * @param name The name of an existing binding.
			 * @param value The value to be assigned to the binding.
			 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
			 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write binding, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
			 */
			virtual void SetSingletonBindingValue(JSString name, JSValue value AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) = 0;
#endif
			/** @brief Assigns a value to an uninitialized binding, might fail if already initialized.
			 *
			 * Initializes a let/const declaration, corresponds to InitializeMutableBinding and InitializeImmutableBinding
			 *
			 * This is not needed for ECMAScript 4, but for consistency sake it is named as a singleton binding.
			 * Unlike SetSingletonBindingValue, this can be used to initialize non-variable properties, but only
			 * if the variable has not yet been initialized.
			 *
			 * @param name The name of an existing binding.
			 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
			 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write binding, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
			 * @param value The value the binding should be initialized with.
			 */
			virtual void InitializeSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSValue value) = 0;

			/** @brief Returns the binding value, it must already exist.
			 *
			 * @param reference A fully qualified reference, usually an identifier.
			 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
			 * @param phase ECMAScript 4 only, whether this look up occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
			 * @return The currently bound value to the already initialized binding.
			 */
			virtual JSValue GetBindingValue(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_PHASE(phase)) = 0;

			/** @brief Deletes a binding value.
			 *
			 * @param reference A fully qualified reference, usually an identifier.
			 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
			 * @return True if the binding no longer exists, either due to a successful delete or because it was not present to begin with.
			 */
			virtual bool DeleteBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces)) = 0;

			/** @brief Checks if this environment has a "this" binding, or for ECMAScript 4 if the "this" binding of outer environments is inaccessible.
			 *
			 * @return True if the environment has a "this" binding or if the one of the outer bindings is shadowed.
			 */
			virtual bool HasThisBinding() = 0;
			/** @brief ECMAScript 6 or later, checks if the environment has a "super" binding
			 *
			 * @return True if the environment has a "super" binding.
			 */
			virtual bool HasSuperBinding() = 0;
			/** @brief Returns the object associated with the "with" statement if one is available.
			 *
			 * @return The bound object in a "with" statement, empty otherwise.
			 */
			virtual std::optional<JSObject> WithBaseObject() = 0;

			/** @brief Initializes the "this" binding of the environment if that has not been initialized yet
			 *
			 * @param value The new value for the "this" binding.
			 * @return The same value that was passed to the function.
			 */
			virtual JSValue BindThisValue(JSValue value);
			/** @brief Retrieves the "this" binding if one is available, raises an error otherwise.
			 *
			 * @return The value of the "this" binding.
			 */
			virtual JSValue GetThisBinding();
			/** @brief ECMAScript 6 or later, returns the value of the "super" binding.
			 *
			 * @return A reference to the prototype of the super class of the current class.
			 */
			virtual JSValue GetSuperBase();

#if ENABLE_ES4
			/** @brief ECMAScript 4 only, searches for a class environment surrounding this environment.
			 *
			 * @return A reference to the innermost class environment surrounding this environment.
			 */
			std::shared_ptr<JS::Runtime::ClassEnvironmentRecord> GetClassEnvironment(); // only for ES4
#endif

			/** @brief E4X, returns the default XML namespace.
			 *
			 * @return The default namespace assigned to this environment.
			 */
			virtual JSNamespace GetDefaultNamespace();
		};

		/** @brief Contains bindings not associated with an object.
		 *
		 * Declarative environments are introduced principally by { } blocks in ECMAScript 6 or later, as well as ECMAScript 4.
		 * They store let/const declarations (and for ECMAScript 4, const declarations or strict mode or non-plain var declarations).
		 *
		 * The ECMAScript 4 Netscape proposal calls this a "LocalFrame".
		 */
		class DeclarativeEnvironmentRecord : public JS::Runtime::EnvironmentRecord
		{
		public:
			/** @brief A collection of all the bindings for this declarative record */
			JS::Runtime::Bindings<JSBinding> bindings;

			bool HasBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
			void CreateHoistedBinding(JSString name, JSValue value) override;
			void SetHoistedBinding(JSString name, JSValue value) override;
			Result SetMutableBinding(JSIdentifierReference reference, JSValue value AND_NAMESPACES(namespaces)) override;
			void CreateSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSBinding binding) override;
#if ENABLE_ES4
			void ExecuteSingletonBindingInitializer(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			void SetSingletonBindingType(JSString name, JSType type AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			void SetSingletonBindingValue(JSString name, JSValue value AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
#endif
			void InitializeSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSValue value) override;
			JSValue GetBindingValue(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_PHASE(phase)) override;
			bool DeleteBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces)) override;

			DeclarativeEnvironmentRecord(std::shared_ptr<EnvironmentRecord> OuterEnvironment = nullptr) : EnvironmentRecord(OuterEnvironment) { }

			bool HasThisBinding() override;
			bool HasSuperBinding() override;
			std::optional<JSObject> WithBaseObject() override;
		};

		/** @brief Contains bindings associated with an object.
		 *
		 * This environment is chiefly used with the 'with' statement. It also appears as one component of a GlobalEnvironmentRecord.
		 * ECMAScript 4 Netscape proposal calls the first instance a "WithFrame"
		 */
		class ObjectEnvironmentRecord : public JS::Runtime::EnvironmentRecord
		{
		public:
			/** @brief The object this environment is bound to.
			 *
			 * Every property access goes through this object.
			 */
			JSObject BindingObject; // TODO: enable JSValue
			/** @brief Set to true if this environment is created by a "with" statement. */
			bool WithEnvironment;

			ObjectEnvironmentRecord(JSObject BindingObject, bool WithEnvironment = false, std::shared_ptr<EnvironmentRecord> OuterEnvironment = nullptr);

			// in compile time phase, with environment has no bindings
			bool HasBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
#if ENABLE_ES4
			bool HasActualBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
#endif
			void CreateHoistedBinding(JSString name, JSValue value) override;
			void SetHoistedBinding(JSString name, JSValue value) override;
			Result SetMutableBinding(JSIdentifierReference reference, JSValue value AND_NAMESPACES(namespaces)) override;
			void CreateSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSBinding binding) override;
#if ENABLE_ES4
			void ExecuteSingletonBindingInitializer(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			void SetSingletonBindingType(JSString name, JSType type AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			void SetSingletonBindingValue(JSString name, JSValue value AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
#endif
			void InitializeSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSValue value) override;
			JSValue GetBindingValue(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_PHASE(phase)) override;
			bool DeleteBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces)) override;

			bool HasThisBinding() override;
			bool HasSuperBinding() override;
			std::optional<JSObject> WithBaseObject() override;

			// E4X
			JSNamespace GetDefaultNamespace() override;
		};

		/** @brief A specialized DeclarativeEnvironmentRecord to hold information related to the function call that produced it.
		 *
		 * The Netscape proposal for ECMAScript 4 cals this a "ParameterFrame".
		 *
		 * Before ECMAScript 5, this was referred to as an activation object.
		 */
		class FunctionEnvironmentRecord : public DeclarativeEnvironmentRecord
		{
		public:
			/** @brief The state of the "this" binding */
			enum class BindingStatus
			{
				/** @brief The function environment does not have a binding for "this".
				 *
				 * ECMAScript 6 uses this for arrow literals that do not capture a "this" value.
				 *
				 * This value is also used for ECMAScript 4 static functions, but the behavior is changed and it does
				 * not allow accessing the "this" binding of outer environments.
				 */
				Lexical, // ES4: used for static calls
				/** @brief The function environment has an initialized binding for "this". */
				Initialized,
				/** @brief The function environment is supposed to have a binding for "this" but it has not been initialized yet. */
				Uninitialized,
#if ENABLE_ES4
				Static = Lexical,
#endif
			};

			/** @brief The value of the "this" binding. */
			JSValue ThisValue;
			/** @brief The current status of the "this" binding. */
			BindingStatus ThisBindingStatus;
			/** @brief Reference to the ECMAScript function object this environment is attached to. */
			gcn::gc_ptr<JS::Class::Function> FunctionObject;
			/** @brief Value of the newTarget parameter passed to the [[Constructor]] method, otherwise empty. */
			std::optional<JSFunction> NewTarget;

			FunctionEnvironmentRecord(
				gcn::gc_ptr<JS::Class::Function> FunctionObject,
				BindingStatus ThisBindingStatus,
				std::optional<JSFunction> NewTarget,
				std::shared_ptr<EnvironmentRecord> OuterEnvironment = nullptr)
					: DeclarativeEnvironmentRecord(OuterEnvironment), ThisBindingStatus(ThisBindingStatus), FunctionObject(FunctionObject), NewTarget(NewTarget) { }

			FunctionEnvironmentRecord(
				gcn::gc_ptr<JS::Class::Function> FunctionObject,
				std::optional<JSFunction> NewTarget);

			/** @brief E4X, the default namespace associated to this function. */
			std::optional<JSNamespace> DefaultNamespace;

			bool HasThisBinding() override;
			bool HasSuperBinding() override;

			JSValue BindThisValue(JSValue value) override;
			JSValue GetThisBinding() override;
			JSValue GetSuperBase() override;

			// E4X
			JSNamespace GetDefaultNamespace() override;
		};

		/** @brief The root environment record which holds a reference to a global to store "var" and top level "function" bindings, and a declarative environment to store "let", "const" and "class" bindings.
		 *
		 * It is also used to represent the ECMAScript 4 package frame to ensure that "this" references resolve properly, although the declarative environment part does not get used.
		 *
		 * Introduced in ECMAScript 6, previously the root environment was identical to an object environment.
		 */
		class GlobalEnvironmentRecord : public JS::Runtime::EnvironmentRecord
		{
		public:
			/** @brief The environment of the global object, used to hold "var" declarations. */
			ObjectEnvironmentRecord ObjectRecord;
			/** @brief The value of "this" within this scope, typically the same as the global object. */
			JSObject GlobalThisValue;
			/** @brief The environment that holds "let" and "const" declarations, used in ECMAScript 6 or later. */
			DeclarativeEnvironmentRecord DeclarativeRecord;

			std::set<JSString> VarNames; // TODO: did this get removed?

			/** @brief E4X, the default namespace associated to the global environment. */
			std::optional<JSNamespace> DefaultNamespace;

#if ENABLE_ES4
			GlobalEnvironmentRecord(JSObject binding_object, JSObject GlobalThisValue, std::shared_ptr<EnvironmentRecord> OuterEnvironment = nullptr)
				: EnvironmentRecord(OuterEnvironment), GlobalThisValue(GlobalThisValue), ObjectRecord(binding_object, false)
#else
			GlobalEnvironmentRecord(JSObject binding_object, JSObject GlobalThisValue)
				: EnvironmentRecord(), GlobalThisValue(GlobalThisValue), ObjectRecord(binding_object, false)
#endif
			{ }

			bool HasBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
#if ENABLE_ES4
			bool HasActualBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
#endif
			void CreateHoistedBinding(JSString name, JSValue value) override;
			void SetHoistedBinding(JSString name, JSValue value) override;
			Result SetMutableBinding(JSIdentifierReference reference, JSValue value AND_NAMESPACES(namespaces)) override;
			void CreateSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSBinding binding) override;
#if ENABLE_ES4
			void ExecuteSingletonBindingInitializer(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			void SetSingletonBindingType(JSString name, JSType type AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
			void SetSingletonBindingValue(JSString name, JSValue value AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
#endif
			void InitializeSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSValue value) override;
			JSValue GetBindingValue(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_PHASE(phase)) override;
			bool DeleteBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces)) override;

			bool HasThisBinding() override;
			bool HasSuperBinding() override;
			std::optional<JSObject> WithBaseObject() override;

			JSValue GetThisBinding() override;

			bool HasVarDeclaration(JSString name);
			bool HasLexicalDeclaration(JSString name);
			bool HasRestrictedGlobalProperty(JSString name);
			bool CanDeclareGlobalVar(JSString name);
			bool CanDeclareGlobalFunction(JSString name);
			void CreateGlobalVariableBinding(JSString name, bool deletable);
			void CreateGlobalFunctionBinding(JSString name, JSValue value, bool deletable);

			// E4X
			JSNamespace GetDefaultNamespace() override;
		};

#if ENABLE_ES4
		/** @brief A specialized GlobalEnvironmentRecord used for ECMAScript 4 to represent the environment record inside a class block */
		class ClassEnvironmentRecord : public GlobalEnvironmentRecord
		{
		public:
			ClassEnvironmentRecord(JSClass class_, std::shared_ptr<EnvironmentRecord> OuterEnvironment = nullptr);

			virtual JSClass GetClass();
			virtual JSString GetClassName();
			virtual std::optional<JSInstanceProperty> CreateInstanceBinding(JSString name, std::set<JSNamespace> namespaces, std::set<JSNamespace> open_namespaces, JS::Runtime::OverrideModifier override, bool explicit_, JSInstanceProperty instance_property);
			virtual void CreateConstructorBinding(JSFunction instance_method);
			// TODO: if overridden, copy enumerable trait

			// TODO
		};
#endif

		/** @brief A specialized DeclarativeEnvironmentRecord that also contains bindings imported from another module, introduced in ECMAScript 6 */
		class ModuleEnvironmentRecord : public DeclarativeEnvironmentRecord
		{
		public:
			ModuleEnvironmentRecord(std::shared_ptr<EnvironmentRecord> OuterEnvironment) : DeclarativeEnvironmentRecord(OuterEnvironment) { }

			/** @brief A representation of an indirect binding. */
			struct IndirectBinding
			{
				/** @brief The module record this binding comes from. */
				JS::Runtime::ModuleRecord * Module;
				/** @brief The name of the binding within the imported module. This can be different from the associated name within this environment. */
				JSString Name;
			};

			/** @brief Set of imported bindings. */
			std::map<JSString, IndirectBinding> indirectBindings;

			bool HasBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;

			JSValue GetBindingValue(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_PHASE(phase)) override;
			bool DeleteBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces)) override;

			bool HasThisBinding() override;

			JSValue GetThisBinding() override;

			virtual void CreateImportBinding(JSString bound_name, JS::Runtime::ModuleRecord * module_record, JSString binding_name);
		};

		/** @brief Unrelated to the EnvironmentRecord structure but fulfills a similar purpose to hold private bindings, introduced in ECMAScript 13 */
		class PrivateEnvironmentRecord : public std::enable_shared_from_this<PrivateEnvironmentRecord>
		{
		public:
			/** @brief The surrounding private environment, may be null if this is the outermost environment. */
			std::shared_ptr<PrivateEnvironmentRecord> OuterPrivateEnvironment;
			/** @brief An association from the string values of names to the unique private name references. */
			std::map<JSString, JSPrivateName> Names;

			PrivateEnvironmentRecord(std::shared_ptr<PrivateEnvironmentRecord> OuterPrivateEnvironment = nullptr) : OuterPrivateEnvironment(OuterPrivateEnvironment) { }

			static JSPrivateName ResolvePrivateIdentifier(std::shared_ptr<PrivateEnvironmentRecord> PrivateEnvironment, JSString string);
		};

		/** @brief Represents a reference occuring on its own in the source code */
		class LexicalReference : public JS::Runtime::IdentifierReference
		{
		public:
			/** @brief The environment record in which to look for the reference
			 * Initially it represents the starting environment for the lookup.
			 * After ResolveBinding, it will point to the exact record that contains the reference or nullptr.
			 *
			 * Except for ECMAScript 4, where resolution depends on whether it is for reading or writing, references are expected to be resolved on creation
			 */
			std::shared_ptr<EnvironmentRecord> Base; /* may be null */
			/** @brief Since ECMAScript 5, references can be accessed in a strict context */
			bool Strict;
#if ENABLE_ES4
			/** @brief The open namespaces to look in for this reference
			 * In ECMAScript 4, all references can be associated with a set of open namespaces.
			 * During lookup, these namespaces will be looked for.
			 * The public namespace is always supposed to be included.
			 */
			std::set<JSNamespace> OpenNamespaces;
#endif

			LexicalReference()
				: IdentifierReference(), Base(nullptr), Strict(false) WITH_NAMESPACES(OpenNamespaces())
			{
			}

			LexicalReference(std::shared_ptr<EnvironmentRecord> Base, bool Strict AND_NAMESPACES(OpenNamespaces))
				: IdentifierReference(), Base(Base), Strict(Strict) WITH_NAMESPACES(OpenNamespaces(OpenNamespaces))
			{
			}

			LexicalReference(std::shared_ptr<EnvironmentRecord> Base, bool Strict AND_NAMESPACES(OpenNamespaces), JSString Name, bool Attribute = false)
				: IdentifierReference(Name, Attribute), Base(Base), Strict(Strict) WITH_NAMESPACES(OpenNamespaces(OpenNamespaces))
			{
			}

			LexicalReference(std::shared_ptr<EnvironmentRecord> Base, bool Strict AND_NAMESPACES(OpenNamespaces), JSNamespace Namespace, JSString Name, bool Attribute = false)
				: IdentifierReference(Namespace, Name, Attribute), Base(Base), Strict(Strict) WITH_NAMESPACES(OpenNamespaces(OpenNamespaces))
			{
			}

			LexicalReference(std::shared_ptr<EnvironmentRecord> Base, bool Strict AND_NAMESPACES(OpenNamespaces), std::optional<JSNamespace> Namespace, JSString Name, bool Attribute = false)
				: IdentifierReference(Namespace, Name, Attribute), Base(Base), Strict(Strict) WITH_NAMESPACES(OpenNamespaces(OpenNamespaces))
			{
			}

			std::set<JSNamespace> GetNamespaces(FOR_PHASE(phase))
			{
				return IdentifierReference::GetNamespaces(JS::Runtime::ReferenceContext(Strict WITH_NAMESPACES(OpenNamespaces) WITH_PHASE(phase)));
			}

			LexicalReference ResolveBinding(FOR_PHASE(phase) AND_ACCESS_SET(accesses));
			std::optional<JSValue> GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run));
		};

		/** @brief Represents a member reference, specified by the '.' or '[]' operators */
		class MemberReference
		{
		public:
			/** @brief The base part of a reference, syntactically appearing before the '.' operator
			 *
			 * Before ECMAScript 5, the base is always converted to an Object before creating the reference.
			 * Since ECMAScript 5 added accessors, primitive values may also be used directly as a base.
			 * During membership lookup, they will be converted to an object.
			 * ECMAScript 4 makes no distinction between primitive values and reference objects within this context.
			 */
			JSValue Base;
			/* The name of the reference, syntactically appearing after the '.' operator
			 * Before ECMAScript 6, all names had to be of type string, and ECMAScript 6 introduced symbols as property names.
			 * In ESNext, referenced names only get converted to strings or symbols once the right hand side of a potential assignment is evaluated.
			 * In ES4, the referenced name may also be AttributeName, AnyName (represented by the string "*") or a QName instance.
			 */
			JSValue ReferencedName;
			/* Since ECMAScript 6, member lookups may occur within a class context.
			 * If 'super' is specified as the base of a member access, the value for "this" will be different from the starting point of the lookup,
			 * which begins at the prototype of the super class.
			 * In such cases, ThisValue will contain the "this" reference and Base will contain the starting point of lookup.
			 * Unlike the ECMAScript specification, ThisValue must always be specified */
			JSValue ThisValue;
			/* The reference occurs in a strict mode environment */
			bool Strict;
#if ENABLE_ES4
			/* The namespace or namespaces to look in for this reference, see LexicalReference for details. */
			std::set<JSNamespace> Namespaces;
			/* In ECMAScript 4, specifies the class where instance property lookup begins.
			 * If this value is different from the class of Base, only instance properties in this class or one of its ancestors is searched.
			 * In particular, no dynamic properties are searched (which correspond to all properties according to other ECMAScript versions).
			 */
			JSClass Limit;
#endif
		};

#if ENABLE_ES4
		/** @brief Represents a member reference with the '[]' operators, only used in ECMAScript 4
		 *
		 * The Netscape proposal for ECMAScript 4 permits having specialized references using the '[]' operators.
		 * These are very similar to MemberReferences, but may have multiple arguments that are stored as values.
		 */
		class BracketReference
		{
		public:
			JSValue Base;
			std::vector<JSValue> Arguments;
			JSValue ThisValue;
			bool Strict;
			JSClass Limit;
		};
#endif

		/** @brief Represents an occurence of the 'super' keyword, used differently in ECMAScript 4 and ECMAScript 6 or later
		 *
		 * These references are temporary, as the 'super' keyword always appears in a membership access or a call expression.
		 */
		class SuperReference
		{
		public:
			/** @brief The starting point for membership lookup
			 *
			 * In ECMAScript 4, this is the same as the ThisValue.
			 * In ECMAScript 6 and later, this points to the prototype object of the super class.
			 */
			JSValue Instance;
			/** @brief The identity of the 'this' object */
			JSValue ThisValue;
#if ENABLE_ES4
			/** @brief Represents the class where instance property lookup should begin
			 *
			 * This is only required for ECMAScript 4 and contains a reference to the super class.
			 */
			JSClass Limit;
#endif
		};

		/** @brief Represents a private member, denoted with a starting '#' symbol, introduced in ECMAScript 13 */
		class PrivateReference
		{
		public:
			/** @brief The base of the reference, occuring before the '.' operator */
			JSValue Base;
			/** @brief The resolved private name of the reference
			 *
			 * Note that this is not a string but a reference to the specific private name corresponding to the class.
			 */
			JSPrivateName ReferencedName;
		};
	}

	namespace Class
	{
		namespace Internal
		{
			template <>
				class Primitive<JS::Runtime::LexicalReference> : public IntermediateValue
			{
			public:
				JS::Runtime::LexicalReference reference;
				Primitive(JS::Runtime::LexicalReference reference) : reference(reference) { }

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;

				JSString Typeof() override;

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				void PutValue(JSValue value) override;
				bool DeleteReference() override;
				JSValue ResolveMethod() override;

				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JS::Runtime::MemberReference> : public IntermediateValue
			{
			public:
				JS::Runtime::MemberReference reference;
				Primitive(JS::Runtime::MemberReference reference) : reference(reference) { }

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				void PutValue(JSValue value) override;
				bool DeleteReference() override;
				JSValue ResolveMethod() override;

				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

#if ENABLE_ES4
			template <>
				class Primitive<JS::Runtime::BracketReference> : public IntermediateValue
			{
			public:
				JS::Runtime::BracketReference reference;
				Primitive(JS::Runtime::BracketReference reference) : reference(reference) { }

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				void PutValue(JSValue value) override;
				bool DeleteReference() override;
				JSValue ResolveMethod() override;

				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};
#endif

			template <>
				class Primitive<JS::Runtime::SuperReference> : public IntermediateValue
			{
			public:
				JS::Runtime::SuperReference reference;
				Primitive(JS::Runtime::SuperReference reference) : reference(reference) { }

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				void PutValue(JSValue value) override;
				bool DeleteReference() override;
				JSValue ResolveMethod() override;

				JSValue CreateReference(JSIdentifierReference identifier_reference, JS::Runtime::ReferenceContext cxt) override;
				JSValue CreateBracketReference(std::vector<JSValue> arguments, JS::Runtime::ReferenceContext cxt) override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JS::Runtime::PrivateReference> : public IntermediateValue
			{
			public:
				JS::Runtime::PrivateReference reference;
				Primitive(JS::Runtime::PrivateReference reference) : reference(reference) { }

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				void PutValue(JSValue value) override;
				bool DeleteReference() override;
				JSValue ResolveMethod() override;

				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};
		}
	}
}

#endif // _ENVIRONMENT_HH
