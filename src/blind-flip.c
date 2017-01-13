/* See LICENSE file for copyright and license details. */
#include "stream.h"
#include "util.h"

#include <string.h>
#include <unistd.h>

USAGE("")

int
main(int argc, char *argv[])
{
	struct stream stream;
	size_t n, ptr, row_size;
	char *buf;

	ENOFLAGS(argc);

	stream.file = "<stdin>";
	stream.fd = STDIN_FILENO;
	einit_stream(&stream);
	fprint_stream_head(stdout, &stream);
	efflush(stdout, "<stdout>");

	echeck_frame_size(stream.width, stream.height, stream.pixel_size, 0, stream.file);
	n = stream.height * (row_size = stream.width * stream.pixel_size);
	buf = emalloc(n);

	memcpy(buf, stream.buf, stream.ptr);
	while (eread_frame(&stream, buf, n))
		for (ptr = n; ptr;)
			ewriteall(STDOUT_FILENO, buf + (ptr -= row_size), row_size, "<stdout>");

	free(buf);
	return 0;
}
