#ifndef _VALUE_HH
#define _VALUE_HH

#include <gc_cpp.h>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <typeinfo>
#include <vector>

#include "common.hh"
#include "macros.hh"

/* Structures required to define value types */
namespace JS
{
	using namespace Public;

	namespace Class
	{
		namespace Internal
		{
			/** @brief Represents the abstract interface to all ECMAScript values
			 *
			 * ECMAScript values can either be primitive values or references. They generally share
			 * the same methods (or they can appear in the same contexts), warranting a unification of
			 * the two types.
			 *
			 * The two main subclasses of Abstract::Value are Abstract::Object and Abstract::PrimitiveValue.
			 * Conceptually speaking, when accessing an object, it is accessed via its reference, while primitive
			 * values are accessed as their values, and their class counterparts describes their behavior.
			 */
			class Value : public gc_cleanup
			{
			public:
#if ENABLE_ES4
				/** @brief Returns a type object that is either the class or the instance type of a generic class corresponding to this object
				 *
				 * Corresponds to objectType() in the Netscape ECMAScript 4 proposal, but may return a parametrized type.
				 *
				 * @qualifier ECMAScript4
				 */
				virtual JSType GetType() = 0;

				/** @brief Returns a class that represents this object, possibly a generic class
				 *
				 * Corresponds to objectType() in the Netscape ECMAScript 4 proposal.
				 *
				 * @qualifier ECMAScript4
				 */
				virtual JSClass GetClass();

				/** @brief Searches for a class defined instance variable slot
				 *
				 * Corresponds to findSlot() in the Netscape ECMAScript 4 proposal.
				 *
				 * @param id The instance variable to look for. The id object should be looked up in the Class object.
				 * @return An optional iterator that points into the internal slots of the object. Slots can be assigned values via this iterator.
				 *
				 * @qualifier ECMAScript4
				 */
				virtual std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> FindSlot(JSInstanceVariable id) = 0;
#endif

				/** @brief Create a new reference for expressions of the form a.b
				 *
				 * The default implementation creates a MemberReference, but this may be overridden, for example in the case of an XML object.
				 */
				virtual JSValue CreateReference(JSIdentifierReference identifier_reference, JS::Runtime::ReferenceContext cxt);

				/** @brief Create a new reference for expressions of the form a[b] (or a[b,...] for ES4)
				 *
				 * The default implementation creates a MemberReference, but this may be overridden, for example in the case of an XML object.
				 * In ECMAScript 4 compatibility mode, it creates a BracketReference instead.
				 */
				virtual JSValue CreateBracketReference(std::vector<JSValue> arguments, JS::Runtime::ReferenceContext cxt);

				/** @brief Resolve the property key of a reference before attempting to access it via Get/Set/Delete
				 *
				 * Since ECMAScript 16, referenced names get converted to string/symbol at the last stage
				 */
				virtual JSValue ResolvePropertyKey(JSValue property_key);

				/** @brief Resolve reference to method
				 *
				 * Needed for E4X, corresponds to the CallMethod operation
				 *
				 * @param reference A reference that describes a member property. If needed, it will be altered to fit the actual access place of the call. Note that this only happens for XML and XMLList objects.
				 * @return The resolved method function
				 */
				virtual JSValue ResolveMethod(JS::Runtime::MemberReference& reference);
#if ENABLE_ES4
				/** @brief Resolve reference to method
				 *
				 * Needed for E4X, corresponds to the CallMethod operation
				 *
				 * @param reference A reference that describes a member property. If needed, it will be altered to fit the actual access place of the call. Note that this only happens for XML and XMLList objects.
				 * @return The resolved method function
				 *
				 * @qualifier ECMAScript4
				 */
				virtual JSValue ResolveMethod(JS::Runtime::BracketReference& reference);
#endif

				/** @brief Determines if new fields can be added to the object
				 *
				 * Introduced in ECMAScript 6 as [[IsExtensible]].
				 *
				 * @return True if extensible, false otherwise
				 */
				virtual bool IsExtensible() = 0;
				/** @brief Determines if new fields can be added to the object
				 *
				 * Introduced in ECMAScript 6 as [[PreventExtensions]]
				 *
				 * @return True on success, meaning that further properties cannot be added, false otherwise
				 */
				virtual bool PreventExtensions() = 0;

				/** @brief Accesses the prototype of the object, if there is one
				 *
				 * Introduced in ECMAScript 6 as [[GetPrototypeOf]]
				 *
				 * Corresponds to archetype() in the Netscape ECMAScript 4 proposal.
				 *
				 * @return If the object has a non-null value for the internal prototype, it is returned as a Value type.
				 *         In case the prototype is null according to ECMAScript, this function will return an empty value.
				 *         This normally only happens for Object.prototype, but other such objects can be created as well.
				 *
				 *         The return type is chosen to be std::optional<JSValue> instead of JSValue because technically
				 *         it could be possible for an ECMAScript 4 object to have null as its prototype as opposed to
				 *         not having one at all.
				 */
				virtual std::optional<JSValue> GetPrototypeOf() = 0;

				/** @brief Changes the prototype of the object
				 *
				 * Introduced in ECMAScript 6 as [[SetPrototypeOf]]
				 *
				 * @param new_prototype The object to be set as the new prototype. This value must be null (if the prototype
				 *                      slot should be cleared) or an object.
				 * @return True if the new prototype was successfully set, false otherwise.
				 */
				virtual bool SetPrototypeOf(JSValue new_prototype) = 0;

				/** @brief Returns a primitive value
				 *
				 * Introduced in ECMAScript 1 as [[DefaultValue]]
				 *
				 * Corresponds to objectToPrimitive() in the Netscape ECMAScript 4 proposal.
				 *
				 * Since ECMAScript 6, the behavior is fixed and accessed through the [Symbol.toPrimitive]] property.
				 *
				 * @param hint Whether to prefer toString, valueOf or a class specific implementation.
				 * @param phase ECMAScript 4 only, whether this evaluation occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
				 * @return The resulting primitive type.
				 */
				virtual JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) = 0;

				/** @brief Returns a primitive value
				 *
				 * Introduced in ECMAScript 1 as [[DefaultValue]]
				 *
				 * Note that since ECMAScript 6, this behavior is accessed through the property [Symbol.toPrimitive].
				 *
				 * @param hint Whether to prefer toString, valueOf or a class specific implementation.
				 * @param phase ECMAScript 4 only, whether this evaluation occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
				 * @return The resulting primitive type.
				 */
				// ES1 [[DefaultValue]]
				virtual JSValue DefaultValue(JS::Runtime::TypeHint hint AND_PHASE(phase));

				/** @brief Returns a copy of the property record for the associated property key
				 *
				 * Introduced in ECMAScript 5 as [[GetOwnProperty]]
				 *
				 * Corresponds to findLocalSingletonProperty() in the Netscape ECMAScript 4 proposal.
				 *
				 * @param property_key A string or (in ECMAScript 6 or later) symbol to identify the property. E4X also permits QName or AttributeName.
				 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
				 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write properties, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
				 * @return A copy of the property record if it exists or empty.
				 */
				virtual std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) = 0;

				/** @brief Returns a copy of the property record in the prototype chain
				 *
				 * Introduced in ECMAScript 5 as [[GetProperty]], removed in ECMAScript 6
				 *
				 * Corresponds to findArchetypeProperty() in the Netscape ECMAScript 4 proposal. (Note that this function is not used for ES4)
				 *
				 * This method is required to recursively look through the prototype chain for [[Get]] and [[Set]] (called [[Put]] in ECMAScript 5),
				 * it is not used outside ECMAScript 5.
				 *
				 * @param property_key A string or (in ECMAScript 6 or later) symbol to identify the property. E4X also permits QName or AttributeName.
				 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
				 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write properties, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
				 * @return A copy of the property record if it exists or empty.
				 */
				virtual std::optional<JSProperty> GetProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) = 0;

				/** @brief Checks if the object has a property of a given name without checking its prototype
				 *
				 * Introduced in ECMAScript 3 as the Object.prototype.hasOwnProperty built-in function.
				 *
				 * Corresponds to hasProperty() in the Netscape ECMAScript 4 proposal when called with flat = true.
				 *
				 * @param property_key A string or (in ECMAScript 6 or later) symbol to identify the property. E4X also permits QName or AttributeName.
				 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
				 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write properties, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
				 * @return True if the property could be found, false otherwise
				 */
				virtual bool HasOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses));

				/** @brief Checks if the object or one of its ancestors in the prototype chain has a property of a given name
				 *
				 * Introduced in ECMAScript 1 as [[HasProperty]]
				 *
				 * Corresponds to hasProperty() in the Netscape ECMAScript 4 proposal.
				 *
				 * @param property_key A string or (in ECMAScript 6 or later) symbol to identify the property. E4X also permits QName or AttributeName.
				 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
				 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write properties, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
				 * @param phase ECMAScript 4 only, whether this evaluation occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
				 * @return True if the property could be found, false otherwise
				 */
				virtual bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) = 0;

				/** @brief Reads the value of a property
				 *
				 * Introduced in ECMAScript 1 as [[Get]]
				 *
				 * Corresponds to the read() class method in the Netscape ECMAScript 4 proposal.
				 *
				 * @param property_key A string or (in ECMAScript 6 or later) symbol to identify the property. E4X also permits QName or AttributeName.
				 * @param receiver The actual "this" object at the start of the prototype chain on which the lookup should occur, introduced in ECMAScript 6 and for primitive types, ECMAScript 5, default is the same as the called object.
				 * @param intent E4X only, whether it is a property read lookup or a method call lookup, default is Intent::Read.
				 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
				 * @param limit ECMAScript 4 only, specifies which class to start looking for instance properties in a class chain, nullptr to only check for dynamic properties. Default is GetClass() of this object. Note that if it is different from the result of the GetClass() call, no dynamic properties are checked.
				 * @param environment ECMAScript 4 for accessing instance properties within a class lexical context, only used by Class objects. Default is nullptr, use macro NO_ENVIRONMENT.
				 * @param phase ECMAScript 4 only, whether this evaluation occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
				 * @return The value of the property or undefined.
				 */
				virtual JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) = 0;

				/** @brief Converts a value to a valid property key
				 *
				 * Corresponds to ES1-ES5 ToString, ES6+ ToPropertyKey, E4X can override it
				 *
				 * @param property_key The value used as a property key
				 * @param phase ECMAScript 4 only, whether this evaluation occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
				 * @return A converted property key
				 */
				virtual JSValue ConvertToPropertyKey(JSValue property_key AND_PHASE(phase));

#if ENABLE_ES4
				/** @brief Reads the value of a property via a bracket access
				 *
				 * Corresponds to the bracketRead() class method in the Netscape ECMAScript 4 proposal.
				 *
				 * @param arguments The arguments passed to the object
				 * @param receiver The actual "this" object at the start of the prototype chain on which the lookup should occur, introduced in ECMAScript 6 and for primitive types, ECMAScript 5, default is the same as the called object.
				 * @param intent E4X only, whether it is a property read lookup or a method call lookup, default is Intent::Read.
				 * @param limit ECMAScript 4 only, specifies which class to start looking for instance properties in a class chain, nullptr to only check for dynamic properties. Default is GetClass() of this object. Note that if it is different from the result of the GetClass() call, no dynamic properties are checked.
				 * @param phase ECMAScript 4 only, whether this evaluation occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
				 * @return The value of the property or undefined.
				 */
				virtual JSValue GetBracket(std::vector<JSValue> arguments, JSValue receiver, Intent intent AND_CLASS(limit) AND_PHASE(phase));
#endif

				/** @brief Writes the value of a property
				 *
				 * Introduced in ECMAScript 1 as [[Put]], renamed in ECMAScript 6 as [[Get]]
				 *
				 * Corresponds to the write() class method in the Netscape ECMAScript 4 proposal.
				 *
				 * @param property_key A string or (in ECMAScript 6 or later) symbol to identify the property. E4X also permits QName or AttributeName.
				 * @param value The new value to assign.
				 * @param receiver The actual "this" object at the start of the prototype chain on which the lookup should occur, introduced in ECMAScript 6 and for primitive types, ECMAScript 5, default is the same as the called object.
				 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
				 * @param limit ECMAScript 4 only, specifies which class to start looking for instance properties in a class chain. Default is GetClass() of this object. Note that if it is different from the result of the GetClass() call, no dynamic properties are checked.
				 * @param environment ECMAScript 4 for accessing instance properties within a class lexical context, only used by Class objects. Default is nullptr, use macro NO_ENVIRONMENT.
				 * @param check_can_put If set, do not perform assignment, only check if it is possible, required by ECMAScript 1-3 and 5.
				 * @return True if the assignment was successful, false otherwise.
				 */
				virtual bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) = 0;

#if ENABLE_ES4
				/** @brief Writes the value of a property via a bracket access
				 *
				 * Corresponds to the bracketWrite() class method in the Netscape ECMAScript 4 proposal.
				 *
				 * @param arguments The arguments passed to the object
				 * @param value The new value to assign.
				 * @param receiver The actual "this" object at the start of the prototype chain on which the lookup should occur, introduced in ECMAScript 6 and for primitive types, ECMAScript 5, default is the same as the called object.
				 * @param limit ECMAScript 4 only, specifies which class to start looking for instance properties in a class chain. Default is GetClass() of this object. Note that if it is different from the result of the GetClass() call, no dynamic properties are checked.
				 * @return The value of the property or undefined.
				 *
				 * @qualifier ECMAScript4
				 */
				virtual bool SetBracket(std::vector<JSValue> arguments, JSValue value, JSValue receiver AND_CLASS(limit));
#endif

				/** @brief Reads a private property
				 *
				 * Introduced in ECMAScript 13
				 *
				 * @param name The private name, specific to the class, should be looked up in the PrivateEnvironmentRecord::Names table.
				 * @return The value of the property or undefined.
				 */
				virtual JSValue GetPrivate(JSPrivateName name) = 0;

				/** @brief Writes a private property
				 *
				 * Introduced in ECMAScript 13
				 *
				 * @param name The private name, specific to the class, should be looked up in the PrivateEnvironmentRecord::Names table.
				 * @param value The new value to assign.
				 */
				virtual void SetPrivate(JSPrivateName name, JSValue value) = 0;

				/** @brief Deletes a property of the object
				 *
				 * Introduced in ECMAScript 1 as [[Delete]]
				 *
				 * Corresponds to the delete() class method in the Netscape ECMAScript 4 proposal.
				 *
				 * @param property_key A string or (in ECMAScript 6 or later) symbol to identify the property. E4X also permits QName or AttributeName.
				 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
				 * @param limit ECMAScript 4 only, specifies which class to start looking for instance properties in a class chain. Default is GetClass() of this object. Note that if it is different from the result of the GetClass() call, no dynamic properties are checked.
				 * @param lexical ECMAScript 4 for accessing instance properties within a class lexical context, only used by Class objects, corresponds to the environment parameter for Get and Set. Default is false.
				 * @return True if the deletion was successful, false otherwise.
				 */
				virtual bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) = 0;

#if ENABLE_ES4
				/** @brief Deletes a property of the object via a bracket access
				 *
				 * Corresponds to the bracketDelete() class method in the Netscape ECMAScript 4 proposal.
				 *
				 * @param arguments The arguments passed to the object
				 * @param limit ECMAScript 4 only, specifies which class to start looking for instance properties in a class chain. Default is GetClass() of this object. Note that if it is different from the result of the GetClass() call, no dynamic properties are checked.
				 * @return The value of the property or undefined.
				 *
				 * @qualifier ECMAScript4
				 */
				virtual bool DeleteBracket(std::vector<JSValue> arguments AND_CLASS(limit));
#endif

				/** @brief Attempts to update or create a property entry in the properties of the object
				 *
				 * Introduced in ECMAScript 5 as [[DefineOwnProperty]]
				 *
				 * @param property_key A string or (in ECMAScript 6 or later) symbol to identify the property. E4X also permits QName or AttributeName.
				 * @param descriptor A property descriptor with a (possibly partial) set of attributes to assign to the property record.
				 * @param namespaces ECMAScript 4 only, which namespaces to look for the name, default is {JS_public}, use macro PUBLIC_NAMESPACES.
				 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write properties, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
				 * @return True if after execution there is a property entry with the specified property attributes, false otherwise.
				 */
				virtual bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) = 0;

				/** @brief Attempts to update or create a property entry in the properties of the object, required for ECMAScript 4 to create variable, getter or setter properties
				 *
				 * ECMAScript 4 and ECMAScript 5 have different concepts of property access.
				 * In ECMAScript 4, properties are themselves objects with reference identities that can be modified, updated and even potentially shared.
				 * In ECMAScript 5 however, properties are handled as a declarative database, where redefining an entry with identical values does
				 * no visible alteration, even when the property is declared read-only (overwriting property values is handled on a higher level).
				 *
				 * To assure this object model can handle both cases, some of these methods come in two varieties:
				 * - DefineOwnProperty updates the property entry, Define handles both ES5-style properties as well as variable modifications
				 *
				 * @param property_key A string or (in ECMAScript 6 or later) symbol to identify the property. E4X also permits QName or AttributeName.
				 * @param property A property record to introduce. For data (in ECMAScript 4, dynamic) and (ES5-style) accessor properties, DefineOwnProperty is used and the entry may already exist. For variable, getter and setter properties, a new entry will be created.
				 * @param namespaces ECMAScript 4 only, which namespaces to look for, default is {JS_public}, use macro PUBLIC_NAMESPACES.
				 * @param accesses ECMAScript 4 only, whether to look for readable, writable or read-write properties, default is AccessSet::ReadWrite, use macro READ_WRITE_ACCESS.
				 * @return True if after execution there is a property entry with the specified property attributes, and for variable, getter and setter properties, if such an entry did not exist before. False otherwise.
				 */
				virtual bool Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) = 0;

				/** @brief Returns a list of the property keys of this instance
				 *
				 * Introduced in ECMAScript 6 as [[OwnPropertyKeys]]
				 *
				 * The order of property keys is specified exactly since ECMAScript 6, first the index properties come in numerical order, then the string properties in creation order, and then the symbol properties in the order they were introduced into the object.
				 *
				 * @param limit ECMAScript 4 only, specifies where to start with the public keys corresponding to class instances. If nullptr, only dynamic variables are returned.
				 * @return A list of property keys that can be used to access properties.
				 */
				virtual std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) = 0;

#if ENABLE_ES4
				struct PropertyKeyData
				{
					JSPropertyKey PropertyKey;
					std::set<JSNamespace> Namespaces;
					AccessSet Accesses;

					bool operator ==(const PropertyKeyData& other) const
					{
						return PropertyKey == other.PropertyKey
							&& Namespaces == other.Namespaces
							&& Accesses == other.Accesses;
					}

					bool operator <(const PropertyKeyData& other) const
					{
						return PropertyKey < other.PropertyKey
							|| (PropertyKey == other.PropertyKey
								&& (Namespaces < other.Namespaces
									|| (Namespaces == other.Namespaces
										&& Accesses < other.Accesses)));
					}
				};
#else
				typedef JSPropertyKey PropertyKeyData;
#endif

				/** @brief Helper method for debugging
				 *
				 * Returns a collection of all distinct properties, identified by their property keys and, for ECMAScript 4, their associated
				 * namespaces and accesses.
				 *
				 * For ordinary object, this is basically an identical copy of its internal bindings. However, unlike OwnPropertyKeys,
				 * it includes any possible hidden properties (this is mostly relevant for ECMAScript 4 where OwnPropertyKeys only shows
				 * public members).
				 */
				virtual std::map<PropertyKeyData, JSProperty> ExtractAllProperties() = 0; // used to return any properties not stored in Bindings

				/** @brief Returns a table of additional properties that are not accessible via the language itself, used for debugging
				 *
				 * ECMAScript types have a collection of internal slots, propertylike entries which cannot be directly accessed within the language itself.
				 * These are usually represented as names enclosed within [[ and ]] symbols, for example [[Prototype]] for the prototype slot of an object.
				 * This method is only intended for debugging and internal documentation.
				 *
				 * @param unified The names of the internal slots has changed between versions, and this method will provide them with the version appropriate
				 *                names unless unified is set to true. In that case, a name is chosen independent of the version to help with identifying the entry.
				 */
				virtual std::map<JSString, JSValue> GetInternalSlots(bool unified = false) = 0;

				/** @brief Returns the value of the [[Class]] internal slot, as accessed by Object.prototype.toString
				 *
				 * Introduced in ECMAScript 1 as the [[Class]] internal slot, removed in ECMAScript 6.
				 */
				virtual JSString GetClassString() = 0;

				/** @brief Returns the value of the [[Class]] internal slot, as understood by E4X
				 *
				 * This is usually the same as the result of GetClassString(), and the default implementation calls that function.
				 */
				virtual JSString GetClassSlot();

				/** @brief Pre-ES5, host objects could override the behavior of new Object(host-object) */
				virtual JSValue NewObject();

				/** @brief Returns a value indicating whether Call and Construct are callable */
				virtual Invocability GetInvocability() = 0;

				/** @brief Evaluates a function and returns the result, as by the syntax function(arguments)
				 *
				 * Introduced in ECMAScript 1 as [[Call]]
				 *
				 * Corresponds to the call instance method in the Netscape ECMAScript 4 proposal.
				 *
				 * @param this_argument The object that is used as the 'this' parameter. Outside ECMAScript 4, this must be a reference object or undefined (since ECMAScript 5) or null (up to ECMAScript 3 and including ECMAScript 4).
				 * @param arguments The list of arguments that were passed to the call.
				 * @param phase ECMAScript 4 only, whether this evaluation occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
				 * @return The result of the call, undefined if there is none.
				 */
				virtual JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) = 0;

				/** @brief Initiates a tail call
				 *
				 * Depending on the method, this function will either create a new execution context on the execution context stack without
				 * beginning execution, or push a result onto the value stack of the current execution context. In the first case, it returns
				 * true, in the second case it returns false.
				 *
				 * For an actual tail call, the caller then has to react differently in these two situations. If the function returns true,
				 * the execution stack has a new execution context on the top of it, and the previous top of the execution context must be
				 * terminated and removed. If the function returns false, the execution context stack is still in its previous state, and
				 * the result of the call is on the top of the stack of the current execution context, which it has to process accordingly.
				 * In this case, the behavior is more or less identical to the Call method, except that the return value is found on the stack.
				 *
				 * The default implementation assumes that this function object is not capable of participating in a tail call, instead calls
				 * Call, pushes the result onto the value stack and returns false.
				 *
				 * @param this_argument The object that is used as the 'this' parameter. Outside ECMAScript 4, this must be a reference object or undefined (since ECMAScript 5) or null (up to ECMAScript 3 and including ECMAScript 4).
				 * @param arguments The list of arguments that were passed to the call.
				 * @return Returns true if a new execution context has been created and pushed onto the execution context stack, returns false if the top of the value stack of the current execution context contains the result of the call.
				 */
				virtual bool Begin_Call(JSValue this_argument, std::vector<JSValue> arguments);

				/** @brief Instantiates the function, as by the syntax new function(arguments)
				 *
				 * Introduced in ECMAScript 1 as [[Construct]]
				 *
				 * Corresponds to the construct instance method in the Netscape ECMAScript 4 proposal.
				 *
				 * @param arguments The list of arguments that were passed to the call.
				 * @param new_target For ECMAScript 6 and later, provides the function object whose instance is being created. Normally this is the same as the current object, but in case of super calls, the constructor of the superclass gets called with the same new_target as in the case of the first constructor call.
				 * @param phase ECMAScript 4 only, whether this evaluation occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
				 * @return The result of the call, undefined if there is none.
				 */
				virtual JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) = 0;

#if ENABLE_ES4
				/** @brief Returns a function signature for callable objects, used for ECMAScript 4 only.
				 *
				 * Non-callables should return the empty value.
				 *
				 * @qualifier ECMAScript4
				 */
				virtual std::optional<JS::Runtime::SignatureType> GetSignature();
#endif
				/** @brief Returns the realm corresponding to a function object.
				 *
				 * Non-callables should return nullptr.
				 */
				virtual JS::Runtime::Realm * GetFunctionRealm();

				/** @brief Resolves an underlying reference or returns the object as a value.
				 *
				 * ECMAScript permits functions to return references to variables, typically represented as an object
				 * and key pair  (though the actual details are more complicated). These references are never accessed
				 * directly in ECMAScript, and get converted to the value of the variable when read.
				 *
				 * This method is also used to convert an instance of a subclass of PrimitiveValue back into its
				 * primitive form. This is needed because PrimitiveValue objects are an artifact of the internal
				 * representation and they cannot directly be cast to the JSValue type.
				 *
				 * Reference objects should return their own reference.
				 *
				 * @param intent E4X only, whether the reference (or object) is accessed for reading or call, default is Intent::Read.
				 * @param phase ECMAScript 4 only, whether this evaluation occurs as a compile time constant expression or not, default is ExecutionPhase::Run, use macro RUNTIME_PHASE.
				 * @return The result of the evaluation.
				 */
				virtual JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) = 0;
				/** @brief Assigns a value to a reference, it will result in an error for normal objects and values
				 *
				 * @param value The new value to assign.
				 */
				virtual void PutValue(JSValue value) = 0;
				/** @brief Deletes a reference, has no effect on objects or primitive values
				 *
				 * @return True if the reference no longer exists, either because it was successfully deleted, or if it did not exist. Returns true for objects and primitive values. */
				virtual bool DeleteReference();
				/** @brief Resolves the reference as a function
				 *
				 * Identical to GetValue(Intent::Call RUNTIME_PHASE)
				 */
				virtual JSValue ResolveMethod();

				/** @brief Checks equality of two values
				 *
				 * This function treats NaNs as the same value but distinguishes between +0 and -0.
				 * Otherwise, SameValue, SameValueZero and IsStrictlyEqual give the same results.
				 *
				 * @param other The other value to compare to.
				 * @return Whether the two values are equal.
				 */
				virtual bool SameValue(const JSValue& other) const = 0;
				/** @brief Checks equality of two values
				 *
				 * This function treats NaNs as the same value and +0 and -0 as the same value.
				 * Otherwise, SameValue, SameValueZero and IsStrictlyEqual give the same results.
				 *
				 * @param other The other value to compare to.
				 * @return Whether the two values are equal.
				 */
				virtual bool SameValueZero(const JSValue& other) const;
				/** @brief Checks equality of two values
				 *
				 * This function treats NaNs as different (even from itself), but treats +0 and -0 as the same.
				 * Otherwise, SameValue, SameValueZero and IsStrictlyEqual give the same results.
				 *
				 * @param other The other value to compare to.
				 * @return Whether the two values are equal.
				 */
				virtual bool IsStrictlyEqual(const JSValue& other) const;
				/** @brief Converts the value to a boolean */
				virtual JSBoolean ToBoolean() = 0;
				/** @brief Converts the value to a 64-bit floating point number */
				virtual JSNumber ToNumber(FOR_PHASE(phase)) = 0;
				/** @brief Converts the value to a numeric value: Number, BigInt and for ECMAScript 4, Long, Ulong, Float, Decimal */
				virtual JSValue ToNumeric(FOR_PHASE(phase));
				/** @brief Converts the value to an arbitrary precision integer */
				virtual JSBigInt ToBigInt() = 0;
				/** @brief Converts the value to a string */
				virtual JSString ToString(FOR_PHASE(phase)) = 0;
				/** @brief Converts the value to an object reference */
				virtual JSObject ToObject() = 0;
				/** @brief Retrieves the "typeof" value of the object */
				virtual JSString Typeof() = 0;
				/** @brief Retrieves the string used by Object.prototype.toString */
				virtual JSString ToStringTag() = 0;

				/** @brief E4X ToXMLString function */
				virtual JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) = 0;
				/** @brief E4X ToXML function */
				virtual JSXML ToXML() = 0;
				/** @brief E4X ToXMLList function */
				virtual JSXMLList ToXMLList() = 0;
				/** @brief E4X ToAttributeName function */
				virtual JSAttributeName ToAttributeName() = 0;
				/** @brief E4X ToXMLName function */
				virtual JSValue ToXMLName() = 0;

				/** @brief Checks if this is of the specified type
				 *
				 * Used to check for polymorphism by the JSValue class, it is expected that objects corresponding to a
				 * primitive value T returns true when type is typeid(T) and reference objects return true when type is
				 * typeid(JSObject) specifically.
				 */
				virtual bool has_value(const std::type_info& type) const = 0;
				/** @brief Returns a pointer that holds the value of the object if type matches the current type
				 *
				 * Objects corresponding to primitive types will return a pointer that holds that value. Reference objects
				 * return the reference value. This is then handled by JSValue to retrieve the value of the propert type.
				 */
				virtual const void * get_if(const std::type_info& type) const = 0;
				/** @brief Displays information for the object, mimicking Node.js util::inspect (partially). */
				virtual void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) = 0;
#if ENABLE_DEBUG
				/** @brief Serializes the internals of this object into an XML format, only for debugging.
				 *
				 * The entire state is serialized (to the extent currently supported), which enables checking the exact structure
				 * of the objects.
				 */
				virtual void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) = 0;
#endif
			};
		}
	}

	namespace Type
	{
#if ENABLE_ES4
		// attempt to emulate Vector.<T>
		struct TypeApplication
		{
		public:
			JSClass parametrized_type;
			std::vector<JSType> parameters;

			inline bool operator ==(const TypeApplication& other) const
			{
				return parametrized_type == other.parametrized_type
					&& parameters == other.parameters;
			}
		};

		// ES4
		struct CompoundAttribute
		{
			bool Empty = true;
			bool Enabled = true;
			std::set<JSNamespace> Namespaces;
			JSBoolean Explicit = false;
			JSBoolean Enumerable = false;
			JSBoolean Dynamic = false;
			JS::Runtime::MemberModifier Category = JS::Runtime::MemberModifier::None;
			JS::Runtime::OverrideModifier Override = JS::Runtime::OverrideModifier::None;
			JSBoolean Prototype = false;
			JSBoolean Unused = false; // TODO: is unused used?
			JSBoolean Native = false; // TODO

			CompoundAttribute() { }
			CompoundAttribute(bool enabled) : Empty(false), Enabled(enabled) { }
			CompoundAttribute(JSNamespace namespace_) : Empty(false), Namespaces({namespace_}) { }
			CompoundAttribute(const CompoundAttribute&) = default;

			void Clear()
			{
				Empty = true;
				Enabled = true;
				Namespaces.clear();
				Explicit = false;
				Enumerable = false;
				Dynamic = false;
				Category = JS::Runtime::MemberModifier::None;
				Override = JS::Runtime::OverrideModifier::None;
				Prototype = false;
				Unused = false;
				Native = false;
			}

			const CompoundAttribute& operator =(const CompoundAttribute& other)
			{
				Empty = other.Empty;
				Enabled = other.Enabled;
				Namespaces = other.Namespaces;
				Explicit = other.Explicit;
				Enumerable = other.Enumerable;
				Dynamic = other.Dynamic;
				Category = other.Category;
				Override = other.Override;
				Prototype = other.Prototype;
				Unused = other.Unused;
				Native = other.Native;
				return *this;
			}

			const CompoundAttribute& operator |=(const CompoundAttribute& other)
			{
				if(!other.Enabled)
				{
					Clear();
					Empty = false;
					Enabled = false;
				}
				else if(Enabled)
				{
					Empty = false;
					Enabled |= other.Enabled;
					Namespaces.insert(other.Namespaces.begin(), other.Namespaces.end());
					Explicit |= other.Explicit;
					Enumerable |= other.Enumerable;
					Dynamic |= other.Dynamic;
					if(Category == JS::Runtime::MemberModifier::None)
					{
						Category = other.Category;
					}
					else if(other.Category != JS::Runtime::MemberModifier::None)
					{
						// TODO: throw new AttributeError
					}
					if(Override == JS::Runtime::OverrideModifier::None)
					{
						Override = other.Override;
					}
					else if(other.Override != JS::Runtime::OverrideModifier::None)
					{
						// TODO: throw new AttributeError
					}
					Prototype |= other.Prototype;
					Unused |= other.Unused;
					Native |= other.Native;
				}
				return *this;
			}

			bool IsEmpty() const
			{
				return Empty
					// the following is not relevant if Empty is set
					&& Enabled
					&& Namespaces.size() == 0
					&& !Explicit
					&& !Enumerable
					&& !Dynamic
					&& Category == JS::Runtime::MemberModifier::None
					&& Override == JS::Runtime::OverrideModifier::None
					&& !Prototype
					&& !Unused
					&& !Native;
			}

			std::set<JSNamespace> GetNamespaces() const
			{
				if(Namespaces.size() == 0)
					return {JS_public};
				else
					return Namespaces;
			}

			CompoundAttribute WithEnabled(bool Enabled = true)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Enabled = Enabled;
				return copy;
			}

			CompoundAttribute WithNamespace(JSNamespace namespace_)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Namespaces.insert(namespace_);
				return copy;
			}

			CompoundAttribute WithExplicit(bool Explicit = true)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Explicit = Explicit;
				return copy;
			}

			CompoundAttribute WithEnumerable(bool Enumerable = true)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Enumerable = Enumerable;
				return copy;
			}

			CompoundAttribute WithDynamic(bool Dynamic = true)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Dynamic = Dynamic;
				return copy;
			}

			CompoundAttribute WithCategory(JS::Runtime::MemberModifier Category)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Category = Category;
				return copy;
			}

			CompoundAttribute WithOverride(JS::Runtime::OverrideModifier Override)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Override = Override;
				return copy;
			}

			CompoundAttribute WithPrototype(bool Prototype = true)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Prototype = Prototype;
				return copy;
			}

			CompoundAttribute WithUnused(bool Unused = true)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Unused = Unused;
				return copy;
			}

			CompoundAttribute WithNative(bool Native = true)
			{
				CompoundAttribute copy = *this;
				copy.Empty = false;
				copy.Native = Native;
				return copy;
			}
		};
#endif
	}

	namespace Class
	{
		namespace Internal
		{
			class PrimitiveValue : public JS::Class::Internal::Value, public std::enable_shared_from_this<PrimitiveValue>
			{
			public:
				static std::set<PrimitiveValue *> to_keep_alive;

#if ENABLE_ES4
				std::optional<std::map<JSInstanceVariable, std::optional<JSValue>>::iterator> FindSlot(JSInstanceVariable id) override; // TODO: object only
#endif
				bool IsExtensible() override; // TODO: object only
				bool PreventExtensions() override; // TODO: object only
				std::optional<JSValue> GetPrototypeOf() override; // TODO: object only?
				bool SetPrototypeOf(JSValue new_prototype) override; // TODO: object only?
				std::optional<JSProperty> GetOwnProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
				std::optional<JSProperty> GetProperty(JSPropertyKey property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
				bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase)) override;
				JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;
				bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false) override;
				JSValue GetPrivate(JSPrivateName name) override; // TODO: object only?
				void SetPrivate(JSPrivateName name, JSValue value) override; // TODO: object only?
				bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical)) override;
				bool DefineOwnProperty(JSPropertyKey property_key, JSPropertyDescriptor descriptor AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override;
				bool Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses)) override; // TODO: object only?
				std::vector<JSPropertyKey> OwnPropertyKeys(FOR_CLASS(limit)) override;
				std::map<PropertyKeyData, JSProperty> ExtractAllProperties() override; // used to return any properties not stored in Bindings
				std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override; // TODO: object only
				JSString GetClassString() override;
				Invocability GetInvocability() override;
#if ENABLE_ES4
				std::optional<JS::Runtime::SignatureType> GetSignature() override;
#endif
				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;

				void PutValue(JSValue value) override;

				JSValue ToNumeric(FOR_PHASE(phase)) override;

				JS::Runtime::Realm * GetFunctionRealm() override;

				JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
				JSXML ToXML() override;
				JSXMLList ToXMLList() override;
				JSAttributeName ToAttributeName() override;
				JSValue ToXMLName() override;
			};
		}
	}

	namespace Class
	{
		namespace Internal
		{
			template <typename T>
				class Primitive;

			template <>
				class Primitive<JS::Type::Internal::Undefined> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
#if ENABLE_ES4
				JSType GetType() override;
#endif

				JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
				JSXML ToXML() override;
				JSXMLList ToXMLList() override;
				JSAttributeName ToAttributeName() override;
				JSValue ToXMLName() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JS::Type::Internal::Null> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
#if ENABLE_ES4
				JSType GetType() override;
#endif

				JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
				JSXML ToXML() override;
				JSXMLList ToXMLList() override;
				JSAttributeName ToAttributeName() override;
				JSValue ToXMLName() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JSBoolean> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSBoolean BooleanData;
				Primitive(JSBoolean BooleanData) : BooleanData(BooleanData) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
#if ENABLE_ES4
				JSType GetType() override;
#endif

				JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
				JSXML ToXML() override;
				JSXMLList ToXMLList() override;
				JSAttributeName ToAttributeName() override;
				JSValue ToXMLName() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JSNumber> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSNumber NumberData;
				Primitive(JSNumber NumberData) : NumberData(NumberData) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				bool SameValueZero(const JSValue& other) const override;
				bool IsStrictlyEqual(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
#if ENABLE_ES4
				JSType GetType() override;
#endif

				JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
				JSXML ToXML() override;
				JSXMLList ToXMLList() override;
				JSAttributeName ToAttributeName() override;
				JSValue ToXMLName() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JSString> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSString StringData;
				Primitive(JSString StringData) : StringData(StringData) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
#if ENABLE_ES4
				JSType GetType() override;
#endif

				JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
				JSXML ToXML() override;
				JSXMLList ToXMLList() override;
				JSAttributeName ToAttributeName() override;
				JSValue ToXMLName() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JSSymbol> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSSymbol SymbolData;
				Primitive(JSSymbol SymbolData) : SymbolData(SymbolData) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
#if ENABLE_ES4
				JSType GetType() override;
#endif

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JSBigInt> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSBigInt BigIntData;
				Primitive(JSBigInt BigIntData) : BigIntData(BigIntData) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
#if ENABLE_ES4
				JSType GetType() override;
#endif

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

#if ENABLE_ES4
			template <>
				class Primitive<JSLong> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSLong value;
				Primitive(JSLong value) : value(value) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
				JSType GetType() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JSUlong> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSUlong value;
				Primitive(JSUlong value) : value(value) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
				JSType GetType() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JSFloat> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSFloat value;
				Primitive(JSFloat value) : value(value) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
				JSType GetType() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

#if ENABLE_DEC128
			template <>
				class Primitive<JSDecimal> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSDecimal value;
				Primitive(JSDecimal value) : value(value) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
				JSType GetType() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};
#endif

			template <>
				class Primitive<JSChar> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSChar value;
				Primitive(JSChar value) : value(value) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
				JSType GetType() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			// ValueContent<JSMethodClosure> is defined later
			// Primitive<JSMethodClosure> is defined later

			template <>
				class Primitive<JSCompoundAttribute> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSCompoundAttribute value;
				Primitive(JSCompoundAttribute value) : value(value) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
				JSType GetType() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JSTypeApplication> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSTypeApplication value;
				Primitive(JSTypeApplication value) : value(value) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
				JSType GetType() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};
#endif

			// used for values that should be converted to JSValue before processed
			class IntermediateValue : public JS::Class::Internal::PrimitiveValue
			{
			public:
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSValue ToNumeric(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
#if ENABLE_ES4
				JSType GetType() override;
#endif

				JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces, uint32_t IndentLevel) override;
				JSXML ToXML() override;
				JSXMLList ToXMLList() override;
				JSAttributeName ToAttributeName() override;
				JSValue ToXMLName() override;

				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JSAttributeName> : public IntermediateValue
			{
			public:
				JSAttributeName value;
				Primitive(JSAttributeName value) : value(value) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;

				JSString ToString(FOR_PHASE(phase)) override;
				JSAttributeName ToAttributeName() override;
				JSValue ToXMLName() override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;

#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};

			template <>
				class Primitive<JS::Runtime::LexicalReference>;
			template <>
				class Primitive<JS::Runtime::MemberReference>;
#if ENABLES_ES4
			template <>
				class Primitive<JS::Runtime::BracketReference>;
#endif
			template <>
				class Primitive<JS::Runtime::SuperReference>;
			template <>
				class Primitive<JS::Runtime::PrivateReference>;
		}
	}

	namespace Type
	{
		/** @brief The root type of all objects and values, it can store references or primitive types
		 *
		 * Modern ECMAScript supports 5 primitive types (boolean, string, number, symbol, bigint)
		 * alongside reference types and 2 special values (null, undefined).
		 * To make it possible to add further primitive types, polymorphism is used to store these
		 * in an extensible variantlike structure. The Value class provides the general interface
		 * shared by all values of ECMAScript.
		 *
		 * Internally, values are stored in a hidden pointer, but the Value class is intended to
		 * be used as a non-heap object. The reference, primitive types and special values are
		 * identified through their types:
		 * - JSObject is used for all reference types
		 * - JSBoolean, JSString, JSNumber, JSSymbol, JSBigInt are used for primitive types
		 * - JS::Type::Internal::Undefined is used to identify an undefined value
		 * - JS::Type::Internal::Null is used to identify a null value
		 *
		 * Note that the two last types are not actually used to store anything, they only appear
		 * internally as type parameters.
		 *
		 * The interface is very similar to that of JSObject, and in fact if the stored value
		 * is a reference, all calls get transferred directly to the reference object.
		 * Many of these methods will not work and raise an error for primitive types, such as
		 * attempting to use the Get/Set methods. However, in ECMAScript 4 mode, these calls produce
		 * actual effects.
		 */
		class Value
		{
#if 1
		public:
			std::variant<std::shared_ptr<JS::Class::Internal::PrimitiveValue>, JSObject> content;

		protected:
			JS::Class::Internal::Value * get_content() const
			{
				return std::visit([](auto&& content) { return static_cast<JS::Class::Internal::Value *>(&*content); }, content);
			}

			Value(JS::Class::Internal::PrimitiveValue * v)
				: content(v->shared_from_this())
			{
			}

		public:
			static JS::Class::Internal::Value * UnwrapValue(const Value& v);
			static Value WrapValue(JS::Class::Internal::Value * v);
			static void ReleaseValue(const Value& v);
			static void ReleaseValue(JS::Class::Internal::Value * v);

			template <typename T>
				std::optional<T> get_if() const
			{
				if(auto pointer = get_content()->get_if(typeid(T)))
				{
					if constexpr(std::is_same_v<T, JSObject>)
					{
						return std::make_optional<T>(gcn::gc_ptr<JS::Class::Object>(reinterpret_cast<JS::Class::Object *>(const_cast<void *>(pointer))));
					}
					else
					{
						return std::make_optional<T>(*reinterpret_cast<const T *>(pointer));
					}
				}
				else
					return std::optional<T>{};
			}
#endif

// TODO: split to Reference and Value, Reference has GetValue, PutValue, DeleteReference
		public:
			Value(JS::Class::Internal::Value * value);
			Value();
			Value(nullptr_t);
			Value(JSBoolean value);
			Value(JSNumber value);
			Value(JSString value);
			Value(const char16_t * value);

			Value(JS::Class::Object * value);

			template <typename T>
				Value(gcn::gc_ptr<T> value)
				: Value(static_cast<JS::Class::Object *>(value.get()))
			{
			}
#if ENABLE_ES4
			explicit Value(JSLong value);
			explicit Value(JSUlong value);
			explicit Value(JSFloat value);
#if ENABLE_DEC128
			explicit Value(JSDecimal value);
#endif
			explicit Value(JSChar value);
			explicit Value(JSMethodClosure value);
			explicit Value(JSCompoundAttribute value);
			explicit Value(JSTypeApplication value);
			explicit Value(JSType type);
#endif
			Value(JSSymbol value);
			Value(JSBigInt value);
			Value(const Value& other) = default;
			Value(JSPropertyKey value);
			// TODO: these should only work for JSReference
			Value(JS::Type::AttributeName name);
			Value(JS::Runtime::LexicalReference reference);
			Value(JS::Runtime::MemberReference reference);
#if ENABLE_ES4
			Value(JS::Runtime::BracketReference reference);
#endif
			Value(JS::Runtime::SuperReference reference);
			Value(JS::Runtime::PrivateReference reference);

			template <typename T>
				bool has_value() const
			{
				return get_content()->has_value(typeid(T));
			}

			template <typename T>
				T get() const
			{
				return get_if<T>().value();
			}

			template <typename T>
				gcn::gc_ptr<T> cast_object() const
			{
				return std::dynamic_pointer_cast<T>(get_if<JSObject>().value_or(nullptr));
			}

			// TODO: these should be JSReference specific, JSValue should behave in a trivial way
			// JSValue::GetValue returns itself
			JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) { return get_content()->GetValue(intent WITH_PHASE(phase)); }
			// JSValue::PutValue throws a ReferenceError
			void PutValue(JSValue value) { get_content()->PutValue(value); }
			// JSValue::DeleteReference returns true and does nothing
			bool DeleteReference() { return get_content()->DeleteReference(); }
			JSValue ResolveMethod() { return get_content()->ResolveMethod(); }

			bool SameValue(const JSValue& other) const { return get_content()->SameValue(other); }
			bool SameValueZero(const JSValue& other) const { return get_content()->SameValueZero(other); }
			bool IsStrictlyEqual(const JSValue& other) const { return get_content()->IsStrictlyEqual(other); }
			JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) { return get_content()->ToPrimitive(hint WITH_PHASE(phase)); }
			JSBoolean ToBoolean() { return get_content()->ToBoolean(); }
			JSNumber ToNumber(FOR_PHASE(phase)) { return get_content()->ToNumber(__ES4_ONLY(phase)); }
			JSValue ToNumeric(FOR_PHASE(phase)) { return get_content()->ToNumeric(__ES4_ONLY(phase)); }
			JSBigInt ToBigInt() { return get_content()->ToBigInt(); }
			JSNumber ToInteger(FOR_PHASE(phase));
			uint8_t ToUint8Clamp();
			int32_t ToInt32(FOR_PHASE(phase));
			uint32_t ToUint32(FOR_PHASE(phase));
#if ENABLE_ES4
			int64_t ToInt64(FOR_PHASE(phase));
			uint64_t ToUint64(FOR_PHASE(phase));
#endif
			JSString ToString(FOR_PHASE(phase)) { return get_content()->ToString(__ES4_ONLY(phase)); }
			JSPropertyKey ToPropertyKey(FOR_PHASE(phase))
			{
				auto primitive = ToPrimitive(JS::Runtime::TypeHint::String WITH_PHASE(phase));
				if(auto symbol = primitive.get_if<JSSymbol>())
				{
					return *symbol;
				}
				else
				{
					return primitive.ToString(VIA_PHASE(phase));
				}
			}
			JSObject ToObject() { return get_content()->ToObject(); }
			JSString Typeof() { return get_content()->Typeof(); }
			JSString ToStringTag() { return get_content()->ToStringTag(); }
#if ENABLE_ES4
			JSType GetType();
			JSClass GetClass();
			JSValue Coerce(JSDeclaredType class_);
#endif

			JSString ToXMLString(std::map<JSString, JSNamespace> AncestorNamespaces = {}, uint32_t IndentLevel = 0) { return get_content()->ToXMLString(AncestorNamespaces, IndentLevel); }
			JSXML ToXML() { return get_content()->ToXML(); }
			JSXMLList ToXMLList() { return get_content()->ToXMLList(); }
			JSAttributeName ToAttributeName() { return get_content()->ToAttributeName(); }
			JSValue ToXMLName() { return get_content()->ToXMLName(); }

			void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) { get_content()->inspect(out, depth, visited, access); }
			void inspect(std::ostream& out, size_t depth = 5) { std::map<JSObject, std::string> visited; inspect(out, depth, visited, "self"); }

			std::optional<JSValue> GetPrototypeOf()
			{
				return get_content()->GetPrototypeOf();
			}

			JSValue CreateReference(JSIdentifierReference identifier_reference, JS::Runtime::ReferenceContext cxt);
			JSValue CreateBracketReference(std::vector<JSValue> arguments, JS::Runtime::ReferenceContext cxt);
			JSValue ResolvePropertyKey(JSValue property_key);
			JSValue ResolveMethod(JS::Runtime::MemberReference& reference);
#if ENABLE_ES4
			JSValue ResolveMethod(JS::Runtime::BracketReference& reference);
#endif

			bool HasProperty(JSValue property_key AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
			{
				return get_content()->HasProperty(property_key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
			}

			JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase))
			{
				return get_content()->Get(property_key, receiver, intent WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment) WITH_PHASE(phase));
			}
#if ENABLE_ES4
			JSValue GetBracket(std::vector<JSValue> arguments, JSValue receiver, Intent intent AND_CLASS(limit) AND_PHASE(phase))
			{
				return get_content()->GetBracket(arguments, receiver, intent WITH_CLASS(limit) WITH_PHASE(phase));
			}
#endif
			JSValue GetPrivate(JSPrivateName name)
			{
				return get_content()->GetPrivate(name);
			}
			bool Set(JSValue property_key, JSValue value, JSValue receiver AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment), bool check_can_put = false)
			{
				return get_content()->Set(property_key, value, receiver WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_ENVIRONMENT(environment), check_can_put);
			}
#if ENABLE_ES4
			bool SetBracket(std::vector<JSValue> arguments, JSValue value, JSValue receiver AND_CLASS(limit))
			{
				return get_content()->SetBracket(arguments, value, receiver WITH_CLASS(limit));
			}
#endif
			bool Define(JSPropertyKey property_key, JSProperty property AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses));

			bool Delete(JSValue property_key AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_FLAG(lexical))
			{
				return get_content()->Delete(property_key WITH_NAMESPACES(namespaces) WITH_CLASS(limit) WITH_FLAG(lexical));
			}

#if ENABLE_ES4
			bool DeleteBracket(std::vector<JSValue> arguments AND_CLASS(limit))
			{
				return get_content()->DeleteBracket(arguments WITH_CLASS(limit));
			}
#endif

			Invocability GetInvocability() { return get_content()->GetInvocability(); }

#if ENABLE_DEBUG
			void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) { get_content()->serialize(debugger, force_output); }
#endif

#if ENABLE_ES4
			// convert long/ulong or Number/float containing an integer to BigInt
			static JSBigInt ES4NumberToBigInt(JSValue arg);
			// attempt to convert a BigInt to long/ulong, preferring ulong if ulong is set to true, otherwise preferring long
			static std::optional<JSValue> ES4BigIntToLong(JSBigInt arg, bool ulong);
#endif

			static JSValue unaryMinus(JSValue x AND_PHASE(phase));
			static JSValue bitwiseNOT(JSValue x AND_PHASE(phase));
			static JSValue exponentiate(JSValue x, JSValue y);
			static JSValue multiply(JSValue x, JSValue y AND_PHASE(phase));
			static JSValue divide(JSValue x, JSValue y AND_PHASE(phase));
			static JSValue remainder(JSValue x, JSValue y AND_PHASE(phase));
			// only arithmetic
			static JSValue add(JSValue x, JSValue y AND_PHASE(phase));
			static JSValue subtract(JSValue x, JSValue y AND_PHASE(phase));
			static JSValue leftShift(JSValue x, JSValue y AND_PHASE(phase));
			static JSValue signedRightShift(JSValue x, JSValue y AND_PHASE(phase));
			static JSValue unsignedRightShift(JSValue x, JSValue y AND_PHASE(phase));
			// <, ===, only arithmetic
			static ComparisonResult compare(JSValue x, JSValue y AND_PHASE(phase));
			// Object.is
			static bool sameValue(JSValue x, JSValue y);
			// [x].includes[y]
			static bool sameValueZero(JSValue x, JSValue y);
			static JSValue bitwiseAND(JSValue x, JSValue y AND_PHASE(phase));
			static JSValue bitwiseXOR(JSValue x, JSValue y AND_PHASE(phase));
			static JSValue bitwiseOR(JSValue x, JSValue y AND_PHASE(phase));
		};

		class Function : public Value
		{
		protected:
			explicit Function(JSValue value);
		public:
			Function() : Value(JSObject(nullptr)) { }
			Function(nullptr_t) : Value(JSObject(nullptr)) { }
			Function(const Function&) = default;
#if ENABLE_ES4
			explicit Function(JSMethodClosure value);
#endif

#if ENABLE_ES4
			Function(gcn::gc_ptr<JS::Class::Class> reference);
#endif
			Function(JS::Class::Function * reference);
			Function(gcn::gc_ptr<JS::Class::Function> reference);
			Function(JS::Class::BoundFunction * reference);
			Function(gcn::gc_ptr<JS::Runtime::Builtin::BuiltinFunction> reference);
			Function(gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> reference);

			static std::optional<Function> GetFunction(JSValue value);
			static Function MakeFunction(JSValue value);

#if ENABLE_ES4
			std::optional<JS::Runtime::SignatureType> GetSignature();
#endif
			JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) { return get_content()->Call(this_argument, arguments WITH_PHASE(phase)); }
			bool Begin_Call(JSValue this_argument, std::vector<JSValue> arguments) { return get_content()->Begin_Call(this_argument, arguments); }
			JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) { return get_content()->Construct(arguments, new_target WITH_PHASE(phase)); }
			JS::Runtime::Realm * GetFunctionRealm() { return get_content()->GetFunctionRealm(); }
		};

#if ENABLE_ES4
		namespace MetaType
		{
			enum class Nullability
			{
				Default,
				Nullable,
				NonNullable,
			};

			class DeclaredTypeContentBase
			{
			public:
				virtual ~DeclaredTypeContentBase() = default;
				virtual DeclaredTypeContentBase * clone() const = 0;
				virtual std::optional<JSValue> Coerce(JSValue value, bool strict = true) = 0;
				virtual bool Is(JSValue value) = 0;
				virtual std::optional<JSValue> DefaultValue() = 0;
				virtual JSDeclaredType ApplyParameters(std::vector<JSDeclaredType> types, Nullability nullable) = 0;
				virtual JSDeclaredType ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable) = 0;
				virtual bool equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const = 0;
				virtual void serialize(JS::Runtime::Debugger * debugger) = 0;
			};

			template <typename T>
				class DeclaredTypeContent;

			class Array;
			class Union;
			class Record;
			class Function;

			/** @brief Represents the declared type of a name that can store values, values can only have type JSType */
			class DeclaredType
			{
			protected:
				DeclaredTypeContentBase * content;

			public:
				Nullability nullable = Nullability::Default;

				DeclaredType(const DeclaredType& other) : content(other.content->clone()), nullable(other.nullable) { }
				DeclaredType(const DeclaredType& other, bool nullable) : content(other.content->clone()), nullable(nullable ? Nullability::Nullable : Nullability::NonNullable) { }
				DeclaredType(DeclaredType&& other) : content(other.content), nullable(other.nullable) { other.content = nullptr; }

				DeclaredType(JSClass class_);
				DeclaredType(JSClass class_, std::vector<JSDeclaredType> parameter_types);
				DeclaredType(JSInterface interface);
				DeclaredType(JSInterface interface, std::vector<JSDeclaredType> parameter_types);
				DeclaredType(JSTypeApplication type_application);
				DeclaredType(JSType type);
				DeclaredType(Array&& array_type);
				DeclaredType(Union&& union_type);
				DeclaredType(Record&& record_type);
				DeclaredType(Function&& function_type);
				DeclaredType(JSString type_parameter);

				DeclaredType WithNullability(Nullability nullable)
				{
					DeclaredType copy = *this;
					copy.nullable = nullable;
					return copy;
				}

				~DeclaredType()
				{
					if(content != nullptr)
						delete content;
				}

				DeclaredType& operator =(const DeclaredType& other)
				{
					if(content != nullptr)
						delete content;
					content = other.content != nullptr ? other.content->clone() : nullptr;
					return *this;
				}

				DeclaredType& operator =(DeclaredType&& other)
				{
					if(content != nullptr)
						delete content;
					content = other.content;
					other.content = nullptr;
					return *this;
				}

				template <typename ... Types>
					static DeclaredType ArrayType(Types ... types)
				{
					return DeclaredType(Array(types...));
				}

				template <typename ... Types>
					static DeclaredType UnionType(Types ... types)
				{
					return DeclaredType(Union(types...));
				}

				template <typename ... Types>
					static DeclaredType RecordType(Types ... types)
				{
					return DeclaredType(Record(types...));
				}

				template <typename ... Types>
					static DeclaredType FunctionType(Types ... types)
				{
					return DeclaredType(Function(types...));
				}

				inline std::optional<JSValue> Coerce(JSValue value, bool strict = true)
				{
					std::optional<JSValue> result;
					switch(nullable)
					{
					case Nullability::Default:
						return content->Coerce(value, strict);
					case Nullability::Nullable:
						if(IsNull(value))
						{
							return value;
						}
						else
						{
							return content->Coerce(value, strict);
						}
					case Nullability::NonNullable:
						result = content->Coerce(value, strict);
						if(result.has_value() && IsNull(result.value()))
						{
							return {};
						}
						else
						{
							return result;
						}
					}
					return {}; // should not occur
				}

				inline bool Is(JSValue value)
				{
					switch(nullable)
					{
					case Nullability::Default:
						break;
					case Nullability::Nullable:
						if(IsNull(value))
						{
							return true;
						}
						break;
					case Nullability::NonNullable:
						if(IsNull(value))
						{
							return false;
						}
						break;
					}
					return content->Is(value);
				}

				inline std::optional<JSValue> DefaultValue()
				{
					std::optional<JSValue> result;
					switch(nullable)
					{
					case Nullability::Default:
						return content->DefaultValue();
					case Nullability::Nullable:
						return JS_null;
					case Nullability::NonNullable:
						result = content->DefaultValue();
						if(result.has_value() && IsNull(result.value()))
						{
							return {};
						}
						else
						{
							return result;
						}
					}
					return {}; // should not occur
				}

				JSDeclaredType ApplyParameters(std::vector<JSDeclaredType> types)
				{
					return content->ApplyParameters(types, nullable);
				}

				JSDeclaredType ReplaceParameters(std::map<JSString, JSDeclaredType> types)
				{
					return content->ReplaceParameters(types, nullable);
				}

				bool operator ==(const JSDeclaredType& other) const
				{
					return content->equal(other.content, std::map<JSString, JSString>{});
				}

				bool operator !=(const JSDeclaredType& other) const
				{
					return !(*this == other);
				}

				bool equal(const JSDeclaredType& other, std::map<JSString, JSString> parameter_replacements) const
				{
					return content->equal(other.content, parameter_replacements);
				}

				void serialize(JS::Runtime::Debugger * debugger)
				{
					content->serialize(debugger);
				}
			};

			class Array
			{
			public:
				std::vector<JSDeclaredType> fixed_types;
				std::optional<JSDeclaredType> rest_type;

				inline void init()
				{
				}

				inline void init(JSDeclaredType type)
				{
					fixed_types.push_back(type);
				}

				inline void init(JSDeclaredType type, bool rest)
				{
					if(rest)
						rest_type = type;
					else
						init(type);
				}

				template <typename ... Types>
					inline void init(JSDeclaredType type, Types ... types)
				{
					init(type);
					init(types...);
				}

			public:
				template <typename ... Types>
					Array(Types ... types)
				{
					init(types...);
				}

				Array(std::vector<JSDeclaredType> fixed_types, std::optional<JSDeclaredType> rest_type)
					: fixed_types(fixed_types), rest_type(rest_type)
				{
				}
			};

			class Union
			{
			public:
				std::vector<JSDeclaredType> types;

			private:
				inline void init()
				{
				}

				template <typename ... Types>
					inline void init(JSDeclaredType type, Types ... remaining_types)
				{
					types.push_back(type);
					init(remaining_types...);
				}

			public:
				template <typename ... Types>
					Union(Types ... types)
				{
					init(types...);
				}

				Union(std::vector<JSDeclaredType> types)
					: types(types)
				{
				}
			};

			class Record
			{
			public:
				std::vector<std::tuple<JSQualifiedName, JSDeclaredType>> fields;

			private:
				inline void init()
				{
				}

				template <typename ... Types>
					inline void init(JSQualifiedName name, JSDeclaredType type, Types ... rest_fields)
				{
					fields.push_back(std::make_tuple(name, type));
					init(rest_fields...);
				}

			public:
				template <typename ... Types>
					Record(Types ... types)
				{
					init(types...);
				}

				Record(std::vector<std::tuple<JSQualifiedName, JSDeclaredType>> fields)
					: fields(fields)
				{
				}
			};

			class Function
			{
			public:
				struct TypeParameter { }; // used to identify a type

				std::vector<JSString> type_parameters;
				std::vector<JSDeclaredType> parameters;
				std::vector<JSDeclaredType> optional_parameters;
				JSDeclaredType result;
				bool rest_parameter = false;

				static constexpr class OptionalMark { } Optional = OptionalMark{};

			private:
				inline void init()
				{
				}

				inline void init(bool rest)
				{
					rest_parameter = rest;
				}

				inline void init_opt()
				{
					init();
				}

				inline void init_opt(bool rest)
				{
					init(rest);
				}

				template <typename ... Types>
					inline void init_opt(JSDeclaredType type, Types ... types)
				{
					optional_parameters.push_back(type);
					init_opt(types...);
				}

				template <typename ... Types>
					inline void init(OptionalMark mark, JSDeclaredType type, Types ... types)
				{
					init_opt(type, types...);
				}

				template <typename ... Types>
					inline void init(JSDeclaredType type, Types ... types)
				{
					parameters.push_back(type);
					init(types...);
				}

				template <typename ... Types>
					inline void init_params(Types ... types)
				{
					init(types...);
				}

				template <typename ... Types>
					inline void init_params(JSString param, Types ... types)
				{
					type_parameters.push_back(param);
					init_params(types...);
				}

			public:
				template <typename ... Types>
					Function(JSDeclaredType result, Types ... types)
					: result(result)
				{
					init_params(types...);
				}

				Function(
					std::vector<JSString> type_parameters,
					std::vector<JSDeclaredType> parameters,
					std::vector<JSDeclaredType> optional_parameters,
					JSDeclaredType result,
					bool rest_parameter)
				: type_parameters(type_parameters),
					parameters(parameters),
					optional_parameters(optional_parameters),
					result(result),
					rest_parameter(rest_parameter)
				{
				}
			};

			template <>
				class DeclaredTypeContent<JSClass> : public DeclaredTypeContentBase
			{
			public:
				std::optional<std::vector<JSDeclaredType>> parameter_types;
				JSClass class_;
				DeclaredTypeContent(JSClass class_) : class_(class_) { }
				DeclaredTypeContent(JSClass class_, std::vector<JSDeclaredType> parameter_types) : class_(class_), parameter_types(parameter_types) { }
				DeclaredTypeContent(JSClass class_, std::vector<JSType> parameter_types);

				DeclaredTypeContentBase * clone() const override;
				std::optional<JSValue> Coerce(JSValue value, bool strict = true) override;
				bool Is(JSValue value) override;
				std::optional<JSValue> DefaultValue() override;
				JSDeclaredType ApplyParameters(std::vector<JSDeclaredType> types, Nullability nullable) override;
				JSDeclaredType ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable) override;
				bool equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const override;
				void serialize(JS::Runtime::Debugger * debugger) override;
			};

			template <>
				class DeclaredTypeContent<JSInterface> : public DeclaredTypeContentBase
			{
			public:
				std::optional<std::vector<JSDeclaredType>> parameter_types;
				JSInterface interface;
				DeclaredTypeContent(JSInterface interface) : interface(interface) { }
				DeclaredTypeContent(JSInterface interface, std::vector<JSDeclaredType> parameter_types) : interface(interface), parameter_types(parameter_types) { }
				DeclaredTypeContent(JSInterface interface, std::vector<JSType> parameter_types);

				DeclaredTypeContentBase * clone() const override;
				std::optional<JSValue> Coerce(JSValue value, bool strict = true) override;
				bool Is(JSValue value) override;
				std::optional<JSValue> DefaultValue() override;
				JSDeclaredType ApplyParameters(std::vector<JSDeclaredType> types, Nullability nullable) override;
				JSDeclaredType ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable) override;
				bool equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const override;
				void serialize(JS::Runtime::Debugger * debugger) override;
			};

			template <>
				class DeclaredTypeContent<Array> : public DeclaredTypeContentBase, public Array
			{
			public:
				DeclaredTypeContent(const Array& array) : Array(array) { }

				DeclaredTypeContentBase * clone() const override;
				std::optional<JSValue> Coerce(JSValue value, bool strict = true) override;
				bool Is(JSValue value) override;
				std::optional<JSValue> DefaultValue() override;
				JSDeclaredType ApplyParameters(std::vector<JSDeclaredType> types, Nullability nullable) override;
				JSDeclaredType ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable) override;
				bool equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const override;
				void serialize(JS::Runtime::Debugger * debugger) override;
			};

			template <>
				class DeclaredTypeContent<Union> : public DeclaredTypeContentBase, public Union
			{
			public:
				DeclaredTypeContent(const Union& union_) : Union(union_) { }

				DeclaredTypeContentBase * clone() const override;
				std::optional<JSValue> Coerce(JSValue value, bool strict = true) override;
				bool Is(JSValue value) override;
				std::optional<JSValue> DefaultValue() override;
				JSDeclaredType ApplyParameters(std::vector<JSDeclaredType> types, Nullability nullable) override;
				JSDeclaredType ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable) override;
				bool equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const override;
				void serialize(JS::Runtime::Debugger * debugger) override;
			};

			template <>
				class DeclaredTypeContent<Record> : public DeclaredTypeContentBase, public Record
			{
			public:
				DeclaredTypeContent(const Record& record) : Record(record) { }

				DeclaredTypeContentBase * clone() const override;
				std::optional<JSValue> Coerce(JSValue value, bool strict = true) override;
				bool Is(JSValue value) override;
				std::optional<JSValue> DefaultValue() override;
				JSDeclaredType ApplyParameters(std::vector<JSDeclaredType> types, Nullability nullable) override;
				JSDeclaredType ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable) override;
				bool equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const override;
				void serialize(JS::Runtime::Debugger * debugger) override;
			};

			template <>
				class DeclaredTypeContent<Function> : public DeclaredTypeContentBase, public Function
			{
			public:
				DeclaredTypeContent(const Function& function) : Function(function) { }

				DeclaredTypeContentBase * clone() const override;
				std::optional<JSValue> Coerce(JSValue value, bool strict = true) override;
				bool Is(JSValue value) override;
				std::optional<JSValue> DefaultValue() override;
				JSDeclaredType ApplyParameters(std::vector<JSDeclaredType> types, Nullability nullable) override;
				JSDeclaredType ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable) override;
				bool equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const override;
				void serialize(JS::Runtime::Debugger * debugger) override;
			};

			template <>
				class DeclaredTypeContent<Function::TypeParameter> : public DeclaredTypeContentBase
			{
			public:
				JSString name;
				DeclaredTypeContent(JSString name) : name(name) { }

				DeclaredTypeContentBase * clone() const override;
				std::optional<JSValue> Coerce(JSValue value, bool strict = true) override;
				bool Is(JSValue value) override;
				std::optional<JSValue> DefaultValue() override;
				JSDeclaredType ApplyParameters(std::vector<JSDeclaredType> types, Nullability nullable) override;
				JSDeclaredType ReplaceParameters(std::map<JSString, JSDeclaredType> types, Nullability nullable) override;
				bool equal(DeclaredTypeContentBase * other, std::map<JSString, JSString> parameter_replacements) const override;
				void serialize(JS::Runtime::Debugger * debugger) override;
			};
		}
#endif

#if ENABLE_ES4
		// ES4
		struct MethodClosure
		{
			JSValue this_;
			JSInstanceFunction method;
			std::optional<JSString> name;

			MethodClosure(JSValue this_, JSInstanceFunction method, std::optional<JSString> name = {})
				: this_(this_), method(method), name(name)
			{
			}
		};
#endif
	}

	namespace Class
	{
		namespace Internal
		{
#if ENABLE_ES4
			template <>
				class Primitive<JSMethodClosure> : public JS::Class::Internal::PrimitiveValue
			{
			public:
				JSMethodClosure value;
				Primitive(JSMethodClosure value) : value(value) { }

				JSValue GetValue(Intent intent = Intent::Read AND_PHASE(phase = JS::Runtime::ExecutionPhase::Run)) override;
				bool SameValue(const JSValue& other) const override;
				JSValue ToPrimitive(JS::Runtime::TypeHint hint AND_PHASE(phase)) override;
				JSBoolean ToBoolean() override;
				JSNumber ToNumber(FOR_PHASE(phase)) override;
				JSBigInt ToBigInt() override;
				JSString ToString(FOR_PHASE(phase)) override;
				JSObject ToObject() override;
				JSString Typeof() override;
				JSString ToStringTag() override;
				JSType GetType() override;

				JSValue Get(JSValue property_key, JSValue receiver, Intent intent AND_NAMESPACES(namespaces) AND_CLASS(limit) AND_ENVIRONMENT(environment) AND_PHASE(phase)) override;

				Invocability GetInvocability() override;
				std::optional<JS::Runtime::SignatureType> GetSignature() override;
				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
				bool Begin_Call(JSValue this_argument, std::vector<JSValue> arguments) override;

				bool has_value(const std::type_info& type) const override;
				const void * get_if(const std::type_info& type) const override;
				void inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access) override;
#if ENABLE_DEBUG
				void serialize(JS::Runtime::Debugger * debugger, bool force_output = false) override;
#endif
			};
#endif
		}
	}

	struct Accessor
	{
		JS::Public::JSFunction Get;
		JS::Public::JSFunction Set;

		Accessor(JS::Public::JSFunction Get, JS::Public::JSFunction Set)
			: Get(Get), Set(Set)
		{
		}

		static inline Accessor Getter(JS::Public::JSFunction get)
		{
			return Accessor(get, nullptr);
		}

		static inline Accessor Setter(JS::Public::JSFunction set)
		{
			return Accessor(nullptr, set);
		}
	};

	static inline JS::Public::JSValue arguments_get_index(const std::vector<JS::Public::JSValue>& arguments, size_t index, JS::Public::JSValue default_value = JS::Public::JS_undefined)
	{
		return arguments.size() > index ? arguments[index] : default_value;
	}
}

//JS::Public::JSFunction::operator JS::Public::JSValue() const { return it; }

#endif // _VALUE_HH
