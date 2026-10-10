
#include "common.hh"
#include "modules.hh"
#include "ast.hh"
#include "parser.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

bool JS::Enable_LegacyOctals = false;

//// Location

std::u16string JS::Parser::Location::tostring()
{
	std::ostringstream oss;
	if(filename.has_value())
	{
		oss << filename.value().u8string();
	}
	oss << ":" << line << ":" << column;
	return UTF8toUTF16(oss.str());
}

//// SourceLocationContext

JS::Parser::Location JS::Parser::SourceLocationContext::CurrentLocation;

//// Tokenizer

char32_t JS::Parser::Tokenizer::NextChar()
{
	char32_t c = _NextChar();

	switch(next_char_type)
	{
	case NextCharEffect::None:
		last_location.column ++;
		last_location.offset ++;
		break;
	case NextCharEffect::CR:
		if(c == U'\x0A')
		{
			last_location.column ++;
			last_location.offset ++;
			next_char_type = NextCharEffect::NL;
			break;
		}
		[[fallthrough]];
	case NextCharEffect::NL:
		last_location.line ++;
		last_location.column = 1;
		last_location.offset ++;
		break;
	}

	if(c == U'\x0D')
	{
		next_char_type = NextCharEffect::CR;
		read_source += u"\n";
	}
	else if(IsLineTerminator(c))
	{
		next_char_type = NextCharEffect::NL;
		read_source += u"\n";
	}
	else
	{
		next_char_type = NextCharEffect::None;
		if(c != WEOF)
		{
			read_source += UTF32toUTF16(c);
		}
	}

	return c;
}

//// UTF8StreamTokenizer

int JS::Parser::UTF8StreamTokenizer::NextByte()
{
	int byte;
	if(next_byte.has_value())
	{
		byte = next_byte.value();
		next_byte = {};
	}
	else if(stream_available)
	{
		byte = input.get();
		if(byte == EOF)
		{
			stream_available = false;
		}
	}
	else
	{
		byte = EOF;
	}
	return byte;
}

void JS::Parser::UTF8StreamTokenizer::PutBackByte(uint8_t byte)
{
	assert(!next_byte.has_value());
	next_byte = std::make_optional<>(byte);
}

char32_t JS::Parser::UTF8StreamTokenizer::_NextChar()
{
	int c;
	char32_t result;
#if 0
	if(PushBackBuffer != U'\0')
	{
		result = PushBackBuffer;
		PushBackBuffer = U'\0';
		return result;
	}
	if(next_char.has_value())
	{
		c = next_char.value();
		next_char = {};
		return c;
	}
#endif

	c = NextByte();
	if(c == EOF)
	{
		return WEOF;
	}
	else if((c & 0x80) == 0)
	{
		return c;
	}
	else if((c & 0xE0) == 0xC0)
	{
		result = (c & 0x1F) << 6;

		c = NextByte();
		if(c == EOF)
		{
			return result;
		}
		else if((c & 0xC0) != 0x80)
		{
			PutBackByte(c);
			// invalid
			return 0;
		}
		result |= c & 0x3F;

		return result;
	}
	else if((c & 0xF0) == 0xE0)
	{
		result = (c & 0x0F) << 12;

		c = NextByte();
		if(c == EOF)
		{
			return result;
		}
		else if((c & 0xC0) != 0x80)
		{
			PutBackByte(c);
			// invalid
			return 0;
		}
		result |= (c & 0x3F) << 6;

		c = NextByte();
		if(c == EOF)
		{
			return result;
		}
		else if((c & 0xC0) != 0x80)
		{
			PutBackByte(c);
			// invalid
			return 0;
		}
		result |= c & 0x3F;

		return result;
	}
	else if((c & 0xF8) == 0xF0)
	{
		result = (char32_t)(c & 0x07) << 18;

		c = NextByte();
		if(c == EOF)
		{
			return result;
		}
		else if((c & 0xC0) != 0x80)
		{
			PutBackByte(c);
			// invalid
			return 0;
		}
		result |= (c & 0x3F) << 12;

		c = NextByte();
		if(c == EOF)
		{
			return result;
		}
		else if((c & 0xC0) != 0x80)
		{
			PutBackByte(c);
			// invalid
			return 0;
		}
		result |= (c & 0x3F) << 6;

		c = NextByte();
		if(c == EOF)
		{
			return result;
		}
		else if((c & 0xC0) != 0x80)
		{
			PutBackByte(c);
			// invalid
			return 0;
		}
		result |= c & 0x3F;

		return result;
	}
	else
	{
		// invalid
		return 0;
	}
}

char32_t JS::Parser::UTF8StreamTokenizer::NextChar()
{
	char32_t result;
	if(PushBackBuffer != U'\0')
	{
		result = PushBackBuffer;
		PushBackBuffer = U'\0';
		return result;
	}
	if(next_char.has_value())
	{
		result = next_char.value();
		next_char = {};
		return result;
	}
	return Tokenizer::NextChar();
}

void JS::Parser::UTF8StreamTokenizer::PutBackChar(char32_t value)
{
	assert(!next_char.has_value());
	next_char = std::make_optional<>(value);
}

char32_t JS::Parser::UTF8StreamTokenizer::LookAhead()
{
	if(PushBackBuffer != U'\0')
	{
		return PushBackBuffer;
	}
	else if(next_char.has_value())
	{
		return next_char.value();
	}
	else
	{
		char32_t c = NextChar();
		if(c != WEOF)
		{
			PutBackChar(c);
		}
		return c;
	}
}

//// Tokenizer

char32_t JS::Parser::Tokenizer::ReadUnicodeEscapeSequence(size_t digit_count)
{
	char32_t c;
	char32_t value = 0;
	for(size_t i = 0; i < digit_count; i++)
	{
		c = NextChar();
		if(IsHexaDecimalDigit(c))
		{
			value |= GetHexaDecimalDigitValue(c) << (4 * (digit_count - 1 - i));
		}
		else
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected hexadecimal digit"));
		}
	}
	return value;
}

bool JS::Parser::Tokenizer::ValidNumericLiteralTermination()
{
	return CurrentVersion <= Version::ES1 || !(IsIdentifierStart(LookAhead()) || LookAhead() == U'\\' || IsDecimalDigit(LookAhead()));
}

void JS::Parser::Tokenizer::ParseEscapeSequence(JSString& destination)
{
	char32_t v;
	char32_t c = NextChar();
	if(CurrentVersion >= Version::ES5 && IsLineTerminator(c))
	{
		if(c == U'\r' && LookAhead() == U'\n')
		{
			NextChar();
		}
		return;
	}

	switch(c)
	{
	case U'"':
	case U'\'':
	case U'\\':
		AddChar(destination, c);
		break;
	case U'b':
		AddChar(destination, U'\u0008');
		break;
	case U'f':
		AddChar(destination, U'\u000C');
		break;
	case U'n':
		AddChar(destination, U'\u000A');
		break;
	case U'r':
		AddChar(destination, U'\u000D');
		break;
	case U't':
		AddChar(destination, U'\u0009');
		break;
	case U'v':
		if(CurrentVersion >= Version::ES3)
		{
			AddChar(destination, U'\u000B');
		}
		else
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected \\v"));
		}
		break;
	case U'0':
		if(!Supports_LegacyOctals())
		{
			if(IsDecimalDigit(LookAhead()))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected digit"));
			}
			AddChar(destination, U'\u0000');
			break;
		}
		[[fallthrough]];
	case U'1':
	case U'2':
	case U'3':
	case U'4':
	case U'5':
	case U'6':
	case U'7':
		if(!Supports_LegacyOctals())
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected octal escape"));
		}
		v = GetOctalDigitValue(c);
		for(size_t i = 1; i < 3 && v < 040 && IsOctalDigit(LookAhead()); i++)
		{
			c = NextChar();
			v = (v << 3) | GetOctalDigitValue(c);
		}
		if(CurrentVersion >= Version::ES3 && IsDecimalDigit(LookAhead()))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected digit after octal escape"));
		}
		AddChar(destination, v);
		break;

	case U'x':
		c = NextChar();
		if(!IsHexaDecimalDigit(c))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected hexadecimal digit"));
		}
		v = GetHexaDecimalDigitValue(c) << 4;
		c = NextChar();
		if(!IsHexaDecimalDigit(c))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected hexadecimal digit"));
		}
		v |= GetHexaDecimalDigitValue(c);
		AddChar(destination, v);
		break;

	case U'u':
		if(CurrentVersion >= Version::ES6 && LookAhead() == U'{')
		{
			NextChar();
			v = 0;
			while((c = LookAhead()) != U'}')
			{
				if(!IsHexaDecimalDigit(c))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected hexadecimal digit"));
				}
				NextChar();
				v = (v << 4) | GetHexaDecimalDigitValue(c);
			}
			NextChar();
		}
		else
		{
			NextChar();
			v = ReadUnicodeEscapeSequence();
		}
		AddChar(destination, v);
		break;

#if ENABLE_ES4
	case U'U':
		if(IsVersionES4())
		{
			NextChar();
			v = ReadUnicodeEscapeSequence(8);
			AddChar(destination, v);
		}
		else
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected escape: \\U only supported in ECMAScript 4"));
		}
		break;
#endif

	default:
		JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected escape"));
	}
}

JS::Parser::Token JS::Parser::Tokenizer::ParseToken(Context context)
{
	char32_t c;
	JS::Parser::Token result;

	switch(context)
	{
	case Context::InputElementXMLTag:
		return ParseXMLTag();
	case Context::InputElementXMLContent:
		return ParseXMLContent();
	default:
		break;
	}

	while(IsWhiteSpace(c = LookAhead()))
	{
		NextChar();
	}

	NextChar();
	result.location = last_location;

	if(c == WEOF)
	{
		result.type = TokenType::EndOfFile;
		return result;
	}
	else if(IsLineTerminator(c))
	{
		/*while(IsLineTerminator(LookAhead()))
		{
			NextChar();
		}*/
		result.type = TokenType::LineTerminator;
		return result;
	}
	else if(IsIdentifierStart(c) || (CurrentVersion >= Version::ES3 && c == U'\\'))
	{
		JSString text;
		if(c == U'\\')
		{
			NextChar();
			if(LookAhead() != U'U')
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'U'"));
			}
			NextChar();
			c = ReadUnicodeEscapeSequence();
			if(!IsIdentifierStart(c))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier start"));
			}
		}
		AddChar(text, c);
		while(IsIdentifierPart(c = LookAhead()) || (CurrentVersion >= Version::ES3 && c == U'\\'))
		{
			if(c == U'\\')
			{
				NextChar();
				if(LookAhead() != U'U')
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'U'"));
				}
				NextChar();
				c = ReadUnicodeEscapeSequence();
				if(!IsIdentifierPart(c))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier continuation"));
				}
			}
			AddChar(text, c);
			NextChar();
		}
		result.value = text;
		result.type = CheckReservedWord(text);
		result.keyword = CheckKeyword(text);
		return result;
	}
	else if(IsDecimalDigit(c) || (c == U'.' && IsDecimalDigit(LookAhead())))
	{
		TokenType token_type = TokenType::NumericLiteral;
		std::string text;
		size_t pos;
		if(c == U'0' && (LookAhead() == U'X' || LookAhead() == U'x'))
		{
			// hexadecimal
			NextChar();
			if(!IsHexaDecimalDigit(c = LookAhead()))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected hexadecimal digit"));
			}

			while(IsHexaDecimalDigit(c = LookAhead()) || (Version::ESNEXT <= CurrentVersion && c == U'_'))
			{
				if(c == U'_')
				{
					NextChar();
					c = LookAhead();
					if(!IsHexaDecimalDigit(c))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '_' after number literal"));
					}
				}
				NextChar();
				text += char(c);
			}

			try
			{
				if(CurrentVersion >= Version::ES11 && LookAhead() == U'n')
				{
					NextChar();
					result.value = mpz_class(text, 16);
					token_type = TokenType::BigIntNumericLiteral;
				}
#if ENABLE_ES4
				else if(IsVersionES4() && (LookAhead() == U'l' || LookAhead() == U'L'))
				{
					NextChar();
					result.value = mpz_class(text, 16);
					token_type = TokenType::LongNumericLiteral;
				}
				else if(IsVersionES4() && (LookAhead() == U'u' || LookAhead() == U'U'))
				{
					NextChar();
					if((LookAhead() == U'l' || LookAhead() == U'L'))
					{
						NextChar();
					}
					else
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'L' after 'U' suffix"));
					}
					result.value = mpz_class(text, 16);
					token_type = TokenType::UlongNumericLiteral;
				}
#endif
				else
				{
					result.value = JSNumber(std::stoll(text, &pos, 16));
					if(pos != text.size())
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
					}
				}
			}
			catch(const std::invalid_argument&)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
			}
			catch(SyntaxError *)
			{
				throw;
			}
		}
		else if(c == U'0' && (CurrentVersion >= Version::ES6 && (LookAhead() == U'O' || LookAhead() == U'o')))
		{
			// octal
			NextChar();
			if(!IsOctalDigit(c = LookAhead()))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected octal digit"));
			}

			while(IsOctalDigit(c = LookAhead()) || (Version::ESNEXT <= CurrentVersion && c == U'_'))
			{
				if(c == U'_')
				{
					NextChar();
					c = LookAhead();
					if(!IsOctalDigit(c))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '_' after number literal"));
					}
				}
				NextChar();
				text += char(c);
			}

			try
			{
				if(CurrentVersion >= Version::ES11 && LookAhead() == U'n')
				{
					NextChar();
					result.value = mpz_class(text, 8);
					token_type = TokenType::BigIntNumericLiteral;
				}
				else
				{
					result.value = JSNumber(std::stoll(text, &pos, 8));
					if(pos != text.size())
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
					}
				}
			}
			catch(const std::invalid_argument&)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
			}
		}
		else if(c == U'0' && (CurrentVersion >= Version::ES6 && (LookAhead() == U'B' || LookAhead() == U'b')))
		{
			// binary
			NextChar();
			if(!IsBinaryDigit(c = LookAhead()))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected binary digit"));
			}

			while(IsBinaryDigit(c = LookAhead()) || (Version::ESNEXT <= CurrentVersion && c == U'_'))
			{
				if(c == U'_')
				{
					NextChar();
					c = LookAhead();
					if(!IsBinaryDigit(c))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '_' after number literal"));
					}
				}
				NextChar();
				text += char(c);
			}

			try
			{
				if(CurrentVersion >= Version::ES11 && LookAhead() == U'n')
				{
					NextChar();
					result.value = mpz_class(text, 2);
					token_type = TokenType::BigIntNumericLiteral;
				}
				else
				{
					result.value = JSNumber(std::stoll(text, &pos, 2));
					if(pos != text.size())
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
					}
				}
			}
			catch(const std::invalid_argument&)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
			}
		}
		else if(c == U'0' && (Supports_LegacyOctals() && IsOctalDigit(LookAhead())))
		{
			bool is_octal = true;
			// legacy octal

			// '_' not permitted
			while(CurrentVersion < Version::ESNEXT ? IsOctalDigit(c = LookAhead()) : IsDecimalDigit(c = LookAhead()))
			{
				if(!IsOctalDigit(c))
				{
					is_octal = false;
				}
				NextChar();
				text += char(c);
			}

			try
			{
				if(CurrentVersion >= Version::ES11 && LookAhead() == U'n')
				{
					NextChar();
					result.value = mpz_class(text, is_octal ? 8 : 10);
					token_type = TokenType::BigIntNumericLiteral;
				}
				else
				{
					result.value = JSNumber(std::stoll(text, &pos, is_octal ? 8 : 10));
					if(pos != text.size())
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
					}
				}
			}
			catch(const std::invalid_argument&)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
			}
		}
		else if(c == U'0' && LookAhead() != U'.')
		{
			if(CurrentVersion >= Version::ES11 && LookAhead() == U'n')
			{
				NextChar();
				result.value = mpz_class(0);
				token_type = TokenType::BigIntNumericLiteral;
			}
#if ENABLE_ES4
			else if(IsVersionES4() && (LookAhead() == U'l' || LookAhead() == U'L'))
			{
				NextChar();
				result.value = mpz_class(0);
				token_type = TokenType::LongNumericLiteral;
			}
			else if(IsVersionES4() && (LookAhead() == U'u' || LookAhead() == U'U'))
			{
				NextChar();
				if((LookAhead() == U'l' || LookAhead() == U'L'))
				{
					NextChar();
				}
				else
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'L' after 'U' suffix"));
				}
				result.value = mpz_class(0);
				token_type = TokenType::UlongNumericLiteral;
			}
#endif
			else
			{
				result.value = JSNumber(0);
			}
		}
		else
		{
			text += char(c);
			if(c != U'.')
			{
				while(IsDecimalDigit(c = LookAhead()) || (Version::ESNEXT <= CurrentVersion && c == U'_'))
				{
					if(c == U'_')
					{
						NextChar();
						c = LookAhead();
						if(!IsDecimalDigit(c))
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '_' after number literal"));
						}
					}
					NextChar();
					text += char(c);
				}
				if(c == U'.')
				{
					text += char(c);
					NextChar();
				}
			}

			if(c == U'.' || LookAhead() == 'E' || LookAhead() == 'e')
			{
				// floating point
				if(c == U'.') // already part of the string
				{
					if(!IsDecimalDigit(c = LookAhead()))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected decimal digit"));
					}

					while(IsDecimalDigit(c = LookAhead()) || (Version::ESNEXT <= CurrentVersion && c == U'_'))
					{
						if(c == U'_')
						{
							NextChar();
							c = LookAhead();
							if(!IsDecimalDigit(c))
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '_' after number literal"));
							}
						}
						NextChar();
						text += char(c);
					}
				}
				if(LookAhead() == 'E' || LookAhead() == 'e')
				{
					text += char(NextChar());
					if(LookAhead() == '+' || LookAhead() == '-')
					{
						text += char(NextChar());
					}

					if(!IsDecimalDigit(LookAhead()))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected decimal digit"));
					}

					while(IsDecimalDigit(c = LookAhead()) || (Version::ESNEXT <= CurrentVersion && c == U'_'))
					{
						if(c == U'_')
						{
							NextChar();
							c = LookAhead();
							if(!IsDecimalDigit(c))
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '_' after number literal"));
							}
						}
						NextChar();
						text += char(c);
					}
				}

				try
				{
#if ENABLE_ES4
					if(IsVersionES4() && (LookAhead() == U'f' || LookAhead() == U'F'))
					{
						NextChar();
						result.value = std::stod(text, &pos);
						if(pos != text.size())
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
						}
						token_type = TokenType::FloatNumericLiteral;
					}
					else
#endif
					{
						result.value = std::stod(text, &pos);
						if(pos != text.size())
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
						}
					}
				}
				catch(const std::invalid_argument&)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
				}
			}
			else
			{
				try
				{
					if(CurrentVersion >= Version::ES11 && LookAhead() == U'n')
					{
						NextChar();
						result.value = mpz_class(text, 10);
						token_type = TokenType::BigIntNumericLiteral;
					}
#if ENABLE_ES4
					else if(IsVersionES4() && (LookAhead() == U'l' || LookAhead() == U'L'))
					{
						NextChar();
						result.value = mpz_class(text, 10);
						token_type = TokenType::LongNumericLiteral;
					}
					else if(IsVersionES4() && (LookAhead() == U'u' || LookAhead() == U'U'))
					{
						NextChar();
						if((LookAhead() == U'l' || LookAhead() == U'L'))
						{
							NextChar();
						}
						else
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'L' after 'U' suffix"));
						}
						result.value = mpz_class(text, 10);
						token_type = TokenType::UlongNumericLiteral;
					}
					else if(IsVersionES4() && (LookAhead() == U'f' || LookAhead() == U'F'))
					{
						NextChar();
						result.value = std::stod(text, &pos);
						if(pos != text.size())
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
						}
						token_type = TokenType::FloatNumericLiteral;
					}
#endif
					else
					{
						result.value = std::stod(text, &pos);
						if(pos != text.size())
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
						}
					}
				}
				catch(const std::invalid_argument&)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
				}
			}
		}

		if(!ValidNumericLiteralTermination())
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected character after number literal"));
		}

		result.type = token_type;
		return result;
	}
	else if(c == U'"' || c == U'\'')
	{
		// strings

		char32_t quote = c;
		JSString text;
		while((c = LookAhead()) != quote)
		{
			if(c == WEOF || IsLineTerminator(c))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated string literal"));
			}
			if(c == U'\\')
			{
				NextChar();
				ParseEscapeSequence(text);
			}
			else
			{
				AddChar(text, c);
				NextChar();
			}
		}
		NextChar();
		result.value = text;
		result.type = TokenType::StringLiteral;
		return result;
	}
	else if(c == U'/')
	{
		// division, comment, regular expression

		bool allow_division;
		switch(context)
		{
		case Context::InputElementDiv:
		case Context::InputElementTemplateTail:
			allow_division = true;
			break;
		default:
			allow_division = false;
			break;
		}

		switch(LookAhead())
		{
		case U'=':
			if(allow_division)
			{
				NextChar();
				result.type = TokenType::Assign_Divide; // '/='
				return result;
			}
			else
			{
				break;
			}
		case U'/':
			NextChar();
			while((c = LookAhead()) != WEOF && !IsLineTerminator(c))
			{
				NextChar();
			}
			result.type = TokenType::Comment; // '//'
			return result;
		case U'*':
			NextChar();
			do
			{
				while((c = LookAhead()) != WEOF && c != U'*')
				{
					NextChar();
				}
				while(c == U'*')
				{
					c = NextChar();
				}
			} while(c != WEOF && c != U'/');
			result.type = TokenType::Comment; // '/*'
			return result;
		default:
			if(allow_division)
			{
				result.type = TokenType(U'/');
				return result;
			}
			else
			{
				break;
			}
		}

		// regular expression
		JSString regexp;
		JSString flags;

		while((c = LookAhead()) != U'/')
		{
			if(c == WEOF || IsLineTerminator(c))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated regular expressoin literal"));
			}
			NextChar();

#if ENABLE_ES4
			if((Version::ES6 <= CurrentVersion || IsVersionES4()) && c == U'[')
#else
			if(Version::ES6 <= CurrentVersion && c == U'[')
#endif
			{
				AddChar(regexp, c);
				while((c = LookAhead()) != U']')
				{
					if(c == WEOF || IsLineTerminator(c))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated regular expressoin literal"));
					}
					NextChar();

					if(c == U'\\')
					{
						// TODO: escapes?
						AddChar(regexp, c);
						c = NextChar();
					}
					else
					{
						AddChar(regexp, c);
					}
				}
			}
			else if(c == U'\\')
			{
				// TODO: escapes?
				AddChar(regexp, c);
				c = NextChar();
			}
			else
			{
				AddChar(regexp, c);
			}
		}

		NextChar();

		while(IsIdentifierPart(c = LookAhead()))
		{
			NextChar();
			AddChar(flags, c);
		}

		result.value = std::make_tuple(regexp, flags);
		result.type = TokenType::RegExpLiteral;
		return result;
	}
	else if(c == U'`')
	{
		// template

		if(CurrentVersion < Version::ES6)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected character: '`' only supported in ECMAScript 6 or later"));
		}

		JSString text;

		while((c = LookAhead()) != U'`')
		{
			if(c == WEOF)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated template literal"));
			}
			NextChar();

			if(c == U'$' && LookAhead() == U'{')
			{
				NextChar();
				result.value = text;
				result.type = TokenType::TemplateHead;
				return result;
			}
			else if(c == U'\\')
			{
				ParseEscapeSequence(text);
			}
			else if(c == U'\r')
			{
				if(LookAhead() == U'\n')
				{
					NextChar();
				}
				AddChar(text, U'\n');
			}
			else
			{
				AddChar(text, c);
			}
		}

		NextChar();
		result.value = text;
		result.type = TokenType::NoSubstitutionTemplate;
		return result;
	}
	else if(c == U'}')
	{
		switch(context)
		{
		case Context::InputElementDiv:
		case Context::InputElementRegExp:
		case Context::InputElementUnspecified:
			result.type = TokenType(U'}');
			return result;
		case Context::InputElementHashbangOrRegExp:
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected character: '}'"));
		default:
			break;
		}

		JSString text;

		while((c = LookAhead()) != U'`')
		{
			if(c == WEOF)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated template literal"));
			}
			NextChar();

			if(c == U'$' && LookAhead() == U'{')
			{
				NextChar();
				result.value = text;
				result.type = TokenType::TemplateMiddle;
				return result;
			}
			else if(c == U'\\')
			{
				ParseEscapeSequence(text);
			}
			else if(c == U'\r')
			{
				if(LookAhead() == U'\n')
				{
					NextChar();
				}
				AddChar(text, U'\n');
			}
			else
			{
				AddChar(text, c);
			}
		}

		NextChar();
		result.value = text;
		result.type = TokenType::TemplateTail;
		return result;
	}
	else if(c == U'<')
	{
		// operators, XML

		bool allow_less_than;
		if(Supports_E4X())
		{
			switch(context)
			{
			case Context::InputElementDiv:
			case Context::InputElementTemplateTail:
				allow_less_than = true;
				break;
			default:
				allow_less_than = false;
				break;
			}
		}
		else
		{
			allow_less_than = true;
		}

		if(allow_less_than)
		{
			switch(LookAhead())
			{
			case U'<':
				NextChar();
				switch(LookAhead())
				{
				case U'=':
					NextChar();
					result.type = TokenType::Assign_ShiftLeft; // '<<='
					return result;
				default:
					result.type = TokenType::ShiftLeft; // '<<'
					return result;
				}
			case U'=':
				NextChar();
				result.type = TokenType::LessEqual; // '<='
				return result;
			default:
				result.type = TokenType(U'<');
				return result;
			}
		}
		else
		{
			if(LookAhead() == U'?' || LookAhead() == U'!')
			{
				assert(PushBackBuffer == U'\0');
				PushBackBuffer = U'<';
				return ParseXMLContent();
			}
			else
			{
				result.type = TokenType(U'<');
				return result;
			}
		}
	}
	else
	{
		switch(c)
		{
		case U'!':
			switch(LookAhead())
			{
			case U'=':
				NextChar();
				if(CurrentVersion >= Version::ES3)
				{
					switch(LookAhead())
					{
					case U'=':
						NextChar();
						result.type = TokenType::StrictNotEqual; // '!=='
						return result;
					default:
						result.type = TokenType::NotEqual; // '!='
						return result;
					}
				}
				else
				{
					result.type = TokenType::NotEqual; // '!='
					return result;
				}
			default:
				result.type = TokenType(U'!');
				return result;
			}
		case U'#':
			if(CurrentVersion == Version::ESNEXT)
			{
				if(context == Context::InputElementHashbangOrRegExp)
				{
					switch(LookAhead())
					{
					case U'!':
						NextChar();
						while((c = LookAhead()) != WEOF && !IsLineTerminator(c))
						{
							NextChar();
						}
						result.type = TokenType::Comment; // '#!'
						return result;
					default:
						result.type = TokenType(U'#');
						return result;
					}
				}
				else
				{
					result.type = TokenType(U'#');
					return result;
				}
			}
			else
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected character: '#' only supported in ESNext"));
			}
			break;
		case U'%':
			switch(LookAhead())
			{
			case U'=':
				NextChar();
				result.type = TokenType::Assign_Modulo; // '%='
				return result;
			default:
				result.type = TokenType(U'%');
				return result;
			}
		case U'&':
			switch(LookAhead())
			{
			case U'&':
				NextChar();
#if ENABLE_ES4
				if(IsVersionES4() || CurrentVersion == Version::ESNEXT)
#else
				if(CurrentVersion == Version::ESNEXT)
#endif
				{
					switch(LookAhead())
					{
					case U'=':
						NextChar();
						result.type = TokenType::Assign_And; // '&='
						return result;
					default:
						result.type = TokenType::And; // '&&'
						return result;
					}
				}
				else
				{
					result.type = TokenType::And; // '&&'
					return result;
				}
			case U'=':
				NextChar();
				result.type = TokenType::Assign_BitAnd; // '&='
				return result;
			default:
				result.type = TokenType(U'&');
				return result;
			}
		case U'(':
			result.type = TokenType(U'(');
			return result;
		case U')':
			result.type = TokenType(U')');
			return result;
		case U'*':
			switch(LookAhead())
			{
			case U'*':
				if(CurrentVersion == Version::ESNEXT)
				{
					NextChar();
					switch(LookAhead())
					{
					case U'=':
						NextChar();
						result.type = TokenType::Assign_Power; // '**='
						return result;
					default:
						result.type = TokenType::Power; // '**'
						return result;
					}
				}
				else
				{
					result.type = TokenType(U'*');
					return result;
				}
			case U'=':
				NextChar();
				result.type = TokenType::Assign_Multiply; // '*='
				return result;
			default:
				result.type = TokenType(U'*');
				return result;
			}
		case U'+':
			switch(LookAhead())
			{
			case U'+':
				NextChar();
				result.type = TokenType::Increment; // '++'
				return result;
			case U'=':
				NextChar();
				result.type = TokenType::Assign_Add; // '+='
				return result;
			default:
				result.type = TokenType(U'+');
				return result;
			}
		case U',':
			result.type = TokenType(U',');
			return result;
		case U'-':
			switch(LookAhead())
			{
			case U'-':
				NextChar();
				result.type = TokenType::Decrement; // '--'
				return result;
			case U'=':
				NextChar();
				result.type = TokenType::Assign_Subtract; // '-='
				return result;
			default:
				result.type = TokenType(U'-');
				return result;
			}
		case U'.':
#if ENABLE_ES4
			if(Version::ES6 <= CurrentVersion || Supports_E4X() || IsVersionES4())
#else
			if(Version::ES6 <= CurrentVersion || Supports_E4X())
#endif
			{
				switch(LookAhead())
				{
#if ENABLE_ES4
				case U'<':
					if(IsVersionES4())
					{
						NextChar();
						result.type = TokenType::TypeParameter_Open; // '.<'
						return result;
					}
					else
					{
						result.type = TokenType(U'.');
						return result;
					}
					break;
#endif
				case U'.':
#if ENABLE_ES4
					if(IsVersionES4() || Version::ES6 <= CurrentVersion)
#else
					if(Version::ES6 <= CurrentVersion)
#endif
					{
						NextChar();
						switch(LookAhead())
						{
						case U'.':
							NextChar();
							result.type = TokenType::Ellipsis; // '...'
							return result;
						default:
							if(Supports_E4X())
							{
								result.type = TokenType::Descendent; // '..'
								return result;
							}
							else
							{
								assert(PushBackBuffer == U'\0');
								PushBackBuffer = U'.';
								result.type = TokenType(U'.');
								return result;
							}
						}
					}
					else
					{
						assert(Supports_E4X());
						NextChar();
						result.type = TokenType::Descendent; // '..'
						return result;
					}
				default:
					result.type = TokenType(U'.');
					return result;
				}
			}
			else
			{
				result.type = TokenType(U'.');
				return result;
			}
		case U':':
#if ENABLE_ES4
			if(Supports_E4X() || IsVersionES4())
#else
			if(Supports_E4X())
#endif
			{
				switch(LookAhead())
				{
				case U':':
					NextChar();
					result.type = TokenType::NameQualifier; // '::'
					return result;
				default:
					result.type = TokenType(U':');
					return result;
				}
			}
			else
			{
				result.type = TokenType(U':');
				return result;
			}
		case U';':
			result.type = TokenType(U';');
			return result;
		case U'=':
			switch(LookAhead())
			{
			case U'=':
				NextChar();
				if(CurrentVersion >= Version::ES3)
				{
					switch(LookAhead())
					{
					case U'=':
						NextChar();
						result.type = TokenType::StrictEqual; // '==='
						return result;
					default:
						result.type = TokenType::Equal; // '=='
						return result;
					}
				}
				else
				{
					result.type = TokenType::Equal; // '=='
					return result;
				}
			case U'>':
				if(CurrentVersion >= Version::ES6)
				{
					NextChar();
					result.type = TokenType::Arrow; // '=>'
					return result;
				}
				else
				{
					result.type = TokenType(U'=');
					return result;
				}
			default:
				result.type = TokenType(U'=');
				return result;
			}
		case U'>':
			switch(LookAhead())
			{
			case U'=':
				NextChar();
				result.type = TokenType::GreaterEqual; // '>='
				return result;
			case U'>':
				NextChar();
				switch(LookAhead())
				{
				case U'=':
					NextChar();
					result.type = TokenType::Assign_ShiftRight; // '>>='
					return result;
				case U'>':
					NextChar();
					switch(LookAhead())
					{
					case U'=':
						NextChar();
						result.type = TokenType::Assign_ShiftRightUnsigned; // '>>>='
						return result;
					default:
						result.type = TokenType::ShiftRightUnsigned; // '>>>'
						return result;
					}
				default:
					result.type = TokenType::ShiftRight; // '>>'
					return result;
				}
			default:
				result.type = TokenType(U'>');
				return result;
			}
		case U'?':
			if(CurrentVersion == Version::ESNEXT)
			{
				switch(LookAhead())
				{
				case U'.':
					NextChar();
					if(IsDecimalDigit(LookAhead()))
					{
						// actually this is the start of a decimal number
						assert(PushBackBuffer == U'\0');
						PushBackBuffer = U'.';
						result.type = TokenType(U'?');
						return result;
					}
					else
					{
						result.type = TokenType::Chaining; // '?.'
						return result;
					}
				case U'?':
					NextChar();
					switch(LookAhead())
					{
					case U'=':
						NextChar();
						result.type = TokenType::Assign_Coalesce; // '??='
						return result;
					default:
						result.type = TokenType::Coalesce; // '??'
						return result;
					}
				default:
					result.type = TokenType(U'?');
					return result;
				}
			}
			else
			{
				result.type = TokenType(U'?');
				return result;
			}
		case U'@':
			if(Supports_E4X())
			{
				result.type = TokenType(U'@');
				return result;
			}
			else
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected character: '@' only supported with E4X enabled"));
			}
		case U'[':
			result.type = TokenType(U'[');
			return result;
		case U'\\':
		case U']':
			result.type = TokenType(U']');
			return result;
		case U'^':
			switch(LookAhead())
			{
			case U'=':
				NextChar();
				result.type = TokenType::Assign_BitXor; // '^='
				return result;
#if ENABLE_ES4
			case U'^':
				if(IsVersionES4())
				{
					NextChar();
					switch(LookAhead())
					{
					case U'=':
						NextChar();
						result.type = TokenType::Assign_Xor; // '^^='
						return result;
					default:
						result.type = TokenType::LogicalXor; // '^^'
						return result;
					}
				}
				else
				{
					result.type = TokenType(U'^');
					return result;
				}
#endif
			default:
				result.type = TokenType(U'^');
				return result;
			}
		case U'{':
			result.type = TokenType(U'{');
			return result;
		case U'|':
			switch(LookAhead())
			{
			case U'|':
				NextChar();
#if ENABLE_ES4
				if(IsVersionES4() || CurrentVersion == Version::ESNEXT)
#else
				if(CurrentVersion == Version::ESNEXT)
#endif
				{
					switch(LookAhead())
					{
					case U'=':
						NextChar();
						result.type = TokenType::Assign_Or; // '||='
						return result;
					default:
						result.type = TokenType::Or; // '||'
						return result;
					}
				}
				else
				{
					result.type = TokenType::Or; // '||'
					return result;
				}
			case U'=':
				NextChar();
				result.type = TokenType::Assign_BitOr; // '|='
				return result;
			default:
				result.type = TokenType(U'|');
				return result;
			}
		case U'~':
			result.type = TokenType(U'~');
			return result;
		default:
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected character"));
		}
	}
}

JS::Parser::Token JS::Parser::Tokenizer::ParseXMLContent()
{
	char32_t c;
	Token result;
	JSString text;
	c = NextChar();

	switch(c)
	{
	case U'{':
		result.type = TokenType(U'{');
		return result;
	case U'<':
		c = LookAhead();
		switch(c)
		{
		case U'?':
			NextChar();
			c = NextChar();
			while(!(c == U'?' && LookAhead() == U'>'))
			{
				if(c == WEOF)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated XML preprocessing instruction"));
				}
				AddChar(text, c);
				c = NextChar();
			}
			NextChar();
			result.value = text;
			result.type = TokenType::XMLPI;
			return result;

		case U'!':
			NextChar();
			c = LookAhead();
			switch(c)
			{
			case U'-':
				// XML comment
				NextChar();
				if(NextChar() != U'-')
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid XML comment start"));
				}

				c = NextChar();
				while(!(c == U'-' && LookAhead() == U'-'))
				{
					if(c == WEOF)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated XML comment"));
					}
					AddChar(text, c);
					c = NextChar();
				}

				if(NextChar() != U'-' || NextChar() != U'>')
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid '--' sequence in XML comment"));
				}
				result.value = text;
				result.type = TokenType::XMLComment;
				return result;
			case U'[':
				NextChar();
				if(NextChar() != U'C' || NextChar() != U'D' || NextChar() != U'A' || NextChar() != U'T' || NextChar() != U'A' || NextChar() != U'[')
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid XML CDATA start"));
				}

				{
					enum { Normal, OneBracket, TwoBrackets, Finished } state = Normal;
					while(state != Finished)
					{
						c = NextChar();
						if(c == WEOF)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated XML CDATA"));
						}
						switch(state)
						{
						case Normal:
							if(c == U']')
							{
								state = OneBracket;
							}
							else
							{
								AddChar(text, c);
							}
							break;
						case OneBracket:
							if(c == U']')
							{
								state = TwoBrackets;
							}
							else
							{
								AddChar(text, U']');
								AddChar(text, c);
								state = Normal;
							}
							break;
						case TwoBrackets:
							if(c == U'>')
							{
								state = Finished;
							}
							else if(c == U']')
							{
								//state = TwoBrackets;
								AddChar(text, U']');
							}
							else
							{
								AddChar(text, U']');
								AddChar(text, U']');
								AddChar(text, c);
								state = Normal;
							}
							break;
						case Finished:
							// loop should have ended
							assert(false);
						}
					}
				}

				result.value = text;
				result.type = TokenType::XMLCDATA;
				return result;

			default:
				JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid XML start"));
			}
		case U'/':
			NextChar();
			result.type = TokenType::XMLEndTagOpener;
			return result;
		default:
			result.type = TokenType(U'<');
			return result;
		}
	default:
		AddChar(text, c);
		while(((c = LookAhead()) != U'{') && (c != U'<'))
		{
			if(c == WEOF)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated XML element"));
			}
			NextChar();
			AddChar(text, c);
		}
		result.value = text;
		result.type = TokenType::XMLText;
		return result;
	}
}

JS::Parser::Token JS::Parser::Tokenizer::ParseXMLTag()
{
	char32_t c;
	Token result;
	JSString text;
	c = NextChar();
	result.location = last_location;

	switch(c)
	{
	case U'\t':
	case U'\n':
	case U'\r':
	case U' ':
		AddChar(text, c);
		while(((c = LookAhead()) == U'\t') || (c == U'\n') || (c == U'\n') || (c == U'\r'))
		{
			NextChar();
			AddChar(text, c);
		}
		result.value = text;
		result.type = TokenType::XMLWhitespace;
		return result;
	case U'{':
		result.type = TokenType(U'{');
		return result;
	case U'\'':
	case U'"':
		{
			char32_t quote = c;
			while(LookAhead() != quote)
			{
				c = NextChar();
				if(c == WEOF)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated XML attribute value"));
				}
				AddChar(text, c);
			}
		}
		NextChar();
		result.value = text;
		result.type = TokenType::XMLAttributeValue;
		return result;
	case U'/':
		if(NextChar() != U'>')
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid character: '/'"));
		}
		result.type = TokenType::XMLSelfClosingTagCloser;
		return result;
	case U'=':
		result.type = TokenType(U'=');
		return result;
	case U'>':
		result.type = TokenType(U'>');
		return result;
	default:
		{
			bool tag_characters = true;
			JS::Parser::TokenType type = IsUnicodeLetter(c) || c == U'_' || c == U':' ? TokenType::XMLName : TokenType::XMLTagCharacters;
			AddChar(text, c);
			while(true)
			{
				switch(c = LookAhead())
				{
				case U'=':
				case U'/':
				case U'>':
				case U'{':
				case U'\'':
				case U'\"':
				case U'\t':
				case U'\n':
				case U'\r':
				case U' ':
					tag_characters = false;
					break;
				case WEOF:
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unterminated XML tag"));
				default:
					if(!(IsUnicodeLetter(c) || IsUnicodeDigit(c) || c == U'.' || c == U'-' || c == U'_' || c == U':'))
					{
						type = TokenType::XMLTagCharacters;
					}
					break;
				}
				if(!tag_characters)
					break;
				NextChar();
				AddChar(text, c);
			}
			result.value = text;
			result.type = type;
			return result;
		}
	}
}

//// Parser

JS::Parser::Token JS::Parser::Parser::PeekToken()
{
	assert(TokenActive);
	return TokenBuffer[TokenPosition].token;
}

void JS::Parser::Parser::PurgeStashedTokens()
{
	TokenBuffer.erase(TokenBuffer.begin(), TokenBuffer.begin() + TokenPosition);
	TokenPosition = 0;
}

JS::Parser::Token JS::Parser::Parser::ConsumeToken()
{
	assert(TokenActive);
	auto token = TokenBuffer[TokenPosition].token;
	TokenActive = false;
	TokenPosition ++;
	PurgeStashedTokens();
	return token;
}

JS::Parser::Token JS::Parser::Parser::AdvanceToken(Context context, std::optional<Context> new_line_context)
{
	assert(!TokenActive);
	TokenActive = true;

	if(TokenPosition < TokenBuffer.size())
	{
		auto result = TokenBuffer[TokenPosition];
		if(result.after_line_terminator && new_line_context.has_value() && new_line_context.value() != InputElementTerminateOnNewLine)
		{
			context = new_line_context.value();
		}
		if(result.token.type != TokenType::LineTerminator)
		{
			assert(result.context == context);
		}
		return result.token;
	}

	bool HadLineTerminator = false;

	if(context == InputElementTerminateOnNewLine && !new_line_context.has_value())
	{
		new_line_context = InputElementTerminateOnNewLine;
	}

	while(true)
	{
		switch(context)
		{
		default:
		case Context::InputElementUnspecified:
			context = Context::InputElementUnspecified;
			break;
		case Context::InputElementDiv:
			context = Context::InputElementDiv;
			break;
		case Context::InputElementRegExp:
			if(CurrentVersion >= Version::ES3)
				context = Context::InputElementRegExp;
			else
				context = Context::InputElementDiv;
			break;
		case Context::InputElementXMLTag:
			assert(Supports_E4X());
			context = Context::InputElementXMLTag;
			break;
		case Context::InputElementXMLContent:
			assert(Supports_E4X());
			context = Context::InputElementXMLContent;
			break;
		case Context::InputElementRegExpOrTemplateTail:
			if(CurrentVersion >= Version::ES6)
				context = Context::InputElementRegExpOrTemplateTail;
			else if(CurrentVersion >= Version::ES3)
				context = Context::InputElementRegExp;
			else
				context = Context::InputElementDiv;
			break;
		case Context::InputElementTemplateTail:
			assert(CurrentVersion >= Version::ES6);
			context = Context::InputElementTemplateTail;
			break;
		case Context::InputElementHashbangOrRegExp:
			if(CurrentVersion == Version::ESNEXT)
				context = Context::InputElementHashbangOrRegExp;
			else if(CurrentVersion >= Version::ES3)
				context = Context::InputElementRegExp;
			else
				context = Context::InputElementDiv;
			break;
		}

		Context CurrentContext = context;
		Token NextToken = tokenizer->ParseToken(CurrentContext);
		switch(NextToken.type)
		{
		case TokenType::Comment:
			break;
		case TokenType::LineTerminator:
#if ENABLE_ES4
			if(!IsVersionES4() || !StrictMode)
#endif
			{
				HadLineTerminator = true;
				if(new_line_context.has_value())
				{
					context = new_line_context.value();
				}
			}
			if(new_line_context.has_value() && new_line_context.value() == InputElementTerminateOnNewLine)
			{
				TokenBuffer.push_back(ParseResult{CurrentContext, NextToken, HadLineTerminator});
				return NextToken;
			}
			break;
		case TokenType::EndOfFile:
			HadLineTerminator = true;
			//context = new_line_context;
			[[fallthrough]];
		default:
			TokenBuffer.push_back(ParseResult{CurrentContext, NextToken, HadLineTerminator});
			return NextToken;
		}
	}
}

JS::Parser::Token JS::Parser::Parser::StashToken()
{
	assert(TokenActive);
	auto token = TokenBuffer[TokenPosition].token;
	TokenActive = false;
	TokenPosition ++;
	return token;
}

JS::Parser::Token JS::Parser::Parser::StepBack(size_t Steps)
{
	assert(TokenPosition >= Steps);
	TokenActive = true;
	TokenPosition -= Steps;
	return PeekToken();
}

bool JS::Parser::Parser::HadLineTerminator()
{
	assert(TokenActive);
	return TokenBuffer[TokenPosition].after_line_terminator || TokenBuffer[TokenPosition].token.type == TokenType::LineTerminator;
}

JS::Parser::Context JS::Parser::Parser::GetCurrentContext()
{
	assert(TokenActive);
	return TokenBuffer[TokenPosition].context;
}

bool JS::Parser::Parser::IsIdentifier(TokenType type)
{
	switch(type)
	{
	case TokenType::Identifier:
	case TokenType::ReservedWord:
		return true;
	default:
		return false;
	}
}

JS::ParseNode::Identifier * JS::Parser::Parser::ProcessIdentifier(Token token, bool Yield, bool Await)
{
	if(!IsIdentifier(token.type) || (token.type == TokenType::ReservedWord && token.keyword != KeywordType::Yield && token.keyword != KeywordType::Await))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier name"));
	}
	if(token.keyword == KeywordType::Yield && (StrictMode || Yield))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'yield'"));
	}
	if(token.keyword == KeywordType::Await && (StrictMode || Await))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'await'"));
	}
	return new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
}

JS::ParseNode::Identifier * JS::Parser::Parser::ProcessBindingIdentifier(Token token, bool Yield, bool Await)
{
	if(!IsIdentifier(token.type) || (token.type == TokenType::ReservedWord && token.keyword != KeywordType::Yield && token.keyword != KeywordType::Await))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier name"));
	}
	if(CurrentVersion >= Version::ES6 && StrictMode && token.type == TokenType::Identifier)
	{
		if(std::get<JSString>(token.value) == u"arguments"
		|| std::get<JSString>(token.value) == u"eval")
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Strict mode does not permit 'arguments' or 'eval'"));
		}
	}
	if(token.keyword == KeywordType::Yield && (StrictMode || (Yield && CurrentVersion < Version::ES9)))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'yield'"));
	}
	if(token.keyword == KeywordType::Await && (StrictMode || (Await && CurrentVersion < Version::ES9)))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'await'"));
	}
	return new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
}

JS::ParseNode::Identifier * JS::Parser::Parser::ParseIdentifier(bool Yield, bool Await)
{
	Token token = PeekToken();
	ConsumeToken();
	return ProcessIdentifier(token, Yield, Await);
}

JS::ParseNode::Identifier * JS::Parser::Parser::ParseBindingIdentifier(bool Yield, bool Await)
{
	Token token = PeekToken();
	ConsumeToken();
	return ProcessBindingIdentifier(token, Yield, Await);
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParsePropertyIdentifier(bool Yield, bool Await, Context following_context, bool terminate_on_new_line, bool allow_keywords)
{
	bool is_attribute = false;
	Location at_location;
	Token token = PeekToken();
	ParseNode * node;
	if(token.type == TokenType(U'@'))
	{
		// PropertyIdentifier : AttributeIdentifier
		at_location = token.location;
		ConsumeToken();
		token = AdvanceToken();
		if(token.type == TokenType(U'['))
		{
			// AttributeIdentifier : '@' '[' Expression ']'
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
			node = new JS::ParseNode::Attribute(at_location, new JS::ParseNode::ComputedPropertyName(token.location, ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await)));
			if(PeekToken().type != TokenType(U']'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ']'"));
			}
			ConsumeToken();
			AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			return node;
		}
		else
		{
			is_attribute = true;
		}
	}

	if(Supports_E4X() && token.type == TokenType(U'*'))
	{
		// WildcardIdentifier : '*'
		node = new JS::ParseNode::WildcardIdentifier(token.location);
		ConsumeToken();
	}
#if ENABLE_ES4
	else if(IsVersionES4() && JS::ParseNode::IsReservedNamespace(token.keyword))
	{
		node = new JS::ParseNode::ReservedNamespace(token.location, JS::ParseNode::GetNamespaceType(token.keyword).value());
		ConsumeToken();
	}
	else if(IsVersionES4() && token.type == TokenType(U'('))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		node = new JS::ParseNode::ComputedPropertyName(token.location, ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await));
		if(PeekToken().type != TokenType(U')'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
		}
		ConsumeToken();
		// TODO: must be followed by '::'
	}
#endif
	else
	{
		// PrimaryExpression : IdentifierReference
		// PropertySelector : Identifier
		if(allow_keywords)
		{
			if(!IsIdentifier(token.type))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
			}
			node = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
			ConsumeToken();
		}
		else
		{
			node = ParseIdentifier(Yield, Await);
		}
	}

#if ENABLE_ES4
	if(Supports_E4X() || IsVersionES4())
#else
	if(Supports_E4X())
#endif
	{
		// AttributeIdentifier : '@' QualifiedIdentifier
		// PropertyIdentifier : QualifiedIdentifier
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		if(token.type == TokenType::NameQualifier)
		{
			ConsumeToken();
			token = AdvanceToken();
			switch(token.type)
			{
			case TokenType(U'['):
#if ENABLE_ES4
				if(!Supports_E4X())
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ']': computed property names only allowed with E4X"));
				}
#endif
				// QualifiedIdentifier : PropertySelector '::' '[' Expression ']'
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				node = new JS::ParseNode::QualifiedIdentifier(node->location, node, new JS::ParseNode::ComputedPropertyName(token.location, ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await)));
				if(PeekToken().type != TokenType(U']'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ']'"));
				}
				ConsumeToken();
				break;
			case TokenType(U'*'):
#if ENABLE_ES4
				if(!Supports_E4X())
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ']': computed property names only allowed with E4X"));
				}
#endif
				// QualifiedIdentifier : PropertySelector '::' PropertySelector
				ConsumeToken();
				node = new JS::ParseNode::QualifiedIdentifier(node->location, node, new JS::ParseNode::WildcardIdentifier(token.location));
				break;
			default:
				// QualifiedIdentifier : PropertySelector '::' PropertySelector
				{
					JS::ParseNode::Identifier * identifier;
#if ENABLE_ES4
					if(IsVersionES4())
					{
						// ES4.2008 permits using reserved words here
						if(!IsIdentifier(token.type))
						{
							JS::Throw(JS::New<SyntaxError> __LOC("Expected identifier name"));
						}
						ConsumeToken();
						identifier = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
					}
					else
#endif
					{
						identifier = ParseIdentifier(Yield, Await);
					}
					node = new JS::ParseNode::QualifiedIdentifier(node->location, node, identifier);
					break;
				}
			}
			AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		}
	}
	else
	{
		AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
	}

	if(is_attribute)
	{
		node = new JS::ParseNode::Attribute(at_location, node);
	}

	return node;
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParseMember(MemberContext member_context, bool Yield, bool Await)
{
	assert(IsRegExpInContext());
	Token token = PeekToken();
	bool is_private = false;
	bool is_generator = false;
	bool is_static = false;
	JS::ParseNode::FunctionType function_type = JS::ParseNode::FunctionType::Normal;
	//ParseNode * property_definition = nullptr;
	ParseNode * property_key = nullptr;

	switch(token.type)
	{
	case TokenType(U';'):
		if(member_context != MemberContext::ClassElement)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ';'"));
		}
		ConsumeToken();
		AdvanceToken();
		return new JS::ParseNode::Empty(token.location);

	case TokenType::Ellipsis:
		// PropertyDefinition : '...' AssignmentExpression
		if(member_context != MemberContext::PropertyDefinition)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '...'"));
		}

#if ENABLE_ES4
		if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
		if(CurrentVersion < Version::ES6)
#endif
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		if(CurrentVersion < Version::ESNEXT)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '...': object literal spread elements are only supported in ESNext mode"));
		}
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		return new JS::ParseNode::RestExpression(token.location, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
	case TokenType::Identifier:
	case TokenType::ReservedWord:
		// PropertyDefinition : MethodDefinition
		if(member_context == MemberContext::ClassElement && token.keyword == KeywordType::Static)
		{
			is_static = true;
			ConsumeToken();
			token = AdvanceToken();
			if(token.type == TokenType(U'{'))
			{
				if(CurrentVersion < Version::ESNEXT)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '{'"));
				}
				ParseNode * node = ParseBlock(/* Yield = */ false, /* Await = */ true, /* Return = */ false);
				AdvanceToken();
				return new JS::ParseNode::StaticClassElement(node->location, node);
			}
			if(!IsIdentifier(token.type) && token.type != TokenType(U'*'))
			{
				break;
			}
		}

		switch(token.keyword)
		{
		case KeywordType::Async:
			if(CurrentVersion >= Version::ESNEXT)
			{
				StashToken();
				token = AdvanceToken(Context::InputElementDiv);
				if(token.type == TokenType(U'*') || token.type == TokenType(U'#') || IsIdentifier(token.type))
				{
					if(HadLineTerminator())
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected line terminator"));
					}
					function_type = JS::ParseNode::FunctionType::Async;
					PurgeStashedTokens(); // async token parsed properly
				}
				else
				{
					token = StepBack();
				}
			}
			break;
		case KeywordType::Get:
			if(Version::ES5 <= CurrentVersion)
			{
				StashToken();
				token = AdvanceToken(Context::InputElementDiv);
				if(token.type == TokenType::Identifier
				|| token.type == TokenType::StringLiteral
				|| token.type == TokenType::NumericLiteral
				|| (Version::ES6 <= CurrentVersion
					&& token.type == TokenType(U'['))
				|| (Version::ES11 <= CurrentVersion
					&& token.type == TokenType::BigIntNumericLiteral)
				|| (Version::ES13 <= CurrentVersion
					&& token.type == TokenType(U'#')))
				{
					PurgeStashedTokens();
					function_type = JS::ParseNode::FunctionType::Get;
				}
				else
				{
					token = StepBack();
				}
			}
			break;
		case KeywordType::Set:
			if(CurrentVersion >= Version::ES5)
			{
				StashToken();
				token = AdvanceToken(Context::InputElementDiv);
				if(token.type == TokenType::Identifier
				|| token.type == TokenType::StringLiteral
				|| token.type == TokenType::NumericLiteral
				|| (Version::ES6 <= CurrentVersion
					&& token.type == TokenType(U'['))
				|| (Version::ES11 <= CurrentVersion
					&& token.type == TokenType::BigIntNumericLiteral)
				|| (Version::ES13 <= CurrentVersion
					&& token.type == TokenType(U'#')))
				{
					PurgeStashedTokens();
					function_type = JS::ParseNode::FunctionType::Set;
				}
				else
				{
					token = StepBack();
				}
			}
			break;
		default:
			break;
		}
		// fallthru
	case TokenType(U'*'):
		if(token.type == TokenType(U'*'))
		{
			if(CurrentVersion < Version::ES6)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '*': generators are only supported in ECMAScript 6 or later"));
			}
			if(function_type == JS::ParseNode::FunctionType::Get || function_type == JS::ParseNode::FunctionType::Set)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '*'"));
			}
			is_generator = true;
			ConsumeToken();
			token = AdvanceToken();
		}
		break;
	default:
		break;
	}

	switch(token.type)
	{
	case TokenType(U'#'):
		if(token.type == TokenType(U'#'))
		{
			if(CurrentVersion < Version::ESNEXT)
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			is_private = true;
			ConsumeToken();
			token = AdvanceToken();
		}
		// fallthru

	default:
		if(!IsIdentifier(token.type))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier name"));
		}

		StashToken();
		switch(AdvanceToken(Context::InputElementDiv).type)
		{
		default:
			if(member_context != MemberContext::ClassElement || CurrentVersion < Version::ESNEXT)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected token"));
			}
			property_key = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value), is_private);
			break;
		case TokenType(U','):
			if(member_context != MemberContext::PropertyDefinition)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ','"));
			}
			// fallthru
		case TokenType(U'}'):
			if(member_context == MemberContext::ClassElement)
			{
				if(CurrentVersion < Version::ESNEXT)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '}'"));
				}
				property_key = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value), is_private);
				break;
			}
			else
			{
				// PropertyDefinition : IdentifierReference
				if(CurrentVersion < Version::ES5)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ':': identifier references in object literals only supported in ECMAScript 5 or later"));
				}
				if((function_type != JS::ParseNode::FunctionType::Normal) || is_generator || is_private)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
				}
				StepBack();
				auto identifier_reference = ParseIdentifier(Yield, Await);
				AdvanceToken(Context::InputElementDiv);
				return identifier_reference;
			}
		case TokenType(U'='):
			if((member_context == MemberContext::ClassElement && CurrentVersion < Version::ESNEXT)
			|| (member_context == MemberContext::PropertyDefinition && is_private))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '='"));
			}
			property_key = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value), is_private);
			break;
		case TokenType(U'('):
			property_key = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value), is_private);
			break;
#if ENABLE_ES4
		case TokenType::NameQualifier:
			if(member_context != MemberContext::PropertyDefinition)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '::'"));
			}

			if(IsVersionES4())
			{
				StepBack();
				property_key = ParsePropertyIdentifier(Yield, Await);
				token = PeekToken();
				if(token.type != TokenType(U':'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ':'"));
				}
			}
			else
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '::'"));
			}
			break;
#endif
		case TokenType(U':'):
			if(member_context != MemberContext::PropertyDefinition)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ':'"));
			}

			if(function_type != JS::ParseNode::FunctionType::Normal || is_generator || is_private)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
			}
#if ENABLE_ES4
			if(IsVersionES4())
			{
				if(property_key == nullptr)
				{
					StepBack();
					property_key = ParsePropertyIdentifier(Yield, Await, Context::InputElementDiv);
				}
			}
			else
#endif
			if(CurrentVersion < Version::ES5)
			{
				// LiteralPropertyName : Identifier
				if(is_private)
				{
					JS::Throw(JS::New<InternalError> __LOC());
				}
				StepBack();
				property_key = ParseIdentifier(Yield, Await);
				AdvanceToken(Context::InputElementDiv);
			}
			else
			{
				// LiteralPropertyName : IdentifierName
				property_key = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value), is_private);
			}
			break;
		}
		break;
	case TokenType::StringLiteral:
		// PropertyName : LiteralPropertyName
		property_key = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
		ConsumeToken();
		AdvanceToken();
		break;
	case TokenType::NumericLiteral:
		// PropertyName : LiteralPropertyName
		property_key = new JS::ParseNode::NumericLiteral(token.location, std::get<JSNumber>(token.value));
		ConsumeToken();
		AdvanceToken();
		break;
	case TokenType::BigIntNumericLiteral:
		// PropertyName : LiteralPropertyName
		property_key = new JS::ParseNode::BigIntNumericLiteral(token.location, std::get<JSBigInt>(token.value));
		ConsumeToken();
		AdvanceToken();
		break;
#if ENABLE_ES4
	case TokenType::LongNumericLiteral:
		// FieldName : Number
		property_key = new JS::ParseNode::LongNumericLiteral(token.location, std::get<JSBigInt>(token.value).get_ui());
		ConsumeToken();
		AdvanceToken();
		break;
	case TokenType::UlongNumericLiteral:
		// FieldName : Number
		property_key = new JS::ParseNode::UlongNumericLiteral(token.location, std::get<JSBigInt>(token.value).get_ui());
		ConsumeToken();
		AdvanceToken();
		break;
	case TokenType::FloatNumericLiteral:
		// FieldName : Number
		property_key = new JS::ParseNode::FloatNumericLiteral(token.location, std::get<JSNumber>(token.value));
		ConsumeToken();
		AdvanceToken();
		break;
#endif
	case TokenType(U'['):
		// PropertyName : ComputedPropertyName
		if(CurrentVersion < Version::ES6)
		{
#if ENABLE_ES4
			if(IsVersionES4())
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '[': computed property names can be introduced via '(' in ECMAScript 4"));
			}
			else
#endif
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '[': computed property names in object literals only supported in ECMAScript 6 or later"));
			}
		}
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		property_key = new JS::ParseNode::ComputedPropertyName(token.location, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
		if(PeekToken().type != TokenType(U']'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ']'"));
		}
		ConsumeToken();
		AdvanceToken();
		break;
#if ENABLE_ES4
	case TokenType(U'('):
		if(!IsVersionES4())
		{
			if(CurrentVersion < Version::ES6)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '('"));
			}
			else
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '(': computed property names can be introduced via '[' in ECMAScript 6 or later"));
			}
		}
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		property_key = new JS::ParseNode::ComputedPropertyName(token.location, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
		if(PeekToken().type != TokenType(U')'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
		}
		ConsumeToken();
		token = AdvanceToken();
		if(token.type == TokenType::NameQualifier)
		{
			ConsumeToken();
			AdvanceToken();
			property_key = new JS::ParseNode::QualifiedIdentifier(property_key->location, property_key, ParseIdentifier(Yield, Await));
			token = AdvanceToken();
		}
		break;
#endif
	}

	assert(property_key != nullptr);

	switch(PeekToken().type)
	{
	case TokenType(U'('):
		{
			if(CurrentVersion < Version::ES6 && function_type == JS::ParseNode::FunctionType::Normal)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ':': method properties in object literals only supported in ECMAScript 6 or later"));
			}
			else if(CurrentVersion < Version::ES5)
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			auto function = new JS::ParseNode::FunctionDefinition(property_key->location);
			function->name = property_key;

			if(member_context == MemberContext::PropertyDefinition)
			{
				function->function_position = JS::ParseNode::FunctionPosition::Property;
			}
			else
			{
				bool is_property_key_constructor;

				if(auto identifier = dynamic_cast<JS::ParseNode::Identifier *>(property_key))
				{
					is_property_key_constructor = identifier->StringValue == u"constructor";
				}
				else if(auto string_literal = dynamic_cast<JS::ParseNode::StringLiteral *>(property_key))
				{
					is_property_key_constructor = string_literal->StringValue == u"constructor";
				}
				else
				{
					is_property_key_constructor = false;
				}

				function->function_position =
					is_property_key_constructor
						? JS::ParseNode::FunctionPosition::ClassConstructor
						: JS::ParseNode::FunctionPosition::Method;
			}

			function->function_kind = is_generator ? JS::ParseNode::FunctionKind::Generator : JS::ParseNode::FunctionKind::Function;
			function->function_type = function_type;
			function->parameters = ParseFormalParameters(/* Yield = */ is_generator, /* Await = */ function_type == JS::ParseNode::FunctionType::Async);
			token = AdvanceToken();
#if ENABLE_ES4
			if(IsVersionES4() && token.type == TokenType(U':'))
			{
				ConsumeToken();
				token = AdvanceToken(Context::InputElementRegExp);
				if(PeekToken().keyword == KeywordType::Void)
				{
					token = ConsumeToken();
					function->result_type = new JS::ParseNode::VoidType(token.location);
					token = AdvanceToken(Context::InputElementRegExp);
				}
				else
				{
					function->result_type = ParseTypeExpression(/* In = */ true, Yield, Await);
					token = PeekToken();
				}
			}
#endif
			switch(function_type)
			{
			case JS::ParseNode::FunctionType::Normal:
			case JS::ParseNode::FunctionType::Async:
				break;
			case JS::ParseNode::FunctionType::Get:
				if(function->parameters->arguments.size() != 0)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Getter methods expect 0 parameters"));
				}
				break;
			case JS::ParseNode::FunctionType::Set:
				if(function->parameters->arguments.size() != 1)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Setter methods expect 1 parameter"));
				}
				break;
			}
			function->body = ParseBlock(/* Yield = */ is_generator, /* Await = */ function_type == JS::ParseNode::FunctionType::Async, /* Return = */ true, /* parse_type = */ BlockParseType::PermitDirectives);
			function->SourceText = tokenizer->read_source.substr(function->location.offset);
			AdvanceToken();
			return is_static ? new JS::ParseNode::StaticClassElement(function->location, function) : static_cast<ParseNode *>(function);
		}
		break;
	case TokenType(U'='):
		{
			if(CurrentVersion < Version::ES6 || (CurrentVersion < Version::ESNEXT && member_context != MemberContext::PropertyDefinition))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '='"));
			}
			if(is_private && member_context != MemberContext::ClassElement)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
			}
			// TODO: this can only occur as the left side of an assignment or as a class element
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
			ParseNode * element = new JS::ParseNode::AssignmentExpression(property_key->location, property_key, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
			if(member_context == MemberContext::ClassElement)
			{
				ParseSemicolon();
			}
			return is_static ? new JS::ParseNode::StaticClassElement(element->location, element) : element;
		}
	case TokenType(U':'):
		// PropertyDefinition : PropertyName ':' AssignmentExpression
		if(member_context != MemberContext::PropertyDefinition)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
		}
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		return new JS::ParseNode::PropertyDefinition(property_key->location, property_key, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
	case TokenType(U'}'):
		if(member_context != MemberContext::ClassElement)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		[[fallthrough]];
	default:
		if(member_context == MemberContext::ClassElement && Version::ESNEXT <= CurrentVersion)
		{
			ParseSemicolon();
			return is_static ? new JS::ParseNode::StaticClassElement(property_key->location, property_key) : property_key;
		}
		else
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected token"));
		}
	}
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParseBindingPropertyDefinition(bool Yield, bool Await)
{
	// BindingIdentifier = Initializer
	// PropertyName : BindingElement

	assert(IsRegExpInContext());
	Token token = PeekToken();
	ParseNode * property_definition = nullptr;
	ParseNode * property_key = nullptr;
	switch(token.type)
	{
	default:
		// LiteralPropertyName : IdentifierName
		if(!IsIdentifier(token.type))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier name"));
		}
		switch(token.keyword)
		{
		default:
			// LiteralPropertyName : IdentifierName
			StashToken();
			switch(AdvanceToken().type)
			{
			case TokenType(U'='):
				//  SingleNameBinding : BindingIdentifier Initializer:opt
				StepBack();
				property_key = ParseBindingIdentifier(Yield, Await);
				AdvanceToken();
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				return new JS::ParseNode::AssignmentExpression(property_key->location, property_key, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
			case TokenType(U'}'):
			case TokenType(U','):
				//  SingleNameBinding : BindingIdentifier Initializer:opt
				StepBack();
				property_key = ParseBindingIdentifier(Yield, Await);
				AdvanceToken();
				return property_key;
			case TokenType(U':'):
				PurgeStashedTokens();
				property_key = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
				break;
			default:
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected token"));
			}
			break;
		}
		break;
	case TokenType::StringLiteral:
		// PropertyName : LiteralPropertyName
		property_key = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
		ConsumeToken();
		AdvanceToken();
		break;
	case TokenType::NumericLiteral:
		// PropertyName : LiteralPropertyName
		property_key = new JS::ParseNode::NumericLiteral(token.location, std::get<JSNumber>(token.value));
		ConsumeToken();
		AdvanceToken();
		break;
	case TokenType::BigIntNumericLiteral:
		// PropertyName : LiteralPropertyName
		property_key = new JS::ParseNode::BigIntNumericLiteral(token.location, std::get<JSBigInt>(token.value));
		ConsumeToken();
		AdvanceToken();
		break;
	case TokenType(U'['):
		// PropertyName : ComputedPropertyName
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		property_key = new JS::ParseNode::ComputedPropertyName(token.location, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
		if(PeekToken().type != TokenType(U']'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ']'"));
		}
		ConsumeToken();
		AdvanceToken();
		break;
	}

	if(property_key != nullptr)
	{
		switch(PeekToken().type)
		{
		case TokenType(U':'):
			// BindingProperty : PropertyName ':' BindingElement
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
			property_definition = new JS::ParseNode::PropertyDefinition(property_key->location, property_key, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
			break;
		default:
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected token"));
		}
	}

	return property_definition;
}

void JS::Parser::Parser::ParseXMLElementContent(std::vector<ParseNode *>& content)
{
	bool element_closed = false;
	Token token;
	while(!element_closed)
	{
		token = AdvanceToken(Context::InputElementXMLContent);
		switch(token.type)
		{
		case TokenType(U'{'):
			// XMLElementContent : '{' Expression '}' XMLElementContent:opt
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
			content.push_back(ParseExpression(Precedence::CommaExpression, /* In = */ true, /* Yield = */ false, /* Await = */ false));
			if(PeekToken().type != TokenType(U'}'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
			}
			ConsumeToken();
			break;
		case TokenType::XMLPI:
			// XMLMarkup : XMLPI
			ConsumeToken();
			content.push_back(new JS::ParseNode::XMLPI(token.location, std::get<JSString>(token.value)));
			break;
		case TokenType::XMLComment:
			// XMLMarkup : XMLComment
			ConsumeToken();
			content.push_back(new JS::ParseNode::XMLComment(token.location, std::get<JSString>(token.value)));
			break;
		case TokenType::XMLCDATA:
			// XMLMarkup : XMLCDATA
			ConsumeToken();
			content.push_back(new JS::ParseNode::XMLCDATA(token.location, std::get<JSString>(token.value)));
			break;
		case TokenType::XMLText:
			// XMLElementContent : XMLText XMLElementContent:opt
			ConsumeToken();
			content.push_back(new JS::ParseNode::XMLText(token.location, std::get<JSString>(token.value)));
			break;
		case TokenType(U'<'):
			// XMLElementContent : XMLElement XMLElementContent:opt
			content.push_back(ParseXMLElement());
			break;
		case TokenType::XMLEndTagOpener:
			ConsumeToken();
			element_closed = true;
			break;
		default:
			break;
		}
	}
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParseXMLElement()
{
	// XMLElement : '<' XMLTagContent XMLWhitespace:opt '/>'
	// XMLElement : '<' XMLTagContent XMLWhitespace:opt '>' XMLElementContent:opto '</' XMLTagName XMLWhitespace:opt '>'
	assert(IsXMLTagInContext());
	bool is_expression_context = GetCurrentContext() == Context::InputElementRegExp;
	Token token = PeekToken();
	if(token.type != TokenType(U'<'))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '<'"));
	}
	auto lt_location = token.location;
	ConsumeToken();
	token = AdvanceToken(Context::InputElementXMLTag);
	ParseNode * tag;
	switch(token.type)
	{
	case TokenType(U'{'):
		// XMLTagName : '{' Expression '}'
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		tag = ParseExpression(Precedence::CommaExpression, /* In = */ true, /* Yield = */ false, /* Await = */ false);
		if(PeekToken().type != TokenType(U'}'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
		}
		ConsumeToken();
		break;
	case TokenType::XMLName:
		// XMLTagName : XMLName
		tag = new JS::ParseNode::XMLIdentifier(token.location, std::get<JSString>(token.value));
		ConsumeToken();
		break;
	case TokenType(U'>'):
		if(is_expression_context)
		{
			// XMLListInitializer : '<' '>' XMLElementContent:opt '</' '>'
			ConsumeToken();
			JS::ParseNode::XMLList * node = new JS::ParseNode::XMLList(lt_location);
			ParseXMLElementContent(node->content);
			token = AdvanceToken(Context::InputElementXMLTag);
			if(token.type != TokenType(U'>'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '>'"));
			}
			ConsumeToken();
			return node;
		}
		// fallthru
	default:
		JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected token"));
	}

	bool attributes_tail = false;
	bool element_closed = false;
	bool tag_over = false;

	token = AdvanceToken(Context::InputElementXMLTag);
	JS::ParseNode::XMLElement * element = new JS::ParseNode::XMLElement(token.location, tag);
	while(!tag_over)
	{
		ParseNode * name;
		ParseNode * value;
		if(token.type == TokenType::XMLName || token.type == TokenType(U'{'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected XML name or '{'"));
		}
		if(token.type == TokenType::XMLWhitespace)
		{
			ConsumeToken();
			token = AdvanceToken(Context::InputElementXMLTag);
		}

		switch(token.type)
		{
		case TokenType::XMLName:
			// XMLAttribute : XMLWhitespace XMLName XMLWhitespace:opt '=' XMLWhiteSpace:opt '{' Expression '}'
			// XMLAttribute : XMLWhitespace XMLName XMLWhitespace:opt '=' XMLWhiteSpace:opt XMLAttributeValue
			if(attributes_tail)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected XML name after attributes expression"));
			}
			[[fallthrough]];
		case TokenType(U'{'):
			// XMLAttributes : XMLWhitespace '{' Expression '}'
			switch(token.type)
			{
			case TokenType(U'{'):
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				name = ParseExpression(Precedence::CommaExpression, /* In = */ true, /* Yield = */ false, /* Await = */ false);
				if(PeekToken().type != TokenType(U'}'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
				}
				ConsumeToken();
				break;
			case TokenType::XMLName:
				name = new JS::ParseNode::XMLIdentifier(token.location, std::get<JSString>(token.value));
				ConsumeToken();
				break;
			default:
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected token"));
			}

			token = AdvanceToken(Context::InputElementXMLTag);
			if(token.type == TokenType::XMLWhitespace)
			{
				ConsumeToken();
				token = AdvanceToken(Context::InputElementXMLTag);
			}
			if(token.type != TokenType(U'='))
			{
				if(dynamic_cast<JS::ParseNode::XMLIdentifier *>(name))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '='"));
				}
				attributes_tail = true;
				element->attributes.push_back(name);
			}
			else
			{
				if(attributes_tail)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected token after attributes expression"));
				}
				ConsumeToken();
				token = AdvanceToken(Context::InputElementXMLTag);
				if(token.type == TokenType::XMLWhitespace)
				{
					ConsumeToken();
					token = AdvanceToken(Context::InputElementXMLTag);
				}
				switch(token.type)
				{
				case TokenType(U'{'):
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp);
					value = ParseExpression(Precedence::CommaExpression, /* In = */ true, /* Yield = */ false, /* Await = */ false);
					if(PeekToken().type != TokenType(U'}'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
					}
					ConsumeToken();
					break;
				case TokenType::XMLAttributeValue:
					ConsumeToken();
					value = new JS::ParseNode::XMLAttributeValue(token.location, std::get<JSString>(token.value));
					break;
				default:
					break;
				}
				element->attributes.push_back(new JS::ParseNode::XMLAttribute(name->location, name, value));
				token = AdvanceToken(Context::InputElementXMLTag);
			}
			break;
		case TokenType::XMLSelfClosingTagCloser: // '/>'
			element_closed = true;
			[[fallthrough]];
		case TokenType('>'):
			tag_over = true;
			ConsumeToken();
			break;
		default:
			break;
		}
	}

	if(!element_closed)
	{
		ParseXMLElementContent(element->content);

		token = AdvanceToken(Context::InputElementXMLTag);
		switch(token.type)
		{
		case TokenType(U'{'):
			// XMLTagName : '{' Expression '}'
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
			element->endtag = ParseExpression(Precedence::CommaExpression, /* In = */ true, /* Yield = */ false, /* Await = */ false);
			if(PeekToken().type != TokenType(U'}'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
			}
			ConsumeToken();
			break;
		case TokenType::XMLName:
			// XMLTagName : XMLName
			element->endtag = new JS::ParseNode::XMLIdentifier(token.location, std::get<JSString>(token.value));
			ConsumeToken();
			break;
		default:
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected token"));
		}
		token = AdvanceToken(Context::InputElementXMLTag);
		if(token.type == TokenType::XMLWhitespace)
		{
			ConsumeToken();
			token = AdvanceToken(Context::InputElementXMLTag);
		}
		if(token.type != TokenType(U'>'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '>'"));
		}
		ConsumeToken();
	}

	return element;
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParseBindingElement(bool Yield, bool Await)
{
	bool rest_element = false;
	Token token = PeekToken();
	switch(token.type)
	{
	case TokenType(U'{'):
		{
			JS::ParseNode::ObjectLiteral * object = new JS::ParseNode::ObjectLiteral(token.location);
			ConsumeToken();
			token = AdvanceToken(Context::InputElementRegExp);
			while(token.type != TokenType::EndOfFile && token.type != TokenType(U'}'))
			{
				if(token.type == TokenType::Ellipsis)
				{
					if(CurrentVersion < Version::ESNEXT)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '...': destructuring rest properties in object patterns only supported in ESNext mode"));
					}
					auto ellipsis_location = token.location;
					ConsumeToken();
					token = AdvanceToken();
					ParseNode * element = new JS::ParseNode::RestExpression(ellipsis_location, ParseBindingElement(Yield, Await));
					object->property_definitions.push_back(element);
					AdvanceToken();
					token = PeekToken();
					if(token.type != TokenType(U'}'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
					}
				}
				else
				{
					auto * property_definition = ParseBindingPropertyDefinition(Yield, Await);
					object->property_definitions.push_back(property_definition);
					token = PeekToken();
					if(token.type == TokenType(U','))
					{
						ConsumeToken();
						token = AdvanceToken(Context::InputElementRegExp);
					}
					else if(token.type != TokenType(U'}'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
					}
				}
			}
			if(token.type != TokenType(U'}'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
			}
			ConsumeToken();
			return object;
		}
		break;
	case TokenType(U'['):
		{
			JS::ParseNode::ArrayLiteral * array = new JS::ParseNode::ArrayLiteral(token.location);
			ConsumeToken();
			token = AdvanceToken(Context::InputElementRegExp);
			while(token.type != TokenType::EndOfFile && token.type != TokenType(U']'))
			{
				if(token.type == TokenType(U','))
				{
					array->elements.push_back(new JS::ParseNode::Empty(token.location));
					ConsumeToken();
					token = AdvanceToken();
				}
				else
				{
					Location ellipsis_location;
					if(token.type == TokenType::Ellipsis)
					{
#if ENABLE_ES4
						if(CurrentVersion < Version::ES6)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '...': spread elements in array literals only supported in ECMAScript 6 or later"));
						}
#endif
						ConsumeToken();
						rest_element = true;
						ellipsis_location = token.location;
						token = AdvanceToken();
					}
					ParseNode * element = ParseBindingElement(Yield, Await);
					AdvanceToken();
					token = PeekToken();
					if(token.type == TokenType(U'='))
					{
						if(rest_element)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected element after rest element"));
						}
						ConsumeToken();
						token = AdvanceToken();
						element = new JS::ParseNode::AssignmentExpression(element->location, element, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
						token = PeekToken();
					}
					if(rest_element)
					{
						element = new JS::ParseNode::RestExpression(ellipsis_location, element);
					}
					array->elements.push_back(element);
					if(token.type == TokenType(U','))
					{
						if(rest_element)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ',' after rest element"));
						}
						ConsumeToken();
						token = AdvanceToken(Context::InputElementRegExp);
					}
					else if(token.type != TokenType(U']'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ']'"));
					}
				}
			}
			if(token.type != TokenType(U']'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ']'"));
			}
			ConsumeToken();
			return array;
		}
	default:
		return ParseBindingIdentifier(Yield, Await);
	}
}

JS::ParseNode::FormalParameters * JS::Parser::Parser::ParseFormalParameters(bool Yield, bool Await)
{
	Token token = PeekToken();
	if(token.type != TokenType(U'('))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
	}
	auto paren_location = token.location;
	ConsumeToken();
	token = AdvanceToken();
	bool rest_parameter = false;
#if ENABLE_ES4
	bool is_const = false;
#endif
	auto parameters = new JS::ParseNode::FormalParameters(paren_location);
	while(token.type != TokenType::EndOfFile && token.type != TokenType(U')'))
	{
#if ENABLE_ES4
		if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
		if(CurrentVersion < Version::ES6)
#endif
		{
			ParseNode * element = ParseBindingIdentifier(Yield, Await);
			parameters->arguments.push_back(element);
			AdvanceToken();
			token = PeekToken();
		}
		else
		{
			Location ellipsis_location;
			if(token.type == TokenType::Ellipsis)
			{
/*#if ENABLE_ES4
				if(CurrentVersion < Version::ES6)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '...': rest elements in formal parameter lists only supported in ECMAScript 6 or later"));
				}
#endif*/
				ConsumeToken();
				rest_parameter = true;
				ellipsis_location = token.location;
				token = AdvanceToken();
			}
#if ENABLE_ES4
			if(IsVersionES4() && token.keyword == KeywordType::Const)
			{
				is_const = true;
				ConsumeToken();
				token = AdvanceToken();
			}
#endif
			ParseNode *  element;
#if ENABLE_ES4
			if(IsVersionES4())
			{
				element = ParseBindingIdentifier(Yield, Await);
				static_cast<JS::ParseNode::Identifier *>(element)->is_const = is_const;
			}
			else
#endif
			{
				element = ParseBindingElement(Yield, Await);
			}
			AdvanceToken();
			token = PeekToken();
#if ENABLE_ES4
			if(IsVersionES4() && token.type == TokenType(U':'))
			{
				if(rest_parameter)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ':' after rest parameter"));
				}
				ConsumeToken();
				token = AdvanceToken(Context::InputElementRegExp);
				static_cast<JS::ParseNode::Identifier *>(element)->type = ParseTypeExpression(/* In = */ true, Yield, Await);
				token = PeekToken();
			}
#endif
			if(token.type == TokenType(U'='))
			{
				if(rest_parameter)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '=' after rest parameter"));
				}
				ConsumeToken();
				token = AdvanceToken();
				element = new JS::ParseNode::AssignmentExpression(element->location, element, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
				token = PeekToken();
			}
			if(rest_parameter)
			{
				element = new JS::ParseNode::RestExpression(ellipsis_location, element);
			}
			parameters->arguments.push_back(element);
		}
		if(token.type == TokenType(U','))
		{
			if(rest_parameter)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ',' after rest parameter"));
			}
			ConsumeToken();
			token = AdvanceToken();
		}
		else if(token.type != TokenType(U')'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
		}
	}
	if(token.type != TokenType(U')'))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
	}
	ConsumeToken();
	return parameters;
}

JS::ParseNode::FormalParameters * JS::Parser::Parser::ConvertToFormalParameters(ParseNode * expression)
{
	JS::ParseNode::FormalParameters * parameters;
	if(auto comma_expression = dynamic_cast<JS::ParseNode::BinaryExpression *>(expression))
	{
		if(comma_expression->op != JS::ParseNode::BinaryExpression::Operator::Comma)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid arrow function parameters"));
		}
		parameters = ConvertToFormalParameters(comma_expression->left_operand);
		parameters->arguments.push_back(comma_expression->right_operand);
		return parameters;
	}
	else
	{
		parameters = new JS::ParseNode::FormalParameters(Location()/*TODO*/);
	}
	parameters->arguments.push_back(expression);
	return parameters;
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParseArrowBody(bool is_async, bool In, Context following_context, bool terminate_on_new_line)
{
	ParseNode * body;
	Token token = AdvanceToken(Context::InputElementRegExp);
	if(token.type == TokenType(U'{'))
	{
		body = ParseBlock(/* Yield = */ false, /* Await = */ is_async, /* Return = */ true);
		AdvanceToken(following_context);
	}
	else
	{
		body = ParseExpression(Precedence::AssignmentExpression, In, /* Yield = */ false, /* Await = */ is_async, following_context, terminate_on_new_line);
	}
	return body;
}

#if ENABLE_ES4
JS::ParseNode::ParseNode * JS::Parser::Parser::ParseTypeExpression(bool In, bool Yield, bool Await, bool is_type_parameter)
{
	if(PeekToken().type == TokenType(U'*'))
	{
		auto node = new JS::ParseNode::WildcardIdentifier(PeekToken().location);
		ConsumeToken();
		AdvanceToken(Context::InputElementDiv);
		return node;
	}
	else if(is_type_parameter)
	{
		// close enough to simulate ActionScript syntax
		return /*TODO:non-assignment*/ParseExpression(Precedence::LeftHandSideExpression, In, Yield, Await);
	}
	else
	{
		return /*TODO:non-assignment*/ParseExpression(Precedence::LogicalOrExpression, In, Yield, Await);
	}
}
#endif

static inline void right_trim(std::u16string& string)
{
	string.erase(
		std::find_if(string.rbegin(), string.rend(),
			[](char16_t c)
		{
			return !JS::Parser::Tokenizer::IsWhiteSpace(c);
		}).base(), string.end());
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParseExpression(Precedence precedence, bool In, bool Yield, bool Await, bool Default, Context following_context, bool terminate_on_new_line)
{
	// TODO: might begin after an automatic semicolon insertion took place
	/*if(CurrentVersion >= Version::ES3)
	{
		assert(IsRegExpInContext());
	}*/
	Location function_location;
	Token token = PeekToken();
	ParseNode * node = nullptr;
	bool is_async = false; // only needed for async function expressions
#if ENABLE_ES4
	bool is_interface = false; // only needed to distinguish between class/interface declarations in ES4
#endif
	switch(token.type)
	{
	case TokenType::NumericLiteral:
		// Literal : NumericLiteral
		ConsumeToken();
		node = new JS::ParseNode::NumericLiteral(token.location, std::get<JSNumber>(token.value));
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType::BigIntNumericLiteral:
		// Literal : NumericLiteral
		if(CurrentVersion < Version::ES11)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		node = new JS::ParseNode::BigIntNumericLiteral(token.location, std::get<JSBigInt>(token.value));
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
#if ENABLE_ES4
	case TokenType::LongNumericLiteral:
		// Literal : NumericLiteral
		if(!IsVersionES4())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		node = new JS::ParseNode::LongNumericLiteral(token.location, std::get<JSBigInt>(token.value).get_ui());
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType::UlongNumericLiteral:
		// Literal : NumericLiteral
		if(!IsVersionES4())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		node = new JS::ParseNode::UlongNumericLiteral(token.location, std::get<JSBigInt>(token.value).get_ui());
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType::FloatNumericLiteral:
		// Literal : NumericLiteral
		if(!IsVersionES4())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		node = new JS::ParseNode::FloatNumericLiteral(token.location, std::get<JSNumber>(token.value));
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
#endif
	case TokenType::StringLiteral:
		// Literal : StringLiteral
		ConsumeToken();
		node = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType(U'['):
		// PrimaryExpression : ArrayLiteral
		if(CurrentVersion < Version::ES3)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '[': array literals only supported in ECMAScript 3 or later"));
		}
		else
		{
			JS::ParseNode::ArrayLiteral * array = new JS::ParseNode::ArrayLiteral(token.location);
			ConsumeToken();
			token = AdvanceToken(Context::InputElementRegExp);
			while(token.type != TokenType(U']'))
			{
				if(token.type == TokenType(U','))
				{
					array->elements.push_back(new JS::ParseNode::Empty(token.location));
					ConsumeToken();
					token = AdvanceToken(Context::InputElementRegExp);
				}
				else
				{
					bool is_spread = false;
					Location ellipsis_location;
					if(token.type == TokenType::Ellipsis)
					{
#if ENABLE_ES4
						if(IsVersionES4())
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '...': spread elements in array literals only supported in ECMAScript 6 or later"));
						}
						else
#endif
						if(CurrentVersion < Version::ES6)
						{
							JS::Throw(JS::New<InternalError> __LOC());
						}
						ConsumeToken();
						AdvanceToken(Context::InputElementRegExp);
						is_spread = true;
						ellipsis_location = token.location;
					}
					ParseNode * element = ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await);
					if(is_spread)
						element = new JS::ParseNode::RestExpression(ellipsis_location, element);
					array->elements.push_back(element);
					token = PeekToken();
					if(token.type == TokenType(U','))
					{
						ConsumeToken();
						token = AdvanceToken(Context::InputElementRegExp);
					}
					else if(token.type != TokenType(U']'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ']'"));
					}
				}
			}
			ConsumeToken();
			node = array;
		}
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType(U'{'):
		// TODO: statement

		// PrimaryExpression : ObjectLiteral
		if(CurrentVersion < Version::ES3)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '{': object literals only supported in ECMAScript 3 or later"));
		}
		else
		{
			JS::ParseNode::ObjectLiteral * object = new JS::ParseNode::ObjectLiteral(token.location);

			ConsumeToken();
			token = AdvanceToken(Context::InputElementRegExp);
			while(token.type != TokenType(U'}'))
			{
				auto * property_definition = ParseMember(MemberContext::PropertyDefinition, Yield, Await);
				object->property_definitions.push_back(property_definition);
				token = PeekToken();
				if(token.type == TokenType(U','))
				{
					ConsumeToken();
					token = AdvanceToken(Context::InputElementRegExp);
					if(CurrentVersion < Version::ES5 && token.type == TokenType(U'}'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '}': trailing comma in property definition list in object literals only supported in ECMAScript 5 or later"));
					}
				}
				else if(token.type != TokenType(U'}'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
				}
			}
			ConsumeToken();
			node = object;
		}
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType(U'('):
		// PrimaryExpression : ParenthesizedExpression
		// PrimaryExpression : CoverParenthesizedExpressionAndArrowParameterList
		{
			ParseNode * expression = nullptr;
			JS::ParseNode::FormalParameters * parameters = nullptr;
			auto paren_location = token.location;

			ConsumeToken();
			token = AdvanceToken(Context::InputElementRegExp);
			if(token.type == TokenType::Ellipsis)
			{
				if(CurrentVersion < Version::ES6)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ',': arrow function literals only supported in ECMAScript 6 or later"));
				}
				parameters = new JS::ParseNode::FormalParameters(paren_location);
				ConsumeToken();
				AdvanceToken();
				parameters->arguments.push_back(new JS::ParseNode::RestExpression(token.location, ParseBindingElement(Yield, Await)));
				token = AdvanceToken();
			}
			else
			{
				expression = ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await);
				token = PeekToken();
				while(token.type != TokenType(U')'))
				{
					if(token.type != TokenType(U','))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ',' or ')'"));
					}
					ConsumeToken();
					token = AdvanceToken(Context::InputElementRegExp);
					if(token.type == TokenType::Ellipsis)
					{
						if(CurrentVersion < Version::ES6)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ',': arrow function literals only supported in ECMAScript 6 or later"));
						}
						parameters = ConvertToFormalParameters(expression);
						expression = nullptr;
						ConsumeToken();
						AdvanceToken();
						parameters->arguments.push_back(new JS::ParseNode::RestExpression(token.location, ParseBindingElement(Yield, Await)));
						token = AdvanceToken();
					}
					else
					{
						auto operand = ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await);
						expression = new JS::ParseNode::BinaryExpression(expression->location, JS::ParseNode::BinaryExpression::Operator::Comma, expression, operand);
					}
					token = PeekToken();
				}
			}
			ConsumeToken();
			token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			if(CurrentVersion >= Version::ES6 && !HadLineTerminator() && token.type == TokenType::Arrow)
			{
				if(parameters == nullptr)
				{
					parameters = ConvertToFormalParameters(expression);
				}
				auto function = new JS::ParseNode::FunctionDefinition(paren_location);
				function->function_position = precedence > Precedence::Statement ? JS::ParseNode::FunctionPosition::Expression : JS::ParseNode::FunctionPosition::Statement;
				function->function_kind = JS::ParseNode::FunctionKind::Arrow;
				function->parameters = parameters;
				ConsumeToken();
				function->body = ParseArrowBody(false, In, following_context, terminate_on_new_line);
				function->SourceText = tokenizer->read_source.substr(function->location.offset, PeekToken().location.offset - function->location.offset);
				right_trim(function->SourceText);
				node = function;
			}
#if ENABLE_ES4
			else if(IsVersionES4() && token.type == TokenType::NameQualifier)
			{
				if(expression == nullptr)
				{
					JS::Throw(JS::New<InternalError> __LOC());
				}
				ConsumeToken();
				token = AdvanceToken();
				node = new JS::ParseNode::QualifiedIdentifier(expression->location, new JS::ParseNode::ComputedPropertyName(expression->location, expression), ParseIdentifier(Yield, Await));
				token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			}
#endif
			else
			{
				if(expression == nullptr)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '=>'"));
				}
				node = new JS::ParseNode::Parenthesis(expression->location, expression);
			}
		}
		break;
	case TokenType::RegExpLiteral:
		// PrimaryExpression : RegularExpressionLiteral
		if(CurrentVersion < Version::ES3)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		{
			auto [regexp, flags] = std::get<std::tuple<JSString, JSString>>(token.value);
			node = new JS::ParseNode::RegularExpressionLiteral(token.location, regexp, flags);
		}
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType::NoSubstitutionTemplate:
	case TokenType::TemplateHead:
		// PrimaryExpression : TemplateLiteral
		if(CurrentVersion < Version::ES6)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		node = ParseSubstitutionTemplate(Yield, Await, /* Tagged = */ false);
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;

	case TokenType::XMLPI:
		// XMLMarkup : XMLPI
		if(!Supports_E4X())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		node = new JS::ParseNode::XMLPI(token.location, std::get<JSString>(token.value));
		node = new JS::ParseNode::XMLInitializer(node->location, node);
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType::XMLComment:
		// XMLMarkup : XMLComment
		if(!Supports_E4X())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		node = new JS::ParseNode::XMLComment(token.location, std::get<JSString>(token.value));
		node = new JS::ParseNode::XMLInitializer(node->location, node);
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType::XMLCDATA:
		// XMLMarkup : XMLCDATA
		if(!Supports_E4X())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		node = new JS::ParseNode::XMLCDATA(token.location, std::get<JSString>(token.value));
		node = new JS::ParseNode::XMLInitializer(node->location, node);
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;

	case TokenType(U'<'):
		if(Supports_E4X())
		{
			// PrimaryExpression : XMLInitializer
			// PrimaryExpression : XMLListInitializer
			node = ParseXMLElement();
			if(!dynamic_cast<JS::ParseNode::XMLList *>(node))
			{
				node = new JS::ParseNode::XMLInitializer(node->location, node);
			}
		}
		else
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '<': XML initializers only supported with E4X enabled"));
		}
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
		break;

	case TokenType(U'@'):
		if(!Supports_E4X())
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '@'"));
		}
		// fallthru
	case TokenType(U'*'):
#if ENABLE_ES4
		if(!Supports_E4X() && !IsVersionES4()) // extension for ES4 via ActionScript to support '*' as a return type
#else
		if(!Supports_E4X())
#endif
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '*'"));
		}
		// fallthru
	default:
		switch(token.keyword)
		{
		case KeywordType::This:
			// PrimaryExpression : 'this'
			ConsumeToken();
			node = new JS::ParseNode::ThisExpression(token.location);
			token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			break;
		case KeywordType::Null:
			// PrimaryExpression : 'null'
			ConsumeToken();
			node = new JS::ParseNode::NullLiteral(token.location);
			token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			break;
		case KeywordType::False:
		case KeywordType::True:
			// PrimaryExpression : 'false'
			// PrimaryExpression : 'true'
			ConsumeToken();
			node = new JS::ParseNode::BooleanLiteral(token.location, token.keyword == KeywordType::True);
			token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			break;
#if ENABLE_ES4
		case KeywordType::Public:
		case KeywordType::Private:
		case KeywordType::Protected: // ES4.2006
		case KeywordType::Internal: // ES4.2006 // TODO: make non-reserved word?
			if(!IsVersionES4())
			{
				node = ParsePropertyIdentifier(Yield, Await, following_context, terminate_on_new_line);
			}
			else
			{
				StashToken();
				if(AdvanceToken(following_context, SameOrTermination(terminate_on_new_line)).type == TokenType::NameQualifier)
				{
					StepBack();
					node = ParsePropertyIdentifier(Yield, Await, following_context, terminate_on_new_line);
				}
				else
				{
					PurgeStashedTokens();
					// ReservedNamespace : 'public'
					// ReservedNamespace : 'private'
					node = new JS::ParseNode::ReservedNamespace(token.location, JS::ParseNode::GetNamespaceType(token.keyword).value());
				}
			}
			break;
#endif
		case KeywordType::Async:
			// PrimaryExpression : AsyncFunctionExpression
			// PrimaryExpression : AsyncGeneratorExpression
			if(CurrentVersion < Version::ESNEXT)
			{
				node = ParsePropertyIdentifier(Yield, Await, following_context, terminate_on_new_line);
				break;
			}
			else
			{
				function_location = token.location;
				StashToken();
				token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
				if(!HadLineTerminator() && token.keyword == KeywordType::Function)
				{
					PurgeStashedTokens();
					is_async = true;
				}
				else if(!HadLineTerminator() && IsIdentifier(token.type))
				{
					PurgeStashedTokens();
					ConsumeToken();
					if(AdvanceToken().type != TokenType::Arrow)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected identifier"));
					}
					if(HadLineTerminator())
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected line terminator"));
					}
					auto function = new JS::ParseNode::FunctionDefinition(function_location);
					function->function_position = precedence > Precedence::Statement ? JS::ParseNode::FunctionPosition::Expression : JS::ParseNode::FunctionPosition::Statement;
					function->function_kind = JS::ParseNode::FunctionKind::Arrow;
					function->function_type = JS::ParseNode::FunctionType::Async;
					function->parameters = new JS::ParseNode::FormalParameters(token.location);
					function->parameters->arguments.push_back(new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value)));

					ConsumeToken();
					function->body = ParseArrowBody(true, In, following_context, terminate_on_new_line);
					function->SourceText = tokenizer->read_source.substr(function->location.offset, PeekToken().location.offset - function->location.offset);
					right_trim(function->SourceText);
					node = function;
					break;
				}
				else
				{
					StepBack();
					node = ParsePropertyIdentifier(Yield, Await, following_context, terminate_on_new_line);
					break;
				}
			}
			// fallthru
		case KeywordType::Function:
			// PrimaryExpression : FunctionExpression
			// PrimaryExpression : GeneratorExpression
			if(CurrentVersion < Version::ES3 && precedence > Precedence::Statement)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'function': function expressions only supported in ECMAScript 3 or later"));
			}
			else
			{
				auto function = new JS::ParseNode::FunctionDefinition(is_async ? function_location : token.location);
				function->function_position = precedence > Precedence::Statement ? JS::ParseNode::FunctionPosition::Expression : JS::ParseNode::FunctionPosition::Statement;
				if(is_async)
				{
					function->function_type = JS::ParseNode::FunctionType::Async;
				}
				ConsumeToken();
				token = AdvanceToken();

				if(token.type == TokenType(U'*'))
				{
					if(CurrentVersion < Version::ES6)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '*': generators are only supported in ECMAScript 6 or later"));
					}
					function->function_kind = JS::ParseNode::FunctionKind::Generator;
					ConsumeToken();
					token = AdvanceToken();
				}

				if(IsIdentifier(token.type))
				{
#if ENABLE_ES4
					if(IsVersionES4() && precedence <= Precedence::Statement
					&& token.type == TokenType::Identifier && (token.keyword == KeywordType::Get || token.keyword == KeywordType::Set))
					{
						StashToken();
						AdvanceToken();
						if(!HadLineTerminator() && PeekToken().type == TokenType::Identifier)
						{
							PurgeStashedTokens();
							switch(token.keyword)
							{
							case KeywordType::Get:
								function->function_type = JS::ParseNode::FunctionType::Get;
								break;
							case KeywordType::Set:
								function->function_type = JS::ParseNode::FunctionType::Set;
								break;
							default:
								JS::Throw(JS::New<InternalError> __LOC());
							}
						}
						else
						{
							StepBack();
						}
					}

					if(IsVersionES4())
					{
						// ES4.2008 permits using reserved words here
						Token token = PeekToken();
						if(!IsIdentifier(token.type))
						{
							JS::Throw(JS::New<SyntaxError> __LOC("Expected identifier name"));
						}
						ConsumeToken();
						function->name = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
					}
					else
#endif
					{
						function->name = ParseBindingIdentifier(
							/* Yield = */ precedence <= Precedence::Statement ? Yield : function->function_kind == JS::ParseNode::FunctionKind::Generator,
							/* Await = */ precedence <= Precedence::Statement ? Await : function->function_type == JS::ParseNode::FunctionType::Async);
					}
					token = AdvanceToken();
				}
				else if(precedence <= Precedence::Statement && !Default)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
				}

#if ENABLE_ES4
				if(token.type == TokenType::TypeParameter_Open)
				{
					if(!IsVersionES4())
					{
						JS::Throw(JS::New<InternalError> __LOC());
					}
					ConsumeToken();
					AdvanceToken();
					while(true)
					{
						function->type_parameters.push_back(ParseIdentifier(Yield, Await));
						token = AdvanceToken();
						if(token.type == TokenType(U','))
						{
							ConsumeToken();
							AdvanceToken();
						}
						else if(token.type == TokenType(U'>'))
						{
							break;
						}
						else
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ',' or '>'"));
						}
					}
					ConsumeToken();
					token = AdvanceToken();
				}
#endif

				function->parameters = ParseFormalParameters(
					/* Yield = */ function->function_kind == JS::ParseNode::FunctionKind::Generator,
					/* Await = */ function->function_type == JS::ParseNode::FunctionType::Async);
				token = AdvanceToken();

#if ENABLE_ES4
				if(IsVersionES4() && token.type == TokenType(U':'))
				{
					ConsumeToken();
					token = AdvanceToken(Context::InputElementRegExp);
					if(PeekToken().keyword == KeywordType::Void)
					{
						function->result_type = new JS::ParseNode::VoidType(PeekToken().location);
						ConsumeToken();
						token = AdvanceToken(Context::InputElementRegExp);
					}
					else
					{
						function->result_type = ParseTypeExpression(In, Yield, Await);
						token = PeekToken();
					}
				}
#endif

#if ENABLE_ES4
				if(IsVersionES4() && precedence <= Precedence::AbstractStatement)
				{
					function->body = new JS::ParseNode::Empty(token.location);
					ParseSemicolon(); // usually function statements do not need a trailing semicolon
					return function;
				}
				else
#endif
				{
					function->body = ParseBlock(
						/* Yield = */ function->function_kind == JS::ParseNode::FunctionKind::Generator,
						/* Await = */ function->function_type == JS::ParseNode::FunctionType::Async,
						/* Return = */ true,
						/* parse_type = */ BlockParseType::PermitDirectives);
				}
				function->SourceText = tokenizer->read_source.substr(function->location.offset);
				node = function;
				token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			}
			break;
#if ENABLE_ES4
		case KeywordType::Interface:
			if(!IsVersionES4())
			{
				goto parse_identifier;
			}
			is_interface = true;
			[[fallthrough]];
#endif
		case KeywordType::Class:
			// PrimaryExpression : ClassExpression
#if ENABLE_ES4
			if(CurrentVersion < Version::ES6 && !IsVersionES4())
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'class': class declarations only supported in ECMAScript 4, 6 or later"));
			}
#else
			if(CurrentVersion < Version::ES6)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'class': class declarations only supported in ECMAScript 6 or later"));
			}
#endif
			else
			{
#if ENABLE_ES4
				if(IsVersionES4() && precedence > Precedence::Statement)
				{
					if(is_interface)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'interface'"));
					}
					else
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'class'"));
					}
				}
#endif
				auto class_ = new JS::ParseNode::ClassDefinition(token.location);
#if ENABLE_ES4
				class_->is_interface = is_interface;
#endif
				class_->is_expression = precedence > Precedence::Statement;
				ConsumeToken();
				token = AdvanceToken();

				if(token.type == TokenType::Identifier)
				{
					class_->name = ParseBindingIdentifier(/* Yield = */ false, /* Await = */ false);
					token = AdvanceToken();
				}
				else if(precedence <= Precedence::Statement && !Default)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
				}

#if ENABLE_ES4
				if(token.type == TokenType::TypeParameter_Open)
				{
					if(!IsVersionES4())
					{
						JS::Throw(JS::New<InternalError> __LOC());
					}
					ConsumeToken();
					AdvanceToken();
					while(true)
					{
						class_->type_parameters.push_back(ParseIdentifier(Yield, Await));
						token = AdvanceToken();
						if(token.type == TokenType(U','))
						{
							ConsumeToken();
							AdvanceToken();
						}
						else if(token.type == TokenType(U'>'))
						{
							break;
						}
						else
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ',' or '>'"));
						}
					}
					ConsumeToken();
					token = AdvanceToken();
				}
#endif

#if ENABLE_ES4
				if(token.keyword == KeywordType::Extends && !is_interface)
#else
				if(token.keyword == KeywordType::Extends)
#endif
				{
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp);
					class_->extends = ParseExpression(Precedence::NewExpression, /* In = */ true, Yield, Await, Context::InputElementUnspecified);
					token = PeekToken();
				}

#if ENABLE_ES4
				if(IsVersionES4() && token.keyword == (is_interface ? KeywordType::Extends : KeywordType::Implements))
				{
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp);
					while(true)
					{
						class_->implements.push_back(ParseExpression(Precedence::NewExpression, /* In = */ true, Yield, Await, Context::InputElementUnspecified));
						token = PeekToken();
						if(token.type == TokenType(U','))
						{
							ConsumeToken();
							AdvanceToken(Context::InputElementRegExp);
						}
						else
						{
							break;
						}
					}
				}
#endif

				if(token.type != TokenType(U'{'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '{'"));
				}
#if ENABLE_ES4
				if(IsVersionES4())
				{
					auto block = ParseBlock(/* Yield = */ Yield, /* Await = */ false, /* Return = */ true,
						is_interface ? BlockParseType::PermitBodylessFunctions : BlockParseType::PermitNothing);
					auto statements = new JS::ParseNode::StatementList(block->location);
					statements->statements = block->statements;
					class_->body = statements;
				}
				else
#endif
				{
					class_->body = ParseClassBody(/* Yield = */ Yield, /* Await = */ false);
				}
				class_->SourceText = tokenizer->read_source.substr(class_->location.offset);
				node = class_;
				token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			}
			break;
		case KeywordType::Super:
			// SuperProperty : 'super' '[' Expression ']'
			// SuperProperty : 'super' '.' IdentifierName
			// SuperCall : 'super' Arguments
			// SuperExpression : 'super' '(' AssignmentExpression ')' // ECMAScript 4
#if ENABLE_ES4
			if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
			if(CurrentVersion < Version::ES6)
#endif
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'super': 'super' expressions only supported in ECMAScript 6 or later"));
			}
			{
				auto super_location = token.location;
				ConsumeToken();
				token = AdvanceToken();
				if(token.type == TokenType(U'('))
				{
#if ENABLE_ES4
					if(IsVersionES4())
					{
						// TODO: only these 3 are valid
						// super ( Expression ) . Identifier
						// super ( Expression ) [ ...
						// super ( ExpressionList ) END_OF_STATEMENT
					}
#endif
					node = new JS::ParseNode::SuperExpression(super_location);
				}
				else if(token.type == TokenType(U'.') || token.type == TokenType(U'['))
				{
					node = new JS::ParseNode::SuperExpression(super_location);
				}
				else
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '.' or '[' or '('"));
				}
			}
			break;
		case KeywordType::New:
			// MemberExpression : 'new' MemberExpression Arguments
			// NewTarget : 'new' '.' 'target'
			// NewExpression : 'new' NewExpression
			{
				auto new_location = token.location;
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				if(PeekToken().type == TokenType(U'.'))
				{
					if(CurrentVersion < Version::ES6)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '.': new.target only supported in ECMAScript 6 or later"));
					}
					ConsumeToken();
					token = AdvanceToken();
					if(token.keyword != KeywordType::Target)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'target'"));
					}
					ConsumeToken();
					node = new JS::ParseNode::NewTargetExpression(new_location);
					token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
				}
				else
				{
					node = ParseExpression(Precedence::NewExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line);
					if(auto call_expression = dynamic_cast<JS::ParseNode::CallExpression *>(node))
					{
						call_expression->is_new = true;
						call_expression->location = new_location;
					}
					else
					{
						call_expression = new JS::ParseNode::CallExpression(new_location, node);
						call_expression->is_new = true;
						node = call_expression;
					}
				}
			}
			break;
		case KeywordType::Import:
			// ImportMeta : 'import' '.' 'meta'
			// ImportCall : 'import' '(' AssignmentExpression ',':opt ')'
			// ImportCall : 'import' '(' AssignmentExpression ',' AssignmentExpression ')'
			if(CurrentVersion < Version::ESNEXT)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'import': import expressions only supported in ESNext mode"));
			}
			{
				auto import_location = token.location;
				ConsumeToken();
				token = AdvanceToken();
				switch(token.type)
				{
				case TokenType(U'.'):
					ConsumeToken();
					token = AdvanceToken();
					if(token.keyword != KeywordType::Meta)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'meta'"));
					}
					ConsumeToken();
					node = new JS::ParseNode::ImportMetaExpression(import_location);
					token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
					break;
				case TokenType(U'('):
					node = new JS::ParseNode::ImportExpression(import_location);
					break;
				default:
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '.' or '('"));
				}
			}
			break;
		case KeywordType::Await:
		case KeywordType::Yield:
		case KeywordType::Let:
			if(CurrentVersion >= Version::ESNEXT && token.keyword == KeywordType::Await && Await)
			{
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				node = new JS::ParseNode::AwaitExpression(token.location, ParseExpression(Precedence::UnaryExpression, /* In = */ true, Yield, /* Await = */ true, following_context, terminate_on_new_line));
				break;
			}
			if(CurrentVersion >= Version::ES6 && token.keyword == KeywordType::Yield && Yield)
			{
				auto yield_location = token.location;
				ConsumeToken();
				token = AdvanceToken(Context::InputElementRegExp, SameOrTermination(following_context, terminate_on_new_line));
				if(HadLineTerminator())
				{
					return new JS::ParseNode::YieldExpression(yield_location);
				}
				else if(token.type == TokenType(U'*'))
				{
					ConsumeToken();
					token = AdvanceToken(Context::InputElementRegExp);
					node = new JS::ParseNode::YieldGeneratorExpression(yield_location, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, /* Await = */ true, following_context, terminate_on_new_line));
				}
				else
				{
					node = new JS::ParseNode::YieldExpression(yield_location, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, /* Await = */ true, following_context, terminate_on_new_line));
				}
				break;
			}
			if(CurrentVersion >= Version::ES6 && token.keyword == KeywordType::Let && precedence <= Precedence::Statement)
			{
				StashToken();
				token = AdvanceToken(Context::InputElementDiv);
				StepBack();
				if(IsIdentifier(token.type) || token.type == TokenType(U'[') || token.type == TokenType(U'{'))
				{
					return ParseVarOrLexicalDeclaration(/* In = */ true, Yield, Await, terminate_on_new_line);
				}
			}
			[[fallthrough]];
		default:
#if ENABLE_ES4
		parse_identifier:
#endif
			// PrimaryExpression : IdentifierReference
			// PrimaryExpression : PropertyIdentifier
			node = ParsePropertyIdentifier(Yield, Await, following_context, terminate_on_new_line);
			if(Version::ES6 <= CurrentVersion && !HadLineTerminator() && PeekToken().type == TokenType::Arrow)
			{
				auto function = new JS::ParseNode::FunctionDefinition(node->location);
				function->function_position = precedence > Precedence::Statement ? JS::ParseNode::FunctionPosition::Expression : JS::ParseNode::FunctionPosition::Statement;
				function->function_kind = JS::ParseNode::FunctionKind::Arrow;
				if(auto identifier = dynamic_cast<JS::ParseNode::Identifier *>(node))
				{
					function->parameters = new JS::ParseNode::FormalParameters(node->location);
					function->parameters->arguments.push_back(new JS::ParseNode::Identifier(node->location, identifier->StringValue));
				}
				else
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '=>'"));
				}
				ConsumeToken();
				function->body = ParseArrowBody(false, In, following_context, terminate_on_new_line);
				function->SourceText = tokenizer->read_source.substr(function->location.offset, PeekToken().location.offset - function->location.offset);
				right_trim(function->SourceText);
				node = function;
			}
			break;
		case KeywordType::Delete:
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
			node = new JS::ParseNode::UnaryExpression(token.location, JS::ParseNode::UnaryExpression::Operator::Delete, ParseExpression(Precedence::UnaryExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line));
			break;
		case KeywordType::Void:
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
			node = new JS::ParseNode::UnaryExpression(token.location, JS::ParseNode::UnaryExpression::Operator::Void, ParseExpression(Precedence::UnaryExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line));
			break;
		case KeywordType::Typeof:
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
			node = new JS::ParseNode::UnaryExpression(token.location, JS::ParseNode::UnaryExpression::Operator::Typeof, ParseExpression(Precedence::UnaryExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line));
			break;
		}
		break;
	case TokenType::Increment:
	case TokenType::Decrement:
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		node = new JS::ParseNode::UpdateExpression(token.location, ParseExpression(Precedence::UnaryExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line), false, token.type != TokenType::Decrement);
		break;
	case TokenType(U'+'):
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		node = new JS::ParseNode::UnaryExpression(token.location, JS::ParseNode::UnaryExpression::Operator::Plus, ParseExpression(Precedence::UnaryExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line));
		break;
	case TokenType(U'-'):
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		node = new JS::ParseNode::UnaryExpression(token.location, JS::ParseNode::UnaryExpression::Operator::Negate, ParseExpression(Precedence::UnaryExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line));
		break;
	case TokenType(U'~'):
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		node = new JS::ParseNode::UnaryExpression(token.location, JS::ParseNode::UnaryExpression::Operator::BitwiseNot, ParseExpression(Precedence::UnaryExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line));
		break;
	case TokenType(U'!'):
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		node = new JS::ParseNode::UnaryExpression(token.location, JS::ParseNode::UnaryExpression::Operator::LogicalNot, ParseExpression(Precedence::UnaryExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line));
		break;
	case TokenType(U'#'):
		if(CurrentVersion < Version::ESNEXT)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		token = AdvanceToken();
		if(!IsIdentifier(token.type))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
		}
		node = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value), true);
		ConsumeToken();
		if(AdvanceToken(Context::InputElementDiv).keyword != KeywordType::In)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'in'"));
		}
		break;
	}

	token = PeekToken();

	while(token.type == TokenType(U'[')
	|| token.type == TokenType(U'.')
	|| token.type == TokenType(U'(')
#if ENABLE_ES4
	|| token.type == TokenType::TypeParameter_Open
#endif
	|| token.type == TokenType::Descendent
	|| token.type == TokenType::Chaining
	|| token.type == TokenType::NoSubstitutionTemplate
	|| token.type == TokenType::TemplateHead)
	{
		switch(token.type)
		{
		case TokenType(U'['):
			// MemberExpression : MemberExpression '[' Expression ']'
			// SuperProperty : 'super' '[' Expression ']'
			// CallExpression : CallExpression '[' Expression ']'
			// OptionalChain : '?.' '[' Expression ']'
			// OptionalChain : OptionalChain '[' Expression ']'
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
#if ENABLE_ES4
			if(IsVersionES4())
			{
				auto index = new JS::ParseNode::IndexExpression(node->location, node, ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
				while(PeekToken().type == TokenType(U','))
				{
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp);
					index->index.push_back(ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
				}
				node = index;
			}
			else
#endif
			{
				node = new JS::ParseNode::IndexExpression(node->location, node, ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await));
			}
			if(PeekToken().type != TokenType(U']'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ']'"));
			}
			ConsumeToken();
			token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			break;
		case TokenType(U'.'):
			// MemberExpression : MemberExpression '.' IdentifierName
			// MemberExpression : MemberExpression '.' PrivateIdentifier
			// SuperProperty : 'super' '.' IdentifierName
			// CallExpression : CallExpression '.' IdentifierName
			// CallExpression : CallExpression '.' PrivateIdentifier
			// OptionalChain : OptionalChain '.' IdentifierName
			// OptionalChain : OptionalChain '.' PrivateIdentifier
#if ENABLE_ES4
			if(IsVersionES4())
			{
				// PropertyOperator : '.' QualifiedIdentifier
				ConsumeToken();
				AdvanceToken();
				auto property = ParsePropertyIdentifier(Yield, Await, following_context, terminate_on_new_line);
				if(auto computed_property = dynamic_cast<JS::ParseNode::ComputedPropertyName *>(property))
				{
					if(!Supports_E4X())
					{
						JS::Throw(JS::New<SyntaxError> __LOC("Unexpected '('"));
					}
					property = new JS::ParseNode::FilteringPredicate(computed_property->location, computed_property->expression);
				}
				node = new JS::ParseNode::MemberAccessExpression(node->location, node, property);
				token = PeekToken();
			}
			else
#endif
			{
				bool private_identifier = false;
				ConsumeToken();
				token = AdvanceToken();
				if(token.type == TokenType(U'#'))
				{
					if(CurrentVersion < Version::ESNEXT)
					{
						JS::Throw(JS::New<InternalError> __LOC());
					}
					private_identifier = true;
					ConsumeToken();
					token = AdvanceToken();
				}
				// TODO: refactor
				ParseNode * member;
				if(Supports_E4X() && token.type == TokenType(U'('))
				{
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp);
					member = new JS::ParseNode::FilteringPredicate(token.location, ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await));
					if(PeekToken().type != TokenType(U')'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
					}
					ConsumeToken();
					AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
				}
#if ENABLE_ES4
				else if(Supports_E4X() || IsVersionES4())
#else
				else if(Supports_E4X())
#endif
				{
					member = ParsePropertyIdentifier(Yield, Await, following_context, terminate_on_new_line, CurrentVersion >= Version::ES5);
				}
				else
				{
					if(CurrentVersion < Version::ES5)
					{
						member = ParseIdentifier(Yield, Await);
					}
					else
					{
						if(!IsIdentifier(token.type))
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier name"));
						}
						member = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
						ConsumeToken();
					}
					AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
				}
				if(private_identifier)
				{
					if(auto identifier = dynamic_cast<JS::ParseNode::Identifier *>(member))
					{
						identifier->is_private = true;
					}
					else
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid private identifier"));
					}
				}
				node = new JS::ParseNode::MemberAccessExpression(node->location, node, member);
				token = PeekToken();
			}
			break;
		case TokenType(U'('):
			// CoverCallExpressionAndAsyncArrowHead : MemberExpression Arguments
			// CallExpression : CallExpression Arguments
			// OptionalChain : '?.' Arguments
			// OptionalChain : OptionalChain Arguments
			// SuperCall : 'super' Arguments
			// ImportCall : 'import' '(' AssignmentExpression ',':opt ')'
			// ImportCall : 'import' '(' AssignmentExpression ',' AssignmentExpression ')'
			// MemberExpression : 'new' MemberExpression Arguments
			{
				auto paren_location = token.location;
				ConsumeToken();
				JS::ParseNode::CallExpression * call = new JS::ParseNode::CallExpression(node->location, node);
				node = call;
				token = AdvanceToken(Context::InputElementRegExp);
				while(token.type != TokenType(U')'))
				{
					bool spread = false;
					Location ellipsis_location;
					if(token.type == TokenType::Ellipsis)
					{
#if ENABLE_ES4
						if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
						if(CurrentVersion < Version::ES6)
#endif
						{
							JS::Throw(JS::New<InternalError> __LOC());
						}
						spread = true;
						ellipsis_location = token.location;
						ConsumeToken();
						AdvanceToken(Context::InputElementRegExp);
					}
					auto argument = ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await);
					if(spread)
						argument = new JS::ParseNode::RestExpression(ellipsis_location, argument);
					call->arguments.push_back(argument);
					token = PeekToken();
					if(token.type == TokenType(U','))
					{
						ConsumeToken();
						token = AdvanceToken(Context::InputElementRegExp);
					}
					else if(token.type != TokenType(U')'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
					}
				}

				ConsumeToken();
				token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
				if(precedence == Precedence::NewExpression)
				{
					return node;
				}
				if(Version::ESNEXT <= CurrentVersion && !HadLineTerminator() && token.type == TokenType::Arrow)
				{
					if(auto identifier = dynamic_cast<JS::ParseNode::Identifier *>(call->function))
					{
						if(identifier->StringValue != u"async")
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '=>'"));
						}
					}
					else
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected '=>'"));
					}

					auto function = new JS::ParseNode::FunctionDefinition(paren_location);
					function->function_position = precedence > Precedence::Statement ? JS::ParseNode::FunctionPosition::Expression : JS::ParseNode::FunctionPosition::Statement;
					function->function_kind = JS::ParseNode::FunctionKind::Arrow;
					function->function_type = JS::ParseNode::FunctionType::Async;
					function->parameters = new JS::ParseNode::FormalParameters(paren_location);
					for(auto argument : call->arguments)
					{
						function->parameters->arguments.push_back(argument);
					}
					ConsumeToken();
					function->body = ParseArrowBody(true, In, following_context, terminate_on_new_line);
					function->SourceText = tokenizer->read_source.substr(function->location.offset, PeekToken().location.offset - function->location.offset);
					right_trim(function->SourceText);
					node = function;
				}
			}
			break;
#if ENABLE_ES4
		case TokenType::TypeParameter_Open:
			if(!IsVersionES4())
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			{
				auto application = new JS::ParseNode::TypeApplication(node->location, node);
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				while(true)
				{
					application->type_parameters.push_back(ParseTypeExpression(In, Yield, Await, /* is_type_parameter = */ true));
					token = PeekToken();
					if(token.type == TokenType(U','))
					{
						ConsumeToken();
						AdvanceToken(Context::InputElementRegExp);
					}
					else if(token.type == TokenType(U'>'))
					{
						break;
					}
					else
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ',' or '>'"));
					}
				}
				ConsumeToken();
				token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
				node = application;
			}
			break;
#endif
		case TokenType::Descendent: // E4X
			// MemberExpression : MemberExpression '..' Identifier
			// MemberExpression : MemberExpression '..' PropertyIdentifier
			// CallExpression : CallExpression '..' Identifier
			// CallExpression : CallExpression '..' PropertyIdentifier
			ConsumeToken();
			token = AdvanceToken();
			ParseNode * member;
			member = ParsePropertyIdentifier(Yield, Await, following_context, terminate_on_new_line);
			node = new JS::ParseNode::DescendentExpression(node->location, node, member);
			token = PeekToken();
			break;
		case TokenType::NoSubstitutionTemplate:
		case TokenType::TemplateHead:
			if(CurrentVersion < Version::ES6)
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			// MemberExpression : MemberExpression TemplateLiteral
			node = new JS::ParseNode::TaggedTemplate(node->location, node, ParseSubstitutionTemplate(Yield, Await, /* Tagged = */ false));
			token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
			break;
		case TokenType::Chaining:
			if(CurrentVersion < Version::ESNEXT)
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			// OptionalExpression : MemberExpression OptionalChain
			// OptionalExpression : CallExpression OptionalChain
			// OptionalExpression : OptionalExpression OptionalChain
			if(precedence == Precedence::NewExpression)
			{
				return node;
			}
			ConsumeToken();
			node = new JS::ParseNode::OptionalChain(node->location, node);
			token = AdvanceToken();
			{
				bool private_identifier = false;
				switch(token.type)
				{
				case TokenType(U'['):
					// OptionalChain : '?.' '[' Expression ']'
				case TokenType(U'('):
					// OptionalChain : '?.' Arguments
				case TokenType::NoSubstitutionTemplate:
				case TokenType::TemplateHead:
					// OptionalChain : TemplateLiteral
					break;
				case TokenType(U'#'):
					// OptionalChain : '?.' PrivateIdentifier
					private_identifier = true;
					ConsumeToken();
					token = AdvanceToken();
					[[fallthrough]];
				default:
					// OptionalChain : '?.' Identifier
					if(!IsIdentifier(token.type))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier name"));
					}
					node = new JS::ParseNode::MemberAccessExpression(node->location, node, new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value), private_identifier));
					ConsumeToken();
					token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
					break;
				}
			}
			break;
		default:
			break;
		}
	}

	if(token.type == TokenType::Increment || token.type == TokenType::Decrement)
	{
		if(HadLineTerminator())
		{
			return node;
		}

		node = new JS::ParseNode::UpdateExpression(node->location, node, true, token.type != TokenType::Decrement);
		ConsumeToken();
		token = AdvanceToken(following_context, SameOrTermination(terminate_on_new_line));
	}

	if(precedence > Precedence::ExponentiationExpression)
	{
		return node;
	}

	if(token.type == TokenType::Power)
	{
		if(CurrentVersion < Version::ESNEXT)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Power, node, ParseExpression(Precedence::ExponentiationExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line));
		token = PeekToken();
	}

	if(precedence > Precedence::MultiplicativeExpression)
	{
		return node;
	}

	while(token.type == TokenType(U'*') || token.type == TokenType(U'/') || token.type == TokenType(U'%'))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::ExponentiationExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line);
		switch(token.type)
		{
		case TokenType(U'*'):
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Multiply, node, operand);
			break;
		case TokenType(U'/'):
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Divide, node, operand);
			break;
		case TokenType(U'%'):
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Modulo, node, operand);
			break;
		default:
			assert(false);
			break;
		}
		token = PeekToken();
	}

	if(precedence > Precedence::AdditiveExpression)
	{
		return node;
	}

	while(token.type == TokenType(U'+') || token.type == TokenType(U'-'))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::MultiplicativeExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line);
		switch(token.type)
		{
		case TokenType(U'+'):
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Add, node, operand);
			break;
		case TokenType(U'-'):
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Subtract, node, operand);
			break;
		default:
			assert(false);
			break;
		}
		token = PeekToken();
	}

	if(precedence > Precedence::ShiftExpression)
	{
		return node;
	}

	while(token.type == TokenType::ShiftLeft || token.type == TokenType::ShiftRight || token.type == TokenType::ShiftRightUnsigned)
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::AdditiveExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line);
		switch(token.type)
		{
		case TokenType::ShiftLeft:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::ShiftLeft, node, operand);
			break;
		case TokenType::ShiftRight:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::ShiftRight, node, operand);
			break;
		case TokenType::ShiftRightUnsigned:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::ShiftRightUnsigned, node, operand);
			break;
		default:
			assert(false);
			break;
		}
		token = PeekToken();
	}

	if(precedence > Precedence::RelationalExpression)
	{
		return node;
	}

	while(token.type == TokenType(U'<')
		|| token.type == TokenType(U'>')
		|| token.type == TokenType::LessEqual
		|| token.type == TokenType::GreaterEqual
#if ENABLE_ES4
		|| (IsVersionES4()
			&& (token.keyword == KeywordType::Is
			|| token.keyword == KeywordType::As))
#endif
		|| (Version::ES3 <= CurrentVersion
			&& (token.keyword == KeywordType::Instanceof
			|| (In && token.keyword == KeywordType::In))))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::ShiftExpression, /* In = */ true, Yield, Await, following_context, terminate_on_new_line);
		switch(token.type)
		{
		case TokenType(U'<'):
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Less, node, operand);
			break;
		case TokenType(U'>'):
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Greater, node, operand);
			break;
		case TokenType::LessEqual:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::LessEqual, node, operand);
			break;
		case TokenType::GreaterEqual:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::GreaterEqual, node, operand);
			break;
		default:
			switch(token.keyword)
			{
#if ENABLE_ES4
			case KeywordType::Is:
				node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Is, node, operand);
				break;
			case KeywordType::As:
				node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::As, node, operand);
				break;
#endif
			case KeywordType::Instanceof:
				node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Instanceof, node, operand);
				break;
			case KeywordType::In:
				node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::In, node, operand);
				break;
			default:
				assert(false);
				break;
			}
			break;
		}
		token = PeekToken();
	}

	if(precedence > Precedence::EqualityExpression)
	{
		return node;
	}

	while(token.type == TokenType::Equal
		|| token.type == TokenType::NotEqual
		|| (Version::ES3 <= CurrentVersion
			&& (token.type == TokenType::StrictEqual
			|| token.type == TokenType::StrictNotEqual)))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::RelationalExpression, In, Yield, Await, following_context, terminate_on_new_line);
		switch(token.type)
		{
		case TokenType::Equal:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Equal, node, operand);
			break;
		case TokenType::NotEqual:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::NotEqual, node, operand);
			break;
		case TokenType::StrictEqual:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::StrictEqual, node, operand);
			break;
		case TokenType::StrictNotEqual:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::StrictNotEqual, node, operand);
			break;
		default:
			assert(false);
			break;
		}
		token = PeekToken();
	}

	if(precedence > Precedence::BitwiseAndExpression)
	{
		return node;
	}

	while(token.type == TokenType(U'&'))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::EqualityExpression, In, Yield, Await, following_context, terminate_on_new_line);
		node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::BitwiseAnd, node, operand);
		token = PeekToken();
	}

	if(precedence > Precedence::BitwiseXorExpression)
	{
		return node;
	}

	while(token.type == TokenType(U'^'))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::BitwiseAndExpression, In, Yield, Await, following_context, terminate_on_new_line);
		node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::BitwiseXor, node, operand);
		token = PeekToken();
	}

	if(precedence > Precedence::BitwiseOrExpression)
	{
		return node;
	}

	while(token.type == TokenType(U'|'))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::BitwiseXorExpression, In, Yield, Await, following_context, terminate_on_new_line);
		node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::BitwiseOr, node, operand);
		token = PeekToken();
	}

	if(precedence > Precedence::LogicalAndExpression)
	{
		return node;
	}

	while(token.type == TokenType::And
		|| (CurrentVersion >= Version::ESNEXT && token.type == TokenType::Coalesce))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::BitwiseOrExpression, In, Yield, Await, following_context, terminate_on_new_line);
		switch(token.type)
		{
		case TokenType::And:
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::LogicalAnd, node, operand);
			break;
		case TokenType::Coalesce:
			// TODO: cannot mix coalesce with logical operators
			node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Coalesce, node, operand);
			break;
		default:
			assert(false);
			break;
		}
		token = PeekToken();
	}

#if ENABLE_ES4
	if(precedence > Precedence::LogicalXorExpression)
	{
		return node;
	}

	while(token.type == TokenType::LogicalXor)
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::LogicalAndExpression, In, Yield, Await, following_context, terminate_on_new_line);
		node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::LogicalXor, node, operand);
		token = PeekToken();
	}
#endif

	if(precedence > Precedence::LogicalOrExpression)
	{
		return node;
	}

	while(token.type == TokenType::Or)
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence(int(Precedence::LogicalOrExpression) + 1), In, Yield, Await, following_context, terminate_on_new_line);
		node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::LogicalOr, node, operand);
		token = PeekToken();
	}

	if(precedence > Precedence::ConditionalExpression)
	{
		return node;
	}

	if(token.type == TokenType(U'?'))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto branch1 = ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await);
		if(PeekToken().type != TokenType(U':'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ':'"));
		}
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto branch2 = ParseExpression(Precedence::AssignmentExpression, In, Yield, Await, following_context, terminate_on_new_line);
		node = new JS::ParseNode::ConditionalExpression(node->location, node, branch1, branch2);
		token = PeekToken();
	}

	if(precedence > Precedence::AssignmentExpression)
	{
		return node;
	}

	if(token.type == TokenType(U'=')
	|| token.type == TokenType::Assign_Modulo
	|| token.type == TokenType::Assign_BitAnd
	|| token.type == TokenType::Assign_And
	|| token.type == TokenType::Assign_Multiply
	|| token.type == TokenType::Assign_Power
	|| token.type == TokenType::Assign_Add
	|| token.type == TokenType::Assign_Subtract
	|| token.type == TokenType::Assign_ShiftLeft
	|| token.type == TokenType::Assign_ShiftRight
	|| token.type == TokenType::Assign_ShiftRightUnsigned
	|| token.type == TokenType::Assign_Coalesce
	|| token.type == TokenType::Assign_Divide
	|| token.type == TokenType::Assign_BitXor
#if ENABLE_ES4
	|| token.type == TokenType::Assign_Xor
#endif
	|| token.type == TokenType::Assign_BitOr
	|| token.type == TokenType::Assign_Or)
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto source = ParseExpression(Precedence::AssignmentExpression, In, Yield, Await, following_context, terminate_on_new_line);
		switch(token.type)
		{
		case TokenType(U'='):
			node = new JS::ParseNode::AssignmentExpression(node->location, node, source);
			break;
		case TokenType::Assign_Modulo:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Modulo, node, source);
			break;
		case TokenType::Assign_BitAnd:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::BitwiseAnd, node, source);
			break;
		case TokenType::Assign_And:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::LogicalAnd, node, source);
			break;
		case TokenType::Assign_Multiply:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Multiply, node, source);
			break;
		case TokenType::Assign_Power:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Power, node, source);
			break;
		case TokenType::Assign_Add:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Add, node, source);
			break;
		case TokenType::Assign_Subtract:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Subtract, node, source);
			break;
		case TokenType::Assign_ShiftLeft:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::ShiftLeft, node, source);
			break;
		case TokenType::Assign_ShiftRight:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::ShiftRight, node, source);
			break;
		case TokenType::Assign_ShiftRightUnsigned:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::ShiftRightUnsigned, node, source);
			break;
		case TokenType::Assign_Coalesce:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Coalesce, node, source);
			break;
		case TokenType::Assign_Divide:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Divide, node, source);
			break;
		case TokenType::Assign_BitXor:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::BitwiseXor, node, source);
			break;
#if ENABLE_ES4
		case TokenType::Assign_Xor:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::LogicalXor, node, source);
			break;
#endif
		case TokenType::Assign_BitOr:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::BitwiseOr, node, source);
			break;
		case TokenType::Assign_Or:
			node = new JS::ParseNode::CombinedAssignmentExpression(node->location, JS::ParseNode::BinaryExpression::Operator::LogicalOr, node, source);
			break;
		default:
			assert(false);
			break;
		}
		token = PeekToken();
	}

	if(precedence > Precedence::CommaExpression)
	{
		return node;
	}

	while(token.type == TokenType(U','))
	{
		ConsumeToken();
		AdvanceToken(Context::InputElementRegExp);
		auto operand = ParseExpression(Precedence::AssignmentExpression, In, Yield, Await, following_context, terminate_on_new_line);
		node = new JS::ParseNode::BinaryExpression(node->location, JS::ParseNode::BinaryExpression::Operator::Comma, node, operand);
		token = PeekToken();
	}

	return node;
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParseVarOrLexicalDeclaration(bool In, bool Yield, bool Await, bool terminate_on_new_line, bool for_declaration)
{
	JS::ParseNode::DeclarationKind kind;
	switch(PeekToken().keyword)
	{
	case KeywordType::Const:
#if ENABLE_ES4
		if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
		if(CurrentVersion < Version::ES6)
#endif
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		kind = JS::ParseNode::DeclarationKind::Const;
		break;
	case KeywordType::Let:
		if(CurrentVersion < Version::ES6)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		kind = JS::ParseNode::DeclarationKind::Let;
		break;
	case KeywordType::Var:
		kind = JS::ParseNode::DeclarationKind::Var;
		break;
	default:
		JS::Throw(JS::New<InternalError> __LOC());
	}
	auto declaration_location = ConsumeToken().location;

	auto node = new JS::ParseNode::IdentifierDeclaration(declaration_location, kind);

	Token token;
	while(true)
	{
		// might come after parsing "let [" which will leave the '[' in an InputElementDiv context
		Token token = AdvanceToken(Context::InputElementDiv);
		bool is_binding_pattern = !IsIdentifier(token.type);
		ParseNode * element;
		switch(token.type)
		{
		default:
			if(!IsIdentifier(token.type))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier or '[' or '{'"));
			}
			// fallthru
		case TokenType('['): // TODO: restrict permitted version
		case TokenType('{'):
#if ENABLE_ES4
			if(IsVersionES4())
			{
				// ES4.2008 permits using reserved words here
				if(!IsIdentifier(token.type))
				{
					JS::Throw(JS::New<SyntaxError> __LOC("Expected identifier name"));
				}
				ConsumeToken();
				element = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
			}
			else
#endif
			{
				element = ParseBindingElement(Yield, Await);
			}
			token = AdvanceToken(Context::InputElementUnspecified, SameOrTermination(terminate_on_new_line));
#if ENABLE_ES4
			if(IsVersionES4() && token.type == TokenType(U':'))
			{
				ConsumeToken();
				token = AdvanceToken(Context::InputElementRegExp);
				static_cast<JS::ParseNode::Identifier *>(element)->type = ParseTypeExpression(In, Yield, Await);
				token = PeekToken();
			}
#endif
			if(token.type == TokenType(U'='))
			{
				ConsumeToken();
				token = AdvanceToken(Context::InputElementRegExp);
				auto assignment = new JS::ParseNode::AssignmentExpression(element->location, element, ParseExpression(Precedence::AssignmentExpression, In, Yield, Await, Context::InputElementDiv, terminate_on_new_line));
				element = assignment;
				token = PeekToken();
#if ENABLE_ES4
				if(IsVersionES4() && !HadLineTerminator() && (token.type == TokenType::Identifier || JS::ParseNode::IsReservedNamespace(token.keyword)))
				{
					auto attribute_combination = new JS::ParseNode::AttributeCombination(assignment->source->location);
					attribute_combination->attributes.push_back(assignment->source);
					assignment->source = attribute_combination;
					while(!HadLineTerminator() && (token.type == TokenType::Identifier || JS::ParseNode::IsReservedNamespace(token.keyword)))
					{
						attribute_combination->attributes.push_back(ParseExpression(Precedence::LeftHandSideExpression, In, Yield, Await, Context::InputElementDiv, terminate_on_new_line));
						token = PeekToken();
					}
				}
#endif
			}
			else if(is_binding_pattern && !for_declaration)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '='"));
			}
			break;
		}
		node->declared_identifiers.push_back(new JS::ParseNode::LocalDeclarator(element->location, element));
		if(token.type != TokenType(U','))
		{
			break;
		}
		ConsumeToken();
	}
	return node;
}

void JS::Parser::Parser::ParseSemicolon(Context following_context AND_FLAG(do_while_block))
{
	Token token = PeekToken();
	if(token.type == TokenType(U';'))
	{
		ConsumeToken();
		AdvanceToken(following_context);
	}
#if ENABLE_ES4
	else if(IsVersionES4() && StrictMode)
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Automatic semicolon insertion disabled in strict mode for ECMAScript 4"));
	}
	else if(IsVersionES4() && token.keyword == KeywordType::Else)
	{
		// virtual semicolon
	}
	else if(IsVersionES4() && do_while_block && token.keyword == KeywordType::While)
	{
		// virtual semicolon
	}
#endif
	else if(token.type != TokenType(U'}') && !HadLineTerminator())
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ';' or '}' or line terminator"));
	}
}

std::optional<JS::ParseNode::ParseNode *> JS::Parser::Parser::ParseForInitializer(bool Yield, bool Await)
{
	Token token = PeekToken();
	switch(token.keyword)
	{
	case KeywordType::Const:
#if ENABLE_ES4
		if(CurrentVersion < Version::ES6 && !IsVersionES4())
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'const': const variables in for loops only supported in ECMAScript 4, ECMAScript 6 or later"));
		}
#else
		if(CurrentVersion < Version::ES6)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'const': const variables in for loops only supported in ECMAScript 6 or later"));
		}
#endif
		// fallthru
	case KeywordType::Var:
		return ParseVarOrLexicalDeclaration(/* In = */ false, Yield, Await, /* terminate_on_newline = */ false, /* for_declaration = */ true);
	case KeywordType::Let:
		if(CurrentVersion >= Version::ES6)
		{
			StashToken();
			token = AdvanceToken(Context::InputElementDiv);
			StepBack();
			if(IsIdentifier(token.type) || token.type == TokenType(U'{') || token.type == TokenType(U'['))
			{
				return ParseVarOrLexicalDeclaration(/* In = */ false, Yield, Await, /* terminate_on_newline = */ false, /* for_declaration = */ true);
			}
		}
		// fallthru
	default:
		if(token.type != TokenType(U';'))
		{
			ParseNode * first_expression = ParseExpression(Precedence::CommaExpression, /* In = */ false, Yield, Await);
#if ENABLE_ES4
			if(IsVersionES4() && !HadLineTerminator())
			{
				std::optional<ParseNode *> actual_initializer;
				switch(PeekToken().keyword)
				{
				default:
					token = PeekToken();
					if(token.type != TokenType::Identifier && !JS::ParseNode::IsReservedNamespace(token.keyword)) // attribute
						break;
					// fallthru
				case KeywordType::Const:
				case KeywordType::False: // attribute
				case KeywordType::True: // attribute
				case KeywordType::Var:
					actual_initializer = ParseForInitializer(Yield, Await);
					if(!actual_initializer.has_value())
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ';' after attributes"));
					}
					first_expression = new JS::ParseNode::AttachedAttribute(first_expression->location, first_expression, actual_initializer.value());
				}
			}
#endif
			return first_expression;
		}
		else
		{
			return {};
		}
	}
}

std::optional<JS::ParseNode::WithClause *> JS::Parser::Parser::ParseWithClause(bool terminate_on_new_line)
{
	Token token = PeekToken();
	if(Version::ESNEXT <= CurrentVersion && token.keyword == KeywordType::With)
	{
		auto with_clause = new JS::ParseNode::WithClause(token.location);
		ConsumeToken();
		token = AdvanceToken();
		if(token.type != TokenType(U'{'))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '{'"));
		}
		ConsumeToken();
		token = AdvanceToken();
		while(token.type != TokenType(U'}'))
		{
			auto attribute_key_location = token.location;
			std::variant<JS::ParseNode::Identifier *, JS::ParseNode::StringLiteral *> attribute_key;
			switch(token.type)
			{
			case TokenType::Identifier:
			case TokenType::ReservedWord:
				attribute_key = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
				break;
			case TokenType::StringLiteral:
				attribute_key = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
				break;
			default:
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier or string literal"));
			}
			ConsumeToken();
			token = AdvanceToken();
			if(token.type != TokenType(U':'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ':'"));
			}
			ConsumeToken();
			token = AdvanceToken();
			if(token.type != TokenType::StringLiteral)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected string literal"));
			}
			with_clause->entries.push_back(new JS::ParseNode::ImportWithEntry(attribute_key_location, attribute_key, std::get<JSString>(token.value)));
			ConsumeToken();
			token = AdvanceToken();
			if(token.type == TokenType(U','))
			{
				ConsumeToken();
				token = AdvanceToken();
			}
			else if(token.type != TokenType(U'}'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ',' or '}'"));
			}
		}
		ConsumeToken();
		token = AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
		return with_clause;
	}
	else
	{
		return {};
	}
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParseStatement(
	ParseLevel level,
	bool Yield,
	bool Await,
	bool Return,
	StatementContext context,
	bool terminate_on_new_line
#if ENABLE_ES4
	, bool permit_bodyless_functions
#endif
)
{
	// TODO: might begin after an automatic semicolon insertion took place
	/*if(CurrentVersion >= Version::ES3)
	{
		assert(IsRegExpInContext());
	}*/
	Token token = PeekToken();
	ParseNode * node = nullptr;

	if(context == StatementContext::SwitchBodyStart && token.keyword != KeywordType::Case && token.keyword != KeywordType::Default)
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected case or default"));
	}

	switch(token.type)
	{
	case TokenType(U';'): // ES1
		ConsumeToken();
		node = new JS::ParseNode::Empty(token.location);
		AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
		break;
	case TokenType(U'{'): // ES1
		node = ParseBlock(Yield, Await, Return);
		AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
		break;
	default:
		switch(token.keyword)
		{
		case KeywordType::Break: // ES1
			{
				ConsumeToken();
				token = AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				std::optional<JS::ParseNode::Identifier *> identifier;
				if(Version::ES3 <= CurrentVersion && !HadLineTerminator() && token.type != TokenType(U';') && token.type != TokenType(U'}'))
				{
					identifier = ParseIdentifier(Yield, Await);
					AdvanceToken(Context::InputElementUnspecified, SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
				}
				ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(context == StatementContext::DoWhileBody));
				return new JS::ParseNode::BreakStatement(token.location, identifier);
			}
			break;
		case KeywordType::Case: // ES3
			if(CurrentVersion < Version::ES3)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'case': switch statements are only supported in ECMAScript 3 or later"));
			}
			else
			{
				switch(context)
				{
				case StatementContext::SwitchBodyStart:
				case StatementContext::SwitchBody:
				case StatementContext::PastDefault:
					break;
				default:
					JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid case statement outside switch block"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				auto condition = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await);
				if(PeekToken().type != TokenType(U':'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ':'"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				return new JS::ParseNode::CaseStatement(token.location, condition);
			}
			break;
		case KeywordType::Continue: // ES1
			{
				ConsumeToken();
				token = AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				std::optional<JS::ParseNode::Identifier *> identifier;
				if(Version::ES3 <= CurrentVersion && !HadLineTerminator() && token.type != TokenType(U';') && token.type != TokenType(U'}'))
				{
					identifier = ParseIdentifier(Yield, Await);
					AdvanceToken(Context::InputElementUnspecified, SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
				}
				ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(context == StatementContext::DoWhileBody));
				return new JS::ParseNode::ContinueStatement(token.location, identifier);
			}
			break;
		case KeywordType::Const: // ES6 and ES4
#if ENABLE_ES4
			if(CurrentVersion < Version::ES6 && !IsVersionES4())
#else
			if(CurrentVersion < Version::ES6)
#endif
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'const'"));
			}
			node = ParseVarOrLexicalDeclaration(/* In = */ true, Yield, Await);
			ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(context == StatementContext::DoWhileBody));
			break;
		case KeywordType::Debugger: // ES5
			if(CurrentVersion < Version::ES5)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'debugger': debugger statements are only supported in ECMAScript 5 or later"));
			}
			else
			{
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(context == StatementContext::DoWhileBody));
				return new JS::ParseNode::DebuggerStatement(token.location);
			}
			break;
		case KeywordType::Default: // ES3
			if(CurrentVersion < Version::ES3)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'default': switch statements are only supported in ECMAScript 3 or later"));
			}
			else
			{
				auto default_location = token.location;
				ConsumeToken();
				AdvanceToken();
				if(Supports_E4X() && PeekToken().keyword == KeywordType::Xml)
				{
					ConsumeToken();
					AdvanceToken();
					if(PeekToken().keyword != KeywordType::Namespace)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'namespace'"));
					}
					ConsumeToken();
					AdvanceToken();
					if(PeekToken().type != TokenType(U'='))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '='"));
					}
					ConsumeToken();
					token = AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
					auto expression = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await, Context::InputElementDiv, terminate_on_new_line);
					ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(context == StatementContext::DoWhileBody));
					return new JS::ParseNode::DefaultXmlNamespaceStatement(default_location, expression);
				}
				else
				{
					switch(context)
					{
					case StatementContext::SwitchBodyStart:
					case StatementContext::SwitchBody:
						break;
					case StatementContext::PastDefault:
						JS::Throw(JS::New<SyntaxError> __LOC(u"Duplicate default statement inside switch block"));
					default:
						JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid default statement outside switch block"));
					}
					if(PeekToken().type != TokenType(U':'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ':'"));
					}
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
					return new JS::ParseNode::DefaultStatement(default_location);
				}
			}
			break;
		case KeywordType::Do: // ES3
			if(CurrentVersion < Version::ES3)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'do': do-while loops are only supported in ECMAScript 3 or later"));
			}
			else
			{
				auto do_location = token.location;
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
#if ENABLE_ES4
				auto body = ParseStatement(ParseLevel::Clause, Yield, Await, Return, IsVersionES4() ? StatementContext::DoWhileBody : StatementContext::General);
#else
				auto body = ParseStatement(ParseLevel::Clause, Yield, Await, Return);
#endif
				if(PeekToken().keyword != KeywordType::While)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'while'"));
				}
				ConsumeToken();
				if(AdvanceToken().type != TokenType(U'('))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				auto condition = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await);
				if(PeekToken().type != TokenType(U')'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
				}
				ConsumeToken();
				if(AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line)).type == TokenType(U';'))
				{
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				}
				return new JS::ParseNode::DoWhileStatement(do_location, body, condition);
			}
		case KeywordType::Export: // ES6
			if(CurrentVersion < Version::ES6)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'import': import statement only supported in ECMAScript 6 or later"));
			}
			else if(level < ParseLevel::Module)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'import': import statement only supported on module level"));
			}
			else
			{
				auto export_statement = new JS::ParseNode::ExportStatement(token.location);
				ConsumeToken();
				token = AdvanceToken();
				bool is_default = false;
				JS::ParseNode::ExportFromClause * export_from = nullptr;
				switch(token.type)
				{
				case TokenType(U'{'):
					export_from = new JS::ParseNode::ExportFromClause(token.location);
					export_statement->expression = export_from;

					{
						auto named_exports = new JS::ParseNode::NamedExports(token.location);
						export_from->export_clause = named_exports;
						ConsumeToken();
						token = AdvanceToken();
						while(token.type != TokenType(U'}'))
						{
							auto first_name_location = token.location;
							JS::ParseNode::ExportSpecifier * export_specifier;
							std::variant<JS::ParseNode::Identifier *, JS::ParseNode::StringLiteral *> first_name;
							switch(token.type)
							{
							case TokenType::Identifier:
							case TokenType::ReservedWord:
								first_name = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
								break;
							case TokenType::StringLiteral:
								first_name = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
								break;
							default:
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier or string literal"));
							}
							ConsumeToken();
							token = AdvanceToken();
							if(token.keyword == KeywordType::As)
							{
								std::variant<JS::ParseNode::Identifier *, JS::ParseNode::StringLiteral *> second_name;

								ConsumeToken();
								token = AdvanceToken();
								switch(token.type)
								{
								case TokenType::Identifier:
								case TokenType::ReservedWord:
									second_name = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
									break;
								case TokenType::StringLiteral:
									second_name = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
									break;
								default:
									JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier or string literal"));
								}

								export_specifier = new JS::ParseNode::ExportSpecifier(first_name_location, first_name, second_name);

								ConsumeToken();
								token = AdvanceToken();
							}
							else
							{
								export_specifier = new JS::ParseNode::ExportSpecifier(first_name_location, first_name, {});
							}
							named_exports->export_specifiers.push_back(export_specifier);
							if(token.type == TokenType(U','))
							{
								ConsumeToken();
								token = AdvanceToken();
							}
							else if(token.type != TokenType(U'}'))
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ',' or '}'"));
							}
						}
						ConsumeToken();
						token = AdvanceToken();
					}
					break;
				case TokenType(U'*'):
					export_from = new JS::ParseNode::ExportFromClause(token.location);
					export_statement->expression = export_from;

					ConsumeToken();
					token = AdvanceToken();
					if(Version::ESNEXT <= CurrentVersion && token.keyword == KeywordType::As)
					{
						ConsumeToken();
						token = AdvanceToken();
						switch(token.type)
						{
						case TokenType::Identifier:
						case TokenType::ReservedWord:
							export_from->export_clause = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
							break;
						case TokenType::StringLiteral:
							export_from->export_clause = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
							break;
						default:
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier or string literal"));
						}
						ConsumeToken();
						token = AdvanceToken();
					}
					break;

				default:
					if(token.keyword == KeywordType::Default)
					{
						is_default = true;
						ConsumeToken();
						token = AdvanceToken();
					}

					switch(token.keyword)
					{
					case KeywordType::Const:
						if(is_default)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'const'"));
						}
						// fallthru
					case KeywordType::Var:
						if(is_default)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'var'"));
						}
						// fallthru
					case KeywordType::Let:
						if(!is_default)
						{
							export_statement->expression = ParseVarOrLexicalDeclaration(/* In = */ true, /* Yield = */ false, /* Await = */ true, terminate_on_new_line);
							ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
							break;
						}
						// fallthru
					default:
						export_statement->expression = ParseExpression(Precedence::AssignmentExpression, /* In = */ true, /* Yield = */ false, /* Await = */ true, Context::InputElementDiv, terminate_on_new_line);
						ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
						break;

					case KeywordType::Async:
						if(CurrentVersion < Version::ESNEXT)
						{
							export_statement->expression = ParseExpression(Precedence::AssignmentExpression, /* In = */ true, /* Yield = */ false, /* Await = */ true, Context::InputElementDiv, terminate_on_new_line);
							ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
							break;
						}
						StashToken();
						token = AdvanceToken(Context::InputElementDiv, Context::InputElementRegExp);
						StepBack();
						if(HadLineTerminator() || token.keyword != KeywordType::Function)
						{
							export_statement->expression = ParseExpression(Precedence::AssignmentExpression, /* In = */ true, /* Yield = */ false, /* Await = */ true, Context::InputElementDiv, terminate_on_new_line);
							ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
							break;
						}
						// fallthru
					case KeywordType::Function:
					case KeywordType::Class:
						export_statement->expression = ParseExpression(Precedence::Statement, /* In = */ true, /* Yield = */ false, /* Await = */ true, /* Default = */ is_default, Context::InputElementDiv, terminate_on_new_line);
						break;
					}

					export_statement->is_default = is_default;
					return export_statement;
				}

				if(export_from == nullptr)
				{
					JS::Throw(JS::New<InternalError> __LOC());
				}

				// note: same syntax as for export
				if(token.keyword != KeywordType::From)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'from'"));
				}
				ConsumeToken();
				token = AdvanceToken();

				if(token.type != TokenType::StringLiteral)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected string literal"));
				}

				export_from->from_module = std::get<JSString>(token.value);

				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				export_from->with_clause = ParseWithClause(terminate_on_new_line);
				ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(false));

				export_statement->is_default = is_default;
				return export_statement;
			}
			break;
		case KeywordType::For: // ES1
			{
				auto for_location = token.location;
				JS::ParseNode::ForEachKind kind = JS::ParseNode::ForEachKind::ForIn;
				ConsumeToken();
				token = AdvanceToken();
				switch(token.keyword)
				{
				case KeywordType::Each:
					if(Supports_E4X())
					{
						if(HadLineTerminator())
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected line terminator"));
						}
						kind = JS::ParseNode::ForEachKind::ForEachIn;
						ConsumeToken();
						token = AdvanceToken();
					}
					break;
				case KeywordType::Await:
					if(Version::ESNEXT <= CurrentVersion)
					{
						kind = JS::ParseNode::ForEachKind::ForAwaitOf;
						ConsumeToken();
						token = AdvanceToken();
					}
					break;
				default:
					break;
				}
				if(PeekToken().type != TokenType(U'('))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				std::optional<ParseNode *> first_expression = ParseForInitializer(Yield, Await);
				ParseNode * iterator;
				token = PeekToken();
				switch(token.keyword)
				{
				case KeywordType::In:
					if(kind == JS::ParseNode::ForEachKind::ForAwaitOf)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'in'"));
					}
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp);
					iterator = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await);
					break;
				case KeywordType::Of:
					if(Version::ES6 <= CurrentVersion)
					{
						if(kind == JS::ParseNode::ForEachKind::ForEachIn)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'of'"));
						}
						if(kind != JS::ParseNode::ForEachKind::ForAwaitOf)
						{
							kind = JS::ParseNode::ForEachKind::ForOf;
						}
						ConsumeToken();
						AdvanceToken(Context::InputElementRegExp);
						iterator = ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await);
						break;
					}
					// fallthru
				default:
					switch(kind)
					{
					case JS::ParseNode::ForEachKind::ForIn:
						if(token.type != TokenType(U';'))
						{
							if(Version::ESNEXT <= CurrentVersion)
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ';' or 'in' or 'of'"));
							}
							else
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ';' or 'in'"));
							}
						}
						else
						{
							// three-part for
							ConsumeToken();
							AdvanceToken(Context::InputElementRegExp);
							std::optional<ParseNode *> condition;
							if(PeekToken().type != TokenType(U';'))
							{
								condition = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await);
							}
							if(PeekToken().type != TokenType(U';'))
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ';'"));
							}

							ConsumeToken();
							AdvanceToken(Context::InputElementRegExp);
							std::optional<ParseNode *> increment;
							if(PeekToken().type != TokenType(U')'))
							{
								increment = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await);
							}
							if(PeekToken().type != TokenType(U')'))
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
							}

							ConsumeToken();
							AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
							auto body = ParseStatement(ParseLevel::Clause, Yield, Await, Return, terminate_on_new_line);
							return new JS::ParseNode::ForStatement(for_location, first_expression, condition, increment, body);
						}
						break;
					case JS::ParseNode::ForEachKind::ForEachIn:
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'in'"));
					case JS::ParseNode::ForEachKind::ForAwaitOf:
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'of'"));
					default:
						JS::Throw(JS::New<InternalError> __LOC());
					}
				}

				// parse for-in family
				if(PeekToken().type != TokenType(U')'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				auto body = ParseStatement(ParseLevel::Clause, Yield, Await, Return, terminate_on_new_line);
				return new JS::ParseNode::ForEachStatement(for_location, kind, first_expression.value(), iterator, body);
			}
			break;
		case KeywordType::If: // ES1
			{
				auto if_location = token.location;
				ConsumeToken();
				if(AdvanceToken().type != TokenType(U'('))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				auto condition = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await);
				if(PeekToken().type != TokenType(U')'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				auto true_statement = ParseStatement(ParseLevel::Clause, Yield, Await, Return, terminate_on_new_line);
				std::optional<ParseNode *> false_statement;
				if(PeekToken().keyword == KeywordType::Else)
				{
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
					false_statement = ParseStatement(ParseLevel::Clause, Yield, Await, Return, terminate_on_new_line);
				}
				return new JS::ParseNode::IfStatement(if_location, condition, true_statement, false_statement);
			}
		case KeywordType::Import: // ES6; ES4
#if ENABLE_ES4
			if(IsVersionES4())
			{
				auto import_location = token.location;
				if(level < ParseLevel::Block)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'import': import statement only supported on block level"));
				}
				ConsumeToken();
				token = AdvanceToken();
				auto import = new JS::ParseNode::ES4ImportStatement(import_location);
				if(token.type == TokenType::Identifier)
				{
					JSString name = std::get<JSString>(token.value);
					StashToken();
					token = AdvanceToken(Context::InputElementUnspecified, Context::InputElementRegExp);
					if(token.type == TokenType(U'='))
					{
						import->import_name = name;
						ConsumeToken();
						token = AdvanceToken();
					}
					else
					{
						token = StepBack();
					}
				}

				if(token.type == TokenType::Identifier)
				{
					auto package_name = new JS::ParseNode::PackageName(token.location);
					import->package_name = package_name;
					package_name->package_names.push_back(std::get<JSString>(token.value));
					ConsumeToken();
					token = AdvanceToken(Context::InputElementUnspecified, SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
					while(token.type == TokenType(U'.'))
					{
						ConsumeToken();
						token = AdvanceToken();
						if(token.type == TokenType(U'*'))
						{
							package_name->package_names.push_back(u"*");
						}
						else if(token.type == TokenType::Identifier)
						{
							package_name->package_names.push_back(std::get<JSString>(token.value));
						}
						else
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
						}
						ConsumeToken();
						token = AdvanceToken(Context::InputElementUnspecified, SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
					}
				}
				else if(token.type == TokenType::StringLiteral)
				{
					import->package_name = std::get<JSString>(token.value);
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				}
				else
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier or string literal"));
				}
				ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line), false);
				return import;
			}
			else
#endif
			if(CurrentVersion < Version::ES6)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'import': import statement only supported in ECMAScript 6 or later"));
			}
			else if(level < ParseLevel::Module)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'import': import statement only supported on module level"));
			}
			else
			{
				auto import = new JS::ParseNode::ImportStatement(token.location);
				ConsumeToken();
				token = AdvanceToken();
				if(token.type == TokenType::StringLiteral)
				{
					// ImportDeclaration : 'import' ModuleSpecifier WithClause:opt ';'
					import->from_module = std::get<JSString>(token.value);
				}
				else
				{
					// ImportDeclaration : 'import' ImportClause FromClause WithClause:opt ';'
					if(IsIdentifier(token.type))
					{
						import->default_binding = ParseBindingIdentifier(/* Yield = */ false, /* Await = */ true);
						token = AdvanceToken();
						if(token.type == TokenType(U','))
						{
							ConsumeToken();
							token = AdvanceToken();
						}
						else if(token.keyword != KeywordType::From)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ',' or 'from'"));
						}
					}
					else if(token.type != TokenType(U'*') && token.type != TokenType(U'{'))
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier, '*', '{' or 'from'"));
					}

					if(token.type == TokenType(U'*'))
					{
						ConsumeToken();
						token = AdvanceToken();
						if(token.keyword != KeywordType::As)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'as'"));
						}
						ConsumeToken();
						AdvanceToken();
						import->bindings = ParseBindingIdentifier(/* Yield = */ false, /* Await = */ true);
						token = AdvanceToken();
					}
					else if(token.type == TokenType(U'{'))
					{
						auto named_import_bindings = new JS::ParseNode::NamedImportBindings(token.location);
						import->bindings = named_import_bindings;
						ConsumeToken();
						token = AdvanceToken();
						while(token.type != TokenType(U'}'))
						{
							if(!IsIdentifier(token.type))
							{
								if(CurrentVersion < Version::ESNEXT)
								{
									JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
								}
								else if(token.type != TokenType::StringLiteral)
								{
									JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier or string literal"));
								}
							}
							StashToken();
							token = AdvanceToken();
							StepBack();
							auto import_binding = new JS::ParseNode::NamedImportBinding(token.location/*TODO*/);
							if(token.keyword == KeywordType::As)
							{
								token = PeekToken();
								switch(token.type)
								{
								case TokenType::Identifier:
								case TokenType::ReservedWord:
									import_binding->export_name = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
									break;
								case TokenType::StringLiteral:
									if(CurrentVersion < Version::ESNEXT)
									{
										JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
									}
									import_binding->export_name = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
									break;
								default:
									if(CurrentVersion < Version::ESNEXT)
									{
										JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
									}
									else
									{
										JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier or string literal"));
									}
								}
								ConsumeToken();
								AdvanceToken();
								ConsumeToken();
								token = AdvanceToken();
							}
							else
							{
								token = PeekToken();
							}

							if(!IsIdentifier(token.type))
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
							}
							import_binding->name = ParseBindingIdentifier(/* Yield = */ false, /* Await = */ true);

							named_import_bindings->bindings.push_back(import_binding);

							token = AdvanceToken();
							if(token.type == TokenType(U','))
							{
								ConsumeToken();
								token = AdvanceToken();
							}
							else if(token.type != TokenType(U'}'))
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ',' or '}'"));
							}
						}
						ConsumeToken();
						token = AdvanceToken();
					}

					if(token.keyword != KeywordType::From)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'from'"));
					}
					ConsumeToken();
					token = AdvanceToken();

					if(token.type != TokenType::StringLiteral)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected string literal"));
					}

					import->from_module = std::get<JSString>(token.value);
				}

				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				import->with_clause = ParseWithClause(terminate_on_new_line);
				ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(false));
				return import;
			}
			break;
		case KeywordType::Return: // ES1
			if(Version::ES6 <= CurrentVersion && !Return)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Invalid return statement in non-function context"));
			}
			else
			{
				auto return_location = token.location;
				ConsumeToken();
				token = AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				std::optional<ParseNode *> result;
				if(!HadLineTerminator() && token.type != TokenType(U';') && token.type != TokenType(U'}'))
				{
					result = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await, Context::InputElementDiv, terminate_on_new_line);
				}
				ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(context == StatementContext::DoWhileBody));
				return new JS::ParseNode::ReturnStatement(return_location, result);
			}
			break;
		case KeywordType::Switch: // ES3
			if(CurrentVersion < Version::ES3)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'switch': switch statements are only supported in ECMAScript 3 or later"));
			}
			else
			{
				auto switch_location = token.location;
				ConsumeToken();
				if(AdvanceToken().type != TokenType(U'('))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				auto condition = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await);
				if(PeekToken().type != TokenType(U')'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
				}
				ConsumeToken();
				AdvanceToken();
				auto body = ParseBlock(Yield, Await, Return, /* parse_type = */ BlockParseType::PermitCaseDefault);
				AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				return new JS::ParseNode::SwitchStatement(switch_location, condition, body);
			}
			break;
		case KeywordType::Throw: // ES3
			if(CurrentVersion < Version::ES3)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'throw': throw statements only supported in ECMAScript 3 or later"));
			}
			else
			{
				auto throw_location = token.location;
				ConsumeToken();
				token = AdvanceToken(Context::InputElementRegExp);
				if(HadLineTerminator())
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected line terminator"));
				}
				auto expression = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await, Context::InputElementDiv, terminate_on_new_line);
				ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(context == StatementContext::DoWhileBody));
				return new JS::ParseNode::ThrowStatement(throw_location, expression);
			}
			break;
		case KeywordType::Try: // ES3
			if(CurrentVersion < Version::ES3)
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'try': try-catch statements are only supported in ECMAScript 3 or later"));
			}
			else
			{
				auto try_location = token.location;
				ConsumeToken();
				AdvanceToken();
				auto body = ParseBlock(Yield, Await, Return);
				AdvanceToken(Context::InputElementRegExp);
				std::vector<JS::ParseNode::CatchStatement *> catch_part;
#if ENABLE_ES4
				while(PeekToken().keyword == KeywordType::Catch)
#else
				if(PeekToken().keyword == KeywordType::Catch)
#endif
				{
					auto catch_location = PeekToken().location;
#if ENABLE_ES4
					if(!IsVersionES4() && catch_part.size() > 0)
					{
						break;
					}
#endif
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp);
					std::optional<ParseNode *> catch_parameter;
					if(PeekToken().type == TokenType(U'('))
					{
						ConsumeToken();
						token = AdvanceToken(Context::InputElementRegExp);
#if ENABLE_ES4
						bool is_const = false;
						if(IsVersionES4() && token.keyword == KeywordType::Const)
						{
							is_const = true;
							ConsumeToken();
							token = AdvanceToken();
						}
						JS::ParseNode::Identifier * identifier; // ES4 only
#endif
						if(Version::ES6 <= CurrentVersion)
						{
							catch_parameter = ParseBindingElement(Yield, Await);
						}
						else
						{
							catch_parameter =
#if ENABLE_ES4
								identifier =
#endif
									ParseBindingIdentifier(Yield, Await);
						}
						token = AdvanceToken();
#if ENABLE_ES4
						if(IsVersionES4())
						{
							identifier->is_const = is_const;
							if(IsVersionES4() && token.type == TokenType(U':'))
							{
								ConsumeToken();
								token = AdvanceToken(Context::InputElementRegExp);
								identifier->type = ParseTypeExpression(/* In = */ true, Yield, Await);
							}
						}
#endif
						if(PeekToken().type != TokenType(U')'))
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
						}
						ConsumeToken();
						AdvanceToken(Context::InputElementRegExp);
					}
					else if(CurrentVersion < Version::ESNEXT)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
					}

					ParseNode * catch_body = ParseBlock(Yield, Await, Return);
					AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));

					catch_part.push_back(new JS::ParseNode::CatchStatement(catch_location, catch_parameter, catch_body));
				}
				std::optional<ParseNode *> finally_part;
				if(PeekToken().keyword == KeywordType::Finally)
				{
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp);
					finally_part = ParseBlock(Yield, Await, Return);
					AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				}
				if(catch_part.size() == 0 && !finally_part.has_value())
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected 'catch' or 'finally'"));
				}
				return new JS::ParseNode::TryStatement(try_location, body, catch_part, finally_part);
			}
			break;
		case KeywordType::While: // ES1
			{
				auto while_location = token.location;
				ConsumeToken();
				if(AdvanceToken().type != TokenType(U'('))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				auto condition = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await);
				if(PeekToken().type != TokenType(U')'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				auto body = ParseStatement(ParseLevel::Clause, Yield, Await, Return, terminate_on_new_line);
				return new JS::ParseNode::WhileStatement(while_location, condition, body);
			}
		case KeywordType::With: // ES1
			{
				auto with_location = token.location;
				ConsumeToken();
				if(AdvanceToken().type != TokenType(U'('))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '('"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				auto expression = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await);
				if(PeekToken().type != TokenType(U')'))
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
				}
				ConsumeToken();
				AdvanceToken(Context::InputElementRegExp);
				auto body = ParseStatement(ParseLevel::Clause, Yield, Await, Return, terminate_on_new_line);
				return new JS::ParseNode::WithStatement(with_location, expression, body);
			}
			break;
		case KeywordType::Var: // ES1
			// TODO: in ES4, on a clause level, none of the declarations may be typed
			node = ParseVarOrLexicalDeclaration(/* In = */ true, Yield, Await);
			ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(context == StatementContext::DoWhileBody));
			break;
		// handled together with expressions
#if ENABLE_ES4
		case KeywordType::Namespace: // ES4
			if(IsVersionES4())
			{
				if(level < ParseLevel::Block)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'namespace': namespace definitions only supported on block level"));
				}
				ConsumeToken();
				AdvanceToken();
				auto namespace_statement = new JS::ParseNode::NamespaceStatement(token.location, ParseIdentifier(Yield, Await));
				node = namespace_statement;
				token = AdvanceToken(Context::InputElementUnspecified, SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
				if(token.type == TokenType(U'='))
				{
					ConsumeToken();
					token = AdvanceToken(Context::InputElementUnspecified, Context::InputElementRegExp);
					switch(token.type)
					{
					case TokenType::Identifier:
						ConsumeToken();
						namespace_statement->uri = new JS::ParseNode::Identifier(token.location, std::get<JSString>(token.value));
						AdvanceToken(Context::InputElementUnspecified, SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
						break;
					case TokenType::StringLiteral:
						ConsumeToken();
						namespace_statement->uri = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
						AdvanceToken(Context::InputElementUnspecified, SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
						break;
					default:
						JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier or string literal"));
					}
				}
				ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
				return node;
			}
			// fallthru
		case KeywordType::Use: // ES4
			if(SelectedVersion == Version::ES4_2003 || SelectedVersion == Version::ES4_2008)
			{
				auto use_location = token.location;
				if(level < ParseLevel::Block)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'use': use and pragma directives only supported on block level"));
				}
				ConsumeToken();
				token = AdvanceToken();
				if(PeekToken().keyword == KeywordType::Namespace)
				{
					if(!IsVersionES4())
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'namespace': 'use namespace only supported in ECMAScript 4 mode"));
					}
					auto use_namespace = new JS::ParseNode::UseNamespaceStatement(use_location);
					ConsumeToken();
					token = AdvanceToken(Context::InputElementRegExp);
					if(token.type == TokenType(U'('))
					{
						ConsumeToken();
						token = AdvanceToken(Context::InputElementRegExp);
						while(token.type != TokenType(U')'))
						{
							use_namespace->namespaces.push_back(ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
							token = PeekToken();
							if(token.type == TokenType(U','))
							{
								ConsumeToken();
								token = AdvanceToken(Context::InputElementRegExp);
								if(token.type == TokenType(U')'))
								{
									JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected ')'"));
								}
							}
							else if(token.type != TokenType(U')'))
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ',' or ')'"));
							}
						}
						ConsumeToken();
						AdvanceToken(Context::InputElementUnspecified, Context::InputElementRegExp);
					}
					else
					{
						use_namespace->namespaces.push_back(ParseExpression(Precedence::AssignmentExpression, /* In = */ true, Yield, Await));
					}
					ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
					return use_namespace;
				}
				else
				{
					auto pragma = new JS::ParseNode::PragmaDeclaration(use_location);
					while(token.type == TokenType::Identifier)
					{
						ConsumeToken();
						auto pragma_item = new JS::ParseNode::PragmaItem(token.location, std::get<JSString>(token.value));
						token = AdvanceToken(Context::InputElementUnspecified, SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
						if(token.type == TokenType(U'('))
						{
							ConsumeToken();
							token = AdvanceToken(Context::InputElementUnspecified);
							switch(token.type)
							{
							case TokenType::StringLiteral:
								ConsumeToken();
								pragma_item->argument = std::get<JSString>(token.value);
								break;
							case TokenType::NumericLiteral:
								ConsumeToken();
								pragma_item->argument = std::get<JSNumber>(token.value);
								break;
							case TokenType('-'):
								ConsumeToken();
								token = AdvanceToken(Context::InputElementUnspecified);
								if(token.type != TokenType::NumericLiteral)
								{
									JS::Throw(JS::New<SyntaxError> __LOC(u"Expected numeric literal"));
								}
								ConsumeToken();
								pragma_item->argument = -std::get<JSNumber>(token.value);
								break;
							default:
								switch(token.keyword)
								{
								case KeywordType::True:
									ConsumeToken();
									pragma_item->argument = true;
									break;
								case KeywordType::False:
									ConsumeToken();
									pragma_item->argument = false;
									break;
								default:
									JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '-', 'true', 'false', string literal or numeric literal"));
								}
							}
							token = AdvanceToken(Context::InputElementUnspecified);
							if(token.type != TokenType(U')'))
							{
								JS::Throw(JS::New<SyntaxError> __LOC(u"Expected ')'"));
							}
							ConsumeToken();
							token = AdvanceToken(Context::InputElementUnspecified, Context::InputElementRegExp);
						}
						if(token.type == TokenType(U'?'))
						{
							ConsumeToken();
							pragma_item->optional = true;
							token = AdvanceToken(Context::InputElementUnspecified, Context::InputElementRegExp);
						}
						pragma->items.push_back(pragma_item);
						if(token.type == TokenType(U','))
						{
							ConsumeToken();
							token = AdvanceToken(Context::InputElementUnspecified, Context::InputElementRegExp);
						}
						else
						{
							break;
						}
					}
					// delay parsing semicolon to enforce strict mode rules
					//ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line));
					return pragma;
				}
				break;
			}
			// fallthru
		case KeywordType::Package: // ES4
			if(IsVersionES4())
			{
				auto package_location = token.location;
				if(level < ParseLevel::Module)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"Unexpected 'package': use and pragma directives only supported on module level"));
				}

				ConsumeToken();
				token = AdvanceToken();
				auto package = new JS::ParseNode::PackageStatement(package_location);
				if(token.type == TokenType::Identifier)
				{
					auto package_name = new JS::ParseNode::PackageName(token.location);
					package->package_name = package_name;
					package_name->package_names.push_back(std::get<JSString>(token.value));
					ConsumeToken();
					token = AdvanceToken(Context::InputElementUnspecified, Context::InputElementRegExp);
					while(token.type == TokenType(U'.'))
					{
						ConsumeToken();
						token = AdvanceToken();
						if(token.type != TokenType::Identifier)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Expected identifier"));
						}
						package_name->package_names.push_back(std::get<JSString>(token.value));
						ConsumeToken();
						token = AdvanceToken();
					}
				}
				else if(token.type == TokenType::StringLiteral)
				{
					package->package_name = new JS::ParseNode::StringLiteral(token.location, std::get<JSString>(token.value));
					ConsumeToken();
					AdvanceToken();
				}

				package->body = ParseBlock(/* Yield = */ false, /* Await = */ false, /* Return = */ false);
				AdvanceToken(Context::InputElementRegExp, SameOrTermination(terminate_on_new_line));
				return package;
			}
			[[fallthrough]];
#endif
#if ENABLE_ES4
		case KeywordType::Abstract: // JScript .NET and ActionScript
		case KeywordType::Native: // ActionScript
			if(IsVersionES4())
			{
				ConsumeToken();
				node = token.keyword == KeywordType::Abstract ? static_cast<ParseNode *>(new JS::ParseNode::AbstractAttribute(token.location)) : new JS::ParseNode::NativeAttribute(token.location);
				AdvanceToken(Context::InputElementRegExp);
				return new JS::ParseNode::AttachedAttribute(node->location, node, ParseStatement(ParseLevel::Block, Yield, Await, Return, StatementContext::General, terminate_on_new_line, true));
			}
			[[fallthrough]];
#endif
		case KeywordType::Async: // ESNext (can start expression for "async function")
		case KeywordType::Class: // ES6 and ES4 (can start expression in ES6 and later)
#if ENABLE_ES4
		case KeywordType::Interface: // ES4
#endif
		case KeywordType::Function: // ES1 (can start expression in ES3 and later)
		case KeywordType::Let: // ES6 (can start expression for "let [")
		default: // ES1
			// also ending up here: ES4 attributes
			if(Version::ES3 <= CurrentVersion && token.type == TokenType::Identifier
			&& (!Await || token.keyword != KeywordType::Await)
			&& (!Yield || token.keyword != KeywordType::Yield))
			{
				// try to parse it as a label
				StashToken();
				if(AdvanceToken(Context::InputElementDiv, SameOrTermination(terminate_on_new_line)).type == TokenType(U':'))
				{
					PurgeStashedTokens();
					ConsumeToken();
					AdvanceToken(Context::InputElementRegExp);
					node = new JS::ParseNode::LabelledStatement(token.location, std::get<JSString>(token.value), ParseStatement(ParseLevel::Clause, Yield, Await, Return, terminate_on_new_line));
					break;
				}
				StepBack();
			}
			node = ParseExpression(
#if ENABLE_ES4
				IsVersionES4() && permit_bodyless_functions ? Precedence::AbstractStatement : Precedence::Statement,
#else
				Precedence::Statement,
#endif
				/* In = */ true,
				Yield,
				Await,
				Context::InputElementDiv,
				terminate_on_new_line);
			{
				bool is_declaration = false;

				if(auto function = dynamic_cast<JS::ParseNode::FunctionDefinition *>(node))
				{
					is_declaration = function->function_position == JS::ParseNode::FunctionPosition::Statement;
				}
				else if(auto class_ = dynamic_cast<JS::ParseNode::ClassDefinition *>(node))
				{
					is_declaration = !class_->is_expression;
				}

				if(is_declaration)
				{
#if ENABLE_ES4
					if(IsVersionES4())
					{
						if(level < ParseLevel::Block)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Function and class declarations only allowed at block scope"));
						}
					}
					else
#endif
					if(CurrentVersion < Version::ES3) // TODO: ES3/ES5 offer it as extension
					{
						if(level <= ParseLevel::Block)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"Function declarations only allowed at program scope"));
						}
					}
				}
				else
				{
#if ENABLE_ES4
					if(IsVersionES4() && !HadLineTerminator())
					{
						switch(PeekToken().keyword)
						{
						default:
							if(PeekToken().type != TokenType::Identifier // attribute
							&& PeekToken().type != TokenType('{'))
								break;
							// fallthru
						case KeywordType::Class:
						case KeywordType::Const:
						case KeywordType::False: // attribute
						case KeywordType::Function:
						case KeywordType::Import:
						case KeywordType::Namespace:
						case KeywordType::Public: // attribute
						case KeywordType::Private: // attribute
						case KeywordType::True: // attribute
						case KeywordType::Use:
						case KeywordType::Var:
							// TODO: on a clause level, a block with attributes attached will only contain other clause level instructions
							return new JS::ParseNode::AttachedAttribute(node->location, node, ParseStatement(ParseLevel::Block, Yield, Await, Return, StatementContext::General, terminate_on_new_line
#if ENABLE_ES4
								, permit_bodyless_functions
#endif
							));
						}
					}
#endif
					ParseSemicolon(SameOrTermination(Context::InputElementRegExp, terminate_on_new_line) WITH_FLAG(context == StatementContext::DoWhileBody));
				}
			}
			break;
		}
	}
	return node;
}

JS::ParseNode::ParseNode * JS::Parser::Parser::ParseSubstitutionTemplate(bool Yield, bool Await, [[maybe_unused]] bool Tagged)
{
	ParseNode * node;
	ParseNode * expression;
	Token token = PeekToken();
	switch(token.type)
	{
	case TokenType::NoSubstitutionTemplate:
		node = new JS::ParseNode::Template(token.location, std::get<JSString>(token.value));
		ConsumeToken();
		break;
	case TokenType::TemplateHead:
		node = new JS::ParseNode::Template(token.location, std::get<JSString>(token.value));
		while(token.type != TokenType::TemplateTail)
		{
			ConsumeToken();
			AdvanceToken(Context::InputElementRegExp);
			expression = ParseExpression(Precedence::CommaExpression, /* In = */ true, Yield, Await, Context::InputElementTemplateTail);
			token = PeekToken();
			switch(token.type)
			{
			case TokenType::TemplateMiddle:
			case TokenType::TemplateTail:
				node = new JS::ParseNode::TemplateSubstitution(node->location, node, expression, new JS::ParseNode::Template(token.location, std::get<JSString>(token.value)));
				break;
			default:
				JS::Throw(JS::New<SyntaxError> __LOC(u"Expected template middle or template tail"));
			}
		}
		ConsumeToken();
		break;
	default:
		assert(false);
		break;
	}
	return node;
}

JS::ParseNode::ClassElementList * JS::Parser::Parser::ParseClassElementList(bool Yield, bool Await)
{
	auto token = AdvanceToken(Context::InputElementRegExp);
	auto block = new JS::ParseNode::ClassElementList(token.location);
	while(PeekToken().type != TokenType(U'}'))
	{
		if(PeekToken().type == TokenType::EndOfFile)
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
		}
		block->elements.push_back(ParseMember(MemberContext::ClassElement, Yield, Await));
	}
	return block;
}

JS::ParseNode::StatementList * JS::Parser::Parser::ParseStatementList(StatementContext context, ParseLevel level, bool Yield, bool Await, bool Return, BlockParseType parse_type)
{
	bool current_strict_mode = JS::StrictMode;
	Version current_version = JS::CurrentVersion;
	bool directives = parse_type == BlockParseType::PermitDirectives;

	auto block = new JS::ParseNode::StatementList(PeekToken().location);
	while(PeekToken().type != TokenType(U'}') && PeekToken().type != TokenType::EndOfFile)
	{
		auto statement = ParseStatement(level, Yield, Await, Return, context);

		if(Version::ES5 <= SelectedVersion && directives)
		{
			if(!ProcessDirective(statement))
			{
				directives = false;
			}
		}
#if ENABLE_ES4
		else if(SelectedVersion == Version::ES4_2003 || SelectedVersion == Version::ES4_2008)
		{
			if(ProcessDirective(statement))
			{
				// semicolon parsing delayed to once the directives take effect
				ParseSemicolon();
			}
		}
#endif

		if(dynamic_cast<JS::ParseNode::DefaultStatement *>(statement))
		{
			context = StatementContext::PastDefault;
		}
		else if(context == StatementContext::SwitchBodyStart)
		{
			context = StatementContext::SwitchBody;
		}
		block->statements.push_back(statement);
	}

	JS::StrictMode = current_strict_mode;
	JS::CurrentVersion = current_version;
	return block;
}

JS::ParseNode::Block * JS::Parser::Parser::ParseBlock(bool Yield, bool Await, bool Return, BlockParseType parse_type)
{
	if(PeekToken().type != TokenType(U'{'))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '{'"));
	}
	auto bracket_location = ConsumeToken().location;
	AdvanceToken(Context::InputElementRegExp);
	auto statements = ParseStatementList(
		parse_type == BlockParseType::PermitCaseDefault
			? StatementContext::SwitchBodyStart
			: StatementContext::General,
		ParseLevel::Block,
		Yield,
		Await,
		Return,
		parse_type == BlockParseType::PermitDirectives
			? BlockParseType::PermitDirectives
#if ENABLE_ES4
			: parse_type == BlockParseType::PermitBodylessFunctions
				? BlockParseType::PermitBodylessFunctions
#endif
				: BlockParseType::PermitNothing);
	if(PeekToken().type != TokenType(U'}'))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
	}
	ConsumeToken();
	auto block = new JS::ParseNode::Block(bracket_location);
	block->statements = statements->statements;
	return block;
}

JS::ParseNode::ClassElementList * JS::Parser::Parser::ParseClassBody(bool Yield, bool Await)
{
	if(PeekToken().type != TokenType(U'{'))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '{'"));
	}
	ConsumeToken();
	auto block = ParseClassElementList(Yield, Await);
	if(PeekToken().type != TokenType(U'}'))
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Expected '}'"));
	}
	ConsumeToken();
	return block;
}

JS::Runtime::ScriptRecord * JS::Parser::Parser::ParseScript(JS::Runtime::Realm * realm)
{
	JS::ParseNode::ScriptOrModule * Code;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		Code = ParseScriptOrModuleText(ExpectedScriptType::Module);
	}
	else
#endif
	{
		Code = ParseScriptOrModuleText(ExpectedScriptType::Script);
	}
	return Code->CreateScriptRecord(realm, tokenizer->last_location.filename);
}

JS::Runtime::SourceTextModuleRecord * JS::Parser::Parser::ParseModule(JS::Runtime::Realm * realm)
{
	if(CurrentVersion < Version::ES6)
	{
		JS::Throw(JS::New<InternalError> __LOC("modules only supported in ECMAScript 6 or later"));
	}

	JS::ParseNode::ScriptOrModule * Code = ParseScriptOrModuleText(ExpectedScriptType::Module);

	return Code->CreateModuleRecord(realm, tokenizer->last_location.filename);
}

JS::Runtime::ScriptOrModuleRecord * JS::Parser::Parser::ParseScriptOrModule(JS::Runtime::Realm * realm, ExpectedScriptType expected)
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		expected = ExpectedScriptType::Module;
	}
	else
#endif
	if(CurrentVersion < Version::ES6)
	{
		expected = ExpectedScriptType::Script;
	}

	JS::ParseNode::ScriptOrModule * Code = ParseScriptOrModuleText(expected);
	bool is_module;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		is_module = false;
	}
	else
#endif
	if(expected != ExpectedScriptType::Either)
	{
		is_module = expected == ExpectedScriptType::Module;
	}
	else
	{
		is_module = false;
		for(auto statement : Code->body->statements)
		{
			// TODO: also needs to look for import.meta
			if(dynamic_cast<JS::ParseNode::ImportStatement *>(statement) || dynamic_cast<JS::ParseNode::ExportStatement *>(statement))
			{
				is_module = true;
			}
		}
	}

	if(!is_module)
	{
		return Code->CreateScriptRecord(realm, tokenizer->last_location.filename);
	}
	else
	{
		return Code->CreateModuleRecord(realm, tokenizer->last_location.filename);
	}
}

//// XML::Parser

char16_t JS::Parser::XML::Parser::GetEntityReference(JSString name)
{
	if(name.size() != 0)
	{
		switch(name[0])
		{
		case u'a':
			if(name.size() != 1)
			{
				switch(name[1])
				{
				case u'm':
					if(name == u"amp")
						return u'&';
					break;
				case u'p':
					if(name == u"apos")
						return u'\'';
					break;
				}
			}
			break;
		case u'g':
			if(name == u"gt")
				return u'>';
			break;
		case u'l':
			if(name == u"lt")
				return u'<';
			break;
		case u'q':
			if(name == u"quot")
				return u'"';
			break;
		}
	}
	JS::Throw(JS::New<SyntaxError> __LOC("unrecognized XML entity reference"));
}

void JS::Parser::XML::Parser::ParseSpacesOpt()
{
	while(GetCharType(PeekNextChar()) == CharType::Space)
	{
		GetNextChar();
	}
}

JSString JS::Parser::XML::Parser::ParseName()
{
	JSString name;
	char16_t c = GetNextChar();
	if(GetCharType(c) != CharType::NameStartChar)
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"invalid name start"));
	}
	name += c;
	while(GetCharType(c = PeekNextChar()) == CharType::NameStartChar
	|| GetCharType(c) == CharType::NameChar)
	{
		name += GetNextChar();
	}
	return name;
}

JSString JS::Parser::XML::Parser::ParseReference()
{
	JSString value;
	switch(PeekNextChar())
	{
	case u'#':
		GetNextChar();
		{
			uint32_t codepoint = 0;
			if(PeekNextChar() == u'x')
			{
				GetNextChar();
				while((u'0' <= PeekNextChar() && PeekNextChar() <= u'9')
					|| (u'A' <= PeekNextChar() && PeekNextChar() <= u'F')
					|| (u'a' <= PeekNextChar() && PeekNextChar() <= u'f'))
				{
					char16_t c = GetNextChar();
					if(u'0' <= c && c <= u'9')
					{
						codepoint = 16 * codepoint + (c - u'0');
					}
					else if(u'A' <= c && c <= u'F')
					{
						codepoint = 16 * codepoint + (c - u'A') + 10;
					}
					else if(u'a' <= c && c <= u'f')
					{
						codepoint = 16 * codepoint + (c - u'a') + 10;
					}
				}
			}
			else
			{
				while(u'0' <= PeekNextChar() && PeekNextChar() <= u'9')
				{
					char16_t c = GetNextChar();
					codepoint = 10 * codepoint + (c - u'0');
				}
			}
			if(GetNextChar() != u';')
			{
				JS::Throw(JS::New<SyntaxError> __LOC("expected ';'"));
			}
			// TODO: check validity of character
			value += UTF32toUTF16(codepoint);
		}
		break;
	default:
		{
			JSString name = ParseName();
			if(GetNextChar() != u';')
			{
				JS::Throw(JS::New<SyntaxError> __LOC("expected ';'"));
			}
			value += GetEntityReference(name);
		}
		break;
	}
	return value;
}

JSString JS::Parser::XML::Parser::ParseAttributeValue()
{
	char16_t quote;
	switch(quote = GetNextChar())
	{
	case u'"':
	case u'\'':
		break;
	default:
		JS::Throw(JS::New<SyntaxError> __LOC("expected '\"' or '''"));
	}

	JSString value;
	char16_t c;
	while((c = GetNextChar()) != quote)
	{
		switch(c)
		{
		default:
			value += c;
			break;
		case u'<':
			JS::Throw(JS::New<SyntaxError> __LOC("unexpected '<' in attribute value"));
		case u'&':
			value += ParseReference();
			break;
		}
	}
	return value;
}

JSString JS::Parser::XML::Parser::ParseContentText()
{
	JSString value;
	char16_t c;
	while((c = PeekNextChar()) != u'\0' && c != u'<')
	{
		switch(c)
		{
		default:
			GetNextChar();
			value += c;
			break;
		case u'<':
			return value;
		case u'&':
			GetNextChar();
			value += ParseReference();
			break;
		}
	}
	return value;
}

static constexpr JS::Class::XML::ClassType EndTagClass = JS::Class::XML::ClassType(-1); // not a real class, used internally for parsing

void JS::Parser::XML::Parser::ParseNamespace(gcn::gc_ptr<JS::Class::XML> xml)
{
	size_t colon = xml->Name.value().localName.find(u':');
	if(colon != std::u16string::npos)
	{
		xml->Name.value().Prefix = xml->Name.value().localName.substr(0, colon);
		xml->Name.value().localName = xml->Name.value().localName.substr(colon + 1);
	}
	else
	{
		// use the default namespace
		xml->Name.value().Prefix = u"";
	}
}

gcn::gc_ptr<JS::Class::XML> JS::Parser::XML::Parser::ParseXMLPart()
{
	gcn::gc_ptr<JS::Class::XML> xml;
	char16_t c;
	switch(c = PeekNextChar())
	{
	default:
		xml = JS::New<JS::Class::XML>();
		xml->Class = JS::Class::XML::ClassType::Text;
		xml->Value = ParseContentText();
		return xml;
	case u'<':
		GetNextChar();
		switch(PeekNextChar())
		{
		case u'/':
			GetNextChar();
			xml = JS::New<JS::Class::XML>();
			xml->Class = EndTagClass;
			xml->Name = ParseName();
			ParseSpacesOpt();
			if(GetNextChar() != u'>')
			{
				JS::Throw(JS::New<SyntaxError> __LOC("unmatched element closing tag"));
			}
			return xml;
		case u'!':
			GetNextChar();
			switch(GetNextChar())
			{
			case u'-':
				switch(GetNextChar())
				{
				case u'-':
					xml = JS::New<JS::Class::XML>();
					xml->Class = JS::Class::XML::ClassType::Comment;
					xml->Value = u"";
					while(true)
					{
						switch(c = GetNextChar())
						{
						case u'-':
							switch(c = GetNextChar())
							{
							case u'-':
								switch(c = GetNextChar())
								{
								case u'>':
									return xml;
								default:
									JS::Throw(JS::New<SyntaxError> __LOC("invalid '--' in comment"));
								}
							default:
								xml->Value.value() += u'-';
								xml->Value.value() += c;
								break;
							}
							break;
						default:
							xml->Value.value() += c;
							break;
						}
					}
				}
				break;
			case u'[':
				switch(GetNextChar())
				{
				case u'C':
					switch(GetNextChar())
					{
					case u'D':
						switch(GetNextChar())
						{
						case u'A':
							switch(GetNextChar())
							{
							case u'T':
								switch(GetNextChar())
								{
								case u'A':
									switch(GetNextChar())
									{
									case u'[':
										xml = JS::New<JS::Class::XML>();
										xml->Class = JS::Class::XML::ClassType::Text;
										xml->Value.value() = u"";
										while(true)
										{
											switch(c = GetNextChar())
											{
											case u']':
												switch(c = GetNextChar())
												{
												case u']':
													while(true)
													{
														switch(c = GetNextChar())
														{
														case u']':
															xml->Value.value() += u']';
															continue;
														case u'>':
															return xml;
														}
														xml->Value.value() += u"]]";
														xml->Value.value() += c;
														break;
													}
													break;
												default:
													xml->Value.value() += u']';
													xml->Value.value() += c;
													break;
												}
												break;
											default:
												xml->Value.value() += c;
												break;
											}
										}
										break;
									default:
										JS::Throw(JS::New<SyntaxError> __LOC());
									}
									break;
								default:
									JS::Throw(JS::New<SyntaxError> __LOC());
								}
								break;
							default:
								JS::Throw(JS::New<SyntaxError> __LOC());
							}
							break;
						default:
							JS::Throw(JS::New<SyntaxError> __LOC());
						}
						break;
					default:
						JS::Throw(JS::New<SyntaxError> __LOC());
					}
					break;
				default:
					JS::Throw(JS::New<SyntaxError> __LOC());
				}
				break;
			}
			break;
		case u'?':
			GetNextChar();
			xml = JS::New<JS::Class::XML>();
			xml->Class = JS::Class::XML::ClassType::ProcessingInstruction;
			xml->Name = ParseName();
			// disallow xml in all capitalization
			if(xml->Name.value().localName.size() == 3
			&& (xml->Name.value().localName[0] == u'X' || xml->Name.value().localName[0] == u'x')
			&& (xml->Name.value().localName[1] == u'M' || xml->Name.value().localName[1] == u'm')
			&& (xml->Name.value().localName[2] == u'L' || xml->Name.value().localName[2] == u'l'))
			{
				JS::Throw(JS::New<SyntaxError> __LOC());
			}
			xml->Value = u"";
			switch(c = PeekNextChar())
			{
			case u' ':
				GetNextChar();
				break;
			case u'?':
				GetNextChar();
				switch(c = GetNextChar())
				{
				case u'>':
					return xml;
				default:
					JS::Throw(JS::New<SyntaxError> __LOC());
				}
			default:
				JS::Throw(JS::New<SyntaxError> __LOC());
			}

			while(true)
			{
				switch(c = GetNextChar())
				{
				case u'?':
					while((c = GetNextChar()) == u'?')
					{
						xml->Value.value() += u'?';
					}
					if(c == u'>')
					{
						return xml;
					}
					else
					{
						xml->Value.value() += u'?';
						xml->Value.value() += c;
					}
					break;
				default:
					xml->Value.value() += c;
					break;
				}
			}
			break;
		default:
			xml = JS::New<JS::Class::XML>();
			xml->Class = JS::Class::XML::ClassType::Element;
			xml->Name = ParseName();
			ParseSpacesOpt();
			while(true)
			{
				switch(PeekNextChar())
				{
				case u'/':
					GetNextChar();
					if(GetNextChar() != u'>')
					{
						JS::Throw(JS::New<SyntaxError> __LOC());
					}
					ParseNamespace(xml);
					return xml;
				case u'>':
					GetNextChar();
					while(true)
					{
						gcn::gc_ptr<JS::Class::XML> child = ParseXMLPart();
						if(child->Class == EndTagClass)
						{
							if(xml->Name != child->Name)
							{
								JS::Throw(JS::New<SyntaxError> __LOC());
							}
							delete child.get();
							ParseNamespace(xml);
							return xml;
						}

						if(child->Class == JS::Class::XML::ClassType::Text && xml->children.size() > 0 && xml->children.back()->Class == JS::Class::XML::ClassType::Text)
						{
							xml->children.back()->Value.value() += child->Value.value();
							delete child.get();
						}
						else
						{
							child->Parent = xml;
							xml->children.push_back(child);
						}
					}
				default:
					{
						gcn::gc_ptr<JS::Class::XML> attribute = JS::New<JS::Class::XML>();
						attribute->Class = JS::Class::XML::ClassType::Attribute;
						attribute->Name = ParseName();
						ParseSpacesOpt();
						if(GetNextChar() != u'=')
						{
							JS::Throw(JS::New<SyntaxError> __LOC());
						}
						ParseSpacesOpt();
						attribute->Value = ParseAttributeValue();
						// first, we associate their textual representation with their identity
						xml->Attributes[attribute->Name.value()] = attribute;
						ParseNamespace(attribute);
						attribute->Parent = xml;
						ParseSpacesOpt();
					}
					break;
				}
			}
		}
	}
	assert(false);
}

void JS::Parser::XML::Parser::ProcessNamespaces(gcn::gc_ptr<JS::Class::XML> xml, std::map<JSString, JSNamespace> InScopeNamespaces)
{
	if(xml->Class == JS::Class::XML::ClassType::Element)
	{
		std::set<JS::Type::QName> attributeNames;
		for(auto attribute_entry : xml->Attributes)
		{
			attributeNames.insert(attribute_entry.first);
		}

		for(auto attribute_name : attributeNames)
		{
			auto attribute = xml->Attributes[attribute_name];

			if(attribute->Name.value().Prefix.value() == u"xmlns")
			{
				InScopeNamespaces[attribute->Name.value().localName] = JS::New<JS::Class::Namespace>(attribute->Name.value().localName, attribute->Value.value());
				xml->Attributes.erase(attribute_name);
			}
			else if(attribute->Name.value().Prefix.value() == u"" && attribute->Name.value().localName == u"xmlns")
			{
				InScopeNamespaces[u""] = JS::New<JS::Class::Namespace>(attribute->Name.value().Prefix.value(), attribute->Value.value());
				xml->Attributes.erase(attribute_name);
			}
		}

		xml->InScopeNamespaces = InScopeNamespaces;
	}

	if(xml->Class == JS::Class::XML::ClassType::Element || xml->Class == JS::Class::XML::ClassType::Attribute)
	{
		auto namespace_entry = InScopeNamespaces.find(xml->Name.value().Prefix.value());
		if(namespace_entry != InScopeNamespaces.end())
		{
			xml->Name.value().uri = namespace_entry->second->uri;
		}
	}

	if(xml->Class == JS::Class::XML::ClassType::Element)
	{
		std::vector<gcn::gc_ptr<JS::Class::XML>> attributes;
		for(auto attribute : xml->Attributes)
		{
			ProcessNamespaces(attribute.second, InScopeNamespaces);
			attributes.push_back(attribute.second);
		}
		// we replace the key we associate attributes with with their full QName
		xml->Attributes.clear();
		for(auto attribute : attributes)
		{
			xml->Attributes[attribute->Name.value()] = attribute;
		}

		for(auto child : xml->children)
		{
			ProcessNamespaces(child, InScopeNamespaces);
		}
	}
}

//// JSON

void JS::Parser::JSON::Parser::SkipWhitespaces()
{
	for(char16_t c = PeekNextChar();
		c == u'\u0009' || c == u'\u000A' || c == u'\u000D' || c == u'\u0020';
		GetNextChar(), c = PeekNextChar())
	{
	}
}

uint16_t JS::Parser::JSON::Parser::GetHexadecimalDigit()
{
	char16_t c = GetNextChar();
	if(u'0' <= c && c <= u'9')
	{
		return c - u'0';
	}
	else if(u'A' <= c && c <= u'F')
	{
		return c - u'A' + 10;
	}
	else if(u'a' <= c && c <= u'f')
	{
		return c - u'a' + 10;
	}
	else
	{
		JS::Throw(JS::New<SyntaxError> __LOC("expected hexadecimal digit"));
	}
}

JSString JS::Parser::JSON::Parser::ParseStringLiteral()
{
	JSString string;

	char16_t c;
	uint16_t v;
	if(GetNextChar() != u'"')
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"not a string"));
	}

	for(c = GetNextChar();
		c != u'"';
		c = GetNextChar())
	{
		switch(c)
		{
		case u'"':
			return string;
		case u'\\':
			c = GetNextChar();
			switch(c)
			{
			case u'"':
			case u'\\':
			case u'/':
				string += c;
				break;
			case 'b':
				string += u'\u0008';
				break;
			case 'f':
				string += u'\u000C';
				break;
			case 'n':
				string += u'\u000A';
				break;
			case 'r':
				string += u'\u000D';
				break;
			case 't':
				string += u'\u0009';
				break;
			case 'u':
				v = GetHexadecimalDigit() << 12;
				v |= GetHexadecimalDigit() << 8;
				v |= GetHexadecimalDigit() << 4;
				v |= GetHexadecimalDigit();
				string += char16_t(v);
				break;
			}
			break;
		default:
			if(c <= u'\u001F') // TODO: might be end-of-string
			{
				JS::Throw(JS::New<SyntaxError> __LOC(u"control character inside string literal"));
			}
			string += c;
			break;
		}
	}

	return string;
}

JSNumber JS::Parser::JSON::Parser::ParseNumberLiteral()
{
	std::string text;
	char16_t c;
	if(PeekNextChar() == u'-')
	{
		GetNextChar();
		text += '-';
	}

	switch(c = GetNextChar())
	{
	case u'0':
		text += char(c);
		break;
	case u'1':
	case u'2':
	case u'3':
	case u'4':
	case u'5':
	case u'6':
	case u'7':
	case u'8':
	case u'9':
		text += char(c);
		for(c = PeekNextChar();
			IsDigit(c);
			c = PeekNextChar())
		{
			text += char(GetNextChar());
		}
		break;
	}

	if(PeekNextChar() == u'.')
	{
		GetNextChar();
		text += '.';
		if(!IsDigit(PeekNextChar()))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
		}
		while(IsDigit(PeekNextChar()))
		{
			text += char(GetNextChar());
		}
	}

	c = PeekNextChar();
	if(c == u'E' || c == u'e')
	{
		GetNextChar();
		text += char(c);
		c = PeekNextChar();
		if(c == u'+' || c == u'-')
		{
			GetNextChar();
			text += char(c);
			c = PeekNextChar();
		}
		if(!IsDigit(PeekNextChar()))
		{
			JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
		}
		while(IsDigit(PeekNextChar()))
		{
			text += char(GetNextChar());
		}
	}

	size_t pos;
	JSNumber result = std::stod(text, &pos);
	if(pos != text.size())
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"Unparseable number"));
	}
	return result;
}

JSObject JS::Parser::JSON::Parser::ParseObjectLiteral()
{
	JSObject object = JS::New<JS::Class::OrdinaryObject>();
	GetNextChar();
	SkipWhitespaces();
	if(PeekNextChar() == u'}')
	{
		return object;
	}

	while(true)
	{
		JSString string = ParseStringLiteral();
		SkipWhitespaces();
		if(GetNextChar() != u':')
		{
			JS::Throw(JS::New<SyntaxError> __LOC());
		}
		SkipWhitespaces();
		JSValue value = ParseValue();
		JS::Set(object, string, value);
		SkipWhitespaces();

		switch(GetNextChar())
		{
		case u',':
			continue;
		case u'}':
			return object;
		default:
			JS::Throw(JS::New<SyntaxError> __LOC());
		}
	}
}

gcn::gc_ptr<JS::Class::Array> JS::Parser::JSON::Parser::ParseArrayLiteral()
{
	//gcn::gc_ptr<JS::Class::Array> array = JS::New<JS::Class::Array>();
	std::vector<JSValue> array;
	GetNextChar();
	SkipWhitespaces();
	if(PeekNextChar() == u']')
	{
		return JS::New<JS::Class::Array>();
	}

	while(true)
	{
		JSValue value = ParseValue();
		array.push_back(value);
		SkipWhitespaces();

		switch(GetNextChar())
		{
		case u',':
			continue;
		case u']':
			return JS::New<JS::Class::Array>(array);
		default:
			JS::Throw(JS::New<SyntaxError> __LOC());
		}
	}
}

JSValue JS::Parser::JSON::Parser::ParseValue()
{
	SkipWhitespaces();

	char16_t c = PeekNextChar();
	switch(c)
	{
	case u'{':
		return ParseObjectLiteral();
	case u'[':
		return ParseArrayLiteral();
	case u'-':
	case u'0':
	case u'1':
	case u'2':
	case u'3':
	case u'4':
	case u'5':
	case u'6':
	case u'7':
	case u'8':
	case u'9':
		return ParseNumberLiteral();
	case u'"':
		return ParseStringLiteral();
	case u'f':
		GetNextChar();
		if(GetNextChar() != u'a'
		|| GetNextChar() != u'l'
		|| GetNextChar() != u's'
		|| GetNextChar() != u'e')
		{
			JS::Throw(JS::New<SyntaxError> __LOC());
		}
		return false;
	case u'n':
		GetNextChar();
		if(GetNextChar() != u'u'
		|| GetNextChar() != u'l'
		|| GetNextChar() != u'l')
		{
			JS::Throw(JS::New<SyntaxError> __LOC());
		}
		return JS_null;
	case u't':
		GetNextChar();
		if(GetNextChar() != u'r'
		|| GetNextChar() != u'u'
		|| GetNextChar() != u'e')
		{
			JS::Throw(JS::New<SyntaxError> __LOC());
		}
		return true;
	default:
		JS::Throw(JS::New<SyntaxError> __LOC());
	}
}

JSValue JS::Parser::JSON::Parser::ParseJSON()
{
	JSValue result = ParseValue();
	SkipWhitespaces();
	if(!EndOfString())
	{
		JS::Throw(JS::New<SyntaxError> __LOC());
	}
	return result;
}

