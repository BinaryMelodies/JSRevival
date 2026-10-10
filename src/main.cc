
#include <cassert>
#include <cmath>
#include <cstring>
#include <fcntl.h> // TODO: move into system specific part
#include <fstream>
#include <gmpxx.h>
#include <optional>
#include <sstream>
#include <string>
#include <unicode/uchar.h>
#include <unistd.h>
#include <variant>
#include <vector>

#include "common.hh"
#include "macros.hh" // TODO: remove?
#include "debugger.hh"
#include "parser.hh"
#include "ast.hh"
#include "readline.hh"
#include "modules.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

void Debug(std::ostream& out, JSValue value, size_t depth = 5)
{
	value.inspect(out, depth);
}

int run_tests()
{
	size_t total_count = 0;
	size_t success_count = 0;
	size_t ignored_count = 0;
	bool success = false;
	bool ignored = false;
	std::string error_message;
#include "../out/tests.gen.cc"

	std::cerr << "Succeeded: \33[1;92m" << success_count << "\33[m/" << total_count << std::endl;
	std::cerr << "Failed: " << ((success_count + ignored_count) != total_count ? "\33[1;91m" : "") << total_count - (success_count + ignored_count) << "\33[m/" << total_count << std::endl;
	if(ignored_count != 0)
	{
		std::cerr << "Ignored: \33[37m" << ignored_count << "\33[m/" << total_count << std::endl;
	}

	return (success_count + ignored_count) != total_count;
}

#if 0
int quick_tests()
{
	JSValue a;

	JS::Runtime::Agent::CurrentAgent = new JS::Runtime::Agent();
	auto realm = new JS::Runtime::Realm(JS::Runtime::Agent::CurrentAgent);
	auto context = JS::GetCurrentExecutionContext();

	//JS::Runtime::Intrinsics intr;

#if 1
	a = JS_undefined;
	Debug(std::cout, a);

	a = JS_null;
	Debug(std::cout, a);

	a = true;
	Debug(std::cout, a);

	a = u"Hello! ő.我,𝄞";
	Debug(std::cout, a);

	a = 1.2;
	Debug(std::cout, a);

	a = JS::NewSymbol(u"hello");
	Debug(std::cout, a);

	a = mpz_class(123);
	Debug(std::cout, a);

#if 1
	a = JS::NewWithPrototype<JS::Class::OrdinaryObject>(JS_null);
	Debug(std::cout, a);

#if ENABLE_ES4
	if(!JS::IsVersionES4())
#endif
	{
		a = new JS::Class::String(u"Hello!");
		Debug(std::cout, a);
	}

	a = JS::New<JS::Class::QName>(u"http://www.example.org");
/*	a = new JS::Class::QName(u"http://www.example.org");
#if ENABLE_ES4
	if(JS::IsVersionES4()) JS::Class::Class::AssignType(static_cast<JS::Class::OrdinaryObject *>(*a.get_if<JSObject>()), JS::Intrinsics()->QName_class);
#endif*/
	Debug(std::cout, a);

	a = JS::New<JS::Class::Namespace>();
	Debug(std::cout, a);

	JSObject c;
	c = JS::New<JS::Class::Array>(0);
	Debug(std::cout, c);

	JS::Set(c, u"length", 10.0);
	Debug(std::cout, c);

	JS::Set(c, u"5", u"Hello");
	Debug(std::cout, c);

	JS::Set(c, u"7", u"Good bye");
	Debug(std::cout, c);

	c->Delete(u"5" PUBLIC_NAMESPACES WITH_CLASS(c->GetClass()) WITH_FLAG(false));
	Debug(std::cout, c);

	JS::Set(c, u"0", u"(to make sure that parts of the array are kept)");
	JS::Set(c, u"length", 7.0);
	Debug(std::cout, c);
#endif
#endif

#if 1
	Debug(std::cout, context->Realm->Intrinsics.Object_prototype);
	Debug(std::cout, context->Realm->Intrinsics.Object);

	a = context->Realm->Intrinsics.Object.Call(JS_null, {} WITH_PHASE(JS::Runtime::ExecutionPhase::Run));
	Debug(std::cout, a);

	Debug(std::cout, context->Realm->Intrinsics.Function);
#endif
	return 0;
}
#endif

struct ES4PackageMap
{
	std::filesystem::path SystemPath;
	std::vector<JSString> MemberPath;
	bool partial = false;
};

int main(int argc, char * argv[])
{
	bool display_parse_tree = false;
	int script_argument = 0;
	bool interactive = false;
	bool batch = false; // evaluate each line separately (always true for interactive)
	JS::Parser::Parser::ExpectedScriptType script_type = JS::Parser::Parser::ExpectedScriptType::Either;

	auto host = new JS::Runtime::DefaultHost();
	JS::Host = host;
	JS::Runtime::Agent::CurrentAgent = new JS::Runtime::Agent();

#if ENABLE_ES4
	std::vector<ES4PackageMap> es4_package_map;
#endif

	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "-test") == 0)
		{
			JS::Runtime::Agent::CurrentAgent = new JS::Runtime::Agent();
			new JS::Runtime::Realm(JS::Runtime::Agent::CurrentAgent);
			return run_tests();
		}
#if 0
		else if(strcmp(argv[i], "-qtest") == 0)
		{
			return quick_tests();
		}
#endif
		else if(strcmp(argv[i], "-es1") == 0)
		{
			JS::CurrentVersion = JS::SelectedVersion = JS::Version::ES1;
		}
		else if(strcmp(argv[i], "-es3") == 0)
		{
			JS::CurrentVersion = JS::SelectedVersion = JS::Version::ES3;
		}
		else if(strcmp(argv[i], "-es4") == 0)
		{
#if ENABLE_ES4
			JS::CurrentVersion = JS::SelectedVersion = JS::Version::ES4_2003;
#else
			std::cerr << "Error: ECMAScript 4 support not compiled in" << std::endl;
			return 1;
#endif
		}
		else if(strcmp(argv[i], "-es5") == 0)
		{
			JS::CurrentVersion = JS::SelectedVersion = JS::Version::ES5;
		}
		else if(strcmp(argv[i], "-es6") == 0)
		{
			JS::CurrentVersion = JS::SelectedVersion = JS::Version::ES6;
		}
		else if(strcmp(argv[i], "-esnext") == 0)
		{
			JS::CurrentVersion = JS::SelectedVersion = JS::Version::ESNEXT;
		}
		else if(strcmp(argv[i], "-e4x") == 0)
		{
			JS::Enable_E4X = true;
		}
		else if(strcmp(argv[i], "-strict") == 0)
		{
			JS::StrictMode = true;
		}
		else if(strcmp(argv[i], "-oct") == 0)
		{
			JS::Enable_LegacyOctals = true;
		}
		else if(strcmp(argv[i], "-cjs") == 0)
		{
			JS::Enable_CommonJS = true;
		}
		else if(strcmp(argv[i], "-amd") == 0)
		{
			JS::Enable_AMD = true;
		}
		else if(strcmp(argv[i], "-sparse-arrays") == 0)
		{
			JS::Class::Array::DefaultLayout = JS::Class::Array::Layout::Sparse;
		}
		else if(strcmp(argv[i], "-debug") == 0)
		{
#if ENABLE_DEBUG
			JS::Enable_Debug = true;
#else
			std::cerr << "Debug XML generation is not compiled in" << std::endl;
#endif
		}
		else if(strcmp(argv[i], "-interactive") == 0
		|| strcmp(argv[i], "-i") == 0)
		{
			interactive = true;
		}
		else if(strcmp(argv[i], "-batch") == 0)
		{
			batch = true;
		}
		else if(strcmp(argv[i], "-module") == 0
		|| strcmp(argv[i], "-m") == 0)
		{
			script_type = JS::Parser::Parser::ExpectedScriptType::Module;
		}
		else if(strcmp(argv[i], "-script") == 0
		|| strcmp(argv[i], "-s") == 0)
		{
			script_type = JS::Parser::Parser::ExpectedScriptType::Script;
		}
		else if(strcmp(argv[i], "-mapes4") == 0)
		{
#if ENABLE_ES4
			ES4PackageMap map;
			if(++i >= argc)
			{
				std::cerr << "Invalid flag `-mapes4'" << std::endl;
				return 1;
			}
			char * eqchr = strchr(argv[i], '=');
			if(eqchr == NULL)
			{
				std::cerr << "Expected `=' in `-mapes4 " << argv[i] << "'" << std::endl;
				return 1;
			}
			char * name = argv[i];
			for(char * dot = strchr(argv[i], '.'); ; dot = strchr(name, '.'))
			{
				if(dot != NULL && dot < eqchr)
				{
					*dot = '\0';
					map.MemberPath.push_back(JS::UTF8toUTF16(name));
					name = dot + 1;
				}
				else
				{
					char * slash = strchr(name, '/');
					if(slash != NULL && slash < eqchr)
					{
						*slash = '\0';
						map.MemberPath.push_back(JS::UTF8toUTF16(name));
						name = slash + 1;

//						// make sure this node in the path is a partial package
//						JS::Host->GetES4Package(std::optional<std::filesystem::path>{}, path, /* partial = */ true);
						map.partial = true;
					}

					*eqchr = '\0';
					map.MemberPath.push_back(JS::UTF8toUTF16(name));
					break;
				}
			}
//			JS::Host->SetES4Package(std::filesystem::path(eqchr + 1), path);
			map.SystemPath = std::filesystem::path(eqchr + 1);
			es4_package_map.push_back(map);
#else
			std::cerr << "Error: ECMAScript 4 support not compiled in" << std::endl;
			return 1;
#endif
		}
		else if(strcmp(argv[i], "-map") == 0)
		{
			if(++i >= argc)
			{
				std::cerr << "Invalid flag `-map'" << std::endl;
				return 1;
			}
			char * eqchr = strchr(argv[i], '=');
			if(eqchr == NULL)
			{
				std::cerr << "Expected `=' in `-map " << argv[i] << "'" << std::endl;
				return 1;
			}
			*eqchr = '\0';
			char * name = argv[i];
			if(eqchr[-1] == '/')
			{
				host->ImportPrefixMap[JS::UTF8toUTF16(name)] = JS::UTF8toUTF16(eqchr + 1);
			}
			else
			{
				host->ImportMap[JS::UTF8toUTF16(name)] = JS::UTF8toUTF16(eqchr + 1);
			}
		}
		else if(strcmp(argv[i], "-parse-tree") == 0)
		{
			display_parse_tree = true;
		}
		else if(strcmp(argv[i], "-debug-stack") == 0)
		{
			JS::Runtime::ExecutionContext::DebugStack = true;
		}
		else if(argv[i][0] == '-')
		{
			std::cerr << "Unknown flag `" << argv[i] << "'" << std::endl;
			return 1;
		}
		else
		{
			script_argument = i;
			break;
		}
	}

	if(JS::Enable_E4X && JS::CurrentVersion <= JS::Version::ES1)
	{
		std::cerr << "E4X not supported with ECMAScript 1" << std::endl;
	}

	open_history_file(".history");

	if(script_argument == 0 && isatty(0))
	{
		interactive = true;
	}

	size_t l = strlen(argv[script_argument]);
	if(script_argument != 0 && strcmp(&argv[script_argument][l - 3], ".as") == 0)
	{
#if ENABLE_ES4
		JS::CurrentVersion = JS::SelectedVersion = JS::Version::ES4_2003;
#else
		std::cerr << "Warning: ECMAScript 4 support not compiled in" << std::endl;
#endif
	}

	if(script_argument != 0 && !interactive && !batch && script_type == JS::Parser::Parser::ExpectedScriptType::Either)
	{
		if(l >= 4 && strcmp(&argv[script_argument][l - 4], ".cjs") == 0)
		{
			script_type = JS::Parser::Parser::ExpectedScriptType::Script;
			JS::Enable_CommonJS = true;
		}
		else if(l >= 4 && strcmp(&argv[script_argument][l - 4], ".mjs") == 0)
		{
			script_type = JS::Parser::Parser::ExpectedScriptType::Module;
		}
	}

	JSString source = u"";
	std::istream * input = &std::cin;
	readline_istream readline_input;
	std::ifstream input_file;
	if(script_argument != 0)
	{
		char16_t wc;
		int shift = 0;
		for(size_t ptr = 0; argv[script_argument][ptr] != '\0'; ptr++)
		{
			uint32_t c = argv[script_argument][ptr] & 0xFF;
			if(c < 0x80)
			{
				source += char16_t(ptr);
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
					source = JS::UTF32toUTF16(wc);
				}
				/*else if(wc <= 0xFFFF)
				{
					source += char16_t(wc);
				}
				else
				{
					wc -= 0x10000;
					source += char16_t(0xD800 | (wc >> 10));
					source += char16_t(0xDC00 | (wc & 0x3FF));
				}*/
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

		input_file.open(argv[script_argument]);
		input = &input_file;
	}
	else if(interactive)
	{
		input = &readline_input;
	}
	JS::Parser::UTF8StreamTokenizer tokenizer(*input);
	if(!source.empty())
	{
		tokenizer.last_location.filename = argv[script_argument];
	}

	JS::Parser::Parser parser(&tokenizer);

	[[maybe_unused]] auto realm = new JS::Runtime::Realm(JS::Runtime::Agent::CurrentAgent);
	auto context = JS::GetCurrentExecutionContext();

#if ENABLE_ES4
	// load package maps
	for(auto map : es4_package_map)
	{
		if(map.partial)
		{
			std::vector<JSString> parent_path = map.MemberPath;
			parent_path.pop_back();
			host->GetES4Package(std::optional<std::filesystem::path>{}, parent_path, /* partial = */ true);
		}
		host->SetES4Package(map.SystemPath, map.MemberPath);
	}
#endif

#if ENABLE_DEBUG
	auto debugger = JS::Runtime::Debugger::Current;
	auto debugger_jslogs = debugger->OpenXMLElement("jslogs")->GetCurrentElement();
	auto debugger_symbols = debugger->OpenXMLElement("symbols")->GetCurrentElement();
	debugger->CloseXMLElement();
#if ENABLE_ES4
	auto debugger_namespaces = debugger->OpenXMLElement("namespaces")->GetCurrentElement();
	debugger->CloseXMLElement();
#endif
	JS::Runtime::Output_Element = debugger->OpenXMLElement("output")->GetCurrentElement();

#if 0
	int old_stdout;
	if(JS::Enable_Debug)
	{
		old_stdout = dup(1);
		close(1);
		int fd = open("/tmp", O_RDWR | O_CREAT | O_TMPFILE);
		if(fd != 1)
		{
			dup2(fd, 1); // probably unnecessary
		}
	}
#endif
#endif

	int return_output;

	if(interactive || batch)
	{
		host->SetupWrappers(JS::GetCurrentExecutionContext()->VariableEnvironment); // TODO: is this the right place to do it?
	}

	bool is_running = true;
	bool started = false;
	if(interactive)
	{
		std::cout << "JavaScript Revival 0.0" << std::endl;
	}

	while(is_running)
	{
		if(!interactive && !batch)
		{
			// not interactive, one execution cycle will suffice
			is_running = false;
		}

		if(!input->good())
		{
			break;
		}

		if(!started)
		{
			context = new JS::Runtime::ExecutionContext(*context);

			JS::Runtime::ExecutionContext::GetContextStack().clear();
			JS::Runtime::ExecutionContext::GetContextStack().push_back(context);
		}
		else
		{
			started = true;
		}

		try
		{
			std::optional<JSValue> value;

			if(interactive || batch)
			{
				if(interactive)
				{
					std::cout << JS::VersionNames[int(JS::CurrentVersion)] << "> " << std::flush;
				}

				auto parse_node = parser.ParseLine();

				if(display_parse_tree)
				{
					parse_node->serialize(std::cout);
					std::cout << std::endl;
					continue;
				}

				JS::Runtime::ValidationContext cxt;
				//parse_node->Validate(cxt);

				JS::Interpreter::Code code;
				JS::Interpreter::State<> state = code.Begin();
				//parse_node->CompileStatement(state);

				JS::ParseNode::ScriptOrModule som;
				som.body = parse_node;
				som.Process(cxt, state);

				context->code = &code;
				context->code_pointer = 0;
				context->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

				value = JS::Runtime::ExecutionContext::ExecuteAndReturn<std::optional<JSValue>>();

				if(JS::Enable_AMD)
				{
					JS::Host->FinishLoadingDefinedModule();
					JS::Runtime::ExecutionContext::GetContextStack().push_back(context);
					JS::Host->EnableDefinedModule(u".");
					JS::Runtime::ExecutionContext::GetContextStack().pop_back();
				}
			}
			else
			{
				auto record = parser.ParseScriptOrModule(JS::GetCurrentRealm(), script_type);

				if(display_parse_tree)
				{
					if(auto script = dynamic_cast<JS::Runtime::ScriptRecord *>(record))
					{
						script->Code->serialize(std::cout);
						std::cout << std::endl;
						continue;
					}
					else if(auto module = dynamic_cast<JS::Runtime::SourceTextModuleRecord *>(record))
					{
						module->Code->serialize(std::cout);
						std::cout << std::endl;
						continue;
					}
					continue;
				}

				value = record->Perform();

				if(JS::Enable_AMD)
				{
					if(auto script = dynamic_cast<JS::Runtime::ScriptRecord *>(record))
					{
						JS::Host->EnableDefinedModule(
							script->sourcefilename.has_value()
							? script->sourcefilename.value().replace_extension().u16string()
							: u".");
					}
				}
			}

			switch(context->completion.Type)
			{
			case JS::Runtime::CompletionRecord::CompletionType::Normal:
#if ENABLE_DEBUG
				if(!JS::Enable_Debug)
#endif
				{
					std::cout << "> result: ";
					if(value.has_value())
					{
						JS::Runtime::ExecutionContext::GetContextStack().push_back(context);
						Debug(std::cout, value.value());
						std::cout << std::endl;
					}
					else
					{
						std::cout << "empty" << std::endl;
					}
				}
#if ENABLE_DEBUG
				else
				{
					debugger->GotoElement(debugger_jslogs);
					debugger->OpenXMLElement("result");
					if(value.has_value())
					{
						JSValue(value.value()).serialize(debugger);
					}
				}
#endif

				return_output = 0;
				break;
			case JS::Runtime::CompletionRecord::CompletionType::Throw:
				value = context->completion.Value;
#if ENABLE_DEBUG
				if(!JS::Enable_Debug)
#endif
				{
					std::cout << "> throw: ";
					JS::Runtime::ExecutionContext::GetContextStack().push_back(context);
					Debug(std::cout, value.value());
					std::cout << std::endl;
				}
				return_output = 1;
				break;
			case JS::Runtime::CompletionRecord::CompletionType::Return:
				value = context->completion.Value;
#if ENABLE_DEBUG
				if(!JS::Enable_Debug)
#endif
				{
					std::cout << "> return value: ";
					if(value.has_value())
					{
						JS::Runtime::ExecutionContext::GetContextStack().push_back(context);
						Debug(std::cout, value.value());
						std::cout << std::endl;
					}
					else
					{
						std::cout << "empty" << std::endl;
					}
				}
				return_output = 1;
				break;
			case JS::Runtime::CompletionRecord::CompletionType::Break:
#if ENABLE_DEBUG
				if(!JS::Enable_Debug)
#endif
				{
					std::cout << "> break" << std::endl;
				}
				return_output = 1;
				break;
			case JS::Runtime::CompletionRecord::CompletionType::Continue:
#if ENABLE_DEBUG
				if(!JS::Enable_Debug)
#endif
				{
					std::cout << "> continue" << std::endl;
				}
				return_output = 1;
				break;
			default:
#if ENABLE_DEBUG
				if(!JS::Enable_Debug)
#endif
				{
					std::cout << "> invalid result" << std::endl;
				}
				return_output = 1;
				break;
			}

			/*if(interactive && is_running)
			{
				std::cout << JS::VersionNames[int(JS::CurrentVersion)] << "> " << std::flush;
			}*/
		}
#if 1
		catch(JS::Class::Internal::Value * thrown_value)
		{
			JSValue error = JSValue::WrapValue(thrown_value);
			// TODO: JS::Host->FinishLoadingDefinedModule_ThrowCompletion("main", thrown_value);
#if ENABLE_DEBUG
			if(!JS::Enable_Debug)
#endif
			{
				if(auto error_object = std::dynamic_pointer_cast<JS::Class::Error>(JS::GetNonNullObject(error).value_or(nullptr)))
				{
					std::cerr << "Uncaught ";
					JSValue name_value = JS::Get(error, u"name");
					if(auto name_pointer = name_value.get_if<JSString>())
					{
						JS::PrintString(std::cerr, *name_pointer);
					}
					else
					{
						auto tmp = JSValue::UnwrapValue(error);
						std::cerr << typeid(*tmp).name();
						JSValue::WrapValue(tmp);
					}
					std::cerr << ": ";
					JSValue message_value = JS::Get(error_object, u"message");
					if(auto message_pointer = message_value.get_if<JSString>())
					{
						JS::PrintString(std::cerr, *message_pointer);
					}
					else
					{
						JS::PrintString(std::cerr, JS::ToString(message_value RUNTIME_PHASE));
					}
				}
				else
				{
					std::cout << "> throw: ";
					JS::Runtime::ExecutionContext::GetContextStack().push_back(context);
					Debug(std::cout, error);
				}
				std::cerr << std::endl;
			}
#if ENABLE_DEBUG
			else
			{
				debugger->GotoElement(debugger_jslogs);
				debugger->OpenXMLElement("error");
				JSValue(error).serialize(debugger);
			}
#endif

			return_output = 1;
		}
#endif

		while(!JS::GetSurroundingAgent()->EnqueuedJobs.empty())
		{
			JS::Runtime::Job * job = JS::GetSurroundingAgent()->EnqueuedJobs.front();
			JS::GetSurroundingAgent()->EnqueuedJobs.pop();
			job->Execute(); // must not produce errors
			delete job;
		}
	}

#if ENABLE_DEBUG
	if(JS::Enable_Debug)
	{
		debugger->GotoElement(debugger_symbols);
		for(auto allocated_symbols_iter : JS::Runtime::SymbolInstance::allocated_symbols)
		{
			for(auto symbol : allocated_symbols_iter.second)
			{
				if(debugger->used_symbols.find(symbol) != debugger->used_symbols.end())
				{
					auto symbol_id = debugger->GetSymbolId(symbol);
					debugger
						->OpenXMLElement("symbol")
						->AddXMLAttribute("id", symbol_id);
					if(symbol->Description.has_value())
					{
						debugger
							->OpenXMLElement("description")
							->AppendText(symbol->Description.value())
							->CloseXMLElement();
					}
					debugger->CloseXMLElement();
				}
			}
		}
#if ENABLE_ES4
		if(JS::IsVersionES4())
		{
			debugger->GotoElement(debugger_namespaces);
			for(auto namespace_ : JS::Class::Namespace::all_allocated_namespaces)
			{
				if(debugger->used_namespaces.find(namespace_) != debugger->used_namespaces.end())
				{
					auto namespace_id = debugger->GetNamespaceId(namespace_);
					debugger
						->OpenXMLElement("namespace")
						->AddXMLAttribute("id", namespace_id);
					if(namespace_->prefix.has_value())
					{
						debugger
							->OpenXMLElement("prefix")
							->AppendText(namespace_->prefix.value())
							->CloseXMLElement();
					}
					debugger
						->OpenXMLElement("name")
						->AppendText(namespace_->uri)
						->CloseXMLElement();
					debugger->CloseXMLElement();
				}
			}
		}
#endif

#if 0
		close(1);
		dup2(old_stdout, 1);
		close(old_stdout);
#endif

		debugger->PrettyPrint(std::cout);
	}
#endif

	return return_output;
}

