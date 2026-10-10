#ifndef _PARSER_HH
#define _PARSER_HH

/* Routines and structures needed for parsing */

#include <iostream>
#include <optional>
#include <tuple>
#include <unicode/uchar.h>
#include <variant>
#include <vector>

#include "common.hh"
#include "ast.hh"

namespace JS
{
	using namespace JS::Public;

	namespace Parser
	{
		enum class Context
		{
			InputElementUnspecified,
			InputElementDiv,
			// ES3
			InputElementRegExp,
			// E4X
			InputElementXMLTag,
			// E4X
			InputElementXMLContent,
			// ES6
			InputElementRegExpOrTemplateTail,
			// ES6
			InputElementTemplateTail,
			// ESNext
			InputElementHashbangOrRegExp,
		};
		constexpr Context InputElementTerminateOnNewLine = Context(int(Context::InputElementHashbangOrRegExp) + 1);

		typedef std::variant<JSString, JSNumber, JSBigInt, std::tuple<JSString, JSString>> ValueType;

		class Token
		{
		public:
			TokenType type;
			ValueType value;
			KeywordType keyword;
			Location location;
			Token() : type(TokenType::EndOfFile), value(JSNumber(0.0)), keyword(KeywordType::not_a_keyword) { }
		};

		class Tokenizer
		{
		public:
			Location last_location;

			// to keep track of the effect of the next character on the position
			enum class NextCharEffect
			{
				None,
				CR,
				NL,
			};
			NextCharEffect next_char_type;

#if 0
			// part of the line already read
			JSString current_line;
			// collects characters
			std::set<JSString *> readers;

			void StartReader(JSString& reader, Location location)
			{
				readers.insert(&reader);
				reader = current_line.substr(location.column - 1);
			}

			void StopReader(JSString& reader)
			{
				readers.erase(&reader);
			}
#endif
			JSString read_source;

			virtual ~Tokenizer() = default;
		protected:
			virtual char32_t _NextChar() = 0;
		public:
			virtual char32_t LookAhead() = 0;

			static inline bool IsWhiteSpace(char32_t c)
			{
				switch(c)
				{
				case U'\u0009':
				case U'\u000B':
				case U'\u000C':
				case U'\u0020':
					return true;
				case U'\u00A0':
					return CurrentVersion >= Version::ES3; // TODO: is this not also U_SPACE_SEPARATOR?
				case U'\uFFEF':
					return CurrentVersion >= Version::ES5;
#if ENABLE_ES4
				case U'\u2000':
				case U'\u2001':
				case U'\u2002':
				case U'\u2003':
				case U'\u2004':
				case U'\u2005':
				case U'\u2006':
				case U'\u2007':
				case U'\u2008':
				case U'\u2009':
				case U'\u200A':
				case U'\u200B':
				case U'\u3000':
					if(IsVersionES4())
						return true;
					[[fallthrough]];
#endif
				default:
#if ENABLE_ES4
					if(IsVersionES4())
						return false;
#endif
					// Zs
					return CurrentVersion >= Version::ES3 && u_charType(c) == U_SPACE_SEPARATOR;
				}
			}

			static inline bool IsLineTerminator(char32_t c)
			{
				switch(c)
				{
				case U'\u000A':
				case U'\u000D':
					return true;
				case U'\u2028':
				case U'\u2029':
					return CurrentVersion >= Version::ES3;
#if ENABLE_ES4
				case U'\u0085':
					return IsVersionES4();
#endif
				default:
					return false;
				}
			}

			virtual char32_t NextChar();

			static inline bool IsUnicodeLetter(char32_t c)
			{
				switch(u_charType(c))
				{
				case U_UPPERCASE_LETTER: // Lu
				case U_LOWERCASE_LETTER: // Ll
				case U_TITLECASE_LETTER: // Lt
				case U_MODIFIER_LETTER: // Lm
				case U_OTHER_LETTER: // Lo
				case U_LETTER_NUMBER: // Nl
					return true;
				default:
					return false;
				}
			}

			static inline bool IsUnicodeDigit(char32_t c)
			{
				return u_charType(c) == U_DECIMAL_DIGIT_NUMBER; // Nd
			}

			static inline bool IsIdentifierStart(char32_t c)
			{
				switch(CurrentVersion)
				{
				case Version::ES1:
					return c == U'_' || c == U'$' || (U'A' <= c && c <= U'Z') || (U'a' <= c && c <= U'z');
				default:
					if(c == U'_' || c == U'$')
					{
						return true;
					}
					return IsUnicodeLetter(c);
				}
			}

			static inline bool IsIdentifierPart(char32_t c)
			{
				switch(CurrentVersion)
				{
				case Version::ES1:
					return (U'0' <= c && c <= U'9') || IsIdentifierStart(c);
				default:
					if(c == U'\u200C' || c == U'\u200D')
					{
						return true;
					}
					[[fallthrough]];

				case Version::ES3:
#if ENABLE_ES4
				case Version::ES4_2003:
				case Version::ES4_2008:
#endif
					if(c == U'_' || c == U'$')
					{
						return true;
					}

					switch(u_charType(c))
					{
					case U_UPPERCASE_LETTER: // Lu
					case U_LOWERCASE_LETTER: // Ll
					case U_TITLECASE_LETTER: // Lt
					case U_MODIFIER_LETTER: // Lm
					case U_OTHER_LETTER: // Lo
					case U_LETTER_NUMBER: // Nl
					case U_NON_SPACING_MARK: // Mn
					case U_COMBINING_SPACING_MARK: // Mc
					case U_DECIMAL_DIGIT_NUMBER: // Nd
					case U_CONNECTOR_PUNCTUATION: // Pc
						return true;
					default:
						return false;
					}
				}
			}

			static inline TokenType CheckReservedWord(JSString identifier)
			{
#if ENABLE_ES4
				if(identifier == u"as")
				{
					return IsVersionES4() ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else
#endif
				if(identifier == u"abstract")
				{
					// ES4.2003 reserves it, ES4.2006 does not treat is as reserved, it is included here for JScript .NET compatibility
					return CurrentVersion < Version::ES5 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"await")
				{
					return Version::ES6 <= CurrentVersion ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"boolean")
				{
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"break")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"byte")
				{
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"case")
				{
					return TokenType::ReservedWord;
				}
#if 0
				else if(identifier == u"cast")
				{
					// ES4.2006 considers this a keyword
					return IsVersionES4() ? TokenType::ReservedWord : TokenType::Identifier;
				}
#endif
				else if(identifier == u"catch")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"char")
				{
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"class")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"const")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"continue")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"debugger")
				{
					// ES4.2003 reserves it, ES4.2006 does not treat is as reserved
#if ENABLE_ES4
					if(IsVersionES4())
						return TokenType::Identifier;
#endif
					return TokenType::ReservedWord;
				}
				else if(identifier == u"default")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"delete")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"do")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"double")
				{
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"else")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"enum")
				{
					// ES4.2003 reserves it, ES4.2006 does not treat is as reserved
#if ENABLE_ES4
					if(IsVersionES4())
						return TokenType::Identifier;
#endif
					return TokenType::ReservedWord;
				}
				else if(identifier == u"export")
				{
					// ES4.2003 reserves it, ES4.2006 does not treat is as reserved
#if ENABLE_ES4
					if(IsVersionES4())
						return TokenType::Identifier;
#endif
					return TokenType::ReservedWord;
				}
				else if(identifier == u"extends")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"false")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"final")
				{
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"finally")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"float")
				{
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"for")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"function")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"goto")
				{
					// ES4.2003 reserves it, ES4.2006 does not treat is as reserved
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"if")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"implements")
				{
					return CurrentVersion < Version::ES5 || StrictMode ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"import")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"in")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"instanceof")
				{
					return TokenType::ReservedWord;
				}
#if ENABLE_ES4
				else if(identifier == u"is")
				{
					return IsVersionES4() ? TokenType::ReservedWord : TokenType::Identifier;
				}
#endif
				else if(identifier == u"int")
				{
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"interface")
				{
					return CurrentVersion < Version::ES5 || StrictMode ? TokenType::ReservedWord : TokenType::Identifier;
				}
#if 0
				else if(identifier == u"internal")
				{
					// ES4.2006 considers this a keyword
					return IsVersionES4() ? TokenType::ReservedWord : TokenType::Identifier;
				}
#endif
				else if(identifier == u"let")
				{
					return Version::ES5 <= CurrentVersion && StrictMode ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"long")
				{
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"native")
				{
					return CurrentVersion < Version::ES5 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"new")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"null")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"package")
				{
					return CurrentVersion < Version::ES5 || StrictMode ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"private")
				{
					return CurrentVersion < Version::ES5 || StrictMode ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"protected")
				{
					return CurrentVersion < Version::ES5 || StrictMode ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"public")
				{
					return CurrentVersion < Version::ES5 || StrictMode ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"return")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"short")
				{
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"static")
				{
					return CurrentVersion <= Version::ES3 || (Version::ES5 <= CurrentVersion && StrictMode) ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"super")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"switch")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"synchronized")
				{
					// ES4.2003 reserves it, ES4.2006 does not treat is as reserved
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"this")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"throw")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"throws")
				{
					// ES4.2003 reserves it, ES4.2006 does not treat is as reserved
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
#if 0
				else if(identifier == u"to")
				{
					// ES4.2006 considers this a keyword
					return IsVersionES4() ? TokenType::ReservedWord : TokenType::Identifier;
				}
#endif
				else if(identifier == u"transient")
				{
					// ES4.2003 reserves it, ES4.2006 does not treat is as reserved
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"true")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"try")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"typeof")
				{
					return TokenType::ReservedWord;
				}
#if ENABLE_ES4
				else if(identifier == u"use")
				{
					return IsVersionES4() ? TokenType::ReservedWord : TokenType::Identifier;
				}
#endif
				else if(identifier == u"var")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"void")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"volatile")
				{
					// ES4.2003 reserves it, ES4.2006 does not treat is as reserved
					return CurrentVersion <= Version::ES3 ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else if(identifier == u"while")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"with")
				{
					return TokenType::ReservedWord;
				}
				else if(identifier == u"yield")
				{
					return (CurrentVersion == Version::ES5 && StrictMode) || Version::ES6 <= CurrentVersion ? TokenType::ReservedWord : TokenType::Identifier;
				}
				else
				{
					return TokenType::Identifier;
				}
			}

			static inline KeywordType CheckKeyword(JSString identifier)
			{
#if ENABLE_ES4
				if(identifier == u"abstract")
				{
					// included for JScript .NET compatibility
					return KeywordType::Abstract;
				}
				else
#endif
				if(identifier == u"as")
				{
					return KeywordType::As;
				}
				else if(identifier == u"async")
				{
					return KeywordType::Async;
				}
				else if(identifier == u"await")
				{
					return KeywordType::Await;
				}
				else if(identifier == u"break")
				{
					return KeywordType::Break;
				}
				else if(identifier == u"case")
				{
					return KeywordType::Case;
				}
#if 0
				else if(identifier == u"cast")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Cast;
				}
#endif
				else if(identifier == u"catch")
				{
					return KeywordType::Catch;
				}
				else if(identifier == u"class")
				{
					return KeywordType::Class;
				}
				else if(identifier == u"const")
				{
					return KeywordType::Const;
				}
				else if(identifier == u"continue")
				{
					return KeywordType::Continue;
				}
				else if(identifier == u"debugger")
				{
					return KeywordType::Debugger;
				}
				else if(identifier == u"default")
				{
					return KeywordType::Default;
				}
				else if(identifier == u"delete")
				{
					return KeywordType::Delete;
				}
				else if(identifier == u"do")
				{
					return KeywordType::Do;
				}
#if ENABLE_ES4
				else if(identifier == u"dynamic")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Dynamic;
				}
#endif
				else if(identifier == u"each")
				{
					return KeywordType::Each;
				}
				else if(identifier == u"else")
				{
					return KeywordType::Else;
				}
				else if(identifier == u"export")
				{
					return KeywordType::Export;
				}
				else if(identifier == u"extends")
				{
					return KeywordType::Extends;
				}
				else if(identifier == u"false")
				{
					return KeywordType::False;
				}
				else if(identifier == u"finally")
				{
					return KeywordType::Finally;
				}
#if ENABLE_ES4
				else if(identifier == u"final")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Final;
				}
#endif
				else if(identifier == u"for")
				{
					return KeywordType::For;
				}
				else if(identifier == u"from")
				{
					return KeywordType::From;
				}
				else if(identifier == u"function")
				{
					return KeywordType::Function;
				}
				else if(identifier == u"get")
				{
					return KeywordType::Get;
				}
				else if(identifier == u"if")
				{
					return KeywordType::If;
				}
#if ENABLE_ES4
				else if(identifier == u"implements")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Implements;
				}
#endif
				else if(identifier == u"import")
				{
					return KeywordType::Import;
				}
				else if(identifier == u"in")
				{
					return KeywordType::In;
				}
#if 0
				else if(identifier == u"include")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Include;
				}
#endif
				else if(identifier == u"instanceof")
				{
					return KeywordType::Instanceof;
				}
#if ENABLE_ES4
				else if(identifier == u"interface")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Interface;
				}
				else if(identifier == u"internal")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Internal;
				}
				else if(identifier == u"is")
				{
					return KeywordType::Is;
				}
#endif
				else if(identifier == u"let")
				{
					return KeywordType::Let;
				}
				else if(identifier == u"meta")
				{
					return KeywordType::Meta;
				}
				else if(identifier == u"namespace")
				{
					return KeywordType::Namespace;
				}
#if ENABLE_ES4
				else if(identifier == u"native")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Native;
				}
#endif
				else if(identifier == u"new")
				{
					return KeywordType::New;
				}
				else if(identifier == u"null")
				{
					return KeywordType::Null;
				}
				else if(identifier == u"of")
				{
					return KeywordType::Of;
				}
#if ENABLE_ES4
				else if(identifier == u"override")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Override;
				}
				else if(identifier == u"package")
				{
					return KeywordType::Package;
				}
				else if(identifier == u"private")
				{
					return KeywordType::Private;
				}
				else if(identifier == u"protected")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Protected;
				}
				else if(identifier == u"prototype")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Prototype;
				}
				else if(identifier == u"public")
				{
					return KeywordType::Public;
				}
#endif
				else if(identifier == u"return")
				{
					return KeywordType::Return;
				}
				else if(identifier == u"set")
				{
					return KeywordType::Set;
				}
				else if(identifier == u"static")
				{
					return KeywordType::Static;
				}
				else if(identifier == u"super")
				{
					return KeywordType::Super;
				}
				else if(identifier == u"switch")
				{
					return KeywordType::Switch;
				}
				else if(identifier == u"target")
				{
					return KeywordType::Target;
				}
				else if(identifier == u"this")
				{
					return KeywordType::This;
				}
				else if(identifier == u"throw")
				{
					return KeywordType::Throw;
				}
#if 0
				else if(identifier == u"to")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::To;
				}
#endif
				else if(identifier == u"true")
				{
					return KeywordType::True;
				}
				else if(identifier == u"try")
				{
					return KeywordType::Try;
				}
#if 0
				else if(identifier == u"type")
				{
					// ES4.2006 considers this a keyword
					return KeywordType::Type;
				}
#endif
				else if(identifier == u"typeof")
				{
					return KeywordType::Typeof;
				}
#if ENABLE_ES4
				else if(identifier == u"use")
				{
					return KeywordType::Use;
				}
#endif
				else if(identifier == u"var")
				{
					return KeywordType::Var;
				}
				else if(identifier == u"void")
				{
					return KeywordType::Void;
				}
				else if(identifier == u"while")
				{
					return KeywordType::While;
				}
				else if(identifier == u"with")
				{
					return KeywordType::With;
				}
				else if(identifier == u"xml")
				{
					return KeywordType::Xml;
				}
				else if(identifier == u"yield")
				{
					return KeywordType::Yield;
				}
				else
				{
					return KeywordType::not_a_keyword;
				}
			}

			static inline bool IsBinaryDigit(char32_t c)
			{
				return c == U'0' || c == U'1';
			}

			static inline int GetBinaryDigitValue(char32_t c)
			{
				return c - U'0';
			}

			static inline bool IsOctalDigit(char32_t c)
			{
				return U'0' <= c && c <= U'7';
			}

			static inline int GetOctalDigitValue(char32_t c)
			{
				return c - U'0';
			}

			static inline bool IsDecimalDigit(char32_t c)
			{
				return U'0' <= c && c <= U'9';
			}

			static inline int GetDecimalDigitValue(char32_t c)
			{
				return c - U'0';
			}

			static inline bool IsHexaDecimalDigit(char32_t c)
			{
				return (U'0' <= c && c <= U'9') || (U'A' <= c && c <= U'F') || (U'a' <= c && c <= U'f');
			}

			static inline int GetHexaDecimalDigitValue(char32_t c)
			{
				if(U'A' <= c && c <= U'F')
				{
					return c - U'A' + 10;
				}
				else if(U'a' <= c && c <= U'f')
				{
					return c - U'a' + 10;
				}
				else
				{
					return c - U'0';
				}
			}

			static inline void AddChar(JSString& text, char32_t c)
			{
				/*if(c <= 0xFFFF)
				{
					text += char16_t(c);
				}
				else if(c <= 0x10FFFF)
				{
					c -= 0x10000;
					text += char16_t(0xD800 + (c >> 10));
					text += char16_t(0xDC00 + (c & 0x03FF));
				}*/
				text += JS::UTF32toUTF16(c);
				// otherwise, invalid
			}

			char32_t PushBackBuffer = U'\0';

			char32_t ReadUnicodeEscapeSequence(size_t digit_count = 4);
			bool ValidNumericLiteralTermination();
			void ParseEscapeSequence(JSString& destination);
			Token ParseXMLContent();
			Token ParseXMLTag();

			Token ParseToken(Context context);
		};

		class UTF8StreamTokenizer : public Tokenizer
		{
		public:
			std::istream& input;
			UTF8StreamTokenizer(std::istream& input) : input(input) { }

			bool stream_available = true;
			std::optional<uint8_t> next_byte;
			std::optional<char32_t> next_char;

			int NextByte();
			void PutBackByte(uint8_t byte);

		protected:
			char32_t _NextChar() override;
		public:
			void PutBackChar(char32_t value);
			char32_t LookAhead() override;
			char32_t NextChar() override;
		};

		class Parser
		{
		public:
			Tokenizer * tokenizer;
			Parser(Tokenizer * tokenizer) : tokenizer(tokenizer) { }

			struct ParseResult
			{
				Context context;
				Token token;
				bool after_line_terminator;
			};

			// we simulate a partial token buffer
			// normally, only one element is part of the buffer which gets flushed on ConsumeToken and filled on AdvanceToken
			// using the StashToken function, we can advance the TokenPosition pointer without flushing the buffer
			// since tokens are parsed in a specific context, we have to maintain the context in which it was parsed and match against any repeated AdvanceToken use
			std::vector<ParseResult> TokenBuffer;
			size_t TokenPosition = 0;
			bool TokenActive = false;

			Token PeekToken();
			Token ConsumeToken();
			Token AdvanceToken(Context context = Context::InputElementUnspecified, std::optional<Context> new_line_context = std::optional<Context>());
			static constexpr std::optional<Context> SameOrTermination(Context context)
			{
				return context == InputElementTerminateOnNewLine ? InputElementTerminateOnNewLine : std::optional<Context>();
			}
			static constexpr std::optional<Context> SameOrTermination(bool terminate_on_new_line)
			{
				return terminate_on_new_line ? InputElementTerminateOnNewLine : std::optional<Context>();
			}
			static constexpr Context SameOrTermination(Context context, bool terminate_on_new_line)
			{
				return terminate_on_new_line ? InputElementTerminateOnNewLine : context;
			}

			Token StashToken();
			Token StepBack(size_t Steps = 1);
			bool HadLineTerminator();
			Context GetCurrentContext();
			void PurgeStashedTokens();

			bool IsDivisionInContext()
			{
				switch(GetCurrentContext())
				{
				case Context::InputElementDiv:
				case Context::InputElementTemplateTail:
					return true;
				case Context::InputElementRegExp:
				case Context::InputElementXMLTag:
				case Context::InputElementXMLContent:
				case Context::InputElementRegExpOrTemplateTail:
				case Context::InputElementHashbangOrRegExp:
				case Context::InputElementUnspecified:
					return false;
				}
				JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"abnormal parse context"));
			}

			bool IsRegExpInContext()
			{
				switch(GetCurrentContext())
				{
				case Context::InputElementDiv:
				case Context::InputElementXMLTag:
				case Context::InputElementXMLContent:
				case Context::InputElementTemplateTail:
					return false;
				case Context::InputElementRegExp:
				case Context::InputElementRegExpOrTemplateTail:
				case Context::InputElementHashbangOrRegExp:
				case Context::InputElementUnspecified:
					return true;
				}
				JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"abnormal parse context"));
			}

			bool IsXMLTagInContext()
			{
				switch(GetCurrentContext())
				{
				case Context::InputElementRegExp:
				case Context::InputElementXMLContent:
					return true;
				case Context::InputElementDiv:
				case Context::InputElementXMLTag:
				case Context::InputElementTemplateTail:
				case Context::InputElementRegExpOrTemplateTail:
				case Context::InputElementHashbangOrRegExp:
				case Context::InputElementUnspecified:
					return false;
				}
				JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"abnormal parse context"));
			}

			bool IsTemplateTailInContext()
			{
				switch(GetCurrentContext())
				{
				case Context::InputElementDiv:
				case Context::InputElementRegExp:
				case Context::InputElementXMLTag:
				case Context::InputElementXMLContent:
				case Context::InputElementHashbangOrRegExp:
					return false;
				case Context::InputElementRegExpOrTemplateTail:
				case Context::InputElementTemplateTail:
				case Context::InputElementUnspecified:
					return true;
				}
				JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"abnormal parse context"));
			}

			bool IsHashBangInContext()
			{
				switch(GetCurrentContext())
				{
				case Context::InputElementDiv:
				case Context::InputElementRegExp:
				case Context::InputElementXMLTag:
				case Context::InputElementXMLContent:
				case Context::InputElementRegExpOrTemplateTail:
				case Context::InputElementTemplateTail:
					return false;
				case Context::InputElementHashbangOrRegExp:
				case Context::InputElementUnspecified:
					return true;
				}
				JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"abnormal parse context"));
			}

			bool IsIdentifier(TokenType type);

			JS::ParseNode::Identifier * ProcessIdentifier(Token token, bool Yield, bool Await);
			JS::ParseNode::Identifier * ProcessBindingIdentifier(Token token, bool Yield, bool Await);
			ParseNode * ParsePropertyIdentifier(bool Yield, bool Await, Context following_context = Context::InputElementDiv, bool terminate_on_new_line = false, bool allow_keywords = false);
			JS::ParseNode::Identifier * ParseIdentifier(bool Yield, bool Await);
			JS::ParseNode::Identifier * ParseBindingIdentifier(bool Yield, bool Await);
			ParseNode * ParseBindingPropertyDefinition(bool Yield, bool Await);
			enum class MemberContext
			{
				PropertyDefinition,
				ClassElement,
			};
			ParseNode * ParseMember(MemberContext member_context, bool Yield, bool Await);
			ParseNode * ParseSubstitutionTemplate(bool Yield, bool Await, bool Tagged);
			void ParseXMLElementContent(std::vector<ParseNode *>& content);
			ParseNode * ParseXMLElement();
			ParseNode * ParseBindingElement(bool Yield, bool Await);
			JS::ParseNode::FormalParameters * ParseFormalParameters(bool Yield, bool Await);

			JS::ParseNode::FormalParameters * ConvertToFormalParameters(ParseNode * expression);
			ParseNode * ParseArrowBody(bool is_async, bool In, Context following_context = Context::InputElementDiv, bool terminate_on_new_line = false);

			enum class Precedence
			{
#if ENABLE_ES4
				AbstractStatement, // functions are parsed without a body
#endif
				Statement, // parses an expression or expression-like statement in a statement context
				CommaExpression,
				AssignmentExpression,
				ConditionalExpression,
				LogicalOrExpression,
#if ENABLE_ES4
				LogicalXorExpression,
#endif
				LogicalAndExpression,
				BitwiseOrExpression,
				BitwiseXorExpression,
				BitwiseAndExpression,
				EqualityExpression,
				RelationalExpression,
				ShiftExpression,
				AdditiveExpression,
				MultiplicativeExpression,
				ExponentiationExpression,
				UnaryExpression,
				LeftHandSideExpression,
				NewExpression,
			};

#if ENABLE_ES4
			ParseNode * ParseTypeExpression(bool In, bool Yield, bool Await, bool is_type_parameter = false /* needed to avoid parsing '>' */);
#endif
			ParseNode * ParseExpression(Precedence precedence, bool In, bool Yield, bool Await, bool Default = true, Context following_context = Context::InputElementDiv, bool terminate_on_new_line = false);
			ParseNode * ParseExpression(Precedence precedence, bool In, bool Yield, bool Await, Context following_context, bool terminate_on_new_line = false)
			{
				return ParseExpression(precedence, In, Yield, Await, /* Default = */ true, following_context, terminate_on_new_line);
			}

			ParseNode * ParseVarOrLexicalDeclaration(bool In, bool Yield, bool Await, bool terminate_on_newline = false, bool for_declaration = false);

			enum class ParseLevel
			{
				Clause,
				Block,
				Script,
				Module,
			};

			enum class StatementContext
			{
				General,
				SwitchBodyStart,
				SwitchBody,
				PastDefault,
#if ENABLE_ES4
				DoWhileBody,
#endif
			};

			enum class BlockParseType
			{
				PermitNothing,
				PermitCaseDefault,
				PermitDirectives,
#if ENABLE_ES4
				PermitBodylessFunctions,
#endif
			};

			std::optional<JS::ParseNode::ParseNode *> ParseForInitializer(bool Yield, bool Await);
			std::optional<JS::ParseNode::WithClause *> ParseWithClause(bool terminate_on_new_line = false);
			void ParseSemicolon(Context following_context = Context::InputElementRegExp AND_FLAG(do_while_block = false));
			ParseNode * ParseStatement(
				ParseLevel level,
				bool Yield,
				bool Await,
				bool Return,
				StatementContext context = StatementContext::General,
				bool terminate_on_new_line = false
				AND_FLAG(permit_bodyless_functions = false)
			);
			ParseNode * ParseStatement(
				ParseLevel level,
				bool Yield,
				bool Await,
				bool Return,
				bool terminate_on_new_line
				AND_FLAG(permit_bodyless_functions = false)
			)
			{
				return ParseStatement(level, Yield, Await, Return, StatementContext::General, terminate_on_new_line WITH_FLAG(permit_bodyless_functions));
			}

			JS::ParseNode::ClassElementList * ParseClassElementList(bool Yield, bool Await);
			JS::ParseNode::StatementList * ParseStatementList(StatementContext context, ParseLevel level, bool Yield, bool Await, bool Return, BlockParseType parse_type = BlockParseType::PermitNothing);
			JS::ParseNode::ClassElementList * ParseClassBody(bool Yield, bool Await);
			JS::ParseNode::Block * ParseBlock(bool Yield, bool Await, bool Return, BlockParseType parse_type = BlockParseType::PermitNothing);

			enum class ExpectedScriptType
			{
				Either,
				Script,
				Module,
			};

			JS::ParseNode::ScriptOrModule * ParseScriptOrModuleText(ExpectedScriptType expected = ExpectedScriptType::Either)
			{
#if ENABLE_ES4
				if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
				if(CurrentVersion < Version::ES6)
#endif
				{
					expected = ExpectedScriptType::Script;
				}

				AdvanceToken(JS::Parser::Context::InputElementHashbangOrRegExp);
				auto script = new JS::ParseNode::ScriptOrModule(tokenizer->last_location);
				script->body = ParseStatementList(
					StatementContext::General,
					expected != ExpectedScriptType::Script ? ParseLevel::Module : ParseLevel::Script,
					/* Yield = */ false,
					/* Await = */ expected != ExpectedScriptType::Script,
					/* Return = */ false,
					BlockParseType::PermitDirectives);
				if(PeekToken().type != JS::Parser::TokenType::EndOfFile)
				{
					JS::Throw(JS::New<::JS::Class::SyntaxError> __LOC(u"Unexpected tokens at the end of the script/module"));
				}
				return script;
			}

			JS::Runtime::ScriptOrModuleRecord * ParseScriptOrModule(JS::Runtime::Realm * realm, ExpectedScriptType expected = ExpectedScriptType::Either);
			JS::Runtime::ScriptRecord * ParseScript(JS::Runtime::Realm * realm);
			JS::Runtime::SourceTextModuleRecord * ParseModule(JS::Runtime::Realm * realm);

			/*void BeginInteractiveParse()
			{
				AdvanceToken(JS::Parser::Context::InputElementHashbangOrRegExp);
			}*/

			JS::ParseNode::StatementList * ParseLine()
			{
				AdvanceToken(JS::Parser::Context::InputElementHashbangOrRegExp, InputElementTerminateOnNewLine);

				auto statement_list = new JS::ParseNode::StatementList(tokenizer->last_location);
				if(PeekToken().type != TokenType::LineTerminator && PeekToken().type != TokenType::EndOfFile)
				{
					do
					{
						// TODO: directives
						auto statement = ParseStatement(ParseLevel::Module, /* Yield = */ false, /* Await = */ false, /* Return = */ false, StatementContext::General, /* terminate_on_new_line = */ true);
						statement_list->statements.push_back(statement);
					} while(!HadLineTerminator());
				}
				ConsumeToken(); // should be a newline
				return statement_list;
			}
		};

		namespace XML
		{
			enum class CharType
			{
				Space,
				NameStartChar,
				NameChar,
				Other,
				Invalid,
			};

			static inline CharType GetCharType(char16_t c)
			{
				switch(c)
				{
				case u'\x09':
				case u'\x0A':
				case u'\x0D':
				case u'\x20':
					return CharType::Space;
				case u':':
				case u'_':
				case u'\u200C':
				case u'\u200D':
					return CharType::NameStartChar;
				case u'-':
				case u'.':
				case u'\xB7':
					return CharType::NameChar;
				case u'\uFFFE':
				case u'\uFFFF':
					return CharType::Invalid;
				default:
					if(c < u' ')
						return CharType::Invalid;
					else if((u'A' <= c && c <= u'Z')
					|| (u'a' <= c && c <= u'z')
					|| (u'\xC0' <= c && c <= u'\xD6')
					|| (u'\xD8' <= c && c <= u'\xF6')
					|| (u'\xF8' <= c && c <= u'\u02FF')
					|| (u'\u0370' <= c && c <= u'\u037D')
					|| (u'\u037F' <= c && c <= u'\u1FFF')
					|| (u'\u2070' <= c && c <= u'\u218F')
					|| (u'\u2C00' <= c && c <= u'\u2FEF')
					|| (u'\u3001' <= c && c <= 0xDB7F)
					|| (u'\uF900' <= c && c <= u'\uFDCF')
					|| (u'\uFDF0' <= c && c <= u'\uFFFD'))
						return CharType::NameStartChar;
					else if((u'0' <= c && c <= u'9')
					|| (u'\u0300' <= c && c <= u'\u036F')
					|| (u'\u203F' <= c && c <= u'\u2040')
					|| (0xDC00 <= c && c <= 0xDFFF))
						return CharType::NameChar;
					else
						return CharType::Other;
				}
			}

			class Parser
			{
			public:
				JSString string;
				size_t position = 0;

				Parser(JSString string)
					: string(string), position(0)
				{
				}

				char16_t GetNextChar()
				{
					if(position >= string.size())
					{
						return u'\0';
					}
					else
					{
						return string[position++];
					}
				}

				char16_t PeekNextChar()
				{
					if(position >= string.size())
					{
						return u'\0';
					}
					else
					{
						return string[position];
					}
				}

				char16_t GetEntityReference(JSString name);
				void ParseSpacesOpt();
				JSString ParseName();

				// assumes the u'&' is already consumed
				JSString ParseReference();
				JSString ParseAttributeValue();
				JSString ParseContentText();

				void ParseNamespace(gcn::gc_ptr<JS::Class::XML> xml);
				gcn::gc_ptr<JS::Class::XML> ParseXMLPart();
				void ProcessNamespaces(gcn::gc_ptr<JS::Class::XML> xml, std::map<JSString, JSNamespace> InScopeNamespaces);

				gcn::gc_ptr<JS::Class::XML> ParseXML()
				{
					gcn::gc_ptr<JS::Class::XML> xml = ParseXMLPart();
					std::map<JSString, JSNamespace> InScopeNamespaces;
					// TODO: add "xml"
					ProcessNamespaces(xml, InScopeNamespaces);
					return xml;
				}
			};
		}

		namespace JSON
		{
			class Parser
			{
			public:
				JSString string;
				size_t position = 0;

				Parser(JSString string)
					: string(string), position(0)
				{
				}

				char16_t GetNextChar()
				{
					if(position >= string.size())
					{
						return u'\0';
					}
					else
					{
						return string[position++];
					}
				}

				char16_t PeekNextChar()
				{
					if(position >= string.size())
					{
						return u'\0';
					}
					else
					{
						return string[position];
					}
				}

				bool EndOfString()
				{
					return position >= string.size();
				}

				constexpr bool IsDigit(char16_t c)
				{
					return u'0' <= c && c <= u'9';
				}

				void SkipWhitespaces();
				uint16_t GetHexadecimalDigit();
				JSString ParseStringLiteral();

				JSNumber ParseNumberLiteral();
				JSObject ParseObjectLiteral();
				gcn::gc_ptr<JS::Class::Array> ParseArrayLiteral();
				JSValue ParseValue();
				JSValue ParseJSON();
			};
		}
	}
}

#endif // _PARSER_HH
