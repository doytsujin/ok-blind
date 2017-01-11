/* See LICENSE file for copyright and license details. */
#include "stream.h"
#include "util.h"

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

USAGE("right-hand-stream")

/* Because the syntax for a function returning a function pointer is disgusting. */
typedef void (*process_func)(struct stream *left, struct stream *right, size_t n);

#define LIST_OPERATORS\
	X(add, *lh += rh)\
	X(sub, *lh -= rh)\
	X(mul, *lh *= rh)\
	X(div, *lh /= rh)\
	X(exp, *lh = pow(*lh, rh))\
	X(log, *lh = log(*lh) / log(rh))\
	X(min, *lh = *lh < rh ? *lh : rh)\
	X(max, *lh = *lh > rh ? *lh : rh)

#define X(NAME, ALGO)\
	static void\
	process_lf_##NAME(struct stream *left, struct stream *right, size_t n)\
	{\
		size_t i;\
		double *lh, rh;\
		for (i = 0; i < n; i += 4 * sizeof(double)) {\
			lh = ((double *)(left->buf + i)) + 0, rh = ((double *)(right->buf + i))[0];\
			ALGO;\
			lh = ((double *)(left->buf + i)) + 1, rh = ((double *)(right->buf + i))[1];\
			ALGO;\
			lh = ((double *)(left->buf + i)) + 2, rh = ((double *)(right->buf + i))[2];\
			ALGO;\
			lh = ((double *)(left->buf + i)) + 3, rh = ((double *)(right->buf + i))[3];\
			ALGO;\
		}\
	}
LIST_OPERATORS
#undef X

static process_func
get_lf_process(const char *operation)
{
#define X(NAME, ALGO)\
	if (!strcmp(operation, #NAME)) return process_lf_##NAME;
LIST_OPERATORS
#undef X
	eprintf("algorithm not recognised: %s\n", operation);
	return NULL;
}

int
main(int argc, char *argv[])
{
	struct stream left, right;
	process_func process = NULL;

	ENOFLAGS(argc != 2);

	left.file = "<stdin>";
	left.fd = STDIN_FILENO;
	einit_stream(&left);

	right.file = argv[1];
	right.fd = eopen(right.file, O_RDONLY);
	einit_stream(&right);

	if (!strcmp(left.pixfmt, "xyza"))
		process = get_lf_process(argv[0]);
	else
		eprintf("pixel format %s is not supported, try xyza\n", left.pixfmt);

	process_two_streams(&left, &right, STDOUT_FILENO, "<stdout>", process);
	return 0;
}
