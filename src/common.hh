#ifndef _COMMON_HH
#define _COMMON_HH

/* Common types and definitions, including primitives and commonly used object types */

#include <cassert>
#include <cstdint>
#if ENABLE_DEC128
#include <decimal/decimal>
#endif
#include <filesystem>
#include <functional>
#include <gc_cpp.h>
#include <gc/gc_allocator.h>
#include <gmpxx.h>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#if __STDCPP_FLOAT16_T__ != 1
# ifdef __GNUC__
namespace std { typedef _Float16 float16_t; }
typedef std::float16_t float16_t;
//#  warning std::float16_t not supported
# else
#  error No 16-bit floating point data supported
# endif
#endif

#if __STDCPP_FLOAT32_T__ != 1
# ifdef __GNUC__
namespace std { typedef _Float32 float32_t; }
typedef std::float32_t float32_t;
//#  warning std::float32_t not supported
# else
namespace std { typedef float float32_t; }
typedef std::float32_t float32_t;
#  warning Using float for std::float32_t
# endif
#endif

#if __STDCPP_FLOAT64_T__ != 1
# ifdef __GNUC__
namespace std { typedef _Float64 float64_t; }
typedef std::float64_t float64_t;
//#  warning std::float64_t not supported
# else
namespace std { typedef double float64_t; }
typedef std::float64_t float64_t;
#  warning Using double for std::float64_t
# endif
#endif

#include <byteswap.h>

namespace std
{
	static inline uint16_t byteswap(uint16_t value)
	{
		return bswap_16(value);
	}

	static inline uint32_t byteswap(uint32_t value)
	{
		return bswap_32(value);
	}

	static inline uint64_t byteswap(uint64_t value)
	{
		return bswap_64(value);
	}
}

#include "macros.hh"

namespace gcn
{
	template <typename T>
		class gc_ptr
	{
	private:
		T * ptr;
	public:
		typedef std::remove_extent_t<T> element_type;

		constexpr gc_ptr() noexcept
			: ptr(nullptr)
		{
		}

		constexpr gc_ptr(std::nullptr_t) noexcept
			: ptr(nullptr)
		{
		}

		template <typename S>
			explicit gc_ptr(S * ptr)
			: ptr(dynamic_cast<T *>(ptr))
		{
			static_assert(std::is_convertible_v<S *, T *>);
		}

		template <typename S>
			gc_ptr(const gc_ptr<S>& ptr)
			: ptr(ptr.get())
		{
			static_assert(std::is_convertible_v<S *, T *>);
		}

		T * get() const noexcept { return ptr; }
		T * operator->() const noexcept { return get(); }
		T& operator *() const noexcept { return *get(); }
		explicit operator bool() const noexcept {  return get() != nullptr; }

		template <class U>
			inline bool operator ==(const gcn::gc_ptr<U>& rhs) const noexcept
		{
			return get() == rhs.get();
		}

		template <class U>
			inline bool operator !=(const gcn::gc_ptr<U>& rhs) const noexcept
		{
			return get() != rhs.get();
		}

		template <class U>
			inline bool operator <(const gcn::gc_ptr<U>& rhs) const noexcept
		{
			return get() < rhs.get();
		}

		template <class U>
			inline bool operator <=(const gcn::gc_ptr<U>& rhs) const noexcept
		{
			return get() <= rhs.get();
		}

		template <class U>
			inline bool operator >(const gcn::gc_ptr<U>& rhs) const noexcept
		{
			return get() > rhs.get();
		}

		template <class U>
			inline bool operator >=(const gcn::gc_ptr<U>& rhs) const noexcept
		{
			return get() >= rhs.get();
		}
	};

	template <typename T, typename ... Args>
		gc_ptr<T> make_gc(Args ... args)
	{
		if constexpr(std::is_base_of_v<gc, T>)
		{
			return gc_ptr<T>(new T(args...));
		}
		else
		{
			return gc_ptr<T>(new (GC) T(args...));
		}
	}

	template <typename T>
		class weak_gc_ptr
	{
	private:
		// the untraced container shadows the pointer so it does not get scanned by the garbage collector
		struct untraced
		{
		public:
			// the tracker gets called when the instance gets deallocated
			struct tracker : public gc_cleanup
			{
			public:
				untraced& untraced_ptr;
				T * ptr;
				tracker(untraced * untraced_ptr, T * ptr) : untraced_ptr(*untraced_ptr), ptr(ptr) { }
				~tracker() override { untraced_ptr.ptr = nullptr; }
			};

			volatile tracker * ptr;
			untraced(T * ptr = nullptr) : ptr(ptr ? new tracker(this, ptr) : nullptr) { }
			~untraced()
			{
				if(ptr)
					delete ptr;
				ptr = nullptr;
			}

			untraced& operator =(T * newptr)
			{
				if(newptr)
				{
					if(ptr)
						ptr->ptr = newptr;
					else
						ptr = new tracker(this, newptr);
				}
				else if(ptr)
				{
					delete ptr;
					ptr = nullptr;
				}
				return *this;
			}
		};
		untraced * ptr;
	public:
		typedef std::remove_extent_t<T> element_type;

		weak_gc_ptr()
			: ptr(new (PointerFreeGC) untraced())
		{
		}

		template <typename S>
			weak_gc_ptr(const gc_ptr<S>& ptr)
			: ptr(new (PointerFreeGC) untraced(dynamic_cast<T *>(ptr.get())))
		{
			static_assert(std::is_convertible_v<S *, T *>);
		}

		template <typename S>
			weak_gc_ptr(const weak_gc_ptr<S>& ptr)
			: ptr(new (PointerFreeGC) untraced(dynamic_cast<T *>(ptr.lock())))
		{
			static_assert(std::is_convertible_v<S *, T *>);
		}

		weak_gc_ptr(const weak_gc_ptr& ptr)
			: weak_gc_ptr(ptr.lock())
		{
		}

		weak_gc_ptr(weak_gc_ptr&& ptr)
			: weak_gc_ptr(ptr.ptr)
		{
			ptr.ptr = nullptr;
		}

		~weak_gc_ptr()
		{
			if(ptr != nullptr)
			{
				delete ptr;
			}
		}

		template <typename S>
			weak_gc_ptr& operator =(const gc_ptr<S>& ptr)
		{
			static_assert(std::is_convertible_v<S *, T *>);
			ptr = ptr->get();
			return *this;
		}

		template <typename S>
			weak_gc_ptr& operator =(const weak_gc_ptr<S>& ptr)
		{
			static_assert(std::is_convertible_v<S *, T *>);
			ptr = ptr->lock();
			return *this;
		}

		weak_gc_ptr& operator =(const weak_gc_ptr& ptr)
		{
			if(this != &ptr)
			{
				ptr = ptr->lock();
			}
			return *this;
		}

		weak_gc_ptr& operator =(weak_gc_ptr&& ptr)
		{
			ptr = ptr.ptr;
			ptr.ptr = nullptr;
			return *this;
		}

		gc_ptr<T> lock() const noexcept
		{
			auto tracker_ptr = ptr->ptr;
			if(tracker_ptr == nullptr)
			{
				return nullptr;
			}
			else
			{
				return gc_ptr<T>(tracker_ptr->ptr);
			}
		}

		explicit operator bool() const noexcept {  return lock() != nullptr; }
	};
}

#if 0
template <class T, class U>
	static inline bool operator ==(const gcn::gc_ptr<T>& lhs, const gcn::gc_ptr<U>& rhs) noexcept
{
	return lhs.get() == rhs.get();
}

template <class T, class U>
	static inline bool operator !=(const gcn::gc_ptr<T>& lhs, const gcn::gc_ptr<U>& rhs) noexcept
{
	return lhs.get() != rhs.get();
}

template <class T, class U>
	static inline bool operator <(const gcn::gc_ptr<T>& lhs, const gcn::gc_ptr<U>& rhs) noexcept
{
	return lhs.get() < rhs.get();
}

template <class T, class U>
	static inline bool operator <=(const gcn::gc_ptr<T>& lhs, const gcn::gc_ptr<U>& rhs) noexcept
{
	return lhs.get() <= rhs.get();
}

template <class T, class U>
	static inline bool operator >(const gcn::gc_ptr<T>& lhs, const gcn::gc_ptr<U>& rhs) noexcept
{
	return lhs.get() > rhs.get();
}

template <class T, class U>
	static inline bool operator >=(const gcn::gc_ptr<T>& lhs, const gcn::gc_ptr<U>& rhs) noexcept
{
	return lhs.get() >= rhs.get();
}
#endif

template <class T, class U>
	gcn::gc_ptr<T> std::static_pointer_cast(const gcn::gc_ptr<U>& r) noexcept
{
	return gcn::gc_ptr<T>(static_cast<T *>(r.get()));
}

template <class T, class U>
	gcn::gc_ptr<T> std::dynamic_pointer_cast(const gcn::gc_ptr<U>& r) noexcept
{
	return gcn::gc_ptr<T>(dynamic_cast<T *>(r.get()));
}

template <class T, class U>
	gcn::gc_ptr<T> std::const_pointer_cast(const gcn::gc_ptr<U>& r) noexcept
{
	return gcn::gc_ptr<T>(const_cast<T *>(r.get()));
}

template <class T, class U>
	gcn::gc_ptr<T> std::reinterpret_pointer_cast(const gcn::gc_ptr<U>& r) noexcept
{
	return gcn::gc_ptr<T>(reinterpret_cast<T *>(r.get()));
}

template <class T>
	struct std::allocator<gcn::gc_ptr<T>> : public traceable_allocator<gcn::gc_ptr<T>>
{
};

template <class T>
	struct std::allocator<gcn::weak_gc_ptr<T>> : public traceable_allocator<gcn::weak_gc_ptr<T>>
{
};

namespace JS
{
	template <typename T>
		struct HasIntrinsicClass
	{
		// https://stackoverflow.com/questions/257288/how-can-you-check-whether-a-templated-class-has-a-member-function
		struct DoesNotExist { char x[1]; };
		struct Exists { char x[2]; };

		template <typename C>
			static Exists test(decltype(&C::IntrinsicClass));
		template <typename>
			static DoesNotExist test(...);
		static constexpr bool Value = sizeof(test<T>(nullptr)) == sizeof(Exists);
	};

	enum class ComparisonResult // std::partial_ordering
	{
		Less,
		Equal,
		Greater,
		Unordered,
	};

	enum class Endian : bool
	{
		BigEndian = false,
		LittleEndian = true,
	};

	/** @brief Indicates whether the object can be called or instantiated with 'new' */
	enum class Invocability
	{
		/** @brief Object has neither [[Call]] nor [[Construct]] slots */
		NonCallable,
		/** @brief Object has a [[Call]] slot but no [[Construct]] slot */
		NonConstructor,
		/** @brief Object has both a [[Call]] and a [[Construct]] slot */
		Constructor,
	};

	/** @brief Required for E4X, whether access is for method call or getting the value */
	enum class Intent
	{
		/** @brief Lookup intended for reading attribute */
		Read,
		/** @brief Lookup intended to call method */
		Call,
	};

#if ENABLE_ES4
	/** @brief Required for ECMAScript 4, indicates whether a binding corresponds to reading or writing access */
	enum class Access
	{
		/** @brief This binding can be accessed on evaluation or calling */
		Read,
		/** @brief This binding can be accessed on assignment or deletion */
		Write,
	};

	/** @brief Required for ECMAScript 4, indicates whether a binding access happens for reading, writing or either */
	enum class AccessSet
	{
		/** @brief Used on evaluation and call, or defining a getter function */
		Read,
		/** @brief Used on assignment and deletion, or defining a setter function */
		Write,
		/** @brief Used when defining a variable */
		ReadWrite,
	};

	/** @brief Convert a read/write Access value to a singleton AccessSet */
	static inline constexpr AccessSet AsAccessSet(Access access)
	{
		return AccessSet(int(access));
	}

	/** @brief Enables iterating through the Access values of an AccessSet */
	struct Accesses
	{
		struct AccessIterator
		{
			int access;
			AccessIterator(int access) : access(access) { }
			AccessIterator(Access access) : access(int(access)) { }

			bool operator !=(AccessIterator other) const
			{
				return access != other.access;
			}

			void operator ++()
			{
				access ++;
			}

			Access operator *() const
			{
				return Access(access);
			}
		};

		AccessSet accesses;
		Accesses(AccessSet accesses) : accesses(accesses) { }

		typedef AccessIterator iterator;
		typedef AccessIterator const_iterator;

		AccessIterator begin() const
		{
			if(accesses == AccessSet::Write)
			{
				return AccessIterator(Access::Write);
			}
			else
			{
				return AccessIterator(Access::Read);
			}
		}

		AccessIterator end() const
		{
			if(accesses == AccessSet::Read)
			{
				return AccessIterator(int(Access::Read) + 1);
			}
			else
			{
				return AccessIterator(int(Access::Write) + 1);
			}
		}
	};
#endif

	/** @brief Types representing non-reference values */
	namespace Type
	{
		/* The root type of all objects and values, it can store references or primitive types */
		class Value;

		/* A type safe subtype of Value that guarantees that functions can be called.
		 * This includes the reference type Function and the value type MethodClosure */
		class Function;

		namespace Internal
		{
			/** @brief Placeholder for an undefined value, only used as a template parameter */
			struct Undefined { };

			/** @brief Placeholder for a null value, only used as a template parameter */
			struct Null { };
		}

		// XML, XMLList and External are handled as objects

		// E4X
		class AttributeName;

		// E4X
		/* A qualified name
		 * Unlike Class::QName, this is treated as a primitive value
		 */
		class QName;
	}

	/** @brief Types representing the layouts of JavaScript objects, typically reflecting the built-in class types
	 * Not all ECMAScript object types map directly to these, only when the internal representation or behavior requires special treatment
	 */
	namespace Class
	{
		/** @brief Reference types representing internal containers and structures, not exposed directly to the programmer but representing crucial concepts */
		namespace Internal
		{
			/* Represents the abstract root of all values, primitive or reference */
			class Value;
			/* Represents the abstract root of all value types (only used internally) */
			class PrimitiveValue;
			/* Abstract unified interface to handle primitive types */
			template <typename T>
				class Primitive;
		}

		/* Represents the abstract root of all reference values */
		class Object;

		/* Ordinary object type
		 * Different from JS::Class::Object in that all the methods contain their ordinary definitions.
		 */
		class OrdinaryObject;

		/* ECMAScript function object type */
		class Function;

		class BoundFunction;

		/* Boolean wrapper object type
		 * different from JSBoolean in that this is used for Boolean object references
		 */
		class Boolean;

		/* Number wrapper object type
		 * different from JSNumber in that this is used for Number object references
		 */
		class Number;

		/* String wrapper object type
		 * different from JSString in that this is used for String object references
		 */
		class String;

		// ES6
		/* Symbol wrapper object type
		 * different from JSSymbol in that this is used for Symbol object references
		 */
		class Symbol;

		// ES11
		/* BigInt wrapper object type
		 * different from JSBigInt in that this is used for BigInt object references
		 */
		class BigInt;

		/* ECMAScript Date object type */
		class Date;
		/* ECMAScript Array object type */
		class Array;

		// ES3
		/* ECMAScript Error object type */
		class Error;

		// ES3
		/* ECMAScript RegExp object type */
		class RegExp;

		/* The type of the Math object */
		class Math; // TODO: convert to Singleton

		// ES5
		/* The type of the JSON */
		class JSON; // TODO: convert to Singleton

		// E4X/ES4
		/* A namespace object, used to control property access */
		class Namespace;

		// E4X
		/* An XML qualified name */
		class QName;

		// E4X
		/* A single XML object */
		class XML;

		// E4X
		/* A sequence of XML objects */
		class XMLList;

#if ENABLE_ES4
		/* Represents an ES4/AS3 class */
		class Class;

		/* Represents an AS3 interface */
		class Interface;

		// ES4
		class Package;
#endif

		// ES6
		class ModuleNamespace;
		// ES6
		class Proxy;

		// ES6
		class Map;
		// ES6
		class Set;
		// ES6
		class WeakMap;
		// ES6
		class WeakSet;

		// ES6
		class ArrayBuffer;
		// ES6
		class DataView;

		// ES6
		class Promise;
		// ES6
		class Reflect; // TODO: remove

		// ES3
		/* Error subtypes, separated to make handling from C++ simpler */
		class EvalError;
		class RangeError;
		class ReferenceError;
		class SyntaxError;
		class TypeError;
		class URIError;
#if ENABLE_ES4
		class ArgumentError;
		class AttributeError;
		class ConstantError;
		class DefinitionError;
		class UninitializedError;
#endif

		/* An error class to represent exceptions caused by unexpected internal state or constraint breaking in the engine */
		class InternalError;

		// Node API
		/* External object, represented as an opaque non-extensible object to the user */
		class External;

		/* The type of global objects */
		class Global; // TODO: convert to Singleton

		// ES5
		/* The Arguments exotic object, already used by ES1 but specified in ES5 */
		class Arguments;

		// ES5
		class BoundFunction;

		// ES6
		class TypedArray;

		// ES6
		class Generator;
	}

	namespace Type
	{
		// ES4
		/* Custom collection of attributes */
		struct CompoundAttribute;

#if ENABLE_ES4
		// ES4
		/* Callable tuple with no internal state */
		struct MethodClosure;

		// ES4
		/* (Experimental) instance of a parametrized type */
		struct TypeApplication;

		namespace MetaType
		{
			/* A variant-like interface to manage types appearing in declarations (as opposed to the types of objects)
			 * Mostly required for structural types, only introduced in ES4.2008
			 */
			class DeclaredType;
		}
#endif
	}

	/* Values that may appear indirectly in evaluation but not exposed directly to the programmer */
	namespace Runtime
	{
		/* Abstract base for all environment records */
		class EnvironmentRecord;
		/* Contains bindings not associated with an object, the ECMAScript 4 Netscape proposal calls this a "LocalFrame" */
		class DeclarativeEnvironmentRecord;
		/* Contains bindings associated with an object, used with the 'with' statement and also for the GlobalEnvironmentRecord, the ECMAScript 4 Netscape proposal calls the first instance a "WithFrame" */
		class ObjectEnvironmentRecord;
		/* A specialized DeclarativeEnvironmentRecord to hold information related to the function call that produced it, the ECMAScript 4 cals this a "ParameterFrame" */
		class FunctionEnvironmentRecord;
		/* The root environment record which holds a reference to a global to store "var" and top level "function" bindings, and a declarative environment to store "let", "const" and "class" bindings
		 * It is also used to represent the ECMAScript 4 package frame to ensure that "this" references resolve properly, although the declarative environment part does not get used */
		class GlobalEnvironmentRecord;
		class ModuleEnvironmentRecord;
#if ENABLE_ES4
		/* A specialized GlobalEnvironmentRecord used for ECMAScript 4 to represent the environment record inside a class blocks */
		class ClassEnvironmentRecord;
#endif

		/* Unrelated to the EnvironmentRecord structure but fulfills a similar purpose to hold private bindings, introduced in ECMAScript 13 */
		class PrivateEnvironmentRecord;

		class LexicalReference;
		class MemberReference;
#if ENABLE_ES4
		class BracketReference;
#else
		typedef MemberReference BracketReference;
#endif
		class SuperReference;
		class PrivateReference;
	}

	/* Types related to the execution manager */
	namespace Interpreter
	{
		template <typename ... Types>
			class State;
		class Code;
		class Wrapper;
	}

	namespace Runtime
	{
		// ES6
		/* Represents a unique symbol instance, treated more or less as an opaque pointer */
		class SymbolInstance;

		// ES13
		/* Private name specific to some class */
		class PrivateName;

#if ENABLE_ES4
		/* Support structure to represent an identifier prefixed with a specific namespace */
		struct QualifiedName
		{
			gcn::gc_ptr<JS::Class::Namespace> Namespace;
			std::u16string Id;
			bool operator ==(const QualifiedName& other) const { return Namespace == other.Namespace && Id == other.Id; }
			bool operator <(const QualifiedName& other) const { return Namespace < other.Namespace || (Namespace == other.Namespace && Id < other.Id); }

			bool operator !=(const QualifiedName& other) const { return !(*this == other); }
		};
#endif

		/* An evaluated name, usually a simple string */
		class IdentifierReference;

		/* An element of an object */
		class Property;

		/* A descriptor used to update properties */
		class PropertyDescriptor;

#if !ENABLE_ES4
		/* An element of the scope */
		class Binding;
#endif

		// ES13
		/* A private element of an object */
		class PrivateElement;

#if ENABLE_ES4
		// ES4
		/* An element of a class that can only be used together with a class instance, with notable subtypes InstanceVariable and InstanceFunction */
		class InstanceProperty;
		/* A variable defined at the class level, with the actual class instances containing the associated values */
		class InstanceVariable;
		/* A function that operates on instances, may be a method, getter or setter */
		class InstanceFunction;
#endif

		/* A structure containing references to the intrinsic objects of a realm */
		class Intrinsics;
		class Realm;
		class Agent;

		class ScriptOrModuleRecord;
		class ScriptRecord;
		class SourceTextModuleRecord;
#if ENABLE_ES4
		class PackageRecord;
		class SourceTextPackageRecord;
#endif
		/* Required for ES6 module loading */
		struct GraphLoadingState;

		class Host;

		class ExecutionContext;

		struct ValidationContext;
		struct ReferenceContext;
		struct BindingContext;

		class CompletionRecord;

		/** @brief Extra parameter passed to ToPrimitive */
		enum class TypeHint
		{
			Default,
			String,
			Number,
		};

#if ENABLE_ES4
		/** @brief ECMAScript 4 permits evaluation of parse nodes before execution as constant expressions, this is called the Compile phase */
		enum class ExecutionPhase
		{
			Compile,
			Run,
			// TODO: add JSON?
		};
#endif

		/** @brief Specifies in what context and which declarations to look for
		 *
		 * When collecting declarations, this can occur according to several types modes
		 * Note that this value is specific to a stage in the evaluation, not the syntactic placement of an element
		 * The same element might behave differently in a Local or Hoisted mode
		 */
		enum class EnvironmentMode
		{
			/** @brief This is the top-level scope of a script or function
			 *
			 * All var/function and let/const/class declarations will be collected
			 */
			Regional,
			/** @brief We are collecting declarations for the current lexical scope
			 *
			 * Only function and let/const/class declarations will be collected
			 */
			Local,
			/** @brief This is an enclosed block and we are only looking for hoisted declarations
			 *
			 * All var declarations will be collected, and all function declarations will create a hoisted binding
			 */
			Hoisted,
			/** @brief Represents the top-level scope for a class
			 *
			 * In ECMAScript 4, var declarations are not hoisted
			 * In ECMAScript 6, this is used for validating class elements
			 */
			Class,
		};

		/** @brief The mode of a declaration, this is mostly relevant to ECMAScript 4 */
		enum class EntityMode
		{
			/** @brief The mode is uninitialized, we cannot rely on it */
			Unknown,
			/** @brief The declaration is hoisted
			 *
			 * In ECMAScript 4, this is the mode of plain var and function declarations in non-strict mode
			 * In other versions, this means the declaration binds to the variable environment, used for var declarations
			 */
			Hoisted,
			/** @brief The declaration is bound lexically
			 *
			 * In ECMAScript 4, this is the mode of non-plain var and function declarations in non-strict mode,
			 * all var and function declarations in strict mode, all const declarations, all non-virtual/final members of a class, and in
			 * In other versions, this means the declaration binds to the lexical environment, used for function/let/const/class declarations
			 */
			Singleton, // or Lexical
#if ENABLE_ES4
			/** @brief ECMAScript 4 only, the declaration belongs to instances of a class, signifying instance properties */
			Instance,
#endif
		};

#if ENABLE_ES4
		enum class MemberModifier
		{
			None,
			Static,
			Virtual,
			Final,
			//Constructor,
			Abstract,
		};

		enum class OverrideModifier
		{
			None,
			True,
			False,
			Undefined,
		};
#endif

#if ENABLE_ES4
		struct SignatureType;
#endif

		template <class C = JS::Class::Object>
			class PromiseCapability;

		namespace Builtin
		{
			class BuiltinFunction;
			class NativeFunction;
		}

#if ENABLE_DEBUG
		/* Used to serialize objects as XML structures during debugging */
		class Debugger;
#endif
	}

	namespace ParseNode
	{
		/* An abstract syntax tree that can be evaluated or compiled */
		class ParseNode;

		enum class DeclarationKind
		{
			None, // unknown or assignment
			Var,
			Const,
			Let,
		};

		class FunctionalElement;
		class FunctionDefinition;
	}

	namespace Parser
	{
		/** Represents a location within some source file */
		struct Location
		{
		public:
			std::optional<std::filesystem::path> filename;
			size_t line = 1;
			size_t column = 1;
			size_t offset = 0;

			std::u16string tostring();

			bool operator ==(const Location& other)
			{
				return filename == other.filename && line == other.line && offset == other.offset;
			}
		};

		/** Used to keep track of the source location during the compilation stage
		 *
		 * When instantiated, it captures the current location and replaces it with the one
		 * provided to the initializer. On exit, the previous location is restored.
		 */
		class SourceLocationContext
		{
		public:
			/** The current source location during compilation
			 *
			 * This variable is accessed when creating new Instruction instances.
			 */
			static Location CurrentLocation;

			std::optional<Location> RestoreState;

			SourceLocationContext(Location location)
				: RestoreState(CurrentLocation)
			{
				CurrentLocation = location;
			}

			~SourceLocationContext()
			{
				if(RestoreState.has_value())
				{
					CurrentLocation = RestoreState.value();
				}
			}
		};
	}

	/* Definitions intended to be used without namespace prefixes */
	namespace Public
	{
		/* The undefined value, cast to the Value type */
		extern ::JS::Type::Value JS_undefined;
		/* The null value, cast to the Value type */
		extern ::JS::Type::Value JS_null;
#if ENABLE_ES4
		/* The universal public namespace */
		extern gcn::gc_ptr<::JS::Class::Namespace> JS_public;
		extern gcn::gc_ptr<::JS::Class::Namespace> JS_meta;
#endif

		typedef JS::Type::Value JSValue;
#if ENABLE_ES4
		typedef JS::Type::MetaType::DeclaredType JSDeclaredType;
#endif

		/* ECMAScript booleans */
		typedef bool JSBoolean;
		/* ECMAScript 64-bit double precision numbers */
		typedef double JSNumber;
		/* ECMAScript UTF-16 encoded strings */
		typedef std::u16string JSString;
		// ES6
		/* ECMAScript symbol instances */
		typedef JS::Runtime::SymbolInstance * JSSymbol;
		// ES11
		/* ECMAScript arbitrary precision integer */
		typedef mpz_class JSBigInt;

		/* Represents objects that are callable, which includes the reference type Function and the value type MethodClosure */
		typedef JS::Type::Function JSFunction;

		/* ECMAScript objects */
		typedef JS::Class::Object JSObjectValue;
		typedef gcn::gc_ptr<JS::Class::Object> JSObject;
		typedef Class::OrdinaryObject * JSOrdinaryObject;
		/* ECMAScript functions */
		typedef JS::Class::Function * JSOrdinaryFunction;
#if ENABLE_ES4
		typedef gcn::gc_ptr<JS::Class::Package> JSPackage;

		/* ECMAScript 4 class type */
		typedef gcn::gc_ptr<JS::Class::Class> JSClass;
		/* ActionScript 3 interface type */
		typedef JS::Class::Interface * JSInterface;
#endif
		typedef JS::Class::Error * JSError;

#if ENABLE_ES4
		// ES4 only
		/* 64-bit signed integer */
		typedef int64_t JSLong;
		/* 64-bit unsigned integer */
		typedef uint64_t JSUlong;
		/* 32-bit single precision value */
		typedef float JSFloat;
#if ENABLE_DEC128
		/* IEEE 128-bit decimal floating point */
		typedef std::decimal::decimal128 JSDecimal;
#endif
		/* An UCS-2 codepoint */
		typedef char16_t JSChar;
		/* Represents an instance method of a class bound to an object instance, this is a value type */
		typedef JS::Type::MethodClosure JSMethodClosure;
		/* Represents a combination of property attributes, this is a value type */
		typedef JS::Type::CompoundAttribute JSCompoundAttribute;
		/* Represents a class with type parameters */
		typedef JS::Type::TypeApplication JSTypeApplication;
		/* Represents the type of an object, which belongs to a specific class and possibly uses type parameters */
		typedef std::variant<JSClass, JSTypeApplication> JSType;
#endif

		// E4X and ES4
		/* Represents a namespace that controls property visibility and access */
		typedef gcn::gc_ptr<JS::Class::Namespace> JSNamespace;

		typedef JS::Type::QName JSQName;

		typedef JS::Class::External * JSExternal;
		typedef JS::Class::XML * JSXML;
		typedef JS::Class::XMLList * JSXMLList;
		typedef JS::Type::AttributeName JSAttributeName;

#if ENABLE_ES4
		typedef JS::Runtime::QualifiedName JSQualifiedName;
#endif
		// properties are accessed as either the string representing its name or a symbol
		typedef std::variant<JSString, JSSymbol> JSPropertyKey;
#if ENABLE_ES4
		typedef JSQualifiedName JSPropertyName;
#else
		typedef JSString JSPropertyName;
#endif
		// property IDs provide a more convenient way to access qualified names
		// these are not used directly by the internal functions, except for debugging routines
		typedef std::variant<JSPropertyName, JSSymbol> JSPropertyId;
		typedef JS::Runtime::IdentifierReference JSIdentifierReference;
		typedef JS::Runtime::Property JSProperty;
		typedef JS::Runtime::PropertyDescriptor JSPropertyDescriptor;
#if ENABLE_ES4
		typedef JS::Runtime::Property JSBinding;
#else
		typedef JS::Runtime::Binding JSBinding;
#endif
		typedef JS::Runtime::PrivateName * JSPrivateName;
		typedef JS::Runtime::PrivateElement JSPrivateElement;
		typedef std::variant<JSPropertyKey, JSPrivateName> JSBindingName;
#if ENABLE_ES4
		typedef JS::Runtime::InstanceProperty * JSInstanceProperty;
		typedef JS::Runtime::InstanceVariable * JSInstanceVariable;
		typedef JS::Runtime::InstanceFunction * JSInstanceFunction;
#endif

		/** Typedefs for the error subtypes, separated to make handling from C++ simpler */
		namespace ErrorClasses
		{
			typedef JS::Class::Error Error;
			typedef JS::Class::EvalError EvalError;
			typedef JS::Class::RangeError RangeError;
			typedef JS::Class::ReferenceError ReferenceError;
			typedef JS::Class::SyntaxError SyntaxError;
			typedef JS::Class::TypeError TypeError;
			typedef JS::Class::URIError URIError;
#if ENABLE_ES4
			typedef JS::Class::ArgumentError ArgumentError;
			typedef JS::Class::AttributeError AttributeError;
			typedef JS::Class::ConstantError ConstantError;
			typedef JS::Class::DefinitionError DefinitionError;
			typedef JS::Class::UninitializedError UninitializedError;
#endif
			typedef JS::Class::InternalError InternalError;
		}
	}

	namespace Type
	{
		// E4X
		/** @brief A qualified name, used by E4X, as a primitive value
		 *
		 * This represents a name potentially prefixed by an URI string.
		 * It is used to access parts of an XML structure.
		 *
		 * Unlike Class::QName, this is treated as a primitive value
		 */
		class QName
		{
		public:
			// internal slots

			/** @brief The internal slot [[Prefix]] */
			std::optional<JS::Public::JSString> Prefix;

			// properties

			// ES4.2008 Name class field identifier
			/** @brief The property localName */
			JS::Public::JSString localName;
			/** @brief The property uri */
			std::optional<JS::Public::JSString> uri;

			QName(const QName& other)
				: Prefix(other.Prefix), uri(other.uri), localName(other.localName)
			{
			}

			QName(JS::Public::JSString uri, JS::Public::JSString localName, std::optional<JS::Public::JSString> prefix = {})
				: Prefix(prefix), uri(uri), localName(localName)
			{
			}

			QName(JS::Public::JSString localName)
				: Prefix(), uri(), localName(localName)
			{
			}

			QName()
				: Prefix(), uri(), localName(u"")
			{
			}

			bool operator ==(const QName& other) const
			{
				return Prefix == other.Prefix && uri == other.uri && localName == other.localName;
			}

			bool operator !=(const QName& other) const
			{
				return !(*this == other);
			}

			bool operator <(const QName& other) const
			{
				return Prefix < other.Prefix
					|| (Prefix == other.Prefix && uri < other.uri)
					|| (Prefix == other.Prefix && uri == other.uri && localName < other.localName);
			}

			static bool IsXMLName(const QName& qname);
			static bool IsXMLName(JS::Public::JSString name);
			static bool IsXMLName(JS::Public::JSValue name);

			JS::Public::JSNamespace GetNamespace(std::map<JS::Public::JSString, JS::Public::JSNamespace> InScopeNamespaces);

			JS::Public::JSString ToString() const
			{
				return uri.value_or(u"*") + u"::" + localName;
			}
		};

		// E4X
		/** @brief Represents an intermediate result that is an XML attribute name, needed for E4X, not exposed to the user */
		class AttributeName
		{
		public:
			JS::Type::QName Name;
			AttributeName(JS::Type::QName Name) : Name(Name) { }
		};
	}

	namespace Symbols
	{
		extern const JS::Public::JSSymbol hasInstance;
		extern const JS::Public::JSSymbol isConcatSpreadable;
		extern const JS::Public::JSSymbol iterator;
		extern const JS::Public::JSSymbol match;
		extern const JS::Public::JSSymbol replace;
		extern const JS::Public::JSSymbol search;
		extern const JS::Public::JSSymbol species;
		extern const JS::Public::JSSymbol split;
		extern const JS::Public::JSSymbol toPrimitive;
		extern const JS::Public::JSSymbol toStringTag;
		extern const JS::Public::JSSymbol unscopables;

		// ESNext
		extern const JS::Public::JSSymbol asyncIterator;
		// ESNext
		extern const JS::Public::JSSymbol matchAll;
	}

	enum class Version
	{
		JS1_0, // TODO
		JS1_1, // TODO
		ES1,
		ES2, // TODO
		ES3,
#if ENABLE_ES4
		ES4_2003, // ECMAScript 4 Netscape proposal from 2003-06-30, with some additional features from ActionScript 3 and JScript .NET
		ES4_2008, // TODO
#endif
		ES5,
		ES5_1, // TODO
		ES6,
		// not distinguished currently
		ES7,
		ES8,
		ES9,
		ES10,
		ES11,
		ES12,
		ES13,
		ES14,
		ES15,
		ES16,
		ESNEXT, // TODO (partial ES16)
	};
	extern std::string VersionNames[];

	extern Version SelectedVersion;
	extern Version CurrentVersion;
	extern bool StrictMode;
	extern bool Enable_E4X;
	extern bool Enable_LegacyOctals;
#if ENABLE_DEBUG
	extern bool Enable_Debug;
#endif
	extern bool Enable_CommonJS;
	extern bool Enable_AMD;
	extern bool Enable_WebExtensions; // TODO

	extern JS::Runtime::Host * Host;

#if ENABLE_ES4
	static inline bool IsVersionES4()
	{
		return CurrentVersion == Version::ES4_2003 || CurrentVersion == Version::ES4_2008;
	}
#endif

	static inline bool Supports_E4X()
	{
		if(CurrentVersion < Version::ES3)
		{
			return false;
		}
		else
		{
//#if ENABLE_ES4
//			return Enable_E4X || IsVersionES4();
//#else
		return Enable_E4X;
//#endif
		}
	}

	static inline bool Supports_LegacyOctals()
	{
		return CurrentVersion == Version::ES1 || Version::ESNEXT <= CurrentVersion || Enable_LegacyOctals;
	}

	static inline bool Supports_WebExtensions()
	{
		return Enable_WebExtensions;
	}

	//JS::Public::JSPropertyId asPropertyId(JS::Public::JSString name);
	bool is_array_index(JS::Public::JSString string, uint32_t& result);

	bool IsUndefined(JS::Public::JSValue value);
	bool IsNull(JS::Public::JSValue value);
	bool IsPrimitive(JS::Public::JSValue value);
	std::optional<JS::Public::JSBoolean> GetBoolean(JS::Public::JSValue value);
	std::optional<JS::Public::JSNumber> GetNumber(JS::Public::JSValue value);
	std::optional<JS::Public::JSString> GetString(JS::Public::JSValue value);
	std::optional<JS::Public::JSSymbol> GetSymbol(JS::Public::JSValue value);
	std::optional<JS::Public::JSBigInt> GetBigInt(JS::Public::JSValue value);
	std::optional<JS::Public::JSObject> GetObjectOrNull(JS::Public::JSValue value);
	std::optional<JS::Public::JSObject> GetNonNullObject(JS::Public::JSValue value);
#if ENABLE_ES4
	std::optional<JS::Public::JSLong> GetLong(JS::Public::JSValue value);
	std::optional<JS::Public::JSUlong> GetUlong(JS::Public::JSValue value);
	std::optional<JS::Public::JSFloat> GetFloat(JS::Public::JSValue value);
	std::optional<JS::Public::JSDecimal> GetDecimal(JS::Public::JSValue value);
	std::optional<JS::Public::JSMethodClosure> GetMethodClosure(JS::Public::JSValue value);
#endif
	std::optional<JS::Public::JSFunction> GetFunction(JS::Public::JSValue value);
	std::optional<JS::Public::JSFunction> GetConstructor(JS::Public::JSValue value);
	bool IsCallable(JS::Public::JSValue value);
	bool IsConstructor(JS::Public::JSValue value);
	JS::Public::JSValue Call(JS::Public::JSFunction callable, JS::Public::JSValue this_argument, std::vector<JS::Public::JSValue> arguments AND_PHASE(phase));
	bool Begin_Call(JS::Public::JSFunction callable, JS::Public::JSValue this_argument, std::vector<JS::Public::JSValue> arguments);
	JS::Public::JSValue Construct(JS::Public::JSFunction callable, std::vector<JS::Public::JSValue> arguments AND_PHASE(phase));
	JS::Public::JSValue Construct(JS::Public::JSFunction callable, std::vector<JS::Public::JSValue> arguments, JS::Public::JSFunction new_target AND_PHASE(phase));
#if ENABLE_ES4
	std::optional<JS::Public::JSClass> GetClass(JS::Public::JSValue value);
	std::optional<JS::Public::JSType> GetType(JS::Public::JSValue value);
#endif
	std::optional<JS::Public::JSFunction> GetMethod(JS::Public::JSValue value, JS::Public::JSPropertyKey property_key);
	std::optional<JS::Public::JSPropertyKey> GetPropertyKey(JS::Public::JSValue value);
	JS::Public::JSPropertyKey assert_property_key(JS::Public::JSValue property_key_value);

	bool SameValue(JS::Public::JSValue a, JS::Public::JSValue b);
	bool IsStrictlyEqual(JS::Public::JSValue a, JS::Public::JSValue b);

	// needed for Map and Set
	bool operator ==(JS::Public::JSValue a, JS::Public::JSValue b);
	bool operator !=(JS::Public::JSValue a, JS::Public::JSValue b);

	bool SameType(JS::Public::JSValue a, JS::Public::JSValue b);

#if ENABLE_ES4
	bool IsInteger(JS::Public::JSValue arg);
	bool IsZero(JS::Public::JSValue arg);
	JS::Public::JSValue GetInteger(JS::Public::JSValue arg);
#endif

	JS::Public::JSValue ToPrimitive(JS::Public::JSValue arg AND_PHASE(phase), Runtime::TypeHint hint = Runtime::TypeHint::Default);
	JS::Public::JSBoolean ToBoolean(JS::Public::JSValue arg);
	JS::Public::JSNumber ToNumber(JS::Public::JSValue arg AND_PHASE(phase));
	// also used for ES4 ToGeneralNumber
	JS::Public::JSValue ToNumeric(JS::Public::JSValue arg AND_PHASE(phase));
	JS::Public::JSNumber ToInteger(JS::Public::JSValue arg AND_PHASE(phase));
	int8_t ToInt8(JS::Public::JSValue arg);
	uint8_t ToUint8(JS::Public::JSValue arg);
	uint8_t ToUint8Clamp(JS::Public::JSValue arg);
	int16_t ToInt16(JS::Public::JSValue arg);
	uint16_t ToUint16(JS::Public::JSValue arg);
	int32_t ToInt32(JS::Public::JSValue arg AND_PHASE(phase));
	uint32_t ToUint32(JS::Public::JSValue arg AND_PHASE(phase));
#if ENABLE_ES4
	int64_t ToInt64(JS::Public::JSValue arg AND_PHASE(phase));
	uint64_t ToUint64(JS::Public::JSValue arg AND_PHASE(phase));
#endif
	int64_t ToBigInt64(JS::Public::JSValue arg);
	uint64_t ToBigUint64(JS::Public::JSValue arg);
	intptr_t ToIndex(JS::Public::JSValue arg);
	uintptr_t ToLength(JS::Public::JSValue arg);
	JS::Public::JSBigInt ToBigInt(JS::Public::JSValue arg);
	JS::Public::JSString ToString(JS::Public::JSValue arg AND_PHASE(phase));
	JS::Public::JSString UInt64ToString(uint64_t value, int radix = 10);
	JS::Public::JSString NumberToString(JS::Public::JSNumber value, int radix = 10);
	JS::Public::JSObject ToObject(JS::Public::JSValue arg);
	JS::Public::JSString TypeOf(JS::Public::JSValue arg);
	JS::Public::JSString ToStringTag(JS::Public::JSValue arg);
	std::optional<JS::Public::JSNumber> CanonicalNumericIndexString(JS::Public::JSString argument);

	JS::Public::JSValue Get(JS::Public::JSValue object, JS::Public::JSString string);
	JS::Public::JSValue Get(JS::Public::JSValue object, JS::Public::JSSymbol symbol);
	JS::Public::JSValue GetByKey(JS::Public::JSValue object, JS::Public::JSPropertyKey property_key);
	JS::Public::JSProperty GetOwnPropertyByKey(JS::Public::JSObject object, JS::Public::JSPropertyKey property_key);
#if ENABLE_ES4
	JS::Public::JSValue Get(JS::Public::JSValue object, JS::Public::JSQualifiedName qualified_name);
	JS::Public::JSValue GetById(JS::Public::JSValue object, JS::Public::JSPropertyId property_id);

	std::optional<JS::Public::JSProperty> HasOwnPropertyById(JS::Public::JSObject object, JS::Public::JSPropertyId property_id);
	JS::Public::JSProperty GetOwnPropertyById(JS::Public::JSObject object, JS::Public::JSPropertyId property_id);
#endif
	JS::Runtime::Realm * GetFunctionRealm(JS::Public::JSValue value);

	struct PropertyAttributes
	{
		bool Writable = true;
		bool Enumerable = true;
		bool Configurable = true;
#if ENABLE_ES4
		bool Explicit = false;
#endif

		PropertyAttributes WithWritable(bool Writable = true) const { PropertyAttributes copy = *this; copy.Writable = Writable; return copy; }
#if ENABLE_ES4
		PropertyAttributes WithConst(bool Const = true) const { PropertyAttributes copy = *this; copy.Writable = !Const; return copy; }
#endif
		PropertyAttributes WithEnumerable(bool Enumerable = true) const { PropertyAttributes copy = *this; copy.Enumerable = Enumerable; return copy; }
		PropertyAttributes WithConfigurable(bool Configurable = true) const { PropertyAttributes copy = *this; copy.Configurable = Configurable; return copy; }
#if ENABLE_ES4
		PropertyAttributes WithSealed(bool Sealed = true) const { PropertyAttributes copy = *this; copy.Configurable = !Sealed; return copy; }
#endif
#if ENABLE_ES4
		PropertyAttributes WithExplicit(bool Explicit = true) const { PropertyAttributes copy = *this; copy.Explicit = Explicit; return copy; }
#endif
	};

	struct Accessor;

	bool Set(JS::Public::JSValue object, JS::Public::JSString string, JS::Public::JSValue value);
	bool Set(JS::Public::JSValue object, JS::Public::JSSymbol symbol, JS::Public::JSValue value);
	bool SetByKey(JS::Public::JSValue object, JS::Public::JSPropertyKey property_key, JS::Public::JSValue value);
#if ENABLE_ES4
	bool Set(JS::Public::JSValue object, JS::Public::JSQualifiedName qualified_name, JS::Public::JSValue value);
#if 0
	bool Set(JS::Public::JSValue object, JS::Public::JSQualifiedName qualified_name, JS::Public::JSDeclaredType type, JS::Public::JSValue value, bool is_const = true, bool is_explicit = false);
	bool Set(JS::Public::JSValue object, JS::Public::JSString name, JS::Public::JSDeclaredType type, JS::Public::JSValue value, bool is_const = true, bool is_explicit = false);
#endif
	bool SetById(JS::Public::JSValue object, JS::Public::JSPropertyId property_id, JS::Public::JSValue value);
#endif

	bool Define(JS::Public::JSValue object, JS::Public::JSString string, JS::Public::JSValue value, PropertyAttributes attributes = PropertyAttributes{});
	bool Define(JS::Public::JSValue object, JS::Public::JSSymbol symbol, JS::Public::JSValue value, PropertyAttributes attributes = PropertyAttributes{});
	bool DefineKey(JS::Public::JSValue object, JS::Public::JSPropertyKey property_key, JS::Public::JSValue value, PropertyAttributes attributes = PropertyAttributes{});
#if ENABLE_ES4
	bool Define(JS::Public::JSValue object, JS::Public::JSQualifiedName qualified_name, JS::Public::JSValue value, PropertyAttributes attributes = PropertyAttributes{});
	bool Define(JS::Public::JSValue object, JS::Public::JSQualifiedName qualified_name, JS::Public::JSDeclaredType type, JS::Public::JSValue value, PropertyAttributes attributes = PropertyAttributes{});
	bool Define(JS::Public::JSValue object, JS::Public::JSString name, JS::Public::JSDeclaredType type, JS::Public::JSValue value, PropertyAttributes attributes = PropertyAttributes{});
	bool DefineId(JS::Public::JSValue object, JS::Public::JSPropertyId property_id, JS::Public::JSValue value, PropertyAttributes attributes = PropertyAttributes{});
#endif

	bool Define(JS::Public::JSValue object, JS::Public::JSString string, Accessor accessor, PropertyAttributes attributes = PropertyAttributes{});
	bool Define(JS::Public::JSValue object, JS::Public::JSSymbol symbol, Accessor accessor, PropertyAttributes attributes = PropertyAttributes{});
	bool DefineKey(JS::Public::JSValue object, JS::Public::JSPropertyKey property_key, Accessor accessor, PropertyAttributes attributes = PropertyAttributes{});
#if ENABLE_ES4
	bool Define(JS::Public::JSValue object, JS::Public::JSQualifiedName qualified_name, Accessor accessor, PropertyAttributes attributes = PropertyAttributes{});
	//bool Define(JS::Public::JSValue object, JS::Public::JSQualifiedName qualified_name, JS::Public::JSDeclaredType type, JS::Public::JSValue value, PropertyAttributes attributes = PropertyAttributes{});
	//bool Define(JS::Public::JSValue object, JS::Public::JSString name, JS::Public::JSDeclaredType type, JS::Public::JSValue value, PropertyAttributes attributes = PropertyAttributes{});
	//bool DefineId(JS::Public::JSValue object, JS::Public::JSPropertyId property_id, Accessor accessor, PropertyAttributes attributes = PropertyAttributes{});
#endif

	void RequireObjectCoercible(JS::Public::JSValue argument);
	bool CreateDataProperty(JS::Public::JSObject O, JS::Public::JSPropertyKey P, JS::Public::JSValue V);
	void CreateDataPropertyOrThrow(JS::Public::JSObject O, JS::Public::JSPropertyKey P, JS::Public::JSValue V);

	bool ProcessDirective(JS::ParseNode::ParseNode * statement);
	void ProcessDirectives(std::vector<JS::ParseNode::ParseNode *>& statements);

//	Class::OrdinaryObject * ObjectCreate(JSObject proto AND_CLASS(class_));

	namespace Runtime
	{
		typedef JS::Type::Value BuiltinFunction(std::optional<JS::Type::Value> this_argument, std::vector<JS::Type::Value> arguments, JS::Public::JSFunction new_target AND_PHASE(phase));

		typedef JS::Interpreter::State<JS::Type::Value> PrecompiledFunction(JS::Interpreter::State<> initial_state, std::optional<JS::Type::Value> this_argument, std::vector<JS::Type::Value> arguments, JS::Public::JSFunction new_target);
	}

	/** Takes a callable C++ object and returns a (possibly constructor) function object */
	gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> CreateBuiltinFunction(std::function<JS::Runtime::BuiltinFunction> function, uint32_t length, JS::Public::JSString name, std::optional<JS::Public::JSObject> prototype, JS::Runtime::Realm * realm, std::optional<JS::Public::JSObject> function_prototype = {}, bool has_constructor_method = false);
	/** Takes a C++ code generator functions and generates the code to return a (possibly constructor) function object */
	gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> CreateBuiltinFunction(std::function<JS::Runtime::PrecompiledFunction> function, uint32_t length, JS::Public::JSString name, std::optional<JS::Public::JSObject> prototype, JS::Runtime::Realm * realm, std::optional<JS::Public::JSObject> function_prototype = {}, bool has_constructor_method = false);
	/** Creates a simple non-constructor function object */
	gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> CreateBuiltinFunction(std::function<JS::Runtime::BuiltinFunction> function, uint32_t length, JS::Public::JSString name, JS::Runtime::Realm * realm);
	/** Creates a simple constructor function object */
	gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> CreateBuiltinConstructor(std::function<JS::Runtime::BuiltinFunction> function, uint32_t length, JS::Public::JSString name, std::optional<JS::Public::JSObject> prototype, JS::Runtime::Realm * realm, std::optional<JS::Public::JSObject> function_prototype = {});

	JS::Public::JSSymbol NewSymbol();
	JS::Public::JSSymbol NewSymbol(JS::Public::JSString description);
	JS::Public::JSString ObjectToString(JS::Public::JSValue value AND_PHASE(phase));
#if ENABLE_ES4
	JS::Public::JSString GetTypeNameString(JS::Public::JSType value);
	JS::Public::JSClass GetClassForType(JS::Public::JSType type);
#endif
	JS::Public::JSObject CreateIteratorResultObject(JS::Public::JSValue value, bool done);

	JS::Public::JSObject FromPropertyDescriptor(JS::Public::JSPropertyDescriptor descriptor);
	JS::Public::JSPropertyDescriptor ToPropertyDescriptor(JS::Public::JSValue value);

	JS::Public::JSPropertyKey ToPropertyKey(JS::Public::JSValue value AND_PHASE(phase));

	Runtime::Agent * GetSurroundingAgent();
	Runtime::ExecutionContext * GetCurrentExecutionContext();
	Runtime::Realm * GetCurrentRealm();
	Runtime::Intrinsics * Intrinsics();
	Runtime::ScriptOrModuleRecord * GetActiveScriptOrModule();
	JS::Public::JSObject GetGlobalObject();

	[[noreturn]] void Throw(JS::Public::JSValue throwable);
	template <typename T>
		[[noreturn]] void Throw(gcn::gc_ptr<T> throwable)
	{
		throw throwable.get();
	}

	// performs a .then operation, the first half of an await instruction
	void Await(JS::Public::JSValue value);

	// E4X
	JS::Public::JSString EscapeElementValue(JS::Public::JSString string);
	JS::Public::JSString EscapeAttributeValue(JS::Public::JSString string);
}

static inline std::ostream& operator<<(std::ostream& out, const std::u8string s)
{
	return out << std::string(s.begin(), s.end());
}

#undef __ES4_ONLY
#undef AND_ACCESS_SET
#undef WITH_ACCESS_SET
#undef AND_NAMESPACE
#undef WITH_NAMESPACE
#undef AND_NAMESPACES
#undef WITH_NAMESPACES
#undef AND_CLASS
#undef WITH_CLASS
#undef FOR_CLASS
#undef VIA_CLASS
#undef FOR_PHASE
#undef VIA_PHASE
#undef AND_PHASE
#undef WITH_PHASE
#undef AND_ENVIRONMENT
#undef WITH_ENVIRONMENT
#undef INDEXES
#undef WITH_TYPE
#undef AND_FLAG
#undef WITH_FLAG

#undef PUBLIC_NAMESPACE
#undef PUBLIC_NAMESPACES
#undef READ_ACCESS
#undef WRITE_ACCESS
#undef READ_WRITE_ACCESS
#undef RUNTIME_PHASE
#undef VIA_RUNTIME_PHASE
#undef COMPILE_TIME_PHASE
#undef VIA_COMPILE_TIME_PHASE
#undef NO_ENVIRONMENT

#undef __STR2
#undef __STR
#undef __LOC

#undef _MACROS_HH // allow reincluding

#endif // _COMMON_HH
