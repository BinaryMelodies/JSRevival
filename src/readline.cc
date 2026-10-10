
#include <cstring>
#include <fstream>
#include <optional>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "readline.hh"

static bool atexit_setup = false;
static bool keyboard_setup = false;
static struct termios old_termios;

void restore_keyboard(void)
{
	if(keyboard_setup)
	{
		tcsetattr(0, TCSAFLUSH, &old_termios);
		keyboard_setup = false;
	}
}

void setup_keyboard(void)
{
	if(keyboard_setup)
		return;

	struct termios new_termios;
	tcgetattr(0, &old_termios);
	if(!atexit_setup)
	{
		atexit(restore_keyboard);
		atexit_setup = true;
	}
	keyboard_setup = true;
	new_termios = old_termios;
	cfmakeraw(&new_termios);
	tcsetattr(0, TCSAFLUSH, &new_termios);
}

int getkey(int fd)
{
	struct pollfd fds[1];
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	if(poll(fds, 1, 0) <= 0)
	{
		return -1;
	}

	char buff[1];
	int count = read(fd, buff, 1);
	if(count < 0)
	{
		return -2;
	}
	else if(count == 0)
	{
		return -1;
	}
	else
	{
		return buff[0] & 0xFF;
	}
}

// history control

std::fstream history_file;

long history_file_position;
long history_file_line_end_position;
long history_file_end_position;

int open_history_file(const std::string& filename)
{
	if(history_file.is_open())
	{
		history_file.close();
	}
	history_file.open(filename, std::ios_base::in | std::ios_base::out | std::ios_base::ate);
	if(!history_file.good())
	{
		std::ofstream f(filename);
		f.close();
		history_file.open(filename, std::ios_base::in | std::ios_base::out | std::ios_base::ate);
	}
	history_file_position = history_file_line_end_position = history_file_end_position = history_file.tellp();
	return 0;
}

int history_append_line(const std::string& line)
{
	history_file.seekp(0, std::ios::end);
	history_file << line << std::endl;
	history_file_end_position = history_file.tellp();
	return 0;
}

void history_reset_start()
{
	history_file_position = history_file_line_end_position = history_file_end_position;
}

int history_previous_line(void)
{
	if(history_file_position == 0)
	{
		return 0;
	}
	history_file_position--;
	history_file_line_end_position = history_file_position;
	while(history_file_position > 0)
	{
		int c;
		history_file_position--;
		history_file.seekg(history_file_position, std::ios::beg);
		c = history_file.get();
		if(c == '\n')
		{
			history_file_position ++;
			return 1;
		}
	}
	return 1;
}

int history_next_line(void)
{
	if(history_file_line_end_position >= history_file_end_position - 1)
	{
		return 0;
	}
	history_file_line_end_position += 1;
	history_file_position = history_file_line_end_position;
	history_file.seekg(history_file_position, std::ios::beg);
	while(history_file_line_end_position < history_file_end_position)
	{
		int c;
		c = history_file.get();
		if(c == '\n')
		{
			return 1;
		}
		history_file_line_end_position ++;
	}
	return 1;
}

std::string history_get_line()
{
	std::string line;
	history_file.seekg(history_file_position);
	for(ssize_t index = history_file_position; index < history_file_line_end_position; index ++)
	{
		line += history_file.get();
	}
	return line;
}

// readline

struct buffer_record
{
	std::string data;
	size_t char_length = 0;
	size_t byte_position = 0;
	size_t char_position = 0;
};

bool buffer_is_empty(buffer_record * buffer)
{
	return buffer->data.size() == 0;
}

void buffer_step_backward(buffer_record * buffer)
{
	buffer->char_position --;
	while((buffer->data[--buffer->byte_position] & 0xC0) == 0x80)
	{
		/* UTF-8 continuation byte */
	}
}

void buffer_step_forward(buffer_record * buffer)
{
	buffer->char_position ++;
	while((buffer->data[++buffer->byte_position] & 0xC0) == 0x80)
	{
		/* UTF-8 continuation byte */
	}
}

void buffer_load_data(buffer_record * buffer, std::string line)
{
	buffer->data = line;

	buffer->char_length = 0;
	for(size_t index = 0;
		index < buffer->data.size();
		index ++)
	{
		if((buffer->data[index] & 0xC0) != 0x80) /* not UTF-8 continuation byte */
		{
			buffer->char_length ++;
		}
	}
}

void buffer_insert_byte(buffer_record * buffer, int c)
{
	buffer->data.insert(buffer->byte_position, 1, c);

	buffer->byte_position ++;
	if((c & 0xC0) != 0x80) // not a UTF-8 continuation character
	{
		buffer->char_length ++;
		buffer->char_position ++;
	}
}

void buffer_erase_back(buffer_record * buffer)
{
	size_t byte_count = 1;
	while((buffer->data[buffer->byte_position - byte_count] & 0xC0) == 0x80)
	{
		/* UTF-8 continuation character */
		byte_count ++;
	}

	buffer->char_length --;
	buffer->char_position --;
	buffer->byte_position -= byte_count;

	buffer->data.erase(buffer->byte_position, byte_count);
}

enum CharType
{
	CharType_Space,
	CharType_Alphanumeric,
	CharType_Symbol,
};

std::optional<std::string> readline()
{
	buffer_record buffer = { };

	do
	{
		int c = getkey(0);
		if(c == -1)
		{
			continue;
		}
		else if(c < 0)
		{
			return {};
		}

		if(c == '\r')
		{
			break;
		}

		switch(c)
		{
		case 0x03:
			// control break
			if(buffer_is_empty(&buffer))
				return {};
			buffer.char_length = 0;
			buffer.data.clear();
			std::cout << "\r\n" << std::flush;
			return buffer.data;
		case 0x04:
			// end of stream
			if(buffer_is_empty(&buffer))
				return {};
			continue;

		case 0x1B:
			// escape, start of code sequences
			c = getkey(0);
			if(c == -1)
			{
				continue;
			}
			else if(c < 0)
			{
				return {};
			}

			// escape sequence
			if(c == '[')
			{
				int parameter_count = 0;
				int parameters[2] = { };
				// control sequence
				c = getkey(0);
				while(c >= 0 && 0x30 <= (c & 0x7F) && (c & 0x7F) <= 0x3F)
				{
					if(parameter_count == 0)
						parameter_count ++;
					if('0' <= c && c <= '9')
					{
						if(parameter_count <= 2)
						{
							parameters[parameter_count - 1] = parameters[parameter_count - 1] * 10 + c - '0';
						}
					}
					else if(c == ';')
					{
						parameter_count ++;
					}
					c = getkey(0);
				}
				switch(c)
				{
				case -1:
					continue;
				case 'A':
					// up
				case 'B':
					// down
					{
						if(c == 'A' ? history_previous_line() : history_next_line())
						{
							size_t old_position = buffer.char_position;

							if(buffer.char_position != 0)
							{
								std::cout << "\33[" << buffer.char_position << "D" << std::flush;
							}

							buffer_load_data(&buffer, history_get_line());

							buffer.byte_position = buffer.data.size();
							buffer.char_position = buffer.char_length;

							std::cout << buffer.data;
							if(old_position > buffer.char_position)
							{
								std::cout << std::string(old_position - buffer.char_position, ' ');
								std::cout << "\33[" << old_position - buffer.char_position << "D" << std::flush;
							}
							std::cout << std::flush;
						}
					}
					break;
				case 'D':
					// left
					if(parameter_count >= 2 && (parameters[1] & 4) != 0)
					{
						// control-left
						size_t old_position = buffer.char_position;
						CharType skip_type = CharType_Space;
						while(buffer.char_position > 0)
						{
							size_t old_char_position = buffer.char_position;
							size_t old_byte_position = buffer.byte_position;

							buffer_step_backward(&buffer);
							CharType char_type;
							int c = buffer.data[buffer.byte_position]; // TODO: does not support unicode
							if(isspace(c))
							{
								char_type = CharType_Space;
							}
							else if(isalnum(c) || c == '_')
							{
								char_type = CharType_Alphanumeric;
							}
							else
							{
								char_type = CharType_Symbol;
							}

							if(skip_type == CharType_Space)
							{
								skip_type = char_type;
							}
							else if(skip_type != char_type)
							{
								buffer.char_position = old_char_position;
								buffer.byte_position = old_byte_position;
								break;
							}
						}

						if(old_position != buffer.char_position)
						{
							std::cout << "\33[" << old_position - buffer.char_position << "D" << std::flush;
						}
					}
					else
					{
						if(buffer.char_position > 0)
						{
							buffer_step_backward(&buffer);
							std::cout << "\33[D" << std::flush;
						}
					}
					break;
				case 'C':
					// right
					if(parameter_count >= 2 && (parameters[1] & 4) != 0)
					{
						// control-right
						size_t old_position = buffer.char_position;
						CharType skip_type;
						int c = buffer.data[buffer.byte_position]; // TODO: does not support unicode
						if(isspace(c))
						{
							skip_type = CharType_Space;
						}
						else if(isalnum(c) || c == '_')
						{
							skip_type = CharType_Alphanumeric;
						}
						else
						{
							skip_type = CharType_Symbol;
						}

						while(buffer.char_position < buffer.char_length)
						{
							buffer_step_forward(&buffer);
							CharType char_type;
							int c = buffer.data[buffer.byte_position]; // TODO: does not support unicode
							if(isspace(c))
							{
								char_type = CharType_Space;
							}
							else if(isalnum(c) || c == '_')
							{
								char_type = CharType_Alphanumeric;
							}
							else
							{
								char_type = CharType_Symbol;
							}

							if(char_type == CharType_Space)
							{
								skip_type = char_type;
							}
							else if(skip_type != char_type)
							{
								break;
							}
						}

						if(old_position != buffer.char_position)
						{
							std::cout << "\33[" << buffer.char_position - old_position << "C" << std::flush;
						}
					}
					else
					{
						if(buffer.char_position < buffer.char_length)
						{
							buffer_step_forward(&buffer);
							std::cout << "\33[C" << std::flush;
						}
					}
					break;
				case 'H':
					// home
					if(buffer.char_position != 0)
					{
						std::cout << "\33[" << buffer.char_position << "D" << std::flush;
					}
					buffer.char_position = 0;
					buffer.byte_position = 0;
					break;
				case 'F':
					// end
					if(buffer.char_position != buffer.char_length)
					{
						std::cout << "\33[" << buffer.char_length - buffer.char_position << "C" << std::flush;
					}
					buffer.char_position = buffer.char_length;
					buffer.byte_position = buffer.data.size();
					break;
				default:
					if(c < -1)
					{
						return {};
					}
					// skip control sequence
					while(c >= 0 && 0x20 <= (c & 0x7F) && (c & 0x7F) <= 0x2F)
					{
						c = getkey(0);
					}
					if(c < -1)
					{
						return {};
					}
					break;
				}
			}
			else
			{
				// skip escape sequence
				while(c >= 0 && 0x20 <= (c & 0x7F) && (c & 0x7F) <= 0x2F)
				{
					c = getkey(0);
				}
				if(c < -1)
				{
					return {};
				}
			}
			continue;

		case 0x7F:
			// backspace
			if(buffer.char_position > 0)
			{
				buffer_erase_back(&buffer);

				std::cout << "\33[D" << buffer.data.substr(buffer.byte_position) << " \33[" << buffer.char_length - buffer.char_position + 1 << "D" << std::flush;
			}
			continue;
		default:
			if(c < ' ')
				continue;
		}

		buffer_insert_byte(&buffer, c);

		std::cout << buffer.data.substr(buffer.byte_position - 1);
		if(buffer.char_position != buffer.char_length)
		{
			std::cout << "\33[" << buffer.char_length - buffer.char_position << "D";
		}
		std::cout << std::flush;

	} while(true);

	std::cout << "\r\n" << std::flush;

	return buffer.data + "\n";
}

#if 0
int main(int argc, char * argv[])
{
	open_history_file(std::string(argv[0]) + ".history");

	int c;
	std::cout << "Compiled in C++" << std::endl; // << "Press 'q' or escape to quit" << std::endl;
	setup_keyboard();

	while(true)
	{
		std::optional<std::string> line;

		std::cout << "> " << std::flush;

		line = readline();

		if(!line)
		{
			std::cout << "^C received\r\n";
			exit(1);
		}

		std::cout << "You typed: \"" << line.value() << "\"\r\n";

		bool record_line;
		record_line = line.value().size() != 0;

		if(record_line)
		{
			history_reset_start();
			if(history_previous_line())
			{
				record_line = history_get_line() != line.value();
			}
		}

		if(record_line)
		{
			history_append_line(line.value());
		}
		history_reset_start();
	}

	return 0;
}
#endif

readline_buffer::int_type readline_buffer::underflow()
{
	/*if(position < line.size())
	{
		return std::char_traits<char>::eof();
	}*/
	setup_keyboard();
	while(true)
	{
		auto next_line = readline();
		if(!next_line.has_value())
		{
			restore_keyboard();
			return std::char_traits<char>::eof();
		}
		else if(next_line.value().size() != 0)
		{
			line = next_line.value();

			bool record_line;
			record_line = line.size() != 0 && line != "\r";

			if(record_line)
			{
				history_reset_start();
				if(history_previous_line())
				{
					record_line = history_get_line() != line;
				}
			}

			if(record_line)
			{
				if(line.back() == '\n')
					history_append_line(line.substr(0, line.size() - 1));
				else
					history_append_line(line);
			}
			history_reset_start();

			restore_keyboard();
			setg(line.data(), line.data(), line.data() + line.size());
			return line[0];
		}
	}
}

// TODO: are we using this?
std::streamsize readline_buffer::xsgetn(char_type * s, std::streamsize count)
{
	if(line.size() - position < size_t(count))
	{
		count = line.size() - position;
	}
	memcpy(s, line.data() + position, count);
	position += count;
	return count;
}

