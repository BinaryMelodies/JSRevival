#ifndef _NODE_HH
#define _NODE_HH

/* Parse nodes, representing the abstract syntax tree */

#include <gc_cpp.h>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "common.hh"
#include "runtime.hh"
#include "execute.hh"

namespace JS
{
	using namespace Public;

	namespace ParseNode
	{
		using Location = JS::Parser::Location;

		class ParseNode : public gc_cleanup
		{
		public:
			Location location;

			ParseNode(Location location) : location(location) { }

			virtual void serialize([[maybe_unused]] std::ostream& out) const { }
			virtual void Validate(JS::Runtime::ValidationContext cxt);
#if ENABLE_ES4
			virtual JSType EvaluateType();
#endif
			JSValue EvaluateExpression(FOR_PHASE(phase));
			virtual JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase));
			virtual JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state);
#if ENABLE_ES4
			JSIdentifierReference EvaluateName(FOR_PHASE(phase));
#endif
			virtual JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase));
#if ENABLE_ES4
			virtual void SetupContent(JS::Runtime::EnvironmentMode mode);
			virtual void SetupDeclarationContent(JS::Runtime::EnvironmentMode mode);
#endif
			virtual bool CollectBoundNames(std::multiset<JSString>& names);
			virtual bool ContainsInitialization();

			// *Binding is the common routine for BindingInitialization and DestructuringAssignment

			// called to finish initialization
			void EvaluateBinding(JS::Runtime::BindingContext cxt);
			// used only in var/let/const statements
			virtual JS::Interpreter::State<> CompileInitialization(JS::Interpreter::State<>& initial_state);
			virtual JS::Interpreter::State<> CompileBinding(JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state);

			void EvaluateIteratorBinding(JS::Runtime::IteratorBindingContext cxt);
			virtual JS::Interpreter::State<> CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state);

			void EvaluatePropertyBinding(JS::Runtime::PropertyBindingContext cxt);
			virtual JS::Interpreter::State<> CompilePropertyBinding(JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state);

			void EvaluateKeyedBinding(JS::Runtime::KeyedBindingContext cxt);
			virtual JS::Interpreter::State<> CompileKeyedBinding(JS::Interpreter::State<JS::Runtime::KeyedBindingContext>& binding_state);

			// called to create bindings
			virtual void CreateBindings(JS::Runtime::EnvironmentMode mode);
			// called to create bindings corresponding to the declarators within a var/const/let statement
			// if as_clause_parameter is set, const/let variables are not initialized, but SetupDeclarationContent is automatically executed
			virtual void CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter));

			virtual JS::Interpreter::State<JSObject> CompilePropertyDefinition(JS::Interpreter::State<JSObject> initial_state);
			virtual JS::Interpreter::State<JS::Runtime::ClassContext> CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state);

			virtual JS::Interpreter::State<std::optional<JSValue>> Process(JS::Runtime::ValidationContext cxt, JS::Interpreter::State<>& initial_state);
		};
	}

	namespace Parser
	{
		typedef JS::ParseNode::ParseNode ParseNode;

		enum class KeywordType
		{
			not_a_keyword,
#if ENABLE_ES4
			Abstract, // included in ES4 for JScript .NET compatibility
#endif
			As, // ESNext (not reserved); ES4 keyword
			Async, // ESNext (not reserved)
			Await, // ES8, reserved since ES6
			Break,
			Case, // ES3, reserved in all versions
#if 0
			Cast, // ES4.2006 considers this a keyword
#endif
			Catch, // ES3, reserved in all versions
			Class, // ES6, reserved in all versions; ES4
			Const, // ES6, reserved in all versions; ES4
			Continue,
			Debugger, // ES5, reserved in all versions
			Default, // ES3, reserved in all versions
			Delete,
			Do, // ES3, reserved in all versions
#if ENABLE_ES4
			Dynamic, // ES4.2006 considers this a keyword
#endif
			Each, // E4X context keyword
			Else,
			Export, // ES6, reserved in all versions; ES4
			Extends, // ES6, reserved in all versions; ES4
			False,
			Finally, // ES3, reserved in all versions
#if ENABLE_ES4
			Final, // ES4.2006 considers this a keyword
#endif
			For,
			From, // ?TODO (not reserved)
			Function,
			Get, // ?TODO (not reserved)
			If,
#if ENABLE_ES4
			Implements, // ES4.2006 considers this a keyword
#endif
			Import, // ES6, reserved in all versions; ES4
			In,
#if 0
			Include, // ES4.2006 considers this a keyword
#endif
			Instanceof, // ES3, reserved in all versions
#if ENABLE_ES4
			Interface, // ES4.2006 considers this a keyword
			Internal, // ES4.2006 considers this a keyword
			Is, // ES4
#endif
			Let, // ES6, reserved in strict mode since ES5 (but not ES6?)
			Meta, // ?TODO (not reserved)
			Namespace, // E4X, ES4
#if ENABLE_ES4
			Native, // ES4.2006 considers this a keyword
#endif
			New,
			Null,
			Of, // ?TODO (not reserved)
#if ENABLE_ES4
			Override, // ES4.2006 considers this a keyword
			Package, // ES4
			Private, // ES4
			Protected, // ES4.2006 considers this a keyword
			Prototype, // ES4.2006 considers this a keyword
			Public, // ES4
#endif
			Return,
			Set, // ?TODO (not reserved)
			Static, // ES6, reserve in all versions
			Super, // ES6, reserved in all versions; ES4
			Switch, // ES3, reserved in all versions; ES4
			Target, // ?TODO (not reserved)
			This,
			Throw, // ES3, reserved in all versions
#if 0
			To, // ES4.2006 considers this a keyword
#endif
			True,
			Try, // ES3, reserved in all versions
			Typeof,
#if 0
			Type, // ES4.2006 considers this a keyword
#endif
#if ENABLE_ES4
			Use, // ES4
#endif
			Var,
			Void,
			While,
			With,
			Xml, // E4X
			Yield, // ES6, reserved in strict mode since ES5
		};

		enum class TokenType
		{
			EndOfFile = -1,

			Not = U'!',
			Private = U'#',
			Modulo = U'%',
			BitAnd = U'&',
			Parenthesis_Open = U'(',
			Parenthesis_Close = U')',
			Multiply = U'*',
			Plus = U'+',
			Comma = U',',
			Minus = U'-',
			Dot = U'.',
			Colon = U':',
			SemiColon = U';',
			Less = U'<',
			Assign = U'=',
			Greater = U'>',
			Divide = U'/',
			QuestionMark = U'?',
			At = U'@', // (E4X)
			Bracket_Open = U'[',
			Bracket_Close = U']',
			BitXor = '^',
			Brace_Open = U'{',
			BitOr = U'|',
			Brace_Close = U'}',
			BitNot = U'~',

			Comment = 128,
			LineTerminator,
			Identifier,
			ReservedWord,
			NumericLiteral,
#if ENABLE_ES4
			LongNumericLiteral, // (ES4)
			UlongNumericLiteral, // (ES4)
			FloatNumericLiteral, // (ES4)
#endif
			BigIntNumericLiteral, // (ES11)
			StringLiteral,
			RegExpLiteral, // (ES3)
			NoSubstitutionTemplate, // `...` (ES6)
			TemplateHead, // `...${ (ES6)
			TemplateMiddle, // }...${ (ES6)
			TemplateTail, // }...` (ES6)

			NotEqual, // '!='
			StrictNotEqual, // '!==' (ES3)
			And, // '&&'
			Power, // '**' (ESNext)
			Increment, // '++'
			Decrement, // '--'
			Ellipsis, // '...' (ES6, ES4)
			ShiftLeft, // '<<'
			LessEqual, // '<='
			Equal, // '=='
			StrictEqual, // '===' (ES3)
			Arrow, // '=>' (ES6)
			GreaterEqual, // '>='
			ShiftRight, // '>>'
			ShiftRightUnsigned, // '>>>'
			Chaining, // '?.' (ESNext)
			Coalesce, // '??' (ESNext)
			Or, // '||'
#if ENABLE_ES4
			LogicalXor, // '^^' (ES4)
#endif

			Assign_Modulo, // '%='
			Assign_BitAnd, // '&='
			Assign_And, // '&&=' (ESNext, ES4)
			Assign_Multiply, // '*='
			Assign_Power, // '**=' (ESNext)
			Assign_Add, // '+='
			Assign_Subtract, // '-='
			Assign_ShiftLeft, // '<<='
			Assign_ShiftRight, // '>>='
			Assign_ShiftRightUnsigned, // '>>>='
			Assign_Coalesce, // '??=' (ESNext)
			Assign_Divide, // '/='
			Assign_BitXor, // '^='
#if ENABLE_ES4
			Assign_Xor, // '^^=' (ES4)
#endif
			Assign_BitOr, // '|='
			Assign_Or, // '||=' (ESNext, ES4)

			Descendent, // '..' (E4X)
#if ENABLE_ES4
			TypeParameter_Open, // '.<' (ES4.2006) // TODO: implement parsing
#endif
			NameQualifier, // '::' (E4X, ES4)
			XMLPI, // <? ... ?>
			XMLComment, // <!-- ... -->
			XMLCDATA, // <![CDATA[ ... ]]>
			XMLEndTagOpener, // '</'
			XMLText,
			XMLWhitespace,
			XMLAttributeValue, // "..." '...'
			XMLSelfClosingTagCloser, // '/>'
			XMLName,
			XMLTagCharacters,
		};
	}

	namespace ParseNode
	{
		template <typename T>
			struct serializer
		/*{
			static inline std::string name();
			static inline void serialize(std::ostream& out, T data);
		}*/;

		template <class T>
			static inline void serialize(std::ostream& out, const T& data)
		{
			serializer<T>::serialize(out, data);
		}

		template <>
			struct serializer<JSString>
		{
			static inline std::string name()
			{
				return "JSString";
			}
			static inline void serialize(std::ostream& out, JSString data)
			{
				out << "\"";
				PrintString(out, data);
				out << "\"";
			}
		};

		template <>
			struct serializer<JSBoolean>
		{
			static inline std::string name()
			{
				return "JSBoolean";
			}
			static inline void serialize(std::ostream& out, JSBoolean data)
			{
				out << (data ? "true" : "false");
			}
		};

		template <>
			struct serializer<JSNumber>
		{
			static inline std::string name()
			{
				return "JSNumber";
			}
			static inline void serialize(std::ostream& out, JSNumber data)
			{
				out << data;
			}
		};

#if ENABLE_ES4
		template <>
			struct serializer<JSLong>
		{
			static inline std::string name()
			{
				return "JSLong";
			}
			static inline void serialize(std::ostream& out, JSLong data)
			{
				out << data << "l";
			}
		};

		template <>
			struct serializer<JSUlong>
		{
			static inline std::string name()
			{
				return "JSUlong";
			}
			static inline void serialize(std::ostream& out, JSUlong data)
			{
				out << data << "ul";
			}
		};

		template <>
			struct serializer<JSFloat>
		{
			static inline std::string name()
			{
				return "JSFloat";
			}
			static inline void serialize(std::ostream& out, JSFloat data)
			{
				out << data << "f";
			}
		};

		template <>
			struct serializer<JSDecimal>
		{
			static inline std::string name()
			{
				return "JSDecimal";
			}
			static inline void serialize(std::ostream& out, JSDecimal data)
			{
				// TODO: proper printing method
				uint64_t (* pair)[2] = reinterpret_cast<uint64_t(*)[2]>(&data);
				out << "(" << pair[0] << "," << pair[1] << ")";
			}
		};
#endif

		template <>
			struct serializer<JSBigInt>
		{
			static inline std::string name()
			{
				return "JSBigInt";
			}
			static inline void serialize(std::ostream& out, JSBigInt data)
			{
				out << data << "_mpz";
			}
		};

		/*template <class C>
			static inline void serialize(std::ostream& out, const C& instance) // optional, if static typing is used
		{
			C::serialize(out, instance);
		}*/

		template <class C>
			struct serializer<C *>
		{
			static inline std::string name()
			{
				return std::string(C::structure_name) + "*";
			}
			static inline void serialize(std::ostream& out, C * data)
			{
				if(data == nullptr)
				{
					out << "null";
				}
				else
				{
					data->serialize(out);
				}
			}
		};

		template <typename T>
			struct serializer<std::vector<T>>
		{
			static inline std::string name()
			{
				return "std::vector<" + serializer<T>::name() + ">";
			}
			static inline void serialize(std::ostream& out, const std::vector<T> data)
			{
				out << "[";
				bool started = false;
				for(auto element : data)
				{
					if(started)
						out << ",";
					else
						started = true;
					serializer<T>::serialize(out, element);
				}
				out << "]";
			}
		};

		template <typename T>
			struct serializer<std::optional<T>>
		{
			static inline std::string name()
			{
				return "std::optional<" + serializer<T>::name() + ">";
			}
			static inline void serialize(std::ostream& out, const std::optional<T>& data)
			{
				out << "<";
				if(data.has_value())
				{
					serializer<T>::serialize(out, data.value());
				}
				out << ">";
			}
		};

		template <typename ... Ts>
			struct type_list;

		template <typename T>
			struct type_list<T>
		{
			static inline std::string name()
			{
				return serializer<T>::name();
			}
		};

		template <typename T, typename ... Ts>
			struct type_list<T, Ts...>
		{
			static inline std::string name()
			{
				return serializer<T>::name() + "," + type_list<Ts...>::name();
			}
		};

		template <typename ... Ts>
			struct serializer<std::variant<Ts...>>
		{
			static inline std::string name()
			{
				return "std::variant<" + type_list<Ts...>::name() + ">";
			}
			static inline void serialize(std::ostream& out, const std::variant<Ts...>& data)
			{
				std::visit([&out](auto&& element)
				{
					using T = std::decay_t<decltype(element)>;
					out << "(" << serializer<T>::name() << ")";
					serializer<T>::serialize(out, element);
				}, data);
			}
		};

		template <class C>
			static inline void serialize_members([[maybe_unused]] std::ostream& out, [[maybe_unused]] const C& instance)
		{
		}
		template <class C, typename T>
			static inline void serialize_members(std::ostream& out, const C& instance, const char * name, T C::* member)
		{
			out << name << "=";
			serializer<T>::serialize(out, instance.*member);
		}
		template <class C, typename T, typename ... Args>
			static inline void serialize_members(std::ostream& out, const C& instance, const char * name, T C::* member, Args... args)
		{
			out << name << "=";
			serializer<T>::serialize(out, instance.*member);
			out << ",";
			serialize_members(out, instance, args...);
		}
		template <class C, typename ... Args>
			static inline void serialize_structure(std::ostream& out, const C& instance, Args... args)
		{
			out << C::structure_name << "{";
			serialize_members(out, instance, args...);
			out << "}";
		}

		template <class C, typename T, T C::*M>
			class member
		{
		public:
			const std::string name;
			member(std::string name) : name(name) { }
		};

#define SERIALIZE_BEGIN(__class) \
			typedef __class This; \
			static constexpr char structure_name[] = #__class; \
			void serialize(std::ostream& out) const override \
			{ \
				serialize(out, *this); \
			} \
			static void serialize(std::ostream& out, const This& instance) \
			{ \
				serialize_structure(out, instance
#define SERIALIZE_FIELD(__field) \
					, #__field, static_cast<decltype(__field) This::*>(&This::__field)
#define SERIALIZE_END() \
				); \
			}

#define SERIALIZE_ENUM_BEGIN(__type) \
		template <> \
			struct serializer<__type> \
		{ \
			static inline std::string name() \
			{ \
				return #__type; \
			} \
			static inline void serialize(std::ostream& out, __type value) \
			{ \
				typedef __type __This; \
				out << #__type "::"; \
				switch(value) \
				{
#define SERIALIZE_ENUM(__name) \
				case __This::__name: \
					out << #__name; \
					break;
#define SERIALIZE_ENUM_END() \
				default: \
					assert(false); \
				} \
			} \
		};

#if ENABLE_ES4
		class DeferredValue : public Runtime::Initialization
		{
		public:
			ParseNode * Code;

			DeferredValue(ParseNode * Code) : Code(Code) { }

			inline JSValue Initialize(FOR_PHASE(phase)) override
			{
				return Code->EvaluateExpression(__ES4_ONLY(phase));
			}
		};
#endif

		// may occur as a binding node
		class Empty : public ParseNode
		{
		public:
			SERIALIZE_BEGIN(Empty)
			SERIALIZE_END()

			Empty(Location location) : ParseNode(location) { }

			JS::Interpreter::State<> CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		// may occur as an expression or a binding node
		class Identifier : public ParseNode
		{
		public:
#if ENABLE_ES4
			bool is_const = false; // only for binding identifiers
#endif
			JSString StringValue = u"";
			bool is_private = false; // only for regular identifiers
#if ENABLE_ES4
			std::optional<ParseNode *> type; // only for binding identifiers
#endif
			Identifier(Location location, JSString StringValue, bool is_private = false) : ParseNode(location), StringValue(StringValue), is_private(is_private) { }

			SERIALIZE_BEGIN(Identifier)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_FIELD(is_private)
#if ENABLE_ES4
			SERIALIZE_FIELD(is_const)
			SERIALIZE_FIELD(type)
#endif
			SERIALIZE_END()

			JS::ParseNode::DeclarationKind declaration_kind;
			JS::Runtime::EntityMode entity_mode = JS::Runtime::EntityMode::Unknown;
#if ENABLE_ES4
			JSCompoundAttribute attributes;

			std::optional<JSProperty> variable;
			std::optional<JSInstanceVariable> instance_variable;
			std::optional<JSInstanceVariable> overridden_variable;
			std::optional<JSValue> DefaultValue;
#endif

			std::set<JSNamespace> open_namespaces;
			bool strict = false;

			// for all types of nodes of the form "ID = INIT"
			// this is only treated as an initializer for declarations, binding patterns and destructuring assignment
			ParseNode * initializer = nullptr;

			// class element only
			bool Static = false;
			JSObject ClassPrototype = nullptr;

			void Assign(JSValue reference, JSValue value);
			JS::Interpreter::State<> CompileAssign(JS::Interpreter::State<JSValue, JSValue> initial_state);

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			void CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter)) override;
			bool CollectBoundNames(std::multiset<JSString>& names) override;
			bool ContainsInitialization() override;
			JS::Interpreter::State<> CompileInitialization(JS::Interpreter::State<>& initial_state);
			JS::Interpreter::State<> CompileBinding(JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state) override;
			JS::Interpreter::State<> CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state) override;
			JS::Interpreter::State<> CompilePropertyBinding(JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state) override;
			JS::Interpreter::State<> CompileKeyedBinding(JS::Interpreter::State<JS::Runtime::KeyedBindingContext>& binding_state) override;
#if ENABLE_ES4
			void SetupDeclarationContent(JS::Runtime::EnvironmentMode mode) override;
#endif

			JS::Interpreter::State<JSObject> CompilePropertyDefinition(JS::Interpreter::State<JSObject> initial_state) override;
			JS::Interpreter::State<JS::Runtime::ClassContext> CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state) override;
		};

		class Parenthesis : public ParseNode
		{
		public:
			ParseNode * expression;
			Parenthesis(Location location, ParseNode * expression) : ParseNode(location), expression(expression) { }
			SERIALIZE_BEGIN(Parenthesis)
			SERIALIZE_FIELD(expression)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

#if ENABLE_ES4
		enum class ReservedNamespaceType
		{
			Internal,
			Private,
			Protected,
			Public,
		};

		SERIALIZE_ENUM_BEGIN(ReservedNamespaceType)
		SERIALIZE_ENUM(Internal)
		SERIALIZE_ENUM(Private)
		SERIALIZE_ENUM(Protected)
		SERIALIZE_ENUM(Public)
		SERIALIZE_ENUM_END()

		class ReservedNamespace : public ParseNode
		{
		public:
			ReservedNamespaceType type;
			ReservedNamespace(Location location, ReservedNamespaceType type) : ParseNode(location), type(type) { }

			SERIALIZE_BEGIN(ReservedNamespace)
			SERIALIZE_FIELD(type)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		static inline std::optional<ReservedNamespaceType> GetNamespaceType(JS::Parser::KeywordType keyword)
		{
			switch(keyword)
			{
			case JS::Parser::KeywordType::Private:
				return ReservedNamespaceType::Private;
			case JS::Parser::KeywordType::Internal:
				return ReservedNamespaceType::Internal;
			case JS::Parser::KeywordType::Protected:
				return ReservedNamespaceType::Protected;
			case JS::Parser::KeywordType::Public:
				return ReservedNamespaceType::Public;
			default:
				return {};
			}
		}

		static inline bool IsReservedNamespace(JS::Parser::KeywordType keyword)
		{
			return GetNamespaceType(keyword).has_value();
		}

		class AbstractAttribute : public ParseNode
		{
		public:
			AbstractAttribute(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(AbstractAttribute)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class NativeAttribute : public ParseNode
		{
		public:
			NativeAttribute(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(NativeAttribute)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};
#endif

		class NewTargetExpression : public ParseNode
		{
		public:
			NewTargetExpression(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(NewTargetExpression)
			SERIALIZE_END()
		};

		class ImportMetaExpression : public ParseNode
		{
		public:
			ImportMetaExpression(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(ImportMetaExpression)
			SERIALIZE_END()
		};

		class ThisExpression : public ParseNode
		{
		public:
			ThisExpression(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(ThisExpression)
			SERIALIZE_END()

#if ENABLE_ES4
			void Validate(JS::Runtime::ValidationContext cxt) override;
#endif
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class SuperExpression : public ParseNode
		{
		public:
			SuperExpression(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(SuperExpression)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class ImportExpression : public ParseNode
		{
		public:
			ImportExpression(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(ImportExpression)
			SERIALIZE_END()
		};

		class NullLiteral : public ParseNode
		{
		public:
			NullLiteral(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(NullLiteral)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class BooleanLiteral : public ParseNode
		{
		public:
			JSBoolean BooleanValue;
			BooleanLiteral(Location location, JSBoolean BooleanValue) : ParseNode(location), BooleanValue(BooleanValue) { }

			SERIALIZE_BEGIN(BooleanLiteral)
			SERIALIZE_FIELD(BooleanValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// for literals that can be used as class element names or property keys
		class Literal : public ParseNode
		{
		public:
			// class element only
			bool strict = false;
			bool Static = false;
			ParseNode * Initializer = nullptr;

			Literal(Location location) : ParseNode(location) { }

			void Validate(JS::Runtime::ValidationContext cxt) override;
			virtual JSPropertyKey GetPropertyKey() = 0;

			JS::Interpreter::State<JS::Runtime::ClassContext> CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state) override;
		};

		// may occur as an expression or a binding node
		class NumericLiteral : public Literal
		{
		public:
			SERIALIZE_BEGIN(NumericLiteral)
			SERIALIZE_FIELD(NumericValue)
			SERIALIZE_END()

			JSNumber NumericValue;
			NumericLiteral(Location location, JSNumber NumericValue) : Literal(location), NumericValue(NumericValue) { }

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JSPropertyKey GetPropertyKey() override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// may occur as an expression or a binding node
		class BigIntNumericLiteral : public Literal
		{
		public:
			SERIALIZE_BEGIN(BigIntNumericLiteral)
			SERIALIZE_FIELD(NumericValue)
			SERIALIZE_END()

			JSBigInt NumericValue;
			BigIntNumericLiteral(Location location, JSBigInt NumericValue) : Literal(location), NumericValue(NumericValue) { }

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JSPropertyKey GetPropertyKey() override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

#if ENABLE_ES4
		class LongNumericLiteral : public ParseNode
		{
		public:
			int64_t NumericValue;
			LongNumericLiteral(Location location, int64_t NumericValue) : ParseNode(location), NumericValue(NumericValue) { }

			SERIALIZE_BEGIN(LongNumericLiteral)
			SERIALIZE_FIELD(NumericValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class UlongNumericLiteral : public ParseNode
		{
		public:
			uint64_t NumericValue;
			UlongNumericLiteral(Location location, uint64_t NumericValue) : ParseNode(location), NumericValue(NumericValue) { }

			SERIALIZE_BEGIN(UlongNumericLiteral)
			SERIALIZE_FIELD(NumericValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class FloatNumericLiteral : public ParseNode
		{
		public:
			float NumericValue;
			FloatNumericLiteral(Location location, float NumericValue) : ParseNode(location), NumericValue(NumericValue) { }

			SERIALIZE_BEGIN(FloatNumericLiteral)
			SERIALIZE_FIELD(NumericValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};
#endif

		// may occur as an expression or a binding node
		class StringLiteral : public Literal
		{
		public:
			JSString StringValue;
			StringLiteral(Location location, JSString StringValue) : Literal(location), StringValue(StringValue) { }

			SERIALIZE_BEGIN(StringLiteral)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JSPropertyKey GetPropertyKey() override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class RegularExpressionLiteral : public ParseNode
		{
		public:
			JSString StringValue;
			JSString Flags;
			RegularExpressionLiteral(Location location, JSString StringValue, JSString Flags) : ParseNode(location), StringValue(StringValue), Flags(Flags) { }

			SERIALIZE_BEGIN(RegularExpressionLiteral)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_FIELD(Flags)
			SERIALIZE_END()
		};

		class Template : public ParseNode
		{
		public:
			JSString StringValue;
			Template(Location location, JSString StringValue) : ParseNode(location), StringValue(StringValue) { }

			SERIALIZE_BEGIN(Template)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_END()
		};

		class TemplateSubstitution : public ParseNode
		{
		public:
			ParseNode * head;
			ParseNode * expression;
			Template * tail;
			TemplateSubstitution(Location location, ParseNode * head, ParseNode * expression, Template * tail) : ParseNode(location), head(head), expression(expression), tail(tail) { }

			SERIALIZE_BEGIN(TemplateSubstitution)
			SERIALIZE_FIELD(head)
			SERIALIZE_FIELD(expression)
			SERIALIZE_FIELD(tail)
			SERIALIZE_END()
		};

		// may occur as an expression or a binding node
		class ArrayLiteral : public ParseNode
		{
		public:
			std::vector<ParseNode *> elements;
			ArrayLiteral(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(ArrayLiteral)
			SERIALIZE_FIELD(elements)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;

			void CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter)) override;
			bool CollectBoundNames(std::multiset<JSString>& names) override;
			bool ContainsInitialization() override;
			JS::Interpreter::State<> CompileBinding(JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state) override;
		};

		// may occur as an expression or a binding node
		class PropertyDefinition : public ParseNode
		{
		public:
			ParseNode * key;
			ParseNode * value;
			PropertyDefinition(Location location, ParseNode * key, ParseNode * value) : ParseNode(location), key(key), value(value) { }

			SERIALIZE_BEGIN(PropertyDefinition)
			SERIALIZE_FIELD(key)
			SERIALIZE_FIELD(value)
			SERIALIZE_END()

			bool strict = false;
#if ENABLE_ES4
			std::set<JSNamespace> open_namespaces;
#endif

			void Validate(JS::Runtime::ValidationContext cxt) override;

			void CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter)) override;
			bool CollectBoundNames(std::multiset<JSString>& names) override;
			bool ContainsInitialization() override;
			JS::Interpreter::State<> CompilePropertyBinding(JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state) override;
			JS::Interpreter::State<JSObject> CompilePropertyDefinition(JS::Interpreter::State<JSObject> initial_state) override;
		};

		// may occur as an expression or a binding node
		class ObjectLiteral : public ParseNode
		{
		public:
			std::vector<ParseNode *> property_definitions;

			ObjectLiteral(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(ObjectLiteral)
			SERIALIZE_FIELD(property_definitions)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;

			void CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter)) override;
			bool CollectBoundNames(std::multiset<JSString>& names) override;
			bool ContainsInitialization() override;
			JS::Interpreter::State<> CompileBinding(JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state) override;
		};

		// may occur as an expression or a binding node
		class RestExpression : public ParseNode
		{
		public:
			ParseNode * expression;
			RestExpression(Location location, ParseNode * expression) : ParseNode(location), expression(expression) { }

			SERIALIZE_BEGIN(RestExpression)
			SERIALIZE_FIELD(expression)
			SERIALIZE_END()

			bool strict;

			void CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter)) override;
			bool CollectBoundNames(std::multiset<JSString>& names) override;
			bool ContainsInitialization() override;
			void Validate(JS::Runtime::ValidationContext cxt) override;

			JS::Interpreter::State<> CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state) override;
			JS::Interpreter::State<JSObject> CompilePropertyDefinition(JS::Interpreter::State<JSObject> initial_state) override;
			JS::Interpreter::State<> CompilePropertyBinding(JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state) override;
		};

		// may occur as an expression (when E4X is enabled) or a binding node
		class ComputedPropertyName : public ParseNode
		{
		public:
			ParseNode * expression;
			ComputedPropertyName(Location location, ParseNode * expression) : ParseNode(location), expression(expression) { }

			SERIALIZE_BEGIN(ComputedPropertyName)
			SERIALIZE_FIELD(expression)
			SERIALIZE_END()

			// class element only
			bool strict = false;
			bool Static = false;
			ParseNode * Initializer = nullptr;

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<JS::Runtime::ClassContext> CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state) override;
		};

		// E4X
		class FilteringPredicate : public ParseNode
		{
		public:
			ParseNode * expression;
			FilteringPredicate(Location location, ParseNode * expression) : ParseNode(location), expression(expression) { }

			SERIALIZE_BEGIN(FilteringPredicate)
			SERIALIZE_FIELD(expression)
			SERIALIZE_END()
		};

		class CallExpression : public ParseNode
		{
		public:
			bool is_new = false;
			ParseNode * function;
			std::vector<ParseNode *> arguments;
			CallExpression(Location location, ParseNode * function) : ParseNode(location), function(function) { }

			SERIALIZE_BEGIN(CallExpression)
			SERIALIZE_FIELD(function)
			SERIALIZE_FIELD(arguments)
			SERIALIZE_FIELD(is_new)
			SERIALIZE_END()

			bool in_tail_position = false;

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
#if ENABLE_ES4
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
#endif
		};

		class IndexExpression : public ParseNode
		{
		public:
			ParseNode * object;
			std::vector<ParseNode *> index; // this is always 1 element, except for ECMAScript 4
			IndexExpression(Location location, ParseNode * object, ParseNode * index) : ParseNode(location), object(object), index{index} { }

			SERIALIZE_BEGIN(IndexExpression)
			SERIALIZE_FIELD(object)
			SERIALIZE_FIELD(index)
			SERIALIZE_END()

			bool strict = false;
#if ENABLE_ES4
			std::set<JSNamespace> open_namespaces;
#endif

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class MemberAccessExpression : public ParseNode
		{
		public:
			ParseNode * object;
			ParseNode * name;
			MemberAccessExpression(Location location, ParseNode * object, ParseNode * name) : ParseNode(location), object(object), name(name) { }

			SERIALIZE_BEGIN(MemberAccessExpression)
			SERIALIZE_FIELD(object)
			SERIALIZE_FIELD(name)
			SERIALIZE_END()

			bool strict = false;
#if ENABLE_ES4
			std::set<JSNamespace> open_namespaces;
#endif

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

#if ENABLE_ES4
		class TypeApplication : public ParseNode
		{
		public:
			ParseNode * generic_name;
			std::vector<ParseNode *> type_parameters;
			TypeApplication(Location location, ParseNode * generic_name) : ParseNode(location), generic_name(generic_name) { }

			SERIALIZE_BEGIN(TypeApplication)
			SERIALIZE_FIELD(generic_name)
			SERIALIZE_FIELD(type_parameters)
			SERIALIZE_END()
		};
#endif

		class DescendentExpression : public ParseNode
		{
		public:
			ParseNode * object;
			ParseNode * name;
			DescendentExpression(Location location, ParseNode * object, ParseNode * name) : ParseNode(location), object(object), name(name) { }

			SERIALIZE_BEGIN(DescendentExpression)
			SERIALIZE_FIELD(object)
			SERIALIZE_FIELD(name)
			SERIALIZE_END()
		};

		class TaggedTemplate : public ParseNode
		{
		public:
			// TODO: can be in tail call
			ParseNode * tag;
			ParseNode * template_argument;
			TaggedTemplate(Location location, ParseNode * tag, ParseNode * template_argument) : ParseNode(location), tag(tag), template_argument(template_argument) { }

			SERIALIZE_BEGIN(TaggedTemplate)
			SERIALIZE_FIELD(tag)
			SERIALIZE_FIELD(template_argument)
			SERIALIZE_END()
		};

		class OptionalChain : public ParseNode
		{
		public:
			ParseNode * object;
			OptionalChain(Location location, ParseNode * object) : ParseNode(location), object(object) { }

			SERIALIZE_BEGIN(OptionalChain)
			SERIALIZE_FIELD(object)
			SERIALIZE_END()
		};

		class FormalParameters : public ParseNode
		{
		public:
			std::vector<ParseNode *> arguments;
			FormalParameters(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(FormalParameters)
			SERIALIZE_FIELD(arguments)
			SERIALIZE_END()
		};

#if ENABLE_ES4
		class VoidType : public ParseNode
		{
		public:
			VoidType(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(VoidType)
			SERIALIZE_END()

			JSType EvaluateType() override;
		};
#endif

		/* Functions will appear in various syntactic contexts in various forms. These are captured in three
		 * not entirely independent parameters: FunctionType, FunctionKind and FunctionPosition.
		 */

		/** @brief Any syntactic prefix added to the function name */
		enum class FunctionType
		{
			/** @brief Not a get/set/async function */
			Normal,
			Get,
			Set,
			Async,
		};
		SERIALIZE_ENUM_BEGIN(FunctionType)
		SERIALIZE_ENUM(Normal)
		SERIALIZE_ENUM(Get)
		SERIALIZE_ENUM(Set)
		SERIALIZE_ENUM(Async)
		SERIALIZE_ENUM_END()

		/** @brief The syntactic style of the function */
		enum class FunctionKind
		{
			/** @brief A normal function expression or statement */
			Function,
			/** @brief A generator function expression or statement, with a '*' appearing after the 'function' keyword */
			Generator,
			/** @brief An arrow function expression */
			Arrow,
		};
		SERIALIZE_ENUM_BEGIN(FunctionKind)
		SERIALIZE_ENUM(Function)
		SERIALIZE_ENUM(Generator)
		SERIALIZE_ENUM(Arrow)
		SERIALIZE_ENUM_END()

		/** @brief Where does the function declaration appear syntactically */
		enum class FunctionPosition
		{
			Statement,
			Expression,
			Method,
			Property,
			ClassConstructor,
		};
		SERIALIZE_ENUM_BEGIN(FunctionPosition)
		SERIALIZE_ENUM(Statement)
		SERIALIZE_ENUM(Expression)
		SERIALIZE_ENUM(Method)
		SERIALIZE_ENUM(Property)
		SERIALIZE_ENUM(ClassConstructor)
		SERIALIZE_ENUM_END()

		JSString GetStringForName(JSBindingName name);

		class FunctionalElement : public ParseNode
		{
		public:
			ParseNode * body = nullptr;
			FunctionKind function_kind = FunctionKind::Function;
			bool strict = false;

			FunctionalElement() : ParseNode(Location()) { }
			FunctionalElement(Location location) : ParseNode(location) { }
			FunctionalElement(Location location, ParseNode * body) : ParseNode(location), body(body) { }

			virtual JS::Class::Function::ThisModeType GetThisMode();
			virtual std::vector<ParseNode *> GetArguments() const;

			// do we need a separate lexical environment for parameter initializer evaluation?
			bool parameter_evaluation_environment_needed = false;
			bool parameters_bound_lexically = true;
			// do we need an arguments object?
			//bool arguments_object_needed = false;
			// do we need a separate variable environment in the formal parameters?
			bool parameter_closure_environment_needed = false;

			bool IsSimple() const
			{
				for(auto argument : GetArguments())
				{
#if ENABLE_ES4
					if(auto identifier = dynamic_cast<Identifier *>(argument))
					{
						if(IsVersionES4() && identifier->type.has_value())
						{
							return false;
						}
					}
					else
#else
					if(!dynamic_cast<Identifier *>(argument))
#endif
					{
						// initializers and rest arguments fall here
						return false;
					}
				}
				return true;
			}

			void Validate(JS::Runtime::ValidationContext cxt) override;
		};

		class FunctionDefinition : public FunctionalElement
		{
		public:
			std::optional<ParseNode *> name;
			FunctionType function_type = FunctionType::Normal;
#if ENABLE_ES4
			std::vector<Identifier *> type_parameters;
#endif
			FormalParameters * parameters = nullptr;
#if ENABLE_ES4
			std::optional<ParseNode *> result_type;
#endif
			FunctionPosition function_position = FunctionPosition::Statement;

			FunctionDefinition(Location location) : FunctionalElement(location) { }

			SERIALIZE_BEGIN(FunctionDefinition)
			SERIALIZE_FIELD(name)
			SERIALIZE_FIELD(function_type)
			SERIALIZE_FIELD(function_kind)
			SERIALIZE_FIELD(function_position)
#if ENABLE_ES4
			SERIALIZE_FIELD(type_parameters)
#endif
			SERIALIZE_FIELD(parameters)
#if ENABLE_ES4
			SERIALIZE_FIELD(result_type)
#endif
			SERIALIZE_FIELD(body)
			SERIALIZE_END()

			JSString SourceText;

			std::optional<JSString> class_name; // for ES6 class constructors, also set when inside ES4 class
			bool member_of_derived_class = false; // as opposed to base class or not a member at all
			bool Static = false; // for ES6 class elements
			bool active = true; // set to false if another function follows in the same frame with the same name
			bool redeclare_function_required = false; // used to mark non-strict function declarations at block scope

#if ENABLE_ES4
			std::set<JSNamespace> open_namespaces;
			JSCompoundAttribute attributes;

			JS::Runtime::EntityMode entity_mode = JS::Runtime::EntityMode::Unknown;
			gcn::gc_ptr<JS::Class::Function> declared_function = nullptr;
			std::optional<JSInstanceFunction> instance_function;
			std::optional<JSInstanceProperty> overridden_property;
#endif

#if ENABLE_ES4
			bool IsVersionES4Plain() const
			{
				if(type_parameters.size() != 0)
				{
					return false; // not in specs, but almost definitely the intended behavior
				}
				if(result_type.has_value())
				{
					return false;
				}
				for(auto argument : parameters->arguments)
				{
					if(auto identifier = dynamic_cast<Identifier *>(argument))
					{
						if(identifier->type.has_value())
						{
							return false;
						}
					}
					else
					{
						// initializers and rest arguments fall here
						return false;
					}
				}
				return true;
			}
#endif

			bool IsPrivate() const
			{
				// only ESNext
				if(CurrentVersion < Version::ESNEXT)
				{
					return false;
				}

				if(!name.has_value())
				{
					return false;
				}

				if(auto identifier = dynamic_cast<Identifier *>(name.value()))
				{
					return identifier->is_private;
				}
				else
				{
					return false;
				}
			}

			JS::Class::Function::FunctionKindType GetFunctionKind();
			JS::Class::Function::ThisModeType GetThisMode() override;
			JSString GetFunctionName(std::optional<JSString> Identifier); // used with an already calculated identifier
			JSString GetName(); // only if the function name is an identifier (or if it is a constructor)
			JS::Interpreter::State<JSPropertyKey> CompileFunctionName(JS::Interpreter::State<> initial_state);
			uint32_t GetLength();
			gcn::gc_ptr<JS::Class::Function> CreateFunction(
				JSString Name,
				JSObject ConstructorParent,
				std::shared_ptr<JS::Runtime::EnvironmentRecord> env/*,
				JS::Class::Function::ThisModeType ThisMode*/);
			gcn::gc_ptr<JS::Class::Function> CreateFunction(
				JSString Name,
				JSObject ConstructorParent/*,
				JS::Class::Function::ThisModeType ThisMode*/);
			gcn::gc_ptr<JS::Class::Function> SetupPrototype(
				gcn::gc_ptr<JS::Class::Function> function,
				JSObject ClassPrototype = nullptr);
			std::vector<ParseNode *> GetArguments() const override;

#if ENABLE_ES4
			bool SharesClassName()
			{
				return class_name.has_value() && GetName() == class_name.value();
			}

			bool IsVersionES4Constructor()
			{
				return function_type == FunctionType::Normal && SharesClassName();
			}
#endif

			void Validate(JS::Runtime::ValidationContext cxt) override;
			gcn::gc_ptr<JS::Class::Function> EvaluateFunction(JSString Name);
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<gcn::gc_ptr<JS::Class::Function>> CompileNamedExpression(JS::Interpreter::State<JSString>& initial_state);
#if ENABLE_ES4
			JS::Runtime::SignatureType GetSignature();
#endif
			bool CollectBoundNames(std::multiset<JSString>& names) override;
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;

			JS::Interpreter::State<JSObject> CompilePropertyDefinition(JS::Interpreter::State<JSObject> initial_state) override;
			JS::Interpreter::State<JS::Runtime::ClassContext> CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state) override;
		};

		class ClassDefinition : public ParseNode
		{
		public:
			std::optional<Identifier *> name;
#if ENABLE_ES4
			bool is_interface = false;
			std::vector<Identifier *> type_parameters;
#endif
			std::optional<ParseNode *> extends;
#if ENABLE_ES4
			std::vector<ParseNode *> implements;
#endif
			ParseNode * body = nullptr;
			bool is_expression = false;

			ClassDefinition(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(ClassDefinition)
			SERIALIZE_FIELD(name)
#if ENABLE_ES4
			SERIALIZE_FIELD(is_interface)
			SERIALIZE_FIELD(type_parameters)
#endif
			SERIALIZE_FIELD(extends)
#if ENABLE_ES4
			SERIALIZE_FIELD(implements)
#endif
			SERIALIZE_FIELD(body)
			SERIALIZE_FIELD(is_expression)
			SERIALIZE_END()

			JSString SourceText;

#if ENABLE_ES4
			JSClass CompiledClass;
			std::set<JSNamespace> open_namespaces;
#endif

			JSString GetName();

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
			bool CollectBoundNames(std::multiset<JSString>& names) override;
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
		};

		class UpdateExpression : public ParseNode
		{
		public:
			ParseNode * operand;
			bool post_update;
			bool increment;
			UpdateExpression(Location location, ParseNode * operand, bool post_update, bool increment) : ParseNode(location), operand(operand), post_update(post_update), increment(increment) { }

			SERIALIZE_BEGIN(UpdateExpression)
			SERIALIZE_FIELD(operand)
			SERIALIZE_FIELD(post_update)
			SERIALIZE_FIELD(increment)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class YieldExpression : public ParseNode
		{
		public:
			std::optional<ParseNode *> operand;
			YieldExpression(Location location, std::optional<ParseNode *> operand = {}) : ParseNode(location), operand(operand) { }

			SERIALIZE_BEGIN(YieldExpression)
			SERIALIZE_FIELD(operand)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class YieldGeneratorExpression : public ParseNode
		{
		public:
			ParseNode * operand;
			YieldGeneratorExpression(Location location, ParseNode * operand) : ParseNode(location), operand(operand) { }

			SERIALIZE_BEGIN(YieldGeneratorExpression)
			SERIALIZE_FIELD(operand)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class AwaitExpression : public ParseNode
		{
		public:
			ParseNode * operand;
			AwaitExpression(Location location, ParseNode * operand) : ParseNode(location), operand(operand) { }

			SERIALIZE_BEGIN(AwaitExpression)
			SERIALIZE_FIELD(operand)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		enum class UnaryOperator
		{
			Delete,
			Void,
			Typeof,
			Plus,
			Negate,
			BitwiseNot,
			LogicalNot,
			//Await,
			//Yield,
			//YieldGenerator,
		};
		SERIALIZE_ENUM_BEGIN(UnaryOperator)
		SERIALIZE_ENUM(Delete)
		SERIALIZE_ENUM(Void)
		SERIALIZE_ENUM(Typeof)
		SERIALIZE_ENUM(Plus)
		SERIALIZE_ENUM(Negate)
		SERIALIZE_ENUM(BitwiseNot)
		SERIALIZE_ENUM(LogicalNot)
		//SERIALIZE_ENUM(Await)
		//SERIALIZE_ENUM(Yield)
		//SERIALIZE_ENUM(YieldGenerator)
		SERIALIZE_ENUM_END()

		class UnaryExpression : public ParseNode
		{
		public:
			typedef UnaryOperator Operator;
			Operator op;
			ParseNode * operand;
			UnaryExpression(Location location, Operator op, ParseNode * operand) : ParseNode(location), op(op), operand(operand) { }

			SERIALIZE_BEGIN(UnaryExpression)
			SERIALIZE_FIELD(op)
			SERIALIZE_FIELD(operand)
			SERIALIZE_END()

			bool strict = false;

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		enum class BinaryOperator
		{
			Power,
			Multiply,
			Divide,
			Modulo,
			Add,
			Subtract,
			ShiftLeft,
			ShiftRight,
			ShiftRightUnsigned,
			Less,
			LessEqual,
			Greater,
			GreaterEqual,
			Instanceof,
			In,
#if ENABLE_ES4
			Is,
			As,
#endif
			Equal,
			NotEqual,
			StrictEqual,
			StrictNotEqual,
			BitwiseAnd,
			BitwiseXor,
			BitwiseOr,
			Coalesce,
			LogicalAnd,
#if ENABLE_ES4
			LogicalXor,
#endif
			LogicalOr,
			Comma,
		};
		SERIALIZE_ENUM_BEGIN(BinaryOperator)
		SERIALIZE_ENUM(Power)
		SERIALIZE_ENUM(Multiply)
		SERIALIZE_ENUM(Divide)
		SERIALIZE_ENUM(Modulo)
		SERIALIZE_ENUM(Add)
		SERIALIZE_ENUM(Subtract)
		SERIALIZE_ENUM(ShiftLeft)
		SERIALIZE_ENUM(ShiftRight)
		SERIALIZE_ENUM(ShiftRightUnsigned)
		SERIALIZE_ENUM(Less)
		SERIALIZE_ENUM(LessEqual)
		SERIALIZE_ENUM(Greater)
		SERIALIZE_ENUM(GreaterEqual)
		SERIALIZE_ENUM(Instanceof)
		SERIALIZE_ENUM(In)
#if ENABLE_ES4
		SERIALIZE_ENUM(Is)
		SERIALIZE_ENUM(As)
#endif
		SERIALIZE_ENUM(Equal)
		SERIALIZE_ENUM(NotEqual)
		SERIALIZE_ENUM(StrictEqual)
		SERIALIZE_ENUM(StrictNotEqual)
		SERIALIZE_ENUM(BitwiseAnd)
		SERIALIZE_ENUM(BitwiseXor)
		SERIALIZE_ENUM(BitwiseOr)
		SERIALIZE_ENUM(Coalesce)
		SERIALIZE_ENUM(LogicalAnd)
#if ENABLE_ES4
		SERIALIZE_ENUM(LogicalXor)
#endif
		SERIALIZE_ENUM(LogicalOr)
		SERIALIZE_ENUM(Comma)
		SERIALIZE_ENUM_END()

		class BinaryExpression : public ParseNode
		{
		public:
			typedef BinaryOperator Operator;
			Operator op;
			ParseNode * left_operand;
			ParseNode * right_operand;
			BinaryExpression(Location location, Operator op, ParseNode * left_operand, ParseNode * right_operand) : ParseNode(location), op(op), left_operand(left_operand), right_operand(right_operand) { }

			SERIALIZE_BEGIN(BinaryExpression)
			SERIALIZE_FIELD(op)
			SERIALIZE_FIELD(left_operand)
			SERIALIZE_FIELD(right_operand)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		class ConditionalExpression : public ParseNode
		{
		public:
			ParseNode * condition;
			ParseNode * true_branch;
			ParseNode * false_branch;
			ConditionalExpression(Location location, ParseNode * condition, ParseNode * true_branch, ParseNode * false_branch) : ParseNode(location), condition(condition), true_branch(true_branch), false_branch(false_branch) { }

			SERIALIZE_BEGIN(ConditionalExpression)
			SERIALIZE_FIELD(condition)
			SERIALIZE_FIELD(true_branch)
			SERIALIZE_FIELD(false_branch)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// may occur as an expression or a binding node (with default initializer)
		class AssignmentExpression : public ParseNode
		{
		public:
			ParseNode * target;
			ParseNode * source;
			AssignmentExpression(Location location, ParseNode * target, ParseNode * source) : ParseNode(location), target(target), source(source) { }

			SERIALIZE_BEGIN(AssignmentExpression)
			SERIALIZE_FIELD(target)
			SERIALIZE_FIELD(source)
			SERIALIZE_END()

			// only when in a binding position
			JS::ParseNode::DeclarationKind declaration_kind;

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			void CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter)) override;
#if ENABLE_ES4
			void SetupDeclarationContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			bool CollectBoundNames(std::multiset<JSString>& names) override;
			bool ContainsInitialization() override;
			JS::Interpreter::State<> CompileInitialization(JS::Interpreter::State<>& initial_state);
			JS::Interpreter::State<> CompileBinding(JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state) override;
			JS::Interpreter::State<> CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state) override;
			JS::Interpreter::State<> CompilePropertyBinding(JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state) override;
			JS::Interpreter::State<> CompileKeyedBinding(JS::Interpreter::State<JS::Runtime::KeyedBindingContext>& binding_state) override;

			JS::Interpreter::State<JS::Runtime::ClassContext> CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state) override;
		};

		class CombinedAssignmentExpression : public ParseNode
		{
		public:
			typedef BinaryOperator Operator;
			Operator op;
			ParseNode * target;
			ParseNode * source;

			SERIALIZE_BEGIN(CombinedAssignmentExpression)
			SERIALIZE_FIELD(op)
			SERIALIZE_FIELD(target)
			SERIALIZE_FIELD(source)
			SERIALIZE_END()

			CombinedAssignmentExpression(Location location, Operator op, ParseNode * target, ParseNode * source) : ParseNode(location), op(op), target(target), source(source) { }
		};

		class LocalDeclarator : public ParseNode
		{
		public:
			ParseNode * local;
			LocalDeclarator(Location location, ParseNode * local) : ParseNode(location), local(local) { }

			SERIALIZE_BEGIN(LocalDeclarator)
			SERIALIZE_FIELD(local)
			SERIALIZE_END()

			JS::ParseNode::DeclarationKind declaration_kind;
			JS::Runtime::EntityMode entity_mode = JS::Runtime::EntityMode::Unknown;

#if ENABLE_ES4
			bool IsVersionES4Plain() const
			{
				if(declaration_kind != DeclarationKind::Var)
				{
					return false;
				}
				else if(auto identifier = dynamic_cast<Identifier *>(local)) // TODO: initialization is allowed
				{
					return !identifier->type.has_value();
				}
				else if(auto assignment = dynamic_cast<AssignmentExpression *>(local))
				{
					return !dynamic_cast<Identifier *>(assignment->target)->type.has_value();
				}
				else
				{
					// initialized and binding and rest parameters fall here
					return false;
				}
			}
#endif

			bool CollectBoundNames(std::multiset<JSString>& names) override;
			void CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter)) override;
#if ENABLE_ES4
			void SetupDeclarationContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		SERIALIZE_ENUM_BEGIN(DeclarationKind)
		SERIALIZE_ENUM(Var)
		SERIALIZE_ENUM(Const)
		SERIALIZE_ENUM(Let)
		SERIALIZE_ENUM_END()

		class IdentifierDeclaration : public ParseNode
		{
		public:
			DeclarationKind declaration_kind;
			std::vector<LocalDeclarator *> declared_identifiers;
			IdentifierDeclaration(Location location, DeclarationKind declaration_kind) : ParseNode(location), declaration_kind(declaration_kind) { }

			SERIALIZE_BEGIN(IdentifierDeclaration)
			SERIALIZE_FIELD(declaration_kind)
			SERIALIZE_FIELD(declared_identifiers)
			SERIALIZE_END()

			bool CollectBoundNames(std::multiset<JSString>& names) override;
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class IfStatement : public ParseNode
		{
		public:
			ParseNode * condition;
			ParseNode * true_branch;
			std::optional<ParseNode *> false_branch;
			IfStatement(Location location, ParseNode * condition, ParseNode * true_branch, std::optional<ParseNode *> false_branch) : ParseNode(location), condition(condition), true_branch(true_branch), false_branch(false_branch) { }

			SERIALIZE_BEGIN(IfStatement)
			SERIALIZE_FIELD(condition)
			SERIALIZE_FIELD(true_branch)
			SERIALIZE_FIELD(false_branch)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class LoopStatement : public ParseNode
		{
		public:
			std::optional<JSString> label;
			LoopStatement(Location location) : ParseNode(location) { }
		};

		class LabelledStatement : public ParseNode
		{
		public:
			JSString StringValue;
			ParseNode * statement;
			LabelledStatement(Location location, JSString StringValue, ParseNode * statement) : ParseNode(location), StringValue(StringValue), statement(statement)
			{
				if(auto loop_statement = dynamic_cast<LoopStatement *>(statement))
				{
					loop_statement->label = StringValue;
				}
			}

			SERIALIZE_BEGIN(LabelledStatement)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_FIELD(statement)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class WhileStatement : public LoopStatement
		{
		public:
			ParseNode * condition;
			ParseNode * body;
			WhileStatement(Location location, ParseNode * condition, ParseNode * body) : LoopStatement(location), condition(condition), body(body) { }

			SERIALIZE_BEGIN(WhileStatement)
			SERIALIZE_FIELD(condition)
			SERIALIZE_FIELD(body)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class DoWhileStatement : public LoopStatement
		{
		public:
			ParseNode * body;
			ParseNode * condition;
			DoWhileStatement(Location location, ParseNode * body, ParseNode * condition) : LoopStatement(location), body(body), condition(condition) { }

			SERIALIZE_BEGIN(DoWhileStatement)
			SERIALIZE_FIELD(body)
			SERIALIZE_FIELD(condition)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class ForStatement : public LoopStatement
		{
		public:
			std::optional<ParseNode *> initial;
			std::optional<ParseNode *> condition;
			std::optional<ParseNode *> increment;
			ParseNode * body;
			ForStatement(Location location, std::optional<ParseNode *> initial, std::optional<ParseNode *> condition, std::optional<ParseNode *> increment, ParseNode * body)
				: LoopStatement(location), initial(initial), condition(condition), increment(increment), body(body)
			{
			}

			SERIALIZE_BEGIN(ForStatement)
			SERIALIZE_FIELD(initial)
			SERIALIZE_FIELD(condition)
			SERIALIZE_FIELD(increment)
			SERIALIZE_FIELD(body)
			SERIALIZE_END()
		};

		enum class ForEachKind
		{
			ForIn,
			ForEachIn, // E4X
			ForOf, // ES6
			ForAwaitOf, // ESNext
		};
		SERIALIZE_ENUM_BEGIN(ForEachKind)
		SERIALIZE_ENUM(ForIn)
		SERIALIZE_ENUM(ForEachIn)
		SERIALIZE_ENUM(ForOf)
		SERIALIZE_ENUM(ForAwaitOf)
		SERIALIZE_ENUM_END()

		class ForEachStatement : public LoopStatement
		{
		public:
			ForEachKind kind;
			ParseNode * variable;
			ParseNode * iterator;
			ParseNode * body;
			ForEachStatement(Location location, ForEachKind kind, ParseNode * variable, ParseNode * iterator, ParseNode * body)
				: LoopStatement(location), kind(kind), variable(variable), iterator(iterator), body(body)
			{
			}

			SERIALIZE_BEGIN(ForEachStatement)
			SERIALIZE_FIELD(kind)
			SERIALIZE_FIELD(variable)
			SERIALIZE_FIELD(iterator)
			SERIALIZE_FIELD(body)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class ContinueStatement : public ParseNode
		{
		public:
			std::optional<Identifier *> label;
			ContinueStatement(Location location, std::optional<Identifier *> label)
				: ParseNode(location), label(label)
			{
			}

			SERIALIZE_BEGIN(ContinueStatement)
			SERIALIZE_FIELD(label)
			SERIALIZE_END()

			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class BreakStatement : public ParseNode
		{
		public:
			std::optional<Identifier *> label;
			BreakStatement(Location location, std::optional<Identifier *> label)
				: ParseNode(location), label(label)
			{
			}

			SERIALIZE_BEGIN(BreakStatement)
			SERIALIZE_FIELD(label)
			SERIALIZE_END()

			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class ReturnStatement : public ParseNode
		{
		public:
			std::optional<ParseNode *> result;
			ReturnStatement(Location location, std::optional<ParseNode *> result)
				: ParseNode(location), result(result)
			{
			}

			SERIALIZE_BEGIN(ReturnStatement)
			SERIALIZE_FIELD(result)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class WithStatement : public ParseNode
		{
		public:
			ParseNode * expression;
			ParseNode * body;
			WithStatement(Location location, ParseNode * expression, ParseNode * body) : ParseNode(location), expression(expression), body(body) { }

			SERIALIZE_BEGIN(WithStatement)
			SERIALIZE_FIELD(expression)
			SERIALIZE_FIELD(body)
			SERIALIZE_END()
		};

		class SwitchStatement : public ParseNode
		{
		public:
			ParseNode * condition;
			ParseNode * body;
			SwitchStatement(Location location, ParseNode * condition, ParseNode * body) : ParseNode(location), condition(condition), body(body) { }

			SERIALIZE_BEGIN(SwitchStatement)
			SERIALIZE_FIELD(condition)
			SERIALIZE_FIELD(body)
			SERIALIZE_END()

			// TODO: handle in_tail_position in Validate
		};

		class CaseStatement : public ParseNode
		{
		public:
			ParseNode * expression;
			CaseStatement(Location location, ParseNode * expression)
				: ParseNode(location), expression(expression)
			{
			}

			SERIALIZE_BEGIN(CaseStatement)
			SERIALIZE_FIELD(expression)
			SERIALIZE_END()
		};

		class DefaultStatement : public ParseNode
		{
		public:
			DefaultStatement(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(DefaultStatement)
			SERIALIZE_END()
		};

		class ThrowStatement : public ParseNode
		{
		public:
			ParseNode * expression;
			ThrowStatement(Location location, ParseNode * expression) : ParseNode(location), expression(expression) { }

			SERIALIZE_BEGIN(ThrowStatement)
			SERIALIZE_FIELD(expression)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class CatchStatement : public ParseNode
		{
		public:
			std::optional<ParseNode *> parameter;
			ParseNode * body;
			CatchStatement(Location location, std::optional<ParseNode *> parameter, ParseNode * body) : ParseNode(location), parameter(parameter), body(body) { }

			SERIALIZE_BEGIN(CatchStatement)
			SERIALIZE_FIELD(parameter)
			SERIALIZE_FIELD(body)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;

			JS::Interpreter::State<std::optional<JSValue>> CompileCatchStatement(JS::Interpreter::State<JSValue>& initial_state);
		};

		class TryStatement : public ParseNode
		{
		public:
			ParseNode * body;
			std::vector<CatchStatement *> catch_part;
			std::optional<ParseNode *> finally_part;
			TryStatement(Location location, ParseNode * body, std::vector<CatchStatement *> catch_part, std::optional<ParseNode *> finally_part) : ParseNode(location), body(body), catch_part(catch_part), finally_part(finally_part) { }

			SERIALIZE_BEGIN(TryStatement)
			SERIALIZE_FIELD(body)
			SERIALIZE_FIELD(catch_part)
			SERIALIZE_FIELD(finally_part)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class DebuggerStatement : public ParseNode
		{
		public:
			DebuggerStatement(Location location) : ParseNode(location) { }
			SERIALIZE_BEGIN(DebuggerStatement)
			SERIALIZE_END()
		};

		// E4X
		class DefaultXmlNamespaceStatement : public ParseNode
		{
		public:
			ParseNode * expression;
			DefaultXmlNamespaceStatement(Location location, ParseNode * expression)
				: ParseNode(location), expression(expression)
			{
			}

			SERIALIZE_BEGIN(DefaultXmlNamespaceStatement)
			SERIALIZE_FIELD(expression)
			SERIALIZE_END()
		};

		class StaticClassElement : public ParseNode
		{
		public:
			ParseNode * element;
			StaticClassElement(Location location, ParseNode * element) : ParseNode(location), element(element) { }

			SERIALIZE_BEGIN(StaticClassElement)
			SERIALIZE_FIELD(element)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JS::Runtime::ClassContext> CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state) override;
		};

#if ENABLE_ES4
		class AttachedAttribute : public ParseNode
		{
		public:
			ParseNode * attribute;
			ParseNode * directive;
			AttachedAttribute(Location location, ParseNode * attribute, ParseNode * directive) : ParseNode(location), attribute(attribute), directive(directive) { }

			SERIALIZE_BEGIN(AttachedAttribute)
			SERIALIZE_FIELD(attribute)
			SERIALIZE_FIELD(directive)
			SERIALIZE_END()

			JSCompoundAttribute compiled_attributes;

			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class AttributeCombination : public ParseNode
		{
		public:
			std::vector<ParseNode *> attributes;
			AttributeCombination(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(AttributeCombination)
			SERIALIZE_FIELD(attributes)
			SERIALIZE_END()

			bool strict = false;

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};
#endif

		class ClassElementList : public ParseNode
		{
		public:
			std::vector<ParseNode *> elements;
			ClassElementList(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(ClassElementList)
			SERIALIZE_FIELD(elements)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
		};

		class StatementList : public ParseNode
		{
		public:
			std::vector<ParseNode *> statements;
			StatementList(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(StatementList)
			SERIALIZE_FIELD(statements)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
		};

		class Block : public ParseNode
		{
		public:
			std::vector<ParseNode *> statements;
			Block(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(Block)
			SERIALIZE_FIELD(statements)
			SERIALIZE_END()

#if ENABLE_ES4
			std::shared_ptr<JS::Runtime::EnvironmentRecord> LexicalEnvironment = nullptr;

			// set to true for the main block of a constructor if the superclass constructor needs to be called implicitly
			bool call_superclass_constructor = false;
#endif

			void Validate(JS::Runtime::ValidationContext cxt) override;
#if ENABLE_ES4
			void SetupContent(JS::Runtime::EnvironmentMode mode) override;
#endif
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
			JS::Interpreter::State<JS::Runtime::ClassContext> CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state) override;
		};

		class NamedImportBinding : public ParseNode
		{
		public:
			Identifier * name;
			std::optional<std::variant<Identifier *, StringLiteral *>> export_name;
			NamedImportBinding(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(NamedImportBinding)
			SERIALIZE_FIELD(name)
			SERIALIZE_FIELD(export_name)
			SERIALIZE_END()
		};

		class ImportWithEntry : public ParseNode
		{
		public:
			std::variant<Identifier *, StringLiteral *> attribute_key;
			JSString string_literal;
			ImportWithEntry(Location location, std::variant<Identifier *, StringLiteral *> attribute_key, JSString string_literal) : ParseNode(location), attribute_key(attribute_key), string_literal(string_literal) { }

			SERIALIZE_BEGIN(ImportWithEntry)
			SERIALIZE_FIELD(attribute_key)
			SERIALIZE_FIELD(string_literal)
			SERIALIZE_END()
		};

		class NamedImportBindings : public ParseNode
		{
		public:
			std::vector<NamedImportBinding *> bindings;
			NamedImportBindings(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(NamedImportBindings)
			SERIALIZE_FIELD(bindings)
			SERIALIZE_END()
		};

		class WithClause : public ParseNode
		{
		public:
			std::vector<ImportWithEntry *> entries;
			WithClause(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(WithClause)
			SERIALIZE_FIELD(entries)
			SERIALIZE_END()
		};

		class ImportStatement : public ParseNode
		{
		public:
			JSString from_module;
			std::optional<Identifier *> default_binding;
			std::optional<std::variant<Identifier *, NamedImportBindings *>> bindings;
			std::optional<WithClause *> with_clause;
			ImportStatement(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(ImportStatement)
			SERIALIZE_FIELD(from_module)
			SERIALIZE_FIELD(default_binding)
			SERIALIZE_FIELD(bindings)
			SERIALIZE_FIELD(with_clause)
			SERIALIZE_END()

			JS::Runtime::ModuleRequestRecord GetModuleRequest();
			std::vector<JS::Runtime::ImportEntry> GetImportEntries();
			void Validate(JS::Runtime::ValidationContext cxt) override;
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class ExportSpecifier : public ParseNode
		{
		public:
			std::variant<Identifier *, StringLiteral *> name;
			std::optional<std::variant<Identifier *, StringLiteral *>> export_name;

			ExportSpecifier(Location location, std::variant<Identifier *, StringLiteral *> name, std::optional<std::variant<Identifier *, StringLiteral *>> export_name) : ParseNode(location), name(name), export_name(export_name) { }

			SERIALIZE_BEGIN(ExportSpecifier)
			SERIALIZE_FIELD(name)
			SERIALIZE_FIELD(export_name)
			SERIALIZE_END()
		};

		class NamedExports : public ParseNode
		{
		public:
			std::vector<ExportSpecifier *> export_specifiers;
			NamedExports(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(NamedExports)
			SERIALIZE_FIELD(export_specifiers)
			SERIALIZE_END()
		};

		class ExportFromClause : public ParseNode
		{
		public:
			// '*' is represented as missing
			// '*' as ModuleExportName is represented as ModuleExportName
			// NamedExports is represented as a NamedExports instance
			std::optional<ParseNode *> export_clause;
			std::optional<JSString> from_module;
			std::optional<WithClause *> with_clause;
			ExportFromClause(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(ExportFromClause)
			SERIALIZE_FIELD(export_clause)
			SERIALIZE_FIELD(from_module)
			SERIALIZE_FIELD(with_clause)
			SERIALIZE_END()

			std::optional<JS::Runtime::ModuleRequestRecord> GetModuleRequest();
		};

		class ExportStatement : public ParseNode
		{
		public:
			ParseNode * expression;
			bool is_default;
			ExportStatement(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(ExportStatement)
			SERIALIZE_FIELD(expression)
			SERIALIZE_FIELD(is_default)
			SERIALIZE_END()

			std::optional<JS::Runtime::ModuleRequestRecord> GetModuleRequest();
			std::vector<JS::Runtime::ExportEntry> GetExportEntries();
			void Validate(JS::Runtime::ValidationContext cxt) override;
			void CreateBindings(JS::Runtime::EnvironmentMode mode) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

#if ENABLE_ES4
		class NamespaceStatement : public ParseNode
		{
		public:
			Identifier * name;
			std::optional<ParseNode *> uri;
			NamespaceStatement(Location location, Identifier * name) : ParseNode(location), name(name) { }

			SERIALIZE_BEGIN(NamespaceStatement)
			SERIALIZE_FIELD(name)
			SERIALIZE_FIELD(uri)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class UseNamespaceStatement : public ParseNode
		{
		public:
			std::vector<ParseNode *> namespaces;
			UseNamespaceStatement(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(UseNamespaceStatement)
			SERIALIZE_FIELD(namespaces)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		// only part of ES4.2003
		class PackageName : public ParseNode
		{
		public:
			std::vector<JSString> package_names;
			PackageName(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(PackageName)
			SERIALIZE_FIELD(package_names)
			SERIALIZE_END()
		};

		// only part of ES4.2003
		class ES4ImportStatement : public ParseNode
		{
		public:
			std::optional<JSString> import_name;
			std::variant<JSString, PackageName *> package_name;
			ES4ImportStatement(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(ES4ImportStatement)
			SERIALIZE_FIELD(import_name)
			SERIALIZE_FIELD(package_name)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};

		class PackageStatement : public ParseNode
		{
		public:
			std::optional<ParseNode *> package_name;
			Block * body;
			PackageStatement(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(PackageStatement)
			SERIALIZE_FIELD(package_name)
			SERIALIZE_FIELD(body)
			SERIALIZE_END()

			JSPackage CompiledPackage;

			JS::Interpreter::State<std::optional<JSValue>> Process(JS::Runtime::ValidationContext cxt, JS::Interpreter::State<>& initial_state) override;

			JS::Runtime::SourceTextPackageRecord * CreatePackageRecord(JS::Runtime::Realm * realm, std::optional<std::filesystem::path> filename);
		};

		class PragmaItem : public ParseNode
		{
		public:
			JSString name;
			std::optional<std::variant<JSBoolean, JSNumber, JSString>> argument;
			bool optional = false;
			PragmaItem(Location location, JSString name) : ParseNode(location), name(name) { }

			SERIALIZE_BEGIN(PragmaItem)
			SERIALIZE_FIELD(name)
			SERIALIZE_FIELD(argument)
			SERIALIZE_FIELD(optional)
			SERIALIZE_END()
		};

		class PragmaDeclaration : public ParseNode
		{
		public:
			std::vector<PragmaItem *> items;
			PragmaDeclaration(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(PragmaDeclaration)
			SERIALIZE_FIELD(items)
			SERIALIZE_END()

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<std::optional<JSValue>> CompileStatement(JS::Interpreter::State<>& initial_state) override;
		};
#endif

		class ScriptOrModule : public ParseNode
		{
		public:
			StatementList * body;
			ScriptOrModule() : ParseNode(Location()) { }
			ScriptOrModule(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(ScriptOrModule)
			SERIALIZE_FIELD(body)
			SERIALIZE_END()

			JS::Interpreter::State<std::optional<JSValue>> Process(JS::Runtime::ValidationContext cxt, JS::Interpreter::State<>& initial_state) override;

			JS::Runtime::ScriptRecord * CreateScriptRecord(JS::Runtime::Realm * realm, std::optional<std::filesystem::path> filename);
			JS::Runtime::SourceTextModuleRecord * CreateModuleRecord(JS::Runtime::Realm * realm, std::optional<std::filesystem::path> filename);
		};

		// E4X
		class Attribute : public ParseNode
		{
		public:
			ParseNode * selector;
			Attribute(Location location, ParseNode * selector) : ParseNode(location), selector(selector) { }

			SERIALIZE_BEGIN(Attribute)
			SERIALIZE_FIELD(selector)
			SERIALIZE_END()

			bool strict = false;

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class WildcardIdentifier : public ParseNode
		{
		public:
			WildcardIdentifier(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(WildcardIdentifier)
			SERIALIZE_END()

#if ENABLE_ES4
			JSType EvaluateType() override;
#endif
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X and ECMAScript 4
		class QualifiedIdentifier : public ParseNode
		{
		public:
			ParseNode * namespace_selector;
			ParseNode * element_selector;
			QualifiedIdentifier(Location location, ParseNode * namespace_selector, ParseNode * element_selector) : ParseNode(location), namespace_selector(namespace_selector), element_selector(element_selector) { }

			SERIALIZE_BEGIN(QualifiedIdentifier)
			SERIALIZE_FIELD(namespace_selector)
			SERIALIZE_FIELD(element_selector)
			SERIALIZE_END()

			bool strict = false;

			void Validate(JS::Runtime::ValidationContext cxt) override;
			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
			JS::Interpreter::State<JSIdentifierReference> CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLIdentifier : public ParseNode
		{
		public:
			JSString StringValue;
			XMLIdentifier(Location location, JSString StringValue) : ParseNode(location), StringValue(StringValue) { }

			SERIALIZE_BEGIN(XMLIdentifier)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLAttributeValue : public ParseNode
		{
		public:
			JSString StringValue;
			XMLAttributeValue(Location location, JSString StringValue) : ParseNode(location), StringValue(StringValue) { }

			SERIALIZE_BEGIN(XMLAttributeValue)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLElement : public ParseNode
		{
		public:
			ParseNode * tag;
			std::vector<ParseNode *> attributes;
			std::vector<ParseNode *> content;
			std::optional<ParseNode *> endtag;
			XMLElement(Location location, ParseNode * tag) : ParseNode(location), tag(tag) { }

			SERIALIZE_BEGIN(XMLElement)
			SERIALIZE_FIELD(tag)
			SERIALIZE_FIELD(attributes)
			SERIALIZE_FIELD(content)
			SERIALIZE_FIELD(endtag)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLAttribute : public ParseNode
		{
		public:
			ParseNode * name;
			ParseNode * value;
			XMLAttribute(Location location, ParseNode * name, ParseNode * value) : ParseNode(location), name(name), value(value) { }

			SERIALIZE_BEGIN(XMLAttribute)
			SERIALIZE_FIELD(name)
			SERIALIZE_FIELD(value)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLPI : public ParseNode
		{
		public:
			JSString StringValue;
			XMLPI(Location location, JSString StringValue) : ParseNode(location), StringValue(StringValue) { }

			SERIALIZE_BEGIN(XMLPI)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLComment : public ParseNode
		{
		public:
			JSString StringValue;
			XMLComment(Location location, JSString StringValue) : ParseNode(location), StringValue(StringValue) { }

			SERIALIZE_BEGIN(XMLComment)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLCDATA : public ParseNode
		{
		public:
			JSString StringValue;
			XMLCDATA(Location location, JSString StringValue) : ParseNode(location), StringValue(StringValue) { }

			SERIALIZE_BEGIN(XMLCDATA)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLText : public ParseNode
		{
		public:
			JSString StringValue;
			XMLText(Location location, JSString StringValue) : ParseNode(location), StringValue(StringValue) { }

			SERIALIZE_BEGIN(XMLText)
			SERIALIZE_FIELD(StringValue)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLInitializer : public ParseNode
		{
		public:
			ParseNode * content;
			XMLInitializer(Location location, ParseNode * content) : ParseNode(location), content(content) { }

			SERIALIZE_BEGIN(XMLInitializer)
			SERIALIZE_FIELD(content)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};

		// E4X
		class XMLList : public ParseNode
		{
		public:
			std::vector<ParseNode *> content;
			XMLList(Location location) : ParseNode(location) { }

			SERIALIZE_BEGIN(XMLList)
			SERIALIZE_FIELD(content)
			SERIALIZE_END()

			JS::Interpreter::State<JSValue> CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase)) override;
		};
	}
}

#endif // _NODE_HH
