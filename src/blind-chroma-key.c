/* See LICENSE file for copyright and license details. */
#include "common.h"

USAGE("key-stream")


#define PROCESS(TYPE, SUFFIX)\
	static void\
	process_##SUFFIX(struct stream *stream, struct stream *key)\
	{\
		size_t i, n, m = 0;\
		TYPE x1, y1, z1, a1, a2, variance2, *keyxyza;\
		TYPE x, y, z, d;\
		do {\
			if (!m) {\
				m = stream->frame_size;\
				while (key->ptr < key->frame_size)\
					if (!eread_stream(key, key->frame_size - key->ptr))\
						return;\
				keyxyza = (TYPE *)(key->buf);\
				x1 = keyxyza[0];\
				y1 = keyxyza[1];\
				z1 = keyxyza[2];\
				a1 = keyxyza[3];\
				x = x1 - keyxyza[4];\
				y = y1 - keyxyza[5];\
				z = z1 - keyxyza[6];\
				a2 = keyxyza[7];\
				variance2 = x * x + y * y + z * z;\
				if (a2 > a1) {\
					a1 = 1 - a1;\
					a2 = 1 - a2;\
				}\
				memmove(key->buf, key->buf + key->frame_size,\
					key->ptr -= key->frame_size);\
			}\
			n = MIN(stream->ptr, m) / stream->pixel_size;\
			for (i = 0; i < n; i++) {\
				x = ((TYPE *)(stream->buf))[4 * i + 0] - x1;\
				y = ((TYPE *)(stream->buf))[4 * i + 1] - y1;\
				z = ((TYPE *)(stream->buf))[4 * i + 2] - z1;\
				d = x * x + y * y + z * z;\
				if (d <= variance2) {\
					if (a1 == a2)\
						d = 0;\
					else\
						d = sqrt(d / variance2) * (a1 - a2) + a2;\
					((TYPE *)(stream->buf))[4 * i + 3] *= d;\
				}\
			}\
			n *= stream->pixel_size;\
			m -= n;\
			ewriteall(STDOUT_FILENO, stream->buf, n, "<stdout>");\
			memmove(stream->buf, stream->buf + n, stream->ptr -= n);\
		} while (eread_stream(stream, SIZE_MAX));\
		if (stream->ptr)\
			eprintf("%s: incomplete frame\n", stream->file);\
	}

PROCESS(double, lf)
PROCESS(float, f)


int
main(int argc, char *argv[])
{
	struct stream stream, key;
	void (*process)(struct stream *stream, struct stream *key);

	UNOFLAGS(argc != 1);

	eopen_stream(&stream, NULL);
	eopen_stream(&key, argv[0]);

	if (!strcmp(stream.pixfmt, "xyza"))
		process = process_lf;
	else if (!strcmp(stream.pixfmt, "xyza f"))
		process = process_f;
	else
		eprintf("pixel format %s is not supported, try xyza\n", stream.pixfmt);

	if (strcmp(stream.pixfmt, key.pixfmt))
		eprintf("videos use incompatible pixel formats\n");

	if (key.width > 2 || key.height > 2 || key.width * key.height != 2)
		eprintf("%s: each frame must contain exactly 2 pixels\n", key.file);

	fprint_stream_head(stdout, &stream);
	efflush(stdout, "<stdout>");
	process(&stream, &key);
	return 0;
}