/* See LICENSE file for copyright and license details. */
#ifndef TYPE
#include "common.h"

USAGE("")

static int equal = 0;

#define FILE "blind-invert-matrix.c"
#include "define-functions.h"

int
main(int argc, char *argv[])
{
	struct stream stream;
	size_t width, x, y, i, row_size;
	char *buf, *one, *p;
	void (*process)(struct stream *stream, void *buf);

	ARGBEGIN {
	case 'e':
		equal = 1;
		break;
	default:
		usage();
	} ARGEND;

	if (argc)
		usage();

	eopen_stream(&stream, NULL);
	echeck_dimensions(&stream, WIDTH | HEIGHT, NULL);
	width = stream.width;
	if (stream.width < stream.height)
		eprintf("<stdin>: the video must be at least as wide as it is tall\n");
	else if (stream.width > stream.height)
		stream.width -= stream.height;
	fprint_stream_head(stdout, &stream);
	stream.width = width;
	efflush(stdout, "<stdout>");

	one = alloca(stream.pixel_size);
	if (!strcmp(stream.pixfmt, "xyza")) {
		*(double *)one = 1;
		process = process_lf;
	} else if (!strcmp(stream.pixfmt, "xyza f")) {
		*(float *)one = 1;
		process = process_f;
	} else {
		eprintf("pixel format %s is not supported, try xyza\n", stream.pixfmt);
	}

	for (i = 1; i < stream.n_chan; i++)
		memcpy(one + i * stream.chan_size, one, stream.chan_size);

	width = stream.width > stream.height ? stream.width : 2 * stream.height;
	buf = emalloc2(width, stream.col_size);
	row_size = width * stream.pixel_size;

	while (eread_frame(&stream, buf)) {
		if (stream.width == stream.height) {
			for (y = stream.height; y--;) {
				memmove(buf + y * row_size, buf + y * stream.row_size, stream.row_size);
				memset(buf + y * row_size + stream.row_size, 0, stream.row_size);
				memcpy(buf + y * row_size + stream.row_size + y * stream.pixel_size, one, stream.pixel_size);
			}
		}
		if (equal) {
			process(&stream, buf);
			for (y = 0; y < stream.height; y++) {
				for (x = 0; x < stream.width; x++) {
					p = buf + y * row_size + x * stream.pixel_size;
					for (i = 1; i < stream.n_chan; i++)
						memcpy(p + i * stream.chan_size, p, stream.chan_size);
				}
			}
		} else {
			for (i = 0; i < stream.n_chan; i++)
				process(&stream, buf + i * stream.chan_size);
		}
		for (y = 0; y < stream.height; y++)
			ewriteall(STDOUT_FILENO, buf + y * row_size + stream.col_size, row_size - stream.col_size, "<stdout>");
	}

	free(buf);
	return 0;
}

#else

#define SUB_ROWS()\
	do {\
		p2 = matrix + r2 * cn;\
		t = p2[r1][0];\
		for (c = 0; c < cn; c++)\
			p2[c][0] -= p1[c][0] * t;\
	} while (0)

static void
PROCESS(struct stream *stream, void *buf)
{
	typedef TYPE pixel_t[4];
	size_t rn = stream->height, r1, r2, c;
	size_t cn = stream->width > rn ? stream->width : 2 * rn;
	pixel_t *matrix = buf, *p1, *p2;
	TYPE t;

	for (r1 = 0; r1 < rn; r1++) {
		p1 = matrix + r1 * cn;
			if (!p1[r1][0]) {
			for (r2 = r1 + 1; r2 < rn; r2++) {
				p2 = matrix + r2 * cn;
				if (p2[r1][0])
					break;
			}
			if (r2 >= rn)
				eprintf("matrix is not invertable\n");
			for (c = 0; c < cn; c++)
				t = p1[c][0], p1[c][0] = p2[c][0], p2[c][0] = t;
		}
		t = 1 / p1[r1][0];
		for (c = 0; c < cn; c++)
			p1[c][0] *= t;
		for (r2 = r1 + 1; r2 < rn; r2++)
			SUB_ROWS();
	}

	for (r1 = rn; r1--;) {
		p1 = matrix + r1 * cn;
		for (r2 = r1; r2--;)
			SUB_ROWS();
	}
}

#undef SUB_ROWS
#endif
