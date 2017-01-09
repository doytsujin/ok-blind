/* See LICENSE file for copyright and license details. */
#include "arg.h"
#include "stream.h"
#include "util.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
	eprintf("usage: %s luma-stream\n", argv0);
}

static void
process_xyza(struct stream *colour, struct stream *luma, size_t n)
{
	size_t i;
	double a;
	for (i = 0; i < n; i += colour->pixel_size) {
		a = ((double *)(luma->buf + i))[1];
		a *= ((double *)(luma->buf + i))[3];
		((double *)(colour->buf + i))[1] *= a;
	}
}

int
main(int argc, char *argv[])
{
	struct stream colour;
	struct stream luma;
	ssize_t r;
	size_t i, n;
	void (*process)(struct stream *colour, struct stream *luma, size_t n);

	ARGBEGIN {
	default:
		usage();
	} ARGEND;

	if (argc != 1)
		usage();

	colour.file = "<stdin>";
	colour.fd = STDIN_FILENO;
	einit_stream(&colour);

	luma.file = argv[0];
	luma.fd = open(luma.file, O_RDONLY);
	if (luma.fd < 0)
		eprintf("open %s:", luma.file);
	einit_stream(&luma);

	if (colour.width != luma.width || colour.height != luma.height)
		eprintf("videos do not have the same geometry\n");
	if (colour.pixel_size != luma.pixel_size)
		eprintf("videos use incompatible pixel formats\n");

	if (!strcmp(colour.pixfmt, "xyza"))
		process = process_xyza;
	else
		eprintf("pixel format %s is not supported, try xyza\n", colour.pixfmt);

	for (;;) {
		if (colour.ptr < sizeof(colour.buf) && !eread_stream(&colour, SIZE_MAX)) {
			close(colour.fd);
			colour.fd = -1;
			break;
		}
		if (luma.ptr < sizeof(luma.buf) && !eread_stream(&luma, SIZE_MAX)) {
			close(luma.fd);
			luma.fd = -1;
			break;
		}

		n = colour.ptr < luma.ptr ? colour.ptr : luma.ptr;
		n -= n % colour.pixel_size;
		colour.ptr -= n;
		luma.ptr -= n;

		process(&colour, &luma, n);

		for (i = 0; i < n; i += (size_t)r) {
			r = write(STDOUT_FILENO, colour.buf + i, n - i);
			if (r < 0)
				eprintf("write <stdout>:");
		}

		if ((n & 3) || colour.ptr != luma.ptr) {
			memmove(colour.buf, colour.buf + n, colour.ptr);
			memmove(luma.buf,  luma.buf  + n, luma.ptr);
		}
	}

	if (luma.fd >= 0)
		close(luma.fd);

	for (i = 0; i < colour.ptr; i += (size_t)r) {
		r = write(STDOUT_FILENO, colour.buf + i, colour.ptr - i);
		if (r < 0)
			eprintf("write <stdout>:");
	}

	if (colour.fd >= 0) {
		for (;;) {
			colour.ptr = 0;
			if (!eread_stream(&colour, SIZE_MAX)) {
				close(colour.fd);
				colour.fd = -1;
				break;
			}

			for (i = 0; i < colour.ptr; i += (size_t)r) {
				r = write(STDOUT_FILENO, colour.buf + i, colour.ptr - i);
				if (r < 0)
					eprintf("write <stdout>:");
			}
		}
	}

	return 0;
}
