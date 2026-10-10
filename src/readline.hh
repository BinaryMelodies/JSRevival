#ifndef _READLINE_HH
#define _READLINE_HH

/* A very basic readline utility */

#include <iostream>
#include <optional>
#include <string>

void restore_keyboard(void);
void setup_keyboard(void);

int getkey(int fd);

// history control

int open_history_file(const std::string& filename);
int history_append_line(const std::string& line);
void history_reset_start();
int history_previous_line(void);
int history_next_line(void);
std::string history_get_line();

// readline

std::optional<std::string> readline();

class readline_buffer : public std::basic_streambuf<char>
{
protected:
	std::string line;
	size_t position = 0;

	int_type underflow() override;
	std::streamsize xsgetn(char_type * s, std::streamsize count) override;
};

class readline_istream : public std::basic_istream<char>
{
protected:
	readline_buffer buffer;
public:
	readline_istream() : std::basic_istream<char>(&buffer) { }
};

#endif // _READLINE_HH
