/* See LICENSE file for copyright and license details. */
#include "stream.h"
#include "util.h"

#include <ctype.h>
#include <unistd.h>

USAGE("")

int
main(int argc, char *argv[])
{
	char buf[4 + 3 * 3 * sizeof(size_t) + sizeof(((struct stream *)0)->pixfmt)];
	char magic[] = {'\0', 'u', 'i', 'v', 'f'};
	char b, *p;
	size_t i, ptr;
	ssize_t r;

	ENOFLAGS(argc);

	for (ptr = 0; ptr < sizeof(buf);) {
		r = read(STDIN_FILENO, buf + ptr, 1);
		if (r < 0)
			eprintf("read <stdin>:");
		if (r == 0)
			goto bad_format;
		if (buf[ptr++] == '\n')
			break;
	}
	if (ptr == sizeof(buf))
		goto bad_format;

	p = buf;
	for (i = 0; i < 5; i++) {
		r = read(STDIN_FILENO, &b, 1);
		if (r < 0)
			eprintf("read <stdin>:");
		if (r == 0 || b != magic[i])
			goto bad_format;
	}

	for (i = 0; i < 3; i++) {
		if (!isdigit(*p))
			goto bad_format;
		while (isdigit(*p)) p++;
		if (*p++ != ' ')
			goto bad_format;
	}
	while (isalnum(*p) || *p == ' ') {
		if (p[0] == ' ' && p[-1] == ' ')
			goto bad_format;
		p++;
	}
	if (p[-1] == ' ' || p[0] != '\n')
		goto bad_format;

	ewriteall(STDOUT_FILENO, buf, ptr, "<stdout>");

	return 0;
bad_format:
	eprintf("<stdin>: file format not supported\n");
	return 0;
}
