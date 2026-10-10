#! /usr/bin/python3

# Generate syntax checking tests

import json
import os
import sys

# mini language to parse ASTs

class Token:
	def __init__(self, kind, value = None):
		self.kind = kind
		self.value = value

	def __repr__(self):
		return f"Token({self.kind}, {self.value})"

class Node:
	def __init__(self, kind, *arguments, value = None):
		self.kind = kind
		self.arguments = list(arguments)
		self.value = value

	def __repr__(self):
		return f"Node({self.kind}" + ''.join(f", {argument}" for argument in self.arguments) + (f", value = {self.value!r}" if self.value is not None else "") + ")"

def tokenize(text):
	tokens = []
	token = None
	for c in text:
		if token is not None and token.kind == 'string':
			if c == '"' and not token.value.endswith('\\'):
				token = None
			else:
				token.value += c
		elif c.isspace():
			if token is not None:
				token = None
		elif c.isdigit():
			if token is None or token.kind not in {'number', 'identifier', '.'}:
				token = Token('number', "")
				tokens.append(token)
			elif token.kind == '.':
				token.kind == 'number'
			token.value += c
		elif c.isalpha() or c == '_' or c == ':':
			if token is None or token.kind not in {'number', 'identifier', '.'}:
				token = Token('identifier', "")
				tokens.append(token)
			elif token.kind == '.':
				token.kind == 'number'
			token.value += c
		elif c in {'+', '-'}:
			if token is None or token.kind not in {'number'}:
				token = Token('number', "")
				tokens.append(token)
			token.value += c
		elif c == '.':
			if token is None or token.kind not in {'number'}:
				token = Token('.', "")
				tokens.append(token)
			token.value += c
		elif c == '"':
			token = Token('string', "")
			tokens.append(token)
		else:
			token = Token(c, c)
			tokens.append(token)
	return tokens

def parse(tokens, result = None):
	global test_index
	if result is None:
		result = {}
	token = tokens.pop(0)
	if token.kind == 'number':
		result['equal'] = token.value
	elif token.kind == 'string':
		result['equal'] = 'u"' + token.value + '"'
	elif token.kind == 'identifier' and token.value in {'null', 'true', 'false'}:
		result['equal'] = token.value
	elif token.kind == '[':
		arguments = []
		while tokens[0].kind != ']':
			arguments.append(parse(tokens))
			if tokens[0].kind == ',':
				tokens.pop(0)
			else:
				assert tokens[0].kind == ']'
		assert tokens.pop(0).kind == ']'
		result['layout'] = layout = [{'property': 'size()', 'equal': str(len(arguments))}]
		for index, argument in enumerate(arguments):
			argument['property'] = str(index)
			layout.append(argument)
	elif token.kind == '<':
		if tokens[0].kind != '>':
			result['layout'] = layout = [{'property': 'has_value()', 'equal': 'true'}]
			content = parse(tokens)
			content['property'] = 'value()'
			layout.append(content)
		else:
			result['layout'] = layout = [{'property': 'has_value()', 'equal': 'false'}]
		assert tokens.pop(0).kind == '>'
	elif token.kind == '(':
		typename = ""
		level = 0
		while level > 0 or tokens[0].kind != ')':
			token = tokens.pop(0)
			if token.kind == 'string':
				typename += '"' + token.value + '"'
			else:
				if token.kind == 'identifier' and token.value not in {'JSString'}:
					# TODO: more types
					typename += 'JS::ParseNode::'
				typename += token.value
			if token.kind == '(':
				level += 1
			elif token.kind == ')':
				level -= 1
		tokens.pop(0)
		result['layout'] = content = [{'access': f'std::get<{typename}>'}]
		parse(tokens, result = content[0])
	elif token.kind == 'identifier':
		if len(tokens) > 0 and tokens[0].kind == '{':
			result['class'] = token.value
			result["fields"] = []
			assert tokens.pop(0).kind == '{'
			field_names = set()
			while tokens[0].kind != '}':
				field = {}
				token = tokens.pop(0)
				assert token.kind == 'identifier'
				if token.value in field_names:
					print(f"[{test_index+1}] Duplicate field name \"{token.value}\"", file = sys.stderr)
				field_names.add(token.value)
				field['name'] = token.value
				assert tokens.pop(0).kind == '='
				value = parse(tokens, result = field)
				result['fields'].append(value)
				if tokens[0].kind == ',':
					tokens.pop(0)
				else:
					assert tokens[0].kind == '}'
			assert tokens.pop(0).kind == '}'
		else:
			result['equal'] = 'JS::ParseNode::' + token.value
	else:
		print(tokens[0], file = sys.stderr)
		assert False
	return result

with open(os.path.splitext(sys.argv[0])[0] + '.json' if len(sys.argv) < 2 else sys.argv[1], 'r') as file:
	tests = json.loads(file.read())

counter = 0
def check_result(varname, structure, indent, offending_structure = None):
	global counter
	if offending_structure is None:
		offending_structure = varname

	if 'class' in structure:
		newvar = f'_{counter}'; counter += 1
		print(indent + f'if(success) {{ if([[maybe_unused]] auto {newvar} = dynamic_cast<JS::ParseNode::{structure["class"]} *>({varname})) {{')
		for field in structure.get('fields', ()):
			check_result(f'{newvar}->{field["name"]}', field, indent + '\t')
		print(indent + '} else {')
		print(indent + '\tsuccess = false;')
		print(indent + '\tstd::ostringstream oss;')
		print(indent + f'\tJS::ParseNode::serialize(oss, {offending_structure});')
		print(indent + '\terror_message = oss.str();')
		print(indent + '} }')
	elif 'equal' in structure:
		print(indent + 'if(success) {')
		print(indent + f'\tsuccess = {varname} == {structure["equal"]};')
		print(indent + '\tif(!success) {')
		print(indent + '\t\tstd::ostringstream oss;')
		print(indent + f'\t\tJS::ParseNode::serialize(oss, {offending_structure});')
		print(indent + '\t\terror_message = oss.str();')
		print(indent + '\t}')
		print(indent + '}')
	elif 'layout' in structure:
		for check in structure['layout']:
			if 'access' in check:
				check_result(f'{check["access"]}({varname})', check, indent)
			else:
				if check['property'].isdigit():
					check_result(f'{varname}[{check["property"]}]', check, indent)
				elif check['property'].startswith('*'):
					check_result(f'{varname}->{check["property"][1:]}', check, indent)
				else:
					check_result(f'{varname}.{check["property"]}', check, indent, offending_structure = varname if check['property'] in {'size()', 'has_value()'} else None)
	else:
		assert False

for test_index, test in enumerate(tests):
	if 'name' in test:
		test_name = test['name']
	else:
		test_name = 'unnamed'
	print(f'\tstd::cerr << " = Test #{test_index + 1}: {test_name.replace("??", "??\"\"")} =" << std::endl;')
	print(f'\terror_message = "\\33[2mno error message provided\\33[m";')
	print('\t\tignored = false;')
	print('\ttry {')
	assert type(test['input']) is list
	print('\t\tstd::istringstream iss(R"js(' + '\n'.join(test['input']) + ')js");')
	print('\t\tJS::Parser::UTF8StreamTokenizer tokenizer(iss);')
	print('\t\tJS::Parser::Parser parser(&tokenizer);')
	if 'parse_context' in test:
		print(f'\t\tparser.AdvanceToken(JS::Parser::Context::{test["parse_context"]});')
	#print('\t\tparser.AdvanceToken(JS::Parser::Context::InputElementRegExp);') # TODO: set context
	#print('\t\t[[maybe_unused]] auto result = parser.ParseExpression(JS::Parser::Parser::Precedence::CommaExpression, true, false, false);')
	print(f'\t\t[[maybe_unused]] auto result = parser.{test["parser"]};')
	else_statement = ''
	for result in test['results']:
		condition = []
		if 'version' in result:
			if result["version"] == 'ES4':
				condition.append(f'JS::IsVersionES4()')
			else:
				condition.append(f'JS::CurrentVersion == JS::Version::{result["version"]}')
			if result["version"] in {'ES4', 'ES4_2003', 'ES4_2008'}:
				print('#if ENABLE_ES4')
		if 'lowest_version' in result:
			condition.append(f'JS::CurrentVersion >= JS::Version::{result["lowest_version"]}')
		if 'highest_version' in result:
			condition.append(f'JS::CurrentVersion <= JS::Version::{result["highest_version"]}')
		if 'enabled' in result:
			condition.append(f'JS::Supports_{result["enabled"]}()')
		if 'disabled' in result:
			condition.append(f'!JS::Supports_{result["disabled"]}()')
		if len(condition) == 0:
			print(f'\t\t{else_statement}{{')
		else:
			print(f'\t\t{else_statement}if({" && ".join(condition)}) {{')
		if result['status'] == 'ignored':
			print('\t\tignored = true;')
		elif result['status'] == 'success':
			print('\t\t\tsuccess = true;')
			print('\t\t\tif(parser.PeekToken().type != JS::Parser::TokenType::EndOfFile) {')
			print('\t\t\t\tsuccess = false;')
			print('\t\t\t\terror_message = "tokens left in stream";') # TODO: more specific
			print('\t\t\t} else {')
			if type(result['output']) is str:
				print(test['name'], file = sys.stderr)
				check_result('result', parse(tokenize(result['output'])), '\t\t\t\t')
			else:
				check_result('result', result['output'], '\t\t\t\t')
			print('\t\t\t}')
		else:
			assert result['status'] == 'failure'
			print('\t\t\tsuccess = parser.PeekToken().type != JS::Parser::TokenType::EndOfFile;')
			print('\t\t\tif(!success) error_message = "parse completed";') # TODO: more specific
		print('\t\t}')
		if result.get('version') in {'ES4', 'ES4_2003', 'ES4_2008'}:
			print('\t\telse')
			print('#endif')
			else_statement = ''
		else:
			else_statement = 'else '
	print('\t}')
	print('\tcatch(SyntaxError * error)')
	print('\t{')
	else_statement = ''
	for result in test['results']:
		condition = []
		if 'version' in result:
			if result["version"] == 'ES4':
				condition.append(f'JS::IsVersionES4()')
			else:
				condition.append(f'JS::CurrentVersion == JS::Version::{result["version"]}')
			if result["version"] in {'ES4', 'ES4_2003', 'ES4_2008'}:
				print('#if ENABLE_ES4')
		if 'lowest_version' in result:
			condition.append(f'JS::CurrentVersion >= JS::Version::{result["lowest_version"]}')
		if 'highest_version' in result:
			condition.append(f'JS::CurrentVersion <= JS::Version::{result["highest_version"]}')
		if 'enabled' in result:
			condition.append(f'JS::Supports_{result["enabled"]}()')
		if 'disabled' in result:
			condition.append(f'!JS::Supports_{result["disabled"]}()')
		if len(condition) == 0:
			print(f'\t\t{else_statement}{{')
		else:
			print(f'\t\t{else_statement}if({" && ".join(condition)}) {{')
		if result['status'] == 'ignored':
			print('\t\tignored = true;')
		elif result['status'] == 'failure':
			print('\t\t\tsuccess = true;')
		else:
			#print(result['status'], file = sys.stderr)
			assert result['status'] == 'success'
			print('\t\t\tstd::cerr << "Parse error: ";')
			print('\t\t\tJS::PrintString(std::cerr, JS::ToString(JS::Get(error, u\"message\") RUNTIME_PHASE));')
			print('\t\t\tstd::cerr << std::endl;')
			print('\t\t\tsuccess = false;')
			print('\t\t\terror_message = "parse error";')
		print('\t\t}')
		if result.get('version') in {'ES4', 'ES4_2003', 'ES4_2008'}:
			print('\t\telse')
			print('#endif')
			else_statement = ''
		else:
			else_statement = 'else '
	print('\t}')
	print('\tif(ignored) {')
	print('\t\tstd::cerr << "[ \\33[37mIGNORED\\33[m ]" << std::endl;')
	print('\t} else if(success) {')
	print('\t\tstd::cerr << "[ \\33[1;92mSUCCESS\\33[m ]" << std::endl;')
	print('\t} else {')
	print('\t\tstd::cerr << "[ \\33[1;91mFAILURE \\33[m ]: " << error_message << std::endl;')
	print('\t}')
	print('\ttotal_count ++;')
	print('\tif(ignored) ignored_count ++;')
	print('\telse if(success) success_count ++;')

