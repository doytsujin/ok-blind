/* See LICENSE file for copyright and license details. */
#include "common.h"

USAGE("[-en] leftmost-stream ... rightmost-stream")

static int equal = 0;
static size_t max_frame_size;

#define PROCESS(TYPE)\
	do {\
		typedef TYPE pixel_t[4];\
		pixel_t *res, *left, *right, *tmp;\
		size_t i, j, w, h, h2, x, y, k, r;\
		res = emalloc(max_frame_size);\
		left = emalloc(max_frame_size);\
		right = emalloc(max_frame_size);\
		\
		while (eread_frame(streams + (n_streams - 1), res)) {\
			w = streams[n_streams - 1].width;\
			h = streams[n_streams - 1].height;\
			for (i = n_streams - 1; i--;) {\
				tmp = res, res = right, right = tmp;\
				if (!eread_frame(streams + i, left))\
					goto done;\
				h2 = streams[i].height;\
				memset(res, 0, w * h2 * streams->pixel_size);\
				\
				/* XXX Is there any significant performance to be gained by transposing `right`? */\
				if (equal) {\
					for (y = r = 0; y < h2; y++) {\
						for (x = 0; x < w; x++, r++) {\
							for (k = 0; k < h; k++) \
								res[r][1] += left[y * h + k][1] * right[k * w + x][1];\
							res[r][3] = res[r][2] = res[r][0] = res[r][1];\
						}\
					}\
				} else {\
					for (y = r = 0; y < h2; y++)\
						for (x = 0; x < w; x++, r++) \
							for (k = 0; k < h; k++)\
								for (j = 0; j < 4; j++)\
									res[r][j] += left[y * h + k][j] * right[k * w + x][j];\
				}\
				\
				h = h2;\
			}\
			ewriteall(STDOUT_FILENO, res, streams->frame_size, "<stdout>");\
		}\
		\
	done:\
		free(res);\
		free(left);\
		free(right);\
	} while (0)

static void process_lf(struct stream *streams, size_t n_streams) { PROCESS(double); }
static void process_f (struct stream *streams, size_t n_streams) { PROCESS(float); }

int
main(int argc, char *argv[])
{
	struct stream *streams;
	size_t n_streams, i, frames = 0;
	int natural = 0, j;
	char **rev_argv;
	size_t max_width = 0, max_height = 0;
	size_t width = 0, height = 0, w, h;
	void (*process)(struct stream *streams, size_t n_streams);

	ARGBEGIN {
	case 'e':
		equal = 1;
		break;
	case 'n':
		natural = 1;
		break;
	default:
		usage();
	} ARGEND;

	if (argc < 2)
		usage();

	if (natural) {
		rev_argv = alloca((size_t)argc * sizeof(*rev_argv));
		for (j = 0; j < argc; j++)
			rev_argv[j] = argv[argc - 1 - j];
		argv = rev_argv;
	}

	n_streams = (size_t)argc;
	streams = ecalloc(n_streams, sizeof(*streams));

	for (i = 0; i < n_streams; i++) {
		eopen_stream(streams + i, argv[i]);
		if (streams[i].frames && streams[i].frames < frames)
			frames = streams[i].frames;
		if (streams->width > max_width)
			max_width = streams->width;
		if (streams->height > max_height)
			max_height = streams->height;
	}
	for (i = 1; i < n_streams; i++)
		if (strcmp(streams->pixfmt, streams[i].pixfmt))
			eprintf("videos use incompatible pixel formats\n");

	width  = streams[n_streams - 1].width;
	height = streams[n_streams - 1].height;
	for (i = n_streams - 1; i--;) {
		if (streams[i].width != height)
			eprintf("videos do not have the compatible geometry\n");
		height = streams[i].height;
	}

	if (!strcmp(streams->pixfmt, "xyza"))
		process = process_lf;
	else if (!strcmp(streams->pixfmt, "xyza f"))
		process = process_f;
	else
		eprintf("pixel format %s is not supported, try xyza\n", streams->pixfmt);

	w = streams->width,  streams->width  = max_width;
	h = streams->height, streams->height = max_height;
	echeck_dimensions(streams, WIDTH | HEIGHT, NULL);
	streams->width  = width;
	streams->height = height;
	streams->frames = frames;
	fprint_stream_head(stdout, streams);
	streams->width  = w;
	streams->height = h;
	efflush(stdout, "<stdout>");
	max_frame_size = max_width * max_height * streams->pixel_size;

	process(streams, n_streams);

	free(streams);
	return 0;
}
