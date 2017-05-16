/* See LICENSE file for copyright and license details. */
#include "common.h"

USAGE("")

#define PROCESS(TYPE, FMT)\
	do {\
		TYPE buf[BUFSIZ / sizeof(TYPE)];\
		size_t i;\
		int r, done = 0;\
		while (!done) {\
			for (i = 0; i < ELEMENTSOF(buf); i += (size_t)r) {\
				r = scanf("%"FMT, buf + i);\
				if (r == EOF) {\
					done = 1;\
					break;\
				}\
			}\
			ewriteall(STDOUT_FILENO, buf, i * sizeof(*buf), "<stdout>");\
		}\
	} while (0)

static void process_xyza (void) {PROCESS(double, "lf");}
static void process_xyzaf(void) {PROCESS(float, "f");}

int
main(int argc, char *argv[])
{
	struct stream stream;
	size_t size = 0;
	char *line = NULL;
	ssize_t len;
	void (*process)(void);

	UNOFLAGS(argc);

	len = getline(&line, &size, stdin);
	if (len < 0) {
		if (ferror(stdin))
	  		eprintf("getline <stdin>:");
		else
			eprintf("<stdin>: no input\n");
	}
	if (len && line[len - 1] == '\n')
		line[--len] = '\0';
	if ((size_t)len + 6 > sizeof(stream.buf))
		eprintf("<stdin>: head is too long\n");
	stream.fd = -1;
	stream.file = "<stdin>";
	memcpy(stream.buf, line, (size_t)len);
	memcpy(stream.buf + len, "\n\0uivf", 6);
	stream.ptr = (size_t)len + 6;
	free(line);
	ewriteall(STDOUT_FILENO, stream.buf, stream.ptr, "<stdout>");
	einit_stream(&stream);

	if (!strcmp(stream.pixfmt, "xyza"))
		process = process_xyza;
	else if (!strcmp(stream.pixfmt, "xyza f"))
		process = process_xyzaf;
	else
		eprintf("pixel format %s is not supported, try xyza\n", stream.pixfmt);

	process();

	efshut(stdin, "<stdin>");
	return 0;
}
