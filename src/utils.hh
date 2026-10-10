#ifndef _UTILS_HH
#define _UTILS_HH

/* Common utilites */

//#include <atomic>
#include <gc_cpp.h>
#include <iostream>
#include <map>
#include <memory>
#include <set>

namespace JS
{
	/* data type where the same entry can be shared by multiple keys, providing a variant of a bidirectional structure */
	template <typename Key, typename T>
		class shared_map
	{
	public:
		struct entry
		{
			std::set<Key> keys;
			T value;
			entry(std::set<Key> keys, T value) : keys(keys), value(value) { }
		};

	private:
		std::map<Key, std::shared_ptr<entry>> values;
		std::map<Key, std::shared_ptr<entry>> first_keys;

	public:
		struct const_iterator
		{
			typename std::map<Key, std::shared_ptr<entry>>::const_iterator iter;

			bool operator !=(const const_iterator& other) const
			{
				return iter != other.iter;
			}

			bool operator ==(const const_iterator& other) const
			{
				return iter == other.iter;
			}

			const_iterator operator++()
			{
				return const_iterator{iter++};
			}

			const entry& operator*() const
			{
				return *iter->second;
			}

			const entry * operator->() const
			{
				return &*iter->second;
			}
		};

		void erase(Key key)
		{
			auto values_iter = values.find(key);
			if(values_iter != values.end())
			{
				auto _entry = values_iter->second;
				bool first = _entry->keys.find(key) == _entry->keys.begin();
				_entry->keys.erase(key);
				if(first)
				{
					first_keys.erase(key);
					if(_entry->keys.size() != 0)
					{
						first_keys[*_entry->keys.begin()] = _entry;
					}
				}

				values.erase(values_iter);
			}
		}

		void clear(Key key)
		{
			auto values_iter = values.find(key);
			if(values_iter != values.end())
			{
				std::set<Key> keys = values_iter->second->keys;
				if(keys.size() != 0)
				{
					first_keys.erase(*keys.begin());
				}
				for(auto other_key : keys)
				{
					values.erase(other_key);
				}
			}
		}

		void assign(Key key, T value)
		{
			erase(key);
			auto _entry = std::make_shared<entry>(std::set<Key>{key}, value);
			values[key] = _entry;
			first_keys[key] = _entry;
		}

		void join(Key key1, Key key2)
		{
			if(key1 == key2)
				return;

			erase(key1);
			auto values_iter = values.find(key2);
			if(values_iter == values.end())
				return;

			values_iter->second->keys.insert(key1);
			values[key1] = values_iter->second;
			if(key1 < key2)
			{
				first_keys.erase(key2);
				first_keys[key1] = values_iter->second;
			}
		}

		void set(Key key, T value)
		{
			auto values_iter = values.find(key);
			if(values_iter == values.end())
			{
				auto _entry = std::make_shared<entry>(std::set<Key>{key}, value);
				values[key] = _entry;
				first_keys[key] = _entry;
			}
			else
			{
				values_iter->second->value = value;
			}
		}

		bool has(Key key) const
		{
			return values.find(key) != values.end();
		}

		std::set<Key> keys(Key key) const
		{
			auto values_iter = values.find(key);
			if(values_iter == values.end())
			{
				return {};
			}
			else
			{
				return values_iter->second->keys;
			}
		}

		T get(Key key) const
		{
			return values.find(key)->second->value;
		}

		size_t size() const
		{
			return values.size();
		}

		const_iterator begin() const
		{
			return const_iterator{first_keys.begin()};
		}

		const_iterator end() const
		{
			return const_iterator{first_keys.end()};
		}
	};

	static inline std::u16string UTF32toUTF16(char32_t c)
	{
		if(c <= 0xFFFF)
		{
			return std::u16string(1, c);
		}
		else if(c <= 0x10FFFF)
		{
			std::u16string s;
			c -= 0x10000;
			s += char16_t(0xD800 + (c >> 10));
			s += char16_t(0xDC00 + (c & 0x3FF));
			return s;
		}
		else
		{
			std::cerr << "out of range character " << std::hex << int(c) << std::endl;
			return u"";
		}
	}

	static inline std::u16string UTF8toUTF16(std::string text)
	{
		std::u16string result;
		char32_t wc;
		int shift = 0;
		for(size_t ptr = 0; text[ptr] != '\0'; ptr++)
		{
			uint32_t c = text[ptr] & 0xFF;
			if(c < 0x80)
			{
				result += char16_t(c);
			}
			else if((c & 0xC0) == 0x80)
			{
				wc |= (c & 0x3F) << shift;
				if(shift != 0)
				{
					shift -= 6;
				}
				else
				{
					result += JS::UTF32toUTF16(wc);
				}
			}
			else if((c & 0xE0) == 0xC0)
			{
				wc = (c & 0x1F) << 6;
				shift = 0;
			}
			else if((c & 0xF0) == 0xE0)
			{
				wc = (c & 0x0F) << 12;
				shift = 6;
			}
			else if((c & 0xF8) == 0xF0)
			{
				wc = (c & 0x07) << 18;
				shift = 12;
			}
		}
		return result;
	}

	enum class UTFresult
	{
		Success,
		NoOutput,
		NoProgress,
	};

	static inline UTFresult UTF16toUTF32(char16_t c, char16_t& high_surrogate, char32_t& result)
	{
		if(high_surrogate != 0)
		{
			if(0xDC00 <= c && c <= 0xDFFF)
			{
				result = 0x010000 + ((((char32_t)high_surrogate & 0x03FF) << 10) | (c & 0x03FF));
				high_surrogate = 0;
				return UTFresult::Success;
			}
			else
			{
				result = high_surrogate;
				high_surrogate = 0;
				return UTFresult::NoProgress;
			}
		}
		else
		{
			if(0xD800 <= c && c <= 0xDBFF)
			{
				high_surrogate = c;
				return UTFresult::NoOutput;
			}
			else
			{
				result = c;
				return UTFresult::Success;
			}
		}
	}

	static inline void PrintUTF8(std::ostream& out, char32_t c)
	{
		if(c <= 0x7F)
		{
			out.put(c);
		}
		else if(c <= 0x07FF)
		{
			out.put(0xC0 | (c >> 6));
			out.put(0x80 | (c & 0x3F));
		}
		else if(c <= 0xFFFF)
		{
			out.put(0xE0 | (c >> 12));
			out.put(0x80 | ((c >> 6) & 0x3F));
			out.put(0x80 | (c & 0x3F));
		}
		else if(c <= 0x10FFFF)
		{
			out.put(0xF0 | (c >> 18));
			out.put(0x80 | ((c >> 12) & 0x3F));
			out.put(0x80 | ((c >> 6) & 0x3F));
			out.put(0x80 | (c & 0x3F));
		}
	}

	static inline void PrintString(std::ostream& out, std::u16string string)
	{
		char32_t result;
		char16_t high_surrogate = 0;
		UTFresult status;
		for(char16_t c : string)
		{
			while((status = UTF16toUTF32(c, high_surrogate, result)) == UTFresult::NoProgress)
			{
				PrintUTF8(out, result);
			}
			if(status != UTFresult::NoOutput)
			{
				PrintUTF8(out, result);
			}
		}
		if(high_surrogate != 0)
		{
			UTF16toUTF32(0, high_surrogate, result);
			PrintUTF8(out, result);
		}
	}

	static inline char16_t PrintString(std::ostream& out, std::u16string string, size_t& start_index)
	{
		char32_t result;
		char16_t high_surrogate = 0;
		UTFresult status;
		while(start_index < string.size())
		{
			char16_t c = string[start_index];
			if((status = UTF16toUTF32(c, high_surrogate, result)) == UTFresult::NoProgress)
			{
				return result;
			}
			start_index++;
			if(status != UTFresult::NoOutput)
			{
				if(0xD800 <= result && result <= 0xDFFF) // technically, we only need to check low surrogates here
				{
					return result;
				}
				PrintUTF8(out, result);
			}
		}
		if(high_surrogate != 0)
		{
			UTF16toUTF32(0, high_surrogate, result);
			return result;
		}
		return 0;
	}
}

#include "common.hh"

template <typename T>
	struct inspect;

template <typename T>
	struct inspect<std::optional<T>>
{
	static void inspect_type(std::ostream& out)
	{
		out << "optional<";
		inspect<T>::inspect_type(out);
		out << ">";
	}

	static void inspect_value(std::ostream& out, const std::optional<T>& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access)
	{
		if(value.has_value())
		{
			out << "optional(";
			inspect<T>::inspect_value(out, value.value(), depth, visited, access.value_or("self") + "^");
			out << ")";
		}
		else
		{
			out << "\33[1mempty\33[m";
		}
	}
};

template <>
	struct inspect<JS::Public::JSBoolean>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSBoolean& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSNumber>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSNumber& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSString>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSString& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSObject>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSObject& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSSymbol>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSSymbol& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSBigInt>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSBigInt& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

#if ENABLE_ES4
template <>
	struct inspect<JS::Public::JSLong>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSLong& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSUlong>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSUlong& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSFloat>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSFloat& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

#if ENABLE_DEC128
template <>
	struct inspect<JS::Public::JSDecimal>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSDecimal& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};
#endif

template <>
	struct inspect<JS::Public::JSChar>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSChar& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSAttributeName>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSAttributeName& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};
#endif

template <>
	struct inspect<JS::Public::JSNamespace>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSNamespace& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Runtime::LexicalReference>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Runtime::LexicalReference& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Runtime::MemberReference>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Runtime::MemberReference& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

#if ENABLE_ES4
template <>
	struct inspect<JS::Runtime::BracketReference>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Runtime::BracketReference& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};
#endif

template <>
	struct inspect<JS::Runtime::SuperReference>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Runtime::SuperReference& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Runtime::PrivateReference>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Runtime::PrivateReference& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

#if ENABLE_ES4
template <>
	struct inspect<JS::Public::JSTypeApplication>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSTypeApplication& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSMethodClosure>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSMethodClosure& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <>
	struct inspect<JS::Public::JSCompoundAttribute>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSCompoundAttribute& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};
#endif

template <>
	struct inspect<JS::Public::JSValue>
{
	static void inspect_type(std::ostream& out);
	static void inspect_value(std::ostream& out, const JS::Public::JSValue& value, size_t depth, std::map<JS::Public::JSObject, std::string>& visited, std::optional<std::string> access);
};

template <typename T>
	struct inspect
{
	static void inspect_type(std::ostream& out)
	{
		out << "C++:" << typeid(T).name();
	}

	static void inspect_value(std::ostream& out, [[maybe_unused]] const T& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JS::Public::JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
	{
		out << "C++ object";
	}
};

#endif // _UTILS_HH
