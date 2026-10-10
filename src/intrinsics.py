#! /usr/bin/python3

import math
import os
import sys

class JSUndefined:
	def __repr__(self):
		return "undefined"

undefined = JSUndefined()

class JSNull:
	def __repr__(self):
		return "null"

null = JSNull()

class JSUninitialized:
	def __init__(self, value_type):
		self.value_type = value_type

	def __repr__(self):
		return f"uninitialized {self.value_type}"

class JSTypedValue:
	def __init__(self, typename, value):
		self.typename = typename
		self.value = value

	def __repr__(self):
		return f"{self.typename} {self.value!r}"

class JSSymbol:
	def __init__(self, description):
		self.description = description

	def __repr__(self):
		return f"Symbol({self.description})"

class JSQName:
	def __init__(self, ns, name):
		self.ns = ns
		self.name = name

	def __repr__(self):
		return f"{self.ns}::{self.name}"

class JSMember:
	def __init__(self, record, member):
		self.record = record
		self.member = member

	def __repr__(self):
		return f"{self.record}[{self.member}]"

class JSIdentifier:
	def __init__(self, name):
		self.name = name

	def __repr__(self):
		return self.name

class Version:
	def __init__(self, Prototype, ObjectType):
		self.line_number = None
		self.Prototype = Prototype
		self.ObjectType = ObjectType
		self.ObjectTypeConstructorArguments = None

		self.introduced = None
		self.removed = None
		self.extensions = set()
		self.ecmascript4 = JSObject.ES4_DEFAULT

#### TODO:
# replace with
#		if self.ecmascript4 == JSObject.ES4_EXCLUSIVE:
#			ecmascript4_allowed = True
#			ecmascript4_unconditional = True
#			non_ecmascript4_allowed = False
#		elif self.ecmascript4 == JSObject.ES4_UNAVAILABLE:
#			ecmascript4_allowed = False
#			ecmascript4_unconditional = False
#			non_ecmascript4_allowed = True
#		elif self.ecmascript4 == JSObject.ES4_ENABLED:
#			ecmascript4_allowed = False
#			ecmascript4_unconditional = True
#			non_ecmascript4_allowed = True
#		elif self.ecmascript4 == JSObject.ES4_DEFAULT:
#			ecmascript4_allowed = (self.introduced is None or self.introduced < 4) and (self.removed is None or 4 < self.removed)
#			ecmascript4_unconditional = False
#			non_ecmascript4_allowed = True


	@staticmethod
	def Object():
		return Version(JSMember('Object', 'prototype'), 'Object')

	@staticmethod
	def Function():
		return Version(JSMember('Function', 'prototype'), 'BuiltinFunction')

class JSObject:
	# only available for ES4
	ES4_EXCLUSIVE = 'exclusive'
#	{
#		"allowed": True,
#		"unconditional": True,
#		"non_ecmascript4": False
#	}

	# unavailable in ES4
	ES4_UNAVAILABLE = 'unavailable'
#	{
#		"allowed": True,
#		"unconditional": False,
#		"non_ecmascript4_allowed": True
#	}

	# available in ES4 mode, even if other conditions are not met
	ES4_ENABLED = 'enabled'
#	{
#		"allowed": True,
#		"unconditional": False,
#		"non_ecmascript4_allowed": True
#	}

	# available in ES4 mode if other conditions are met
	ES4_DEFAULT = 'default'
#	{
#		"allowed": True, # TODO: depends on the version set
#		"unconditional": False,
#		"non_ecmascript4_allowed": True
#	}

	def __init__(self):
#		self.Prototype = JSMember('Object', 'prototype')
		self.flags = {'E': True}
		self.slots = []
#		self.ObjectType = 'Object'
		self.versions = [Version(JSMember('Object', 'prototype'), 'Object')]

		self.declared_type = None # ES4 specific

		self.identifier = None
		self.usage_count = 0
		self.label = None # used for debugging

		self.introduced = None
		self.removed = None
		self.extensions = set()
		self.ecmascript4 = JSObject.ES4_DEFAULT

	def __repr__(self):
		return f"Object({self.versions[0].Prototype}, Extensible = {self.flags['E']}" + (f", ObjectType = {self.versions[0].ObjectType}" if self.versions[0].ObjectType != 'Object' else "") + ")"

	def add_version(self, version):
		#if self.label == 'EvalError':
		#	print(self.label, self.introduced, version.introduced, self.ecmascript4, version.ecmascript4, file = sys.stderr)
		#self.make_versions()
		self.versions.append(version)

		if self.ecmascript4 == JSObject.ES4_EXCLUSIVE:
			ecmascript4_allowed = True
			ecmascript4_unconditional = True
			non_ecmascript4_allowed = False
		elif self.ecmascript4 == JSObject.ES4_UNAVAILABLE:
			ecmascript4_allowed = False
			ecmascript4_unconditional = False
			non_ecmascript4_allowed = True
		elif self.ecmascript4 == JSObject.ES4_ENABLED:
			ecmascript4_allowed = True
			ecmascript4_unconditional = False
			non_ecmascript4_allowed = True
#			ecmascript4_allowed = False
#			ecmascript4_unconditional = True
#			non_ecmascript4_allowed = True
		elif self.ecmascript4 == JSObject.ES4_DEFAULT:
			ecmascript4_allowed = (self.introduced is None or self.introduced < 4) and (self.removed is None or 4 < self.removed)
			ecmascript4_unconditional = False
			non_ecmascript4_allowed = True

		if version.introduced is not None:
			if self.introduced is None or version.introduced < self.introduced:
				self.introduced = version.introduced
				non_ecmascript4_allowed = True

		if self.removed is not None:
			if version.removed is None or self.removed < version.removed:
				self.removed = version.removed
				non_ecmascript4_allowed = True

		self.extensions.intersection_update(version.extensions)

		if version.ecmascript4 == JSObject.ES4_EXCLUSIVE:
			ecmascript4_allowed = True
			ecmascript4_unconditional = True
			#non_ecmascript4_allowed = False
		elif version.ecmascript4 == JSObject.ES4_UNAVAILABLE:
			#ecmascript4_allowed = False
			#ecmascript4_unconditional = False
			non_ecmascript4_allowed = True
		elif version.ecmascript4 == JSObject.ES4_ENABLED:
			ecmascript4_allowed = True
			#ecmascript4_unconditional = False
			non_ecmascript4_allowed = True
#			#ecmascript4_allowed = False
#			ecmascript4_unconditional = True
#			#non_ecmascript4_allowed = True
		elif version.ecmascript4 == JSObject.ES4_DEFAULT:
			if (version.introduced is None or version.introduced < 4) and (version.removed is None or 4 < version.removed):
				ecmascript4_allowed = True
			#ecmascript4_unconditional = False
			non_ecmascript4_allowed = True

		if not non_ecmascript4_allowed:
			self.ecmascript4 = JSObject.ES4_EXCLUSIVE
		elif ecmascript4_unconditional or (ecmascript4_allowed and not ((self.introduced is None or self.introduced < 4) and (self.removed is None or 4 < self.removed))):
			self.ecmascript4 = JSObject.ES4_ENABLED
		elif not ecmascript4_allowed and ((self.introduced is None or self.introduced < 4) and (self.removed is None or 4 < self.removed)):
			self.ecmascript4 = JSObject.ES4_UNAVAILABLE
		else:
			self.ecmascript4 = JSObject.ES4_DEFAULT

		"""
		#   X U E D
		# X X E E E
		# U E U E D
		# E E E E E
		# D E D E D
		if self.ecmascript4 != JSObject.ES4_ENABLED and self.ecmascript4 != version.ecmascript4:
			if self.ecmascript4 == JSObject.ES4_DEFAULT and version.ecmascript4 == JSObject.ES4_UNAVAILABLE:
				pass
			elif self.ecmascript4 == JSObject.ES4_UNAVAILABLE and version.ecmascript4 == JSObject.ES4_DEFAULT:
				self.ecmascript4 = JSObject.ES4_DEFAULT
			else:
				self.ecmacript4 = JSObject.ES4_ENABLED
		"""

		#if self.label == 'EvalError':
		#	print(self.label, self.introduced, self.ecmascript4, file = sys.stderr)

class JSFunction(JSObject):
	def __init__(self):
		super(JSFunction, self).__init__()
		#self.Prototype = JSMember('Function', 'prototype')
		#self.ObjectType = 'BuiltinFunction'
		self.versions[-1].Prototype = JSMember('Function', 'prototype')
		self.versions[-1].ObjectType = 'BuiltinFunction'
		self.ReturnType = 'Object'
		self.Instance = None
		self.CppType = None
		# ES4 only
		self.CppClassType = None
		self.CppClassArguments = []

	def __repr__(self):
		return f"Function({self.versions[0].Prototype}, Extensible = {self.flags['E']}, ReturnType = {self.ReturnType})"

class JSConstructor(JSFunction):
	def __init__(self):
		super(JSConstructor, self).__init__()
		self.Super = JSIdentifier('Object') # only for ES4

	def __repr__(self):
		return f"Constructor({self.versions[0].Prototype}, Extensible = {self.flags['E']})"

class JSTypeLayout(JSConstructor):
	def __init__(self):
		super(JSTypeLayout, self).__init__()

	def __repr__(self):
		return f"TypeLayout({self.versions[0].Prototype}, Extensible = {self.flags['E']})"

class JSNative:
	def __init__(self, code):
		self.code = code

	def __repr__(self):
		return f"Native({self.code})"

class JSAlias:
	def __init__(self, code = None):
		self.declared_type = None # ES4 specific
		self.versions = [Version(JSMember('Object', 'prototype'), 'Object')]
		self.versions[0].code = code
		self.extensions = {}

		self.label = None # should not be set
		self.identifier = None
		self.usage_count = 0

		self.introduced = None
		self.removed = None
		self.extensions = set()
		self.ecmascript4 = JSObject.ES4_DEFAULT

	def __repr__(self):
		return f"Alias({self.versions[0].code})"

	def add_version(self, version):
		# TODO: very unpythonic
		JSObject.add_version(self, version)

class JSProperty:
	def __init__(self, name = None):
		self.name = name
		self.introduced = None
		self.removed = None
		self.extensions = set()
		self.ecmascript4 = JSObject.ES4_DEFAULT

class JSDataProperty(JSProperty):
	def __init__(self, name = None, value = None, **flags):
		super(JSDataProperty, self).__init__(name)
		self.flags = {'W': True, 'E': False, 'C': True, 'X': False, 'I': False, 'F': False} # X is explicit, I is fixed (class instance) only used by ES4, F is final (for class instances)
		for f, v in flags.items():
			self.flags[f] = v
		self.value = value
		self.declared_type = None # ES4 specific

	def __repr__(self):
		return f"DataProperty({self.name}, Value = {self.value}, " + (f"Type = {self.declared_type}, " if self.declared_type is not None else "") + f"Writable = {self.flags['W']}, Enumerable = {self.flags['E']}, Configurable = {self.flags['C']}" + (", Explicit = True" if self.flags.get('X') == True else "") + (", Instance = True" if self.flags.get('I') == True else "") + (", Final = True" if self.flags.get('F') == True else "") + ")"

class JSAccessorProperty(JSProperty):
	def __init__(self):
		super(JSAccessorProperty, self).__init__(None)
		self.flags = {'E': False, 'C': True, 'I': False} # I is fixed (class instance) only used by ES4
		self.slots = []

	def __repr__(self):
		return f"AccessorProperty({self.name}, Enumerable = {self.flags['E']}, Configurable = {self.flags['C']}" + (", Instance = True" if self.flags.get('I') == True else "") + ")"

def get_tokens(line):
	tokens = []
	token = None
	for c in line:
		while True:
			if token is None:
				if c.isspace():
					pass
				else:
					token = c
			elif token[0].isalpha() or token[0] == '_':
				if c.isalnum() or c == '_':
					token += c
				else:
					tokens.append(token)
					token = None
					continue
			elif token[0].isdigit() or token[0] in {'+', '-'}:
				if c.isdigit() or c == '.':
					token += c
				else:
					tokens.append(token)
					token = None
					continue
			elif token[0] == '"':
				if c == '"':
					token += c
				elif token[1:].endswith('"') and not token[1:].endswith('""'):
					tokens.append(token)
					token = None
					continue
				else:
					token += c
			else:
				if c.isspace() or c.isalnum() or c == '_' or c == '"':
					assert token != ']/'
					tokens.append(token)
					token = None
					continue
				else:
					token += c
			break
	if token is not None:
		tokens.append(token)
	return tokens

def get_access(tokens):
	value = JSIdentifier(tokens.pop(0))
	while len(tokens) > 0:
		if tokens[0] == '.':
			tokens.pop(0)
			value = JSMember(value, tokens.pop(0))
		elif tokens[0] == '[':
			tokens.pop(0)
			value = JSMember(value, get_access(tokens))
			assert tokens.pop(0) == ']'
		else:
			break
	return value

def get_string(tree):
	assert type(tree) is str
	assert tree.startswith('"') and tree.endswith('"')
	return tree[1:-1].replace('""', '"')

def is_string(tree):
	return type(tree) is str and tree.startswith('"') and tree.endswith('"')

def get_identifier(tree):
	assert type(tree) is str
	assert not tree.startswith('"') and not tree.endswith('"')
	return tree

def is_identifier(tree):
	return type(tree) is str and not tree.startswith('"') and not tree.endswith('"') #and len(tree) > 0 and (tree[0].isalnum() or tree[0] == '_')

def get_number(tree):
	assert type(tree) is str
	return float(tree)

def get_value(tokens):
	assert type(tokens[0]) is str
	if is_string(tokens[0]):
		return get_string(tokens.pop(0))
	else:
		try:
			value = get_number(tokens[0])
		except ValueError:
			return get_access(tokens)

		tokens.pop(0)
		if len(tokens) > 0 and tokens[0] == '<':
			tokens.pop(0)
			declared_type = get_access(tokens)
			assert type(declared_type) is JSIdentifier
			if declared_type.name != 'Number':
				value = JSTypedValue(declared_type.name, value)
			assert tokens.pop(0) == '>'
		return value

# a name can be an identifier, a '.' reference, a symbol or an internal slot
def get_name(tokens):
	if len(tokens) > 0 and tokens[0] == '[':
		tokens.pop(0)
		symbol = JSSymbol(get_access(tokens))
		assert tokens.pop(0) == ']'
		return symbol
	elif len(tokens) > 0 and tokens[0] == '[[':
		tokens.pop(0)
		value = tokens.pop(0)
		assert tokens.pop(0) == ']]'
		return '[[' + value + ']]'
	else:
		name = tokens.pop(0)
		if len(tokens) > 0 and tokens[0] == '::':
			name = get_identifier(name)
			tokens.pop(0)
			return JSQName(name, get_name(tokens))
		else:
			return name

class Macro:
	def __init__(self, *arguments):
		self.arguments = arguments
		self.lines = []

def process_prefix(thing, prefix):
	if prefix == '4':
		thing.ecmascript4 = JSObject.ES4_EXCLUSIVE
		return
	elif prefix == 'D':
		# TODO: make sure ENABLE_DEC128 is checked
		thing.ecmascript4 = JSObject.ES4_EXCLUSIVE
		thing.extensions.add('D')
		return

	if prefix.endswith('!4'):
		thing.ecmascript4 = JSObject.ES4_UNAVAILABLE
		prefix = prefix[:-2]

	es4_explicit = False
	if '4,' in prefix:
		# such as "4,5 JSON"
		es4_explicit = True
		thing.ecmascript4 = JSObject.ES4_ENABLED
		prefix = prefix.replace('4,', '')

	if prefix.endswith(',4'):
		# such as "X,4 Namespace"
		es4_explicit = True
		thing.ecmascript4 = JSObject.ES4_ENABLED
		prefix = prefix[:-2]

	introduced = prefix.replace('W', '').replace('X', '').replace('D', '').replace('d', '')
	if '!' in introduced:
		introduced, removed = introduced.split('!')
		thing.removed = int(removed)

	if len(introduced) != 0:
		thing.introduced = int(introduced)
		assert thing.introduced != 4
	elif 'X' in prefix:
		thing.introduced = 3

	if thing.introduced is not None and thing.removed is not None:
		assert thing.introduced < thing.removed

	if 'W' in prefix:
		thing.extensions.add('W')
	if 'X' in prefix:
		thing.extensions.add('X')
	if 'd' in prefix:
		thing.extensions.add('d')

	if not es4_explicit \
	and ((thing.introduced is not None and 4 < thing.introduced) or (thing.removed is not None and thing.removed < 4)):
		thing.ecmascript4 = JSObject.ES4_UNAVAILABLE

defines = {}
DEFINE = None

UNIVERSE = {}
CHAIN = []
ENTITIES = set()
with open(sys.argv[1], 'r') as file:
	lines = [(line_number + 1, line) for (line_number, line) in enumerate(file)]
	while len(lines) > 0:
		line_number, line = lines.pop(0)
		if line.startswith('%define'):
			parts = line.strip().split()
			parts.pop(0) # %define
			name = parts.pop(0)
			DEFINE = Macro(*parts)
			defines[name] = DEFINE
			continue
		elif DEFINE is not None and line.startswith('%end'):
			DEFINE = None
			continue

		line = line.rstrip()
		if line.startswith('#'):
			continue
		prefix = ""
		while len(line) > 0 and not line.startswith('\t'):
			prefix += line[0]
			line = line[1:]

		indent = -1
		while line.startswith('\t'):
			indent += 1
			line = line[1:]

		if DEFINE is not None:
			DEFINE.lines.append((line_number, prefix, indent, line))
			continue

		tokens = get_tokens(line)
		if '#' in tokens:
			tokens = tokens[:tokens.index('#')]
		if len(tokens) == 0:
			continue

		#print(repr(prefix), indent, tokens, file = sys.stderr)
		if indent == -1:
			continue

		if prefix.startswith('~'):
			is_variant = True
			prefix = prefix[1:]
			if len(CHAIN) != indent + 1:
				print(line_number, "Invalid chain depth", len(CHAIN), indent + 1, file = sys.stderr)
				exit(1)
		else:
			is_variant = False
			while len(CHAIN) > indent:
				CHAIN.pop()
			if len(CHAIN) != indent:
				print(line_number, "Invalid chain depth", len(CHAIN), indent, file = sys.stderr)
				exit(1)
		entity_type = tokens.pop(0)

#		print(is_variant, CHAIN, line, file = sys.stderr)

		if entity_type == '%':
			# macro insertion
			name = tokens.pop(0)
			assert tokens.pop(0) == '('
			arguments = []
			while len(tokens) > 0 and tokens[0] != ')':
				if tokens[0] == ',':
					arguments.append("")
					tokens.pop(0)
				else:
					if len(arguments) == 0:
						arguments.append(tokens.pop(0))
					elif arguments[-1] == "":
						arguments[-1] += tokens.pop(0)
					else:
						arguments[-1] += " " + tokens.pop(0)
			#print(name, arguments)
			if name not in defines:
				print(line_number, "Undefined macro", file = sys.stderr)
				exit(1)
			macro = defines[name]
			if len(macro.arguments) != len(arguments):
				print(line_number, "Macro argument count mismatch", file = sys.stderr)
				exit(1)
			argument_names = {}
			for m_name, a_name in zip(macro.arguments, arguments):
				argument_names[m_name] = a_name
			new_lines = []
			last_indent = -1
			for m_line_number, m_prefix, m_indent, m_line in macro.lines:
				#print(prefix, m_prefix, name, file = sys.stderr)

				if prefix == '':
					c_prefix = m_prefix
				elif prefix == m_prefix:
					c_prefix = prefix
				else:
					# combine prefixes, only meaningful for plain integers
					tm_prefix = m_prefix
					if tm_prefix.startswith('~'):
						tm_prefix = tm_prefix[1:]

					if '!' in m_prefix:
						assert tm_prefix.endswith('!6') # TODO: more complicated rules? not necessary here
						im_prefix = int(tm_prefix[:tm_prefix.find('!')])
					else:
						im_prefix = int(tm_prefix)

					iprefix = int(prefix)
					if (iprefix == 4 and 4 < iprefix) and (4 < im_prefix or im_prefix == 4):
						continue # skip this line
					elif iprefix == 4 or im_prefix == 4:
						c_prefix = '4'
					else:
						c_prefix = str(max(iprefix, im_prefix))

					if m_prefix.endswith('!6') and c_prefix != '4':
						c_prefix += '!6' # TODO: more complicated rules?

					if m_prefix.startswith('~'):
						c_prefix = '~' + c_prefix
					#print(prefix, m_prefix, c_prefix, file = sys.stderr)

				ix = 0
				while '{' in m_line[ix:]:
					ix1 = m_line.find('{', ix)
					if m_line[ix1 + 1] == '{':
						m_line = m_line[:ix1] + m_line[ix1 + 1:]
						ix = ix1 + 1
						continue
					ix2 = m_line.find('}', ix1)
					argument = m_line[ix1 + 1:ix2]
					if argument in argument_names:
						argument_value = argument_names[argument]
						m_line = m_line[:ix1] + argument_value + m_line[ix2 + 1:]
						ix = ix1 + len(argument_value)
					else:
						ix = ix2
				m_line = m_line.replace('}}', '}')

				if c_prefix.startswith('~') and last_indent != m_indent:
					# if a non-'~' line got removed due to impossible version constraints, do not attempt to attach this line to it
					c_prefix = c_prefix[1:]
				last_indent = m_indent
				#print(c_prefix + '\t' * (indent + m_indent + 1) + m_line, file = sys.stderr)
				new_lines.append((m_line_number, c_prefix + '\t' * (indent + m_indent + 1) + m_line))
			lines[:0] = new_lines
			continue

		prop = None
		entity = None
		version = None

		if len(CHAIN) == 0 or len(CHAIN) == 1 and is_variant:
			# universe context
			if entity_type not in {'U', 'O', 'F', 'C', 'B', 'N', 'S', 'Z', 'I', 'T', '*'}:
				print(line_number, "Invalid entity type", file = sys.stderr)
				exit(1)

			if entity_type == 'O':
				assert not is_variant
				entity = JSObject()
				entity.versions[-1].line_number = line_number
				name = get_identifier(tokens.pop(0))
				UNIVERSE[name] = entity
				entity.label = name
			elif entity_type == 'T':
				assert not is_variant
				entity = JSTypeLayout()
				name = get_identifier(tokens.pop(0))
				UNIVERSE[name] = entity
				entity.label = name
			elif entity_type == 'F' or entity_type == 'C':
				if is_variant:
					version = Version.Function()
					version.line_number = line_number
				else:
					if entity_type == 'C':
						entity = JSConstructor()
					else:
						entity = JSFunction()
					entity.versions[-1].line_number = line_number

				name = get_identifier(tokens.pop(0))
				if not is_variant:
					UNIVERSE[name] = entity
					entity.label = name

				if len(tokens) > 0 and tokens[0] == '=':
					tokens.pop(0)
					function_name = get_string(tokens.pop(0))
				else:
					function_name = name

				if len(tokens) > 0 and tokens[0] == '/':
					tokens.pop(0)
					length = get_number(tokens.pop(0))
				else:
					print(line_number, "Expected parameter count for function", file = sys.stderr)
					exit(1)

				if not is_variant:
					entity.slots.append(JSDataProperty(name = "length", value = length, W = False, E = False, C = True))
					entity.slots.append(JSDataProperty(name = "name", value = function_name, W = False, E = False, C = True))
			elif entity_type == '*':
				name = get_identifier(tokens.pop(0))
				assert len(tokens) > 0 and tokens[0] == ':='
				tokens.pop(0)
				if len(tokens) > 0 and is_string(tokens[0]):
					code = get_string(tokens.pop(0))
				else:
					code = get_access(tokens)

				if is_variant:
					version = Version.Object()
					version.line_number = line_number
					version.code = code
				else:
					entity = JSAlias(code)
					entity.versions[-1].line_number = line_number
					UNIVERSE[name] = entity
					entity.label = name
			else:
				assert False

			if is_variant:
				# only for alias
				process_prefix(version, prefix)
				CHAIN[-1].add_version(version)
			else:
				process_prefix(entity.versions[0], prefix)
				process_prefix(entity, prefix)

		else:
			# object context
			if entity_type not in {'U', 'O', 'F', 'C', 'B', 'N', 'S', 'Z', 'I', '*', 'A', '>', ':', '='}:
				print(line_number, "Invalid entity type", file = sys.stderr)
				exit(1)

			if entity_type == '>':
				assert not is_variant
				if not isinstance(CHAIN[-1], JSFunction):
					print(line_number, "Invalid instance layout specification for non-function", file = sys.stderr)
					exit(1)
				entity = JSObject()
				entity.versions[-1].line_number = line_number
				entity.versions[-1].ObjectType = CHAIN[-1] #.ReturnType
				entity.versions[-1].Prototype = None
				CHAIN[-1].Instance = entity
			elif entity_type == ':':
				assert not is_variant
				if not isinstance(CHAIN[-1], JSFunction):
					print(line_number, "Invalid C++ class name specification for non-function", file = sys.stderr)
					exit(1)
				CHAIN[-1].CppType = get_string(tokens.pop(0))
			elif entity_type == '=':
				assert not is_variant
				if not isinstance(CHAIN[-1], JSConstructor):
					print(line_number, "Invalid C++ class name specification for non-constructor", file = sys.stderr)
					exit(1)
				CHAIN[-1].CppClassType = get_string(tokens.pop(0))
				if len(tokens) > 0 and tokens[0] == '(':
					tokens.pop(0)
					while len(tokens) > 0 and tokens[0] != ')':
						CHAIN[-1].CppClassArguments.append(get_value(tokens))
						if len(tokens) > 0 and tokens[0] == ',':
							tokens.pop(0)
					assert tokens.pop(0) == ')'
			elif entity_type == 'A':
				assert not is_variant
				prop = JSAccessorProperty()
				CHAIN[-1].slots.append(prop)
				while len(tokens) > 0 and tokens[0] in {'+', '-'}:
					value = tokens.pop(0)
					name = tokens.pop(0)
					assert name in {'E', 'C', 'I', 'F'}
					prop.flags[name] = value == '+'

				name = get_name(tokens)
				if is_string(name):
					prop.name = get_string(name)
				else:
					assert type(name) is not str
					prop.name = name
			else:
				prop = JSDataProperty()
				if is_variant:
					CHAIN[-2].slots.append(prop)
				else:
					CHAIN[-1].slots.append(prop)

				while len(tokens) > 0 and tokens[0] in {'+', '-'}:
					value = tokens.pop(0)
					name = tokens.pop(0)
					assert name in {'W', 'E', 'C', 'X', 'I', 'F'}
					prop.flags[name] = value == '+'

				is_universe_entity = False

				name = get_name(tokens)
				if is_identifier(name):
					if not name.startswith('[') and not type(CHAIN[-1]) is JSAccessorProperty:
						is_universe_entity = True
					prop.name = get_identifier(name)
				elif is_string(name):
					prop.name = get_string(name)
				else:
					assert type(name) is not str
					prop.name = name

				if entity_type == 'O':
					if len(tokens) > 0 and tokens[0] == ':=':
						assert not is_variant
						tokens.pop(0)
						entity = get_access(tokens)
						if isinstance(entity, JSIdentifier):
							if entity.name == 'null':
								entity = null
							elif entity.name == 'undefined':
								entity = undefined
					else:
						if is_variant:
							version = Version.Object()
							version.line_number = line_number
						else:
							entity = JSObject()
							entity.versions[-1].line_number = line_number
				elif entity_type == 'F' or entity_type == 'C':
					if len(tokens) > 0 and tokens[0] == ':=':
						assert not is_variant
						tokens.pop(0)
						entity = get_access(tokens)
						if isinstance(entity, JSIdentifier):
							if entity.name == 'null':
								entity = null
					else:
						if is_variant:
							version = Version.Function()
							version.line_number = line_number
						else:
							if entity_type == 'C':
								entity = JSConstructor()
							else:
								entity = JSFunction()
							entity.versions[-1].line_number = line_number

						if len(tokens) > 0 and tokens[0] == '=':
							tokens.pop(0)
							name = get_string(tokens.pop(0))
						else:
							if isinstance(CHAIN[-1], JSAccessorProperty):
								prop_name = CHAIN[-1].name
								if type(prop_name) is not str:
									print(line_number, "Name not given to symbol keyed accessor function", file = sys.stderr)
									exit(1)

								if prop.name == 'get':
									name = 'get ' + prop_name
								elif prop.name == 'set':
									name = 'set ' + prop_name
								else:
									print(line_number, "Invalid accessor member", file = sys.stderr)
									exit(1)
							else:
								name = prop.name

						if len(tokens) > 0 and tokens[0] == '/':
							tokens.pop(0)
							length = tokens.pop(0)
						elif isinstance(CHAIN[-1], JSAccessorProperty):
							if prop.name == 'get':
								length = 0.0
							elif prop.name == 'set':
								length = 1.0
							else:
								print(line_number, "Invalid accessor member", file = sys.stderr)
								exit(1)
						else:
							print(line_number, "Expected parameter count for function", file = sys.stderr)
							exit(1)
						if not is_variant:
							entity.slots.append(JSDataProperty(name = "length", value = float(length), W = False, E = False, C = True))
							entity.slots.append(JSDataProperty(name = "name", value = name, W = False, E = False, C = True))
				elif entity_type == 'U':
					assert not is_variant
					entity = undefined
				elif entity_type == 'B':
					assert not is_variant
					if len(tokens) > 0 and tokens[0] == ':=':
						tokens.pop(0)
						value = tokens.pop(0)
						if value.upper() == 'TRUE':
							entity = True
						elif value.upper() == 'FALSE':
							entity = False
						else:
							print(line_number, "Invalid boolean constant", file = sys.stderr)
							exit(1)
					else:
						entity = JSUninitialized('boolean')
				elif entity_type == 'N':
					assert not is_variant
					if len(tokens) > 0 and tokens[0] == ':=':
						tokens.pop(0)
						value = tokens.pop(0)
						if value == '-' and len(tokens) > 0 and tokens[0] == 'Infinity':
							tokens.pop(0)
							entity = -math.inf
						elif value == 'Infinity':
							entity = math.inf
						elif value == 'NaN':
							entity = math.nan
						else:
							entity = float(value)
					else:
						entity = JSUninitialized('number')
				elif entity_type == 'S':
					assert not is_variant
					if len(tokens) > 0 and tokens[0] == ':=':
						tokens.pop(0)
						if tokens[0] == "null":
							tokens.pop(0)
							entity = null
						else:
							entity = get_string(tokens.pop(0))
					else:
						entity = JSUninitialized('string')
				elif entity_type == 'I':
					assert not is_variant
					if len(tokens) > 0 and tokens[0] == ':=':
						tokens.pop(0)
						entity = int(tokens.pop(0))
					else:
						entity = JSUninitialized('bigint')
				elif entity_type == 'Z':
					assert not is_variant
					if len(tokens) > 0 and tokens[0] == ':=':
						assert False # TODO: parse assignment
					else:
						name = prop.name
						if name.startswith('[['):
							entity = JSUninitialized('symbol') # an internal slot, should not be initialized
						else:
							entity = JSSymbol("Symbol." + name)
				elif entity_type == '*':
					assert not is_variant
					if len(tokens) > 0 and tokens[0] == ':=':
						tokens.pop(0)
						entity = JSNative(get_string(tokens.pop(0)))
					else:
						entity = JSUninitialized('native')
				else:
					print(entity_type, file = sys.stderr)
					assert False

				if is_variant:
					CHAIN[-1].add_version(version)
					prop.value = CHAIN[-1]
				else:
					prop.value = entity
					if is_universe_entity:
						assert type(prop.name) is str
						UNIVERSE[prop.name] = entity
						entity.label = prop.name

			if prop is not None:
				process_prefix(prop, prefix)

		if entity_type == 'A':
			assert not is_variant
			CHAIN.append(prop)
		elif len(CHAIN) == 0 and entity_type == '*':
			# alias
			CHAIN.append(entity)
		elif entity_type == '>' or entity_type == 'T':
			assert not is_variant
			CHAIN.append(entity)
		elif entity_type in {'O', 'F', 'C'}:
			if is_variant:
				entity = CHAIN[-1]
				if prop is not None:
					version.introduced = prop.introduced
					version.removed = prop.removed
					version.extensions = prop.extensions.copy()
					version.ecmascript4 = prop.ecmascript4
			else:
				if isinstance(entity, JSObject):
					ENTITIES.add(entity)
					if prop is not None:
						# let entity inherit introduction of property if not specified separately
						# (it can be specified later, in which case this value will be overwritten)
						if entity.versions[0].introduced is None:
							entity.introduced = entity.versions[0].introduced = prop.introduced
						if entity.versions[0].removed is None:
							entity.removed = entity.versions[0].removed = prop.removed
						if len(entity.versions[0].extensions) == 0:
							entity.versions[0].extensions = prop.extensions.copy()
							entity.extensions = prop.extensions.copy()
						if entity.versions[0].ecmascript4 == JSObject.ES4_DEFAULT:
							entity.ecmascript4 = entity.versions[0].ecmascript4 = prop.ecmascript4

			if not is_variant:
				CHAIN.append(entity)
			if len(tokens) > 0 and tokens[0] == ':':
				# prototype
				tokens.pop(0)
				entity.versions[-1].Prototype = get_access(tokens)
			if len(tokens) > 0 and tokens[0] == '<:':
				if entity_type != 'C':
					print(line_number, "Superclass declaration for non-class", file = sys.stderr)
				tokens.pop(0)
				entity.Super = get_access(tokens)
				if isinstance(entity.Super, JSIdentifier) and entity.Super.name == 'null':
					entity.Super = None
			if len(tokens) > 0 and tokens[0] in {'+', '-'}:
				value = tokens.pop(0)
				name = tokens.pop(0)
				assert name == 'E'
				entity.flags[name] = value == '+'
			if len(tokens) > 0 and tokens[0] == '{':
				#if entity_type in {'F', 'C'}:
				#	tokens.pop(0)
				#	entity.ReturnType = get_access(tokens)
				#else:
				tokens.pop(0)
				entity.versions[-1].ObjectType = get_access(tokens)
				if tokens[0] == '(':
					tokens.pop(0)
					entity.versions[-1].ObjectTypeConstructorArguments = get_string(tokens.pop(0))
					assert tokens.pop(0) == ')'
				assert tokens.pop(0) == '}'
			if len(tokens) > 0 and tokens[0] == '=:':
				tokens.pop(0)
				name = tokens.pop(0)
				UNIVERSE[name] = entity
				entity.label = name
			#print(tokens)

		if len(tokens) > 0 and tokens[0] == '<':
			if prop is not None:
				target = prop
			elif type(entity) is JSAlias:
				target = entity
			elif is_variant and isinstance(CHAIN[-1], JSAlias):
				# make up a fake JSAlias to appease the code (TODO: very unpythonic, very bad pattern)
				target = JSAlias("")
			else:
				print(line_number, "Invalid type information on non-property", file = sys.stderr)
				exit(1)
			tokens.pop(0)
			target.declared_type = get_access(tokens)
			if prop is not None:
				if type(prop.value) in {int, float} and type(prop.declared_type) is JSIdentifier and prop.declared_type.name != 'Number':
					prop.value = JSTypedValue(prop.declared_type.name, prop.value)
				elif isinstance(prop.value, JSObject):
					prop.value.declared_type = prop.declared_type.name
			assert tokens.pop(0) == '>'

		if len(tokens) != 0:
			print(line_number, "Superfluous tokens on line:", tokens, file = sys.stderr)
			exit(1)

		if prop is None and isinstance(entity, JSAlias) and entity.declared_type is None:
			print(line_number, "Missing type declaration for intrinsic object:", tokens, file = sys.stderr)
			exit(1)

		#print(entity)

def get_symbol_description(symbol):
	assert type(symbol.description) is JSMember
	assert type(symbol.description.record) is JSIdentifier
	assert symbol.description.record.name == "Symbol"
	assert type(symbol.description.member) is str
	return symbol.description.member

def name_sort_order(name):
	if type(name) is JSSymbol:
		# sorts after all other elements alphabetically
		return '^' + get_symbol_description(name).upper()
	elif type(name) is JSQName:
		# sort after unqualified name but before other names
		return name.name.upper() + " " + name.ns.upper()
	else:
		return name.upper()

for entity in ENTITIES:
	entity.slots = sorted(entity.slots, key = lambda slot: name_sort_order(slot.name))

def display_layout(value, indent = ''):
	if isinstance(value, JSObject):
		for slot in value.slots:
			print(indent + repr(slot))
			if isinstance(slot, JSDataProperty):
				display_layout(slot.value, indent + '\t')
			elif isinstance(slot, JSAccessorProperty):
				display_layout(slot, indent + '\t')
		if isinstance(value, JSFunction) and value.Instance is not None:
			print(indent + "Instance layout")
			display_layout(value.Instance, indent + '\t')
	elif isinstance(value, JSAccessorProperty):
		for slot in value.slots:
			print(indent + repr(slot))
			if isinstance(slot, JSDataProperty):
				display_layout(slot.value, indent + '\t')
			elif isinstance(slot, JSAccessorProperty):
				display_layout(slot, indent + '\t')

#for name, value in UNIVERSE.items():
#	print(name, value)
#	display_layout(value, '\t')

# create a C++ reference and obtain a Python object description for the Member or Identifier value
def lookup_entity(value):
	if type(value) is JSMember:
		record = lookup_entity(value.record)
		#print(record, value.member, record[1].slots, file = sys.stderr)
		for slot in record[1].slots:
			if slot.name == value.member:
				assert type(slot) is JSDataProperty
				if isinstance(slot.value, JSObject) and slot.value.identifier is not None:
					reference = slot.value.identifier
				elif isinstance(slot.value, JSMember) or isinstance(slot.value, JSIdentifier):
					#print(slot.value, file = sys.stderr)
					reference = lookup_entity(slot.value)[0]
				else:
					reference = f"JS::Get({record[0]}, u\"{access_entity(slot.name)}\")"

				value = slot.value
				if isinstance(value, JSMember) or isinstance(value, JSIdentifier):
					value = lookup_entity(value)[1]
				return (reference, value)
		print(value, record[1], record[1].slots)
		assert False
	else:
		if type(value) is JSIdentifier:
			value = value.name
		assert type(value) is str
		if value == 'host_defined':
			return ('host_defined', None)
		elif value == 'null':
			return ('nullptr', None)
		elif value == 'undefined':
			return ('JS_undefined', None)
		assert value in UNIVERSE
		return (value, UNIVERSE[value])

# convert to Python value (representing a JavaScript value) to a C++ expression
# no_value_cast is set to False if the result is expected to be passed as a JSValue argument, otherwise it is the pure type
def access_entity(value, no_value_cast = False):
	if isinstance(value, JSObject):
		if value.usage_count >= 2 or value.identifier in UNIVERSE:
			return value.identifier
		else:
			return create_entity(value)
	elif type(value) is float:
		if math.isnan(value):
			return "NAN"
		elif math.isinf(value):
			if value < 0:
				return "-INFINITY"
			else:
				return "INFINITY"
		else:
			return str(value)
	elif type(value) is int:
		return str(value) + "_mpz"
	elif type(value) is str:
		return 'u"' + value + '"'
	elif type(value) is bool:
		return "true" if value else "false"
	elif type(value) is JSMember or type(value) is JSIdentifier:
		#print(value, file = sys.stderr)
		return lookup_entity(value)[0]
	elif type(value) is JSSymbol:
		if type(value.description) is str:
			assert value.description.startswith("Symbol.")
			return "Symbols::" + value.description[len("Symbol."):]
		else:
			assert type(value.description) is JSMember
			assert type(value.description.record) is JSIdentifier
			assert value.description.record.name == "Symbol"
			assert type(value.description.member) is str
			return "Symbols::" + value.description.member
	elif type(value) is JSUndefined:
		return "JS_undefined"
	elif type(value) is JSNull:
		return "JS_null"
	elif type(value) is JSUninitialized and value.value_type == 'native':
		return "JS_undefined /* TODO */" # TODO: currently used for ES4 compound attributes
	elif type(value) is JSNative:
		return value.code
	elif type(value) is JSAlias:
		if type(value.code) is str:
			return value.code
		else:
			return access_entity(value.code)
	else:
		if type(value) is JSTypedValue:
			if value.typename == 'float':
				assert not no_value_cast
				return "JSValue(JSFloat(" + access_entity(value.value) + "))"
			elif value.typename == 'long':
				assert not no_value_cast
				if value.value == -0x8000000000000000:
					return "JSValue(JSLong(INT64_C(" + str(value.value + 1) + ") - 1))"
				else:
					return "JSValue(JSLong(INT64_C(" + str(value.value) + ")))"
			elif value.typename == 'ulong':
				assert not no_value_cast
				return "JSValue(JSUlong(UINT64_C(" + str(value.value) + ")))"
			elif value.typename == 'int':
				assert no_value_cast
				return str(int(value.value))
		print(type(value), value, file = sys.stderr)
		assert False

for name, entity in list(UNIVERSE.items()):
	if isinstance(entity, JSObject):
		for slot in entity.slots:
			if slot.name == "prototype" and isinstance(slot, JSDataProperty):
				if isinstance(slot.value, JSObject) and slot.value.label is None:
					# we do not handle member access and identifier cases here
					slot.value.label = name + ".prototype"
					UNIVERSE[name + "_prototype"] = slot.value

def version_sort_order(thing):
	if thing.ecmascript4 == JSObject.ES4_EXCLUSIVE:
		version = 4
	elif thing.introduced is None:
		version = 1
	else:
		version = thing.introduced
	if 'W' in thing.extensions:
		version += 0.5
	if 'X' in thing.extensions:
		version += 0.25
	return version

intrinsic_entity_list = sorted(UNIVERSE.items(),
	key = lambda slot: (version_sort_order(slot[1]), name_sort_order(slot[0])))

# assign C++ identifiers to objects
entities_assigned_already = set()
entities_to_assign = intrinsic_entity_list + [(prop.name, prop.value) for prop in UNIVERSE["global"].slots]
while len(entities_to_assign) > 0:
	name, entity = entities_to_assign.pop(0)
	if isinstance(entity, JSTypeLayout):
		continue
	if entity in entities_assigned_already:
		continue
	if isinstance(entity, JSObject):
		if entity.identifier is not None:
			continue # already had a shorter name

		entity.identifier = name

		if name in UNIVERSE or len(entity.slots) > 0:
			entities_assigned_already.add(entity)
			for prop in entity.slots:
				prop_name = prop.name
				if type(prop_name) is JSSymbol:
					prop_name = get_symbol_description(prop_name)
				elif type(prop_name) is JSQName:
					prop_name = prop_name.name
				elif type(prop_name) is str:
					entity.usage_count += 1
					if isinstance(entity, JSFunction) and prop_name in {u"name", u"length"}:
						entity.usage_count -= 1 # property created in same line as object
					if isinstance(entity, JSConstructor) and prop_name in {u"prototype"}:
						entity.usage_count -= 1 # property created in same line as object

				if type(prop) is JSDataProperty:
					prop_value = prop.value
					if isinstance(prop_value, JSMember) or isinstance(prop_value, JSIdentifier):
						prop_value = lookup_entity(prop_value)[1]

					if prop_name in UNIVERSE:
						entities_to_assign.append((prop_name, prop_value))
					else:
						entities_to_assign.append((name + '_' + prop_name, prop_value))
					if isinstance(prop_value, JSObject):
						prop_value.usage_count += 1
				elif type(prop) is JSAccessorProperty:
					entities_to_assign.append((name + '_' + prop_name, prop))
				else:
					assert False

		if isinstance(entity, JSFunction) and entity.Instance is not None:
			for slot in entity.Instance.slots:
				if not slot.flags.get('I'):
					continue
				#print("!", entity.identifier, slot.name, file = sys.stderr)
				if isinstance(slot, JSAccessorProperty):
					if isinstance(slot.name, JSQName):
						assert type(slot.name.ns) is str
						slot_name = slot.name.ns + "_" + slot.name.name
					else:
						slot_name = slot.name
					for accessor in slot.slots:
						accessor.value.identifier = f"{entity.identifier}_{slot_name}_{accessor.name}"

	elif isinstance(entity, JSAccessorProperty):
		for prop in entity.slots:
			assert prop.name in {'get', 'set'}
			entities_to_assign.append((name + '_' + prop.name, prop.value))
			if isinstance(prop.value, JSObject):
				prop.value.usage_count += 1
	elif isinstance(entity, JSAlias):
		entity.identifier = name

# returns a built-in class object for ECMAScript 4
def get_class_of(value):
	if isinstance(value, JSConstructor):
		return lookup_entity("Class")[1]
	elif isinstance(value, JSFunction):
		return lookup_entity("Function")[1]
	elif isinstance(value, JSObject):
		return lookup_entity(value.versions[0].ObjectType)[1] # TODO: other versions
	elif type(value) is float:
		# TODO: or float
		return lookup_entity("Number")[1]
	elif type(value) is int:
		# TODO: or ulong
		return lookup_entity("long")[1]
	elif type(value) is str:
		return lookup_entity("String")[1]
	elif type(value) is bool:
		return lookup_entity("Boolean")[1]
	elif type(value) is JSUndefined:
		return lookup_entity("Void")[1]
	else:
		print(type(value), value, file = sys.stderr)
		assert False

def call_new(CppType, Prototype, Class, arguments):
	arguments = list(arguments)

	if Class is None:
		if Prototype is None:
			call = 'New'
		else:
			call = 'NewWithPrototype'
	else:
		if Prototype is None:
			call = 'NewWithClass'
		else:
			call = 'NewWithClassAndPrototype'

	if Prototype is not None:
		arguments.insert(0, Prototype)
	if Class is not None:
		arguments.insert(0, Class)

	return f"JS::{call}<{CppType}>({', '.join(argument for argument in arguments)})"

# creates the C++ expression that will create an instance (not yet populated)
def create_entity(value, version = None, ecmascript4 = False):
	if isinstance(value, JSObject):
		if version is None:
			assert len(value.versions) == 1
			version = value.versions[0]

		prototype_reference, prototype = lookup_entity(version.Prototype)
		if prototype_reference == 'nullptr':
			prototype_reference = "std::optional<JSValue>{}" # this is what C++ needs here, otherwise the prototype will become the literal null value
		default_prototype = False

		if isinstance(prototype, JSFunction) and prototype.label != 'Function.prototype':
			prototype_reference = f"{prototype_reference}.cast_object<JS::Class::Object>()"

		arguments = []
		for slot in value.slots:
			if type(slot.name) is str and slot.name.startswith('[['):
				arguments.append(access_entity(slot.value))

		if ecmascript4 and value.label is not None and value.label.endswith(".prototype"):
			# ECMAScript 4 prototype objects are all of type Object rather than some other type as in other versions
			default_prototype = prototype == lookup_entity("Object_prototype")[1]

			cpp_type = lookup_entity("Object")[1]
			assert cpp_type.CppType is not None
			if cpp_type.label == 'Object':
				arguments.clear() # do not generate arguments for an Object (this can result with Boolean/Number/String in ES4 mode)
			#if default_prototype:
			#	obj = f"JS::New<{cpp_type.CppType}>({', '.join(argument for argument in arguments)})"
			#else:
			#	obj = f"JS::NewWithPrototype<{cpp_type.CppType}>({prototype_reference}{''.join(', ' + argument for argument in arguments)})"
			class_reference = None
			if value.declared_type is not None:
				class_reference, class_entity = lookup_entity(value.declared_type)
				#print(cpp_type, class_entity, file = sys.stderr)
				if class_entity is cpp_type:
					class_reference = None
				else:
					class_reference += "_class"
			obj = call_new(cpp_type.CppType, None if default_prototype else prototype_reference, class_reference, arguments)
		else:
			if version.ObjectType == 'BuiltinFunction':
				default_prototype = prototype_reference == "Function_prototype" # TODO: check for function object
			elif version.ObjectType in UNIVERSE:
				fallback_prototype = None
				for slot in UNIVERSE[version.ObjectType].slots:
					if slot.name == "prototype":
						fallback_prototype = slot.value
						break
				default_prototype = prototype is fallback_prototype

			if version.ObjectType == 'BuiltinFunction' or type(version.ObjectType) is JSIdentifier and version.ObjectType.name == 'BuiltinFunction':
				name = ""
				length = 0
				prototype = None
				for slot in value.slots:
					if slot.name == "name":
						#print(value.identifier)
						name = slot.value
						assert type(name) is str
					elif slot.name == "length":
						length = slot.value
						assert type(length) is float
						length = int(length)
					elif slot.name == "prototype":
						prototype = access_entity(slot.value)
						assert type(prototype) is str

				if prototype is None:
					prototype = "{}"

				if isinstance(value, JSConstructor) and ecmascript4:
					assert value.CppClassType is not None
					obj = f"JS::New<{value.CppClassType}>({''.join(access_entity(argument, no_value_cast = True) + ', ' for argument in value.CppClassArguments)}&realm)"
				else:
					if isinstance(value, JSConstructor):
						creator = 'CreateBuiltinConstructor'
					else:
						creator = 'CreateBuiltinFunction'

					if default_prototype:
						obj = f"JS::{creator}(JS::Runtime::Builtin::{value.identifier}_Call, {length}, u\"{name}\", {prototype}, &realm)"
					else:
						obj = f"JS::{creator}(JS::Runtime::Builtin::{value.identifier}_Call, {length}, u\"{name}\", {prototype}, &realm, {prototype_reference})"
			elif version.ObjectTypeConstructorArguments is not None:
				cpp_type = lookup_entity(version.ObjectType)[1]
				assert cpp_type.CppType is not None
				obj = f"JSFunction::MakeFunction(JS::New<{cpp_type.CppType}>({version.ObjectTypeConstructorArguments}))"
			else:
				cpp_type = lookup_entity(version.ObjectType)[1]
				assert cpp_type.CppType is not None
				if cpp_type.label == 'Object':
					arguments.clear() # do not generate arguments for an Object (this can result with Boolean/Number/String in ES4 mode)
				#if default_prototype:
				#	obj = f"JS::New<{cpp_type.CppType}>({', '.join(argument for argument in arguments)})"
				#else:
				#	obj = f"JS::NewWithPrototype<{cpp_type.CppType}>({prototype_reference}{''.join(', ' + argument for argument in arguments)})"
				class_reference = None
				if ecmascript4 and value.declared_type is not None:
					class_reference, class_entity = lookup_entity(value.declared_type)
					#print(cpp_type.label, class_entity.label, class_entity is cpp_type, class_reference, file = sys.stderr)
					if class_entity is cpp_type:
						class_reference = None
					else:
						class_reference += "_class"
				obj = call_new(cpp_type.CppType, None if default_prototype else prototype_reference, class_reference, arguments)

		return obj
	elif isinstance(value, JSAlias):
		if version is None:
			assert len(value.versions) == 1
			version = value.versions[0]

		if type(version.code) is str:
			return version.code
		else:
			return access_entity(version.code)
	else:
		assert False

FAKE_INFINITY = 9999 # random large number, TODO: change if ECMAScript 9999 ever gets published (probably not until the year 12008)
CURRENT_CONDITION = {
	"introduced": 1,
	"removed": FAKE_INFINITY,
	"extensions": set(),
	"ecmascript4": JSObject.ES4_DEFAULT
}

# checks if the set of conditions requires introducing an if/then block
def needs_if_clause(**condition):
	if condition["introduced"] == 1 \
	and condition["removed"] == FAKE_INFINITY \
	and condition["ecmascript4"] == JSObject.ES4_DEFAULT:
		for key in condition["extensions"]:
			if key not in {'d', 'D'}:
				return True
		else:
			return False
	else:
		return True

# if the current condition is not the default, terminate it
def close_condition():
	global CURRENT_CONDITION

	if needs_if_clause(**CURRENT_CONDITION):
		print("\t}")
	if CURRENT_CONDITION["ecmascript4"] == JSObject.ES4_EXCLUSIVE:
		print("#endif")
	if 'D' in CURRENT_CONDITION["extensions"]:
		print("#endif")
	if 'd' in CURRENT_CONDITION["extensions"]:
		print("#endif")

	CURRENT_CONDITION["introduced"] = 1
	CURRENT_CONDITION["removed"] = FAKE_INFINITY
	CURRENT_CONDITION["extensions"].clear()
	CURRENT_CONDITION["ecmascript4"] = JSObject.ES4_DEFAULT

def is_ecmascript4_allowed(**condition):
	if condition.get("introduced") is None:
		condition["introduced"] = 1
	if condition.get("removed") is None:
		condition["removed"] = FAKE_INFINITY # CURRENT_CONDITION["removed"]
	if condition.get("extensions") is None:
		condition["extensions"] = set()
	elif type(condition["extensions"]) is not set:
		condition["extensions"] = set(condition["extensions"])
	if condition.get("ecmascript4") is None:
		condition["ecmascript4"] = JSObject.ES4_DEFAULT

	if condition["ecmascript4"] == JSObject.ES4_DEFAULT and not (condition["introduced"] < 4 < condition["removed"]):
		condition["ecmascript4"] = JSObject.ES4_UNAVAILABLE

	if condition["ecmascript4"] == JSObject.ES4_UNAVAILABLE:
		return False
	else:
		return True

def is_non_ecmascript4_allowed(**condition):
	if condition.get("introduced") is None:
		condition["introduced"] = 1
	if condition.get("removed") is None:
		condition["removed"] = FAKE_INFINITY # CURRENT_CONDITION["removed"]
	if condition.get("extensions") is None:
		condition["extensions"] = set()
	elif type(condition["extensions"]) is not set:
		condition["extensions"] = set(condition["extensions"])
	if condition.get("ecmascript4") is None:
		condition["ecmascript4"] = JSObject.ES4_DEFAULT

	if condition["ecmascript4"] == JSObject.ES4_DEFAULT and not (condition["introduced"] < 4 < condition["removed"]):
		condition["ecmascript4"] = JSObject.ES4_UNAVAILABLE

	if condition["ecmascript4"] == JSObject.ES4_EXCLUSIVE:
		return False
	else:
		return True

# prepares the code to be in the specific condition
def setup_condition(**condition):
	global CURRENT_CONDITION

	for key in condition:
		assert key in {"introduced", "removed", "extensions", "ecmascript4"}

	if condition.get("introduced") is None:
		condition["introduced"] = 1 # CURRENT_CONDITION["introduced"]
	if condition.get("removed") is None:
		condition["removed"] = FAKE_INFINITY # CURRENT_CONDITION["removed"]
	if condition.get("extensions") is None:
		condition["extensions"] = CURRENT_CONDITION["extensions"]
	elif type(condition["extensions"]) is not set:
		condition["extensions"] = set(condition["extensions"])
	if condition.get("ecmascript4") is None:
		if condition["introduced"] < 4 < condition["removed"]:
			condition["ecmascript4"] = JSObject.ES4_DEFAULT
		else:
			condition["ecmascript4"] = JSObject.ES4_UNAVAILABLE

#	if condition["introduced"] == 1 and condition["removed"] == FAKE_INFINITY and len(condition["extensions"]) == 0 and condition["ecmascript4"] == JSObject.ES4_DEFAULT:
#		close_condition()
#	else:
	if CURRENT_CONDITION != condition:
		close_condition()
		if needs_if_clause(**condition):
			predicates = []
			if condition["introduced"] != 1:
				predicates.append(f"JS::Version::ES{condition['introduced']} <= JS::CurrentVersion")
			if condition["removed"] != FAKE_INFINITY:
				predicates.append(f"JS::CurrentVersion < JS::Version::ES{condition['removed']}")
			if 'W' in condition["extensions"]:
				predicates.append("JS::Supports_WebExtensions()")
			if 'X' in condition["extensions"]:
				predicates.append("JS::Supports_E4X()")

			predicates = " && ".join(predicates)

			if condition["ecmascript4"] == JSObject.ES4_UNAVAILABLE and condition["introduced"] < 4 < condition["removed"]:
				if predicates == "":
					print("#ifdef ENABLE_ES4")
					print("\tif(!IsVersionES4())")
					print("#endif")
				else:
					print("#ifdef ENABLE_ES4")
					print(f"\tif({predicates} && !IsVersionES4())")
					print("#else")
					print(f"\tif({predicates})")
					print("#endif")
			elif condition["ecmascript4"] == JSObject.ES4_EXCLUSIVE:
				print("#ifdef ENABLE_ES4")
				print("\tif(IsVersionES4())")
			elif condition["ecmascript4"] == JSObject.ES4_ENABLED:
				print("#ifdef ENABLE_ES4")
				print(f"\tif(({predicates}) || IsVersionES4())")
				print("#else")
				print(f"\tif({predicates})")
				print("#endif")
			else:
				print(f"\tif({predicates})")

			print("\t{")

		if 'd' in condition["extensions"]:
			print("#if ENABLE_DEBUG")
		if 'D' in condition["extensions"]:
			print("#if ENABLE_DEC128")

		for key, value in condition.items():
			CURRENT_CONDITION[key] = value

# prints line with the specific conditions in place
def print_line(line, **condition):
	global CURRENT_CONDITION

	setup_condition(**condition)

	#if CURRENT_CONDITION["introduced"] == 1 and CURRENT_CONDITION["removed"] == FAKE_INFINITY and len(CURRENT_CONDITION["extensions"]) == 0 and CURRENT_CONDITION["ecmascript4"] == JSObject.ES4_DEFAULT:
	if not needs_if_clause(**CURRENT_CONDITION):
		print(f"\t{line}")
	else:
		print(f"\t\t{line}")

# prints a C++ line that creates and stores this object in its specified variable
def define_entity(value):
	for version in value.versions:
		if value.usage_count >= 2 or value.identifier in UNIVERSE:
			#print(f"// {version.introduced} {version.removed} {version.extensions} {version.ecmascript4}")
			for ecmascript4_allowed in [False, True]:
				if isinstance(value, JSConstructor):
					separate_ecmascript4_variant = True
				elif value.label is not None and value.label.endswith(".prototype"):
					# ES4 makes all prototypes of layout Object
					separate_ecmascript4_variant = lookup_entity(version.ObjectType)[1] != lookup_entity("Object")[1] and lookup_entity(version.ObjectType)[1] != lookup_entity("ImmutablePrototypeObject")[1]
				else:
					separate_ecmascript4_variant = False

				if separate_ecmascript4_variant:
					if ecmascript4_allowed and not is_ecmascript4_allowed(introduced = version.introduced, removed = version.removed, extensions = version.extensions, ecmascript4 = version.ecmascript4):
						continue
					elif not ecmascript4_allowed and not is_non_ecmascript4_allowed(introduced = version.introduced, removed = version.removed, extensions = version.extensions, ecmascript4 = version.ecmascript4):
						continue

					if ecmascript4_allowed:
						ecmascript4 = JSObject.ES4_EXCLUSIVE
					else:
						ecmascript4 = JSObject.ES4_UNAVAILABLE
				else:
					if ecmascript4_allowed:
						continue
					ecmascript4 = version.ecmascript4
					# enable if allowed
					#ecmascript4_allowed = is_ecmascript4_allowed(introduced = version.introduced, removed = version.removed, extensions = version.extensions, ecmascript4 = version.ecmascript4)
					ecmascript4_allowed = version.ecmascript4 == JSObject.ES4_EXCLUSIVE

				obj = create_entity(value, version, ecmascript4 = ecmascript4_allowed)

				if isinstance(value, JSConstructor) and ecmascript4_allowed:
					class_identifier = value.identifier + "_class"
					line = f"{class_identifier} = {obj};"
					print_line(line, introduced = version.introduced, removed = version.removed, extensions = version.extensions, ecmascript4 = ecmascript4)
					if is_non_ecmascript4_allowed(introduced = version.introduced, removed = version.removed, extensions = version.extensions, ecmascript4 = version.ecmascript4):
						line = f"{value.identifier} = {class_identifier};"
						print_line(line, introduced = version.introduced, removed = version.removed, extensions = version.extensions, ecmascript4 = ecmascript4)
				else:
					line = f"{value.identifier} = {obj};"
					print_line(line, introduced = version.introduced, removed = version.removed, extensions = version.extensions, ecmascript4 = ecmascript4)

				if isinstance(value, JSObject) and value.label is not None:
					# set up label
					identifier = value.identifier
					if separate_ecmascript4_variant:
						#if is_non_ecmascript4_allowed(introduced = version.introduced, removed = version.removed, extensions = version.extensions, ecmascript4 = version.ecmascript4) and not ecmascript4_allowed:
						#	continue # print code once both branches have been generated
						if isinstance(value, JSConstructor) and ecmascript4_allowed:
							identifier = f"{value.identifier}_class"

					print("#if ENABLE_DEBUG")
					print_line(f"JS::Runtime::Debugger::SetObjectLabel({identifier}, \"{value.label}\");", introduced = version.introduced, removed = version.removed, extensions = version.extensions, ecmascript4 = ecmascript4) #version.ecmascript4)
					print("#endif")

# initializes all properties of this object
def setup_entity(value):
	if isinstance(value, JSObject):
		for slot in sorted(value.slots, key = lambda slot: name_sort_order(slot.name)):
			if type(slot.name) is str and slot.name.startswith('[['):
				continue # internal slot, ignore for now
			elif isinstance(slot.name, JSQName):
				#slot_name = ""
				assert type(slot.name.ns) is str
				#if slot.name.ns == 'internal':
				#	ns = "dynamic_cast<JS::Class::Package *>(global)->InternalNamespace"
				#else:
				ns = slot.name.ns
				slot_key = f"JSQualifiedName{{{ns}, u\"{slot.name.name}\"}}"
			elif isinstance(slot.name, JSSymbol):
				assert type(slot.name.description) is JSMember
				assert type(slot.name.description.record) is JSIdentifier
				assert slot.name.description.record.name == "Symbol"
				assert type(slot.name.description.member) is str
				#slot_name = slot.name.description.member
				slot_key = "Symbols::" + slot.name.description.member
			else:
				if isinstance(value, JSFunction) and slot.name in {u"name", u"length"}:
					continue # creation initialized it
				if isinstance(value, JSConstructor) and slot.name in {u"prototype"}:
					continue # creation initialized it

				#slot_name = slot.name
				slot_key = 'u"' + slot.name + '"'

			if slot.flags.get('I'):
				print("Instance:", slot, file = sys.stderr)
				exit(1)

			#if entity.identifier in UNIVERSE:
			#	print(value.ObjectType)
			#	cpp_type = lookup_entity(value.ObjectType)[1]
			#	assert cpp_type.CppType is not None
			#	reference = f"dynamic_cast<{cpp_type.CppType} *>({name})"
			#else:
			reference = value.identifier
			if isinstance(value, JSConstructor) and \
					not is_non_ecmascript4_allowed(introduced = value.introduced, removed = value.removed, extensions = value.extensions, ecmascript4 = value.ecmascript4):
				reference = f"{reference}_class"

			if isinstance(slot, JSDataProperty):
				entity = slot.value
				if isinstance(entity, JSIdentifier) or isinstance(entity, JSMember):
					entity = lookup_entity(entity)[1]

				expr = access_entity(entity)
				if isinstance(entity, JSConstructor) and \
						not is_non_ecmascript4_allowed(introduced = entity.introduced, removed = entity.removed, extensions = entity.extensions, ecmascript4 = entity.ecmascript4):
					expr = f"{expr}_class"

				with_type = ""
				if slot.declared_type is not None:
					declared_type = lookup_entity(slot.declared_type)[1]
					if declared_type is not None and declared_type.label != 'Object':
						with_type = f" WITH_TYPE({declared_type.identifier}_class)"

				attributes = ""
				if not slot.flags.get('W'):
					attributes += ".WithWritable(false)"
				if not slot.flags.get('E'):
					attributes += ".WithEnumerable(false)"
				if not slot.flags.get('C'):
					attributes += ".WithConfigurable(false)"
				if slot.flags.get('X'):
					attributes += ".WithExplicit(true)"
				# TODO: 'F'
				if attributes != "":
					attributes = f", PropertyAttributes(){attributes}"

				line = f"JS::Define({reference}, {slot_key}{with_type}, {expr}{attributes});"
				print_line(line, introduced = slot.introduced, removed = slot.removed, extensions = slot.extensions, ecmascript4 = slot.ecmascript4)

			elif isinstance(slot, JSAccessorProperty):
				getter = None
				setter = None
				for accessor in slot.slots:
					if accessor.name == 'get':
						getter = accessor.value
					if accessor.name == 'set':
						setter = accessor.value

				if isinstance(getter, JSIdentifier) or isinstance(getter, JSMember):
					getter = lookup_entity(getter)[1]
				if getter is not None:
					getter = access_entity(getter)
				if isinstance(setter, JSIdentifier) or isinstance(setter, JSMember):
					setter = lookup_entity(setter)[1]
				if setter is not None:
					setter = access_entity(setter)

				if setter is None:
					accessor = f"JS::Accessor::Getter({getter})"
				elif getter is None:
					accessor = f"JS::Accessor::Setter({setter})"
				else:
					accessor = f"JS::Accessor({getter}, {setter})"

				attributes = ""
				#if not slot.flags.get('W'):
				#	attributes += ".WithWritable(false)"
				if not slot.flags.get('E'):
					attributes += ".WithEnumerable(false)"
				if not slot.flags.get('C'):
					attributes += ".WithConfigurable(false)"
				if slot.flags.get('X'):
					attributes += ".WithExplicit(true)"
				# TODO: 'F'
				if attributes != "":
					attributes = f", PropertyAttributes(){attributes}"

				line = f"JS::Define({reference}, {slot_key}, {accessor}{attributes});"
				print_line(line, introduced = slot.introduced, removed = slot.removed, extensions = slot.extensions, ecmascript4 = slot.ecmascript4)

# setup class instance members
def setup_entity_instance_properties(value):
	if isinstance(value, JSFunction) and value.Instance is not None:
		for slot in sorted(value.Instance.slots, key = lambda slot: name_sort_order(slot.name)):
			if not slot.flags.get('I'):
				# only generating instance slots
				continue

			#print(value.label, slot.name, file = sys.stderr)
			if isinstance(slot.name, JSQName):
				assert type(slot.name.ns) is str
				#if slot.name.ns == 'internal':
				#	slot_namespace = "dynamic_cast<JS::Class::Package *>(global)->InternalNamespace" # TODO: create local variable to make this simpler
				#else:
				slot_namespace = slot.name.ns
				slot_key = f"u\"{slot.name.name}\""
			else:
				slot_namespace = "JS_public"
				slot_key = 'u"' + slot.name + '"'

			reference = f"{value.identifier}_class"
			#print("!", type(slot), file = sys.stderr)
			#if isinstance(slot, JSAccessorProperty):
			#	print("!", slot.slots, file = sys.stderr)
			for prop in slot.slots if isinstance(slot, JSAccessorProperty) else [slot]:
				#print(slot, file = sys.stderr)
				assert isinstance(prop, JSDataProperty)

				Final = "true" if prop.flags.get('F') else "false"
				Enumerable = "true" if prop.flags.get('E') else "false"

				entity = prop.value
				if isinstance(entity, JSIdentifier) or isinstance(entity, JSMember):
					entity = lookup_entity(entity)[1]

				expr = access_entity(entity)
				if isinstance(entity, JSConstructor) and \
						not is_non_ecmascript4_allowed(introduced = entity.introduced, removed = entity.removed, extensions = entity.extensions, ecmascript4 = entity.ecmascript4):
					expr = f"{expr}_class"

				if type(slot) is JSDataProperty:
					Immutable = "false" if prop.flags.get('W') else "true"
					declared_type = lookup_entity(prop.declared_type)[1]
					if declared_type is not None and declared_type.label != 'Object':
						Type = f"{declared_type.identifier}_class"
					else:
						Type = "Object_class"

					prop_text = f"JS::Runtime::InstanceVariable::Variable({Final}, {Enumerable}, {Immutable}, {Type}, {expr})"
					access = "READ_WRITE_ACCESS"
				else:
					if '_Call, ' in expr:
						expr = expr.replace('_Call, ', ', ') # TODO: very messy

					if prop.name == 'get':
						prop_text = f"JS::Runtime::InstanceFunction::Getter({Final}, {Enumerable}, JS::Runtime::SignatureType{{}}, {expr})"
						access = "READ_ACCESS"
					elif prop.name == 'set':
						prop_text = f"JS::Runtime::InstanceFunction::Setter({Final}, {Enumerable}, JS::Runtime::SignatureType{{}}, {expr})"
						access = "WRITE_ACCESS"
				line = f"{reference}->InstanceMembers.Set({slot_key}, {{{slot_namespace}}} {access}, {prop_text});"
				print_line(line, introduced = prop.introduced, removed = prop.removed, extensions = prop.extensions, ecmascript4 = prop.ecmascript4)

def collect_entities(value, entity_setup_sequence):
	if isinstance(value, JSTypeLayout):
		return # not a real object
	elif isinstance(value, JSObject):
		for version in value.versions:
			collect_entities(version.Prototype, entity_setup_sequence)
		for slot in value.slots:
			if type(slot) is JSDataProperty:
				collect_entities(slot.value, entity_setup_sequence)
			elif type(slot) is JSAccessorProperty:
				collect_entities(slot, entity_setup_sequence)
		if value not in entity_setup_sequence:
			entity_setup_sequence.append(value)
	elif isinstance(value, JSAccessorProperty):
		for slot in value.slots:
			collect_entities(slot.value, entity_setup_sequence)
	elif isinstance(value, JSAlias):
		entity_setup_sequence.append(value)

# collect all entities, in sequence of appearance within the membership hierarchy starting from intrinsics
entity_setup_sequence = []
for name, value in intrinsic_entity_list:
	if isinstance(value, JSTypeLayout):
		continue
	collect_entities(value, entity_setup_sequence)
	if value not in entity_setup_sequence:
		entity_setup_sequence.append(value)
entity_setup_sequence.sort(key = version_sort_order)
#assert any([entity.identifier == 'Array_private' for entity in entity_setup_sequence])

# create graph of ES4 entity relations
entity_relation = {}
for entity in entity_setup_sequence:
	if isinstance(entity, JSObject):
		entity_relation[entity] = {}

		# Check the object prototype, must be a tree
		prototype = lookup_entity(entity.versions[0].Prototype)[1] # TODO: prototypes for other versions

		if prototype is not None:
			if isinstance(entity, JSConstructor):
				# Store the ES1/ES6 prototype under the key "!", "P" will contain the ES4 prototype
				entity_relation[entity]["!"] = [prototype]
				prototype = lookup_entity("Class_prototype")[1]
			entity_relation[entity]["P"] = prototype

		#if entity.label == 'Object':
		#	print(">", prototype.label, file = sys.stderr)

		# Check ES4 object class
		if entity.label is not None and entity.label.endswith(".prototype"):
			# ECMAScript 4 prototypes are instances of Object, not of some other type
			the_class = lookup_entity("Object")[1]
		else:
			the_class = get_class_of(entity)

		entity_relation[entity]["T"] = the_class

		if isinstance(entity, JSConstructor):
			# Check ES4 superclass
			if entity.Super is not None:
				entity_relation[entity]["S"] = lookup_entity(entity.Super)[1]

			# Check class prototype
			class_prototype = None
			for slot in entity.slots:
				if slot.name == "prototype" and type(slot) is JSDataProperty:
					class_prototype = slot.value
					break

			if isinstance(class_prototype, JSIdentifier) or isinstance(class_prototype, JSMember):
				class_prototype = lookup_entity(class_prototype)[1]

			if class_prototype is not None:
				entity_relation[entity]["C"] = class_prototype

	elif isinstance(entity, JSAlias):
		entity_relation[entity] = {}

		# Check ES4 object class
		the_class = lookup_entity(entity.declared_type)[1]
		if not isinstance(the_class, JSTypeLayout): # type layouts are not created in C++
			entity_relation[entity]["T"] = the_class

		# Check value, using "!" to force the non-fixup rule (i.e. there is no point of initializing this variable if the initializer is not ready yet)
		for version in entity.versions:
			if type(version.code) is not str:
				if "!" not in entity_relation:
					entity_relation[entity]["!"] = []
				entity_relation[entity]["!"].append(lookup_entity(version.code)[1])

# collect all entities according to the prototype they are using
entity_create_sequence = []
relation_fixups = {}
entities_to_create = entity_setup_sequence[:]
while len(entities_to_create) > 0:
	outgoing_node_count = 9999 # how many significant connections go to not-yet defined objects (significant being a connection that gets initialized at creation time)
	new_entity_create_sequence = []
	new_relation_fixups = {}
	_necs = []
	new_entity_outgoing_connections = {}
	entities_to_check = entities_to_create[:]
	entities_to_create.clear()
	for entity in entities_to_check:
		if entity in entity_create_sequence:
			continue # already added

		assert isinstance(entity, JSObject) or isinstance(entity, JSAlias)
		outgoing_count = 0
		_out = ""
		fixups = {}

		for step in ["P", "T", "S", "C"]:
			if step in entity_relation[entity]:
				if entity_relation[entity][step] not in entity_create_sequence:
					target = entity_relation[entity][step]
					#if target not in fixups:
					#	fixups[target] = []
					#fixups[target].append((entity, step))
					#if step == 'P':
					#	outgoing_count = FAKE_INFINITY
					if entity in entity_relation[target].values() or entity in entity_relation[target].get("!", ()):
						#_out += step.lower()
						continue # they form a 2-long cycle
					outgoing_count += 1
					_out += step
				#else:
				#	_out += step.lower()

		# Check the ES1/ES6 object prototype, must be a tree
		if "!" in entity_relation[entity]:
			for forced_predecessor in entity_relation[entity]["!"]:
				if forced_predecessor not in entity_create_sequence:
					# make sure the pre-ES4 prototype tree is totally ordered
					outgoing_count = FAKE_INFINITY
					break
		elif "P" in entity_relation[entity]:
			forced_predecessor = entity_relation[entity]["P"]
			if forced_predecessor not in entity_create_sequence:
				# make sure the pre-ES4 prototype tree is totally ordered
				outgoing_count = FAKE_INFINITY

		# put it back at end in case this is not part of the best batch
		entities_to_create.append(entity)

		if outgoing_count <= outgoing_node_count:
			if outgoing_count < outgoing_node_count:
				new_entity_create_sequence.clear()
				_necs.clear()
				new_relation_fixups.clear()
				outgoing_node_count = outgoing_count
			new_entity_create_sequence.append(entity)
			_necs.append(_out)
			for step in ["P", "T", "S", "C"]:
				if step in entity_relation[entity]:
					if entity_relation[entity][step] not in entity_create_sequence + new_entity_create_sequence:
						target = entity_relation[entity][step]
						if target not in fixups:
							fixups[target] = []
						fixups[target].append((entity, step))
			for target, steps in fixups.items():
				if target in new_relation_fixups:
					new_relation_fixups[target] += steps
				else:
					new_relation_fixups[target] = steps

	# ensure that each generation is in the order of introduction
	new_entity_create_sequence.sort(key = version_sort_order)
	#print(f"Batch with outgoing count {outgoing_node_count}, count {len(new_entity_create_sequence)}", file = sys.stderr)
	#for entity, _out in zip(new_entity_create_sequence, _necs):
	#	if isinstance(entity, JSObject) and entity.label is not None:
	#		print(f"\t{entity.label}\t{_out}", file = sys.stderr)
	entity_create_sequence += new_entity_create_sequence
	for target, steps in new_relation_fixups.items():
		if target in relation_fixups:
			relation_fixups[target] += steps
		else:
			relation_fixups[target] = steps

with open("out/intrinsics.hh", 'w') as sys.stdout:
	print("#ifndef _INTRINSICS_HH")
	print("#define _INTRINSICS_HH")
	print('#include "../src/common.hh"')
	print('#include "../src/value.hh"')
	print("namespace JS")
	print("{")
	print("\tnamespace Runtime")
	print("\t{")
	print("\t\tclass Intrinsics : public gc")
	print("\t\t{")
	print("\t\tpublic:")
	print("\t\t\tvoid Initialize(Realm& realm);")
	print("\t\t\tvoid InitializeGlobalObject(Realm& realm, JSObject global);")

	lowest_version = None
	enable_es4 = False
	for entity in entity_setup_sequence:
		if entity.identifier in UNIVERSE:
			if entity.identifier == 'global':
				continue # do not make this an intrinsic
			new_lowest_version = entity.introduced
			if new_lowest_version is None:
				if entity.ecmascript4 == JSObject.ES4_EXCLUSIVE:
					new_lowest_version = 4
				else:
					new_lowest_version = 1

			# list JSON as version 5
			#if 4 < new_lowest_version and entity.ecmascript4 != JSObject.ES4_UNAVAILABLE:
			#	new_lowest_version = 4

			if lowest_version != new_lowest_version:
				lowest_version = new_lowest_version
				comment = f"\t\t\t// ES{lowest_version}"
			else:
				comment = None

			if isinstance(entity, JSConstructor) and is_ecmascript4_allowed(introduced = entity.introduced, removed = entity.removed, extensions = entity.extensions, ecmascript4 = entity.ecmascript4):
				if not enable_es4:
					print("#if ENABLE_ES4")
					enable_es4 = True
				if comment is not None:
					print(comment)
				print(f"\t\t\tJSClass {entity.identifier}_class;")
				if is_non_ecmascript4_allowed(introduced = entity.introduced, removed = entity.removed, extensions = entity.extensions, ecmascript4 = entity.ecmascript4):
					if enable_es4:
						print("#endif")
						enable_es4 = False
					print(f"\t\t\tJSFunction {entity.identifier};") # TODO: type
			elif isinstance(entity, JSFunction):
				if enable_es4:
					print("#endif")
					enable_es4 = False
				if comment is not None:
					print(comment)
				if entity.label == "Function.prototype":
					# ECMAScript 4 has a non-function Function.prototype object
					print(f"\t\t\tJSObject {entity.identifier};")
				else:
					print(f"\t\t\tJSFunction {entity.identifier};") # TODO: type
			elif isinstance(entity, JSObject):
				if is_non_ecmascript4_allowed(introduced = entity.introduced, removed = entity.removed, extensions = entity.extensions, ecmascript4 = entity.ecmascript4):
					if enable_es4:
						print("#endif")
						enable_es4 = False
				else:
					if not enable_es4:
						print("#if ENABLE_ES4")
						enable_es4 = True
				if comment is not None:
					print(comment)
				print(f"\t\t\tJSObject {entity.identifier};") # TODO: type
			elif isinstance(entity, JSAlias):
				cpp_type = lookup_entity(entity.declared_type)[1]
				assert cpp_type.CppType is not None
				print(f"\t\t\tgcn::gc_ptr<{cpp_type.CppType}> {entity.identifier};")
			else:
				assert False

	print("\t\t};")
	print("\t}")
#	print("}")

#	print("namespace JS")
#	print("{")
	print("\tnamespace Runtime")
	print("\t{")
	print("\t\tnamespace Builtin")
	print("\t\t{")

	enable_es4 = False
	for entity in entity_setup_sequence:
		if isinstance(entity, JSFunction):
			for version in entity.versions:
				if version.ObjectTypeConstructorArguments is None:
					print_function = True
					break
			else:
				print_function = False

			if is_non_ecmascript4_allowed(introduced = entity.introduced, removed = entity.removed, extensions = entity.extensions, ecmascript4 = entity.ecmascript4):
				if enable_es4:
					print("#endif")
				enable_es4 = False
			else:
				if isinstance(entity, JSConstructor):
					print_function = False # this is a class

				if not enable_es4:
					print("#ifdef ENABLE_ES4")
				enable_es4 = True

			if print_function:
				print(f"\t\t\tJSValue {entity.identifier}_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));")

			if entity.Instance is not None:
				for slot in entity.Instance.slots:
					if isinstance(slot, JSAccessorProperty):
						#if isinstance(slot.name, JSQName):
						#	assert type(slot.name.ns) is str
						#	slot_name = slot.name.ns + "_" + slot.name.name
						#else:
						#	slot_name = slot.name

						for accessor in slot.slots:
							print(f"\t\t\tJSValue {accessor.value.identifier}(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));")
							#print(f"\t\t\tJSValue {entity.identifier}_{slot_name}_{accessor.name}(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase));")

	if enable_es4:
		print("#endif")

	print("\t\t};")
	print("\t}")
	print("}")
	print("#endif // _INTRINSICS_HH")

with open("out/template.cc", 'w') as sys.stdout:
	enable_es4 = False
	for entity in sorted([entity for entity in entity_setup_sequence if isinstance(entity, JSFunction)], key = lambda entity: entity.identifier.upper()):
		if is_non_ecmascript4_allowed(introduced = entity.introduced, removed = entity.removed, extensions = entity.extensions, ecmascript4 = entity.ecmascript4):
			if enable_es4:
				print("#endif")
			enable_es4 = False
		else:
			if not enable_es4:
				print("#ifdef ENABLE_ES4")
			enable_es4 = True
		print()
		print(f"JSValue JS::Runtime::Builtin::{entity.identifier}_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))")
		print("{")
		print("\t// TODO")
		print("\treturn JS_undefined;")
		print("}")
	if enable_es4:
		print("#endif")

with open("out/intrinsics.cc", 'w') as sys.stdout:
	print('#include <cmath>')
	print('#include "intrinsics.hh"')
	print('#include "../src/builtins.hh"')
	print('#include "../src/execute.hh"')
	print('#include "../src/debugger.hh"')
	print("void JS::Runtime::Intrinsics::Initialize(Realm& realm)")
	print("{")

	# declare variables to store objects that do not have a field in the Intrinsics class but still need to be accessed in multiple places during initialization
	for entity in entity_create_sequence:
		if entity.usage_count >= 2 and entity.identifier not in UNIVERSE:
			cpp_type = lookup_entity(value.versions[0].ObjectType)[1] # TODO: other versions
			assert cpp_type.CppType is not None
			print(f"\tgcn::gc_ptr<{cpp_type.CppType}> {entity.identifier};")

	started_functions = False
	for entity in sorted(entity_create_sequence, key = lambda entity: isinstance(entity, JSFunction) and not isinstance(entity, JSConstructor) and entity.label != 'Function.prototype'):
		if entity.label == 'global':
			continue

		if not started_functions and \
				isinstance(entity, JSFunction) and not isinstance(entity, JSConstructor) and entity.label != 'Function.prototype':
			# force innitialization of class instance properties before creating functions
			started_functions = True
			for entity2 in sorted(entity_create_sequence, key = lambda entity: entity.label != 'Function'): # start with Function class
				setup_entity_instance_properties(entity2)

		define_entity(entity)

		if entity in relation_fixups:
			# TODO: ES4 only
			assert entity.identifier is not None
			for source, step in relation_fixups[entity]:
				assert source.identifier is not None
				identifier = source.identifier
				if step == 'P':
					print(f"\t\t{identifier}->Prototype = {entity.identifier};")
				elif step == 'T':
					if isinstance(source, JSConstructor):
						identifier = f"{identifier}_class"
					print(f"\t\tJS::Class::Class::AssignType(std::dynamic_pointer_cast<JS::Class::OrdinaryObject>({identifier}), {entity.identifier}_class);")
				elif step == 'S':
					print(f"\t\t{identifier}_class->Super = {entity.identifier};")
				elif step == 'C':
					print(f"\t\t{identifier}_class->ClassPrototype = {entity.identifier};")

	for entity in entity_setup_sequence:
		if entity.label == 'global':
			continue
		setup_entity(entity)

	close_condition()
	print("}")

	print("void JS::Runtime::Intrinsics::InitializeGlobalObject(Realm& realm, JSObject global)")
	print("{")
	print("#if ENABLE_ES4")
	print("\tJSNamespace internal;")
	print("\tif(IsVersionES4())")
	print("\t{")
	print("\t\tinternal = std::dynamic_pointer_cast<JS::Class::Package>(global)->InternalNamespace;")
	print("\t}")
	print("#endif")
	setup_entity(lookup_entity("global")[1])
	close_condition()
	print("}")

def get_tokens2(line):
	tokens = []
	for token in get_tokens(line):
		if token[0].isalnum() or token[0] == '_' or token[0] == '"':
			tokens.append(token)
		else:
			while len(token) > 0:
				if token.startswith('...'):
					tokens.append('...')
					token = token[3:]
				else:
					tokens.append(token[0])
					token = token[1:]
	return tokens

def parse_argument_list(tokens, optional = False):
	name = tokens.pop(0)
	if name == '...':
		# vararg
		name = tokens.pop(0)
		assert is_identifier(name)
		yield (name, '*')
	else:
		assert is_identifier(name)
		if optional:
			yield (name, '?')
		else:
			yield (name, '')

		if tokens[0] == '[':
			tokens.pop(0)
			assert tokens.pop(0) == ','
			yield from parse_argument_list(tokens, optional = True)
			assert tokens.pop(0) == ']'
		elif tokens[0] == ',':
			tokens.pop(0)
			yield from parse_argument_list(tokens, optional = optional)

def parse_arguments(tokens):
	if tokens[0] == '[':
		# optional
		tokens.pop(0)
		yield from parse_argument_list(tokens, optional = True)
		assert tokens.pop(0) == ']'
	else:
		yield from parse_argument_list(tokens, optional = False)

DOCSTRINGS = {}
CURRENT_DOCSTRING = []
with open(os.path.splitext(sys.argv[1])[0] + '.doc', 'r') as file:
	for line in file:
		if line.startswith('*'):
			CURRENT_DOCSTRING.append(line[1:].strip())
		else:
			tokens = get_tokens2(line)
			if len(tokens) > 0:
				CURRENT_DOCSTRING.insert(0, line.strip())
				if tokens[0] == '[':
					tokens.pop(0)
					assert tokens.pop(0) == 'new'
					assert tokens.pop(0) == ']'
				elif tokens[0] == 'new':
					tokens.pop(0)
				name = tokens.pop(0)
				assert is_identifier(name)
				qname = [name]
				while len(tokens) > 0:
					if tokens[0] == '.':
						tokens.pop(0)
						name = tokens.pop(0)
						assert is_identifier(name)
						qname.append(name)
					elif tokens[0] == '[':
						tokens.pop(0)
						assert tokens.pop(0) == 'Symbol'
						assert tokens.pop(0) == '.'
						name = tokens.pop(0)
						assert is_identifier(name)
						assert tokens.pop(0) == ']'
						qname.append(name)
					else:
						break
				if len(tokens) > 0:
					assert tokens.pop(0) == '('
					if tokens[0] == ')':
						arguments = ()
					else:
						arguments = tuple(parse_arguments(tokens))
					#print(arguments, file = sys.stderr)
					assert tokens.pop(0) == ')'
					assert len(tokens) == 0
			DOCSTRINGS[tuple(qname)] = (CURRENT_DOCSTRING, arguments)
			CURRENT_DOCSTRING = []

# argument_sequence : arguments | '[' arguments ']'
# arguments : argument_list | argument_list ',' '...'

PYTHONIFIED = set()
def pythonify(thing, indent = "", reference = ()):
	global PYTHONIFIED
	if isinstance(thing, JSObject):
		entity = thing
		name = entity.label

	elif isinstance(thing, JSDataProperty):
		entity = thing.value
		name = thing.name

	elif isinstance(thing, JSAccessorProperty):
		entity = thing
		name = thing.name

	else:
		print(type(thing), file = sys.stderr) # TODO
		return

	PYTHONIFIED.add(entity)

	if isinstance(name, JSSymbol):
		name = name.description

	actual_slots = []

	#print(reference, type(entity), file = sys.stderr)
	if reference != ('global',):
		reference = reference + (name,)
	else:
		reference = (name,)
	docstring, arguments = DOCSTRINGS.get(reference, ([], ()))
#	print(docstring)
	docstring = list(docstring)
	if thing.ecmascript4 == JSObject.ES4_EXCLUSIVE:
		since = "ECMAScript 4 only"
		if 'D' in thing.extensions:
			since += ", decimal extension"
	else:
		if thing.introduced is None:
			if 'X' in thing.extensions:
				since = "E4X"
			else:
				since = "ECMAScript 1"
		elif version.ecmascript4 != JSObject.ES4_UNAVAILABLE and thing.introduced > 4:
			since = "ECMAScript 4, ECMAScript 5"
		else:
			since = f"ECMAScript {thing.introduced}"

	if 'W' in thing.extensions:
		since += ", web extension"

	if 'd' in thing.extensions:
		since += ", debugging extension"

	docstring.append(f"@since {since}")

	if isinstance(entity, JSObject) or isinstance(entity, JSAccessorProperty):
		for slot in entity.slots:
			if type(slot.name) is str and slot.name.startswith('[['):
				continue
			if isinstance(entity, JSFunction) and slot.name in {'name', 'length'}:
				continue
			actual_slots.append(slot)

	if isinstance(entity, JSFunction):
		arguments = [name if opt == '' else f'{name} = undefined' if opt == '?' else f'*{name}' if opt == '*' else None for name, opt in arguments]
		print(f"{indent}def {name}({', '.join(arguments)}):")
		if len(docstring) > 0:
			for n, line in enumerate(docstring):
				if n == 0:
					print(f"{indent}  \"\"\"! {line}")
				else:
					print(f"{indent}  {line}")
			print(f"{indent}  \"\"\"")
		print(f"{indent}  pass")

	if len(actual_slots) > 0:
		print(f"{indent}class {name}:")
		#if not isinstance(entity, JSFunction) and len(docstring) > 0:
		if len(docstring) > 0:
			for n, line in enumerate(docstring):
				if n == 0:
					print(f"{indent}  \"\"\"! {line}")
				else:
					print(f"{indent}  {line}")
			print(f"{indent}  \"\"\"")
		for slot in actual_slots:
			pythonify(slot, indent + "  ", reference)
	elif not isinstance(entity, JSFunction):
		if len(docstring) > 0:
			for n, line in enumerate(docstring):
				if n == 0:
					print(f"{indent}## {line}")
				else:
					print(f"{indent}# {line}")
		print(f"{indent}{name} = None")

with open("out/js.py", 'w') as sys.stdout:
	pythonify(lookup_entity("global")[1])
	for entity in UNIVERSE.values():
		if entity not in PYTHONIFIED:
			pythonify(entity)

