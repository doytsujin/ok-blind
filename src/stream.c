/* See LICENSE file for copyright and license details. */
#include "stream.h"
#include "util.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void
eninit_stream(int status, struct stream *stream)
{
	ssize_t r;
	size_t n;
	char *p = NULL, *w, *h, *f, *end;

	for (stream->ptr = 0; stream->fd >= 0 && !p;) {
		r = read(stream->fd, stream->buf + stream->ptr, sizeof(stream->buf) - stream->ptr);
		if (r < 0)
			enprintf(status, "read %s:", stream->file);
		if (r == 0)
			goto bad_format;
		stream->ptr += (size_t)r;
		p = memchr(stream->buf, '\n', stream->ptr);
	}

	*p = '\0';
	w = strchr(stream->buf, ' ');
	if (!w)
		goto bad_format;
	h = strchr(w + 1, ' ');
	if (!h)
		goto bad_format;
	f = strchr(h + 1, ' ');
	if (!f)
		goto bad_format;
	*w++ = *h++ = *f++ = '\0';

	if (strlen(f) >= sizeof(stream->pixfmt))
		goto bad_format;
	strcpy(stream->pixfmt, f);
	errno = 0;
	stream->frames = strtoul(stream->buf, &end, 10);
	if (errno == ERANGE && *stream->buf != '-')
		eprintf("%s: video is too long\n", stream->file);
	if (errno || *end)
		goto bad_format;
	errno = 0;
	stream->width = strtoul(w, &end, 10);
	if (errno == ERANGE && *stream->buf != '-')
		eprintf("%s: video is too wide\n", stream->file);
	if (errno || *end)
		goto bad_format;
	errno = 0;
	stream->height = strtoul(h, &end, 10);
	if (errno == ERANGE && *stream->buf != '-')
		eprintf("%s: video is too tall\n", stream->file);
	if (errno || *end)
		goto bad_format;

	if (!stream->width)
		eprintf("%s: width is zero\n", stream->file);
	if (!stream->height)
		eprintf("%s: height is zero\n", stream->file);

	n = (size_t)(p - stream->buf) + 1;
	memmove(stream->buf, stream->buf + n, stream->ptr -= n);
	while (stream->ptr < 5) {
		r = read(stream->fd, stream->buf + stream->ptr, sizeof(stream->buf) - stream->ptr);
		if (r < 0)
			enprintf(status, "read %s:", stream->file);
		if (r == 0)
			goto bad_format;
		stream->ptr += (size_t)r;
	}
	if (stream->buf[0] != '\0' ||
	    stream->buf[1] != 'u' || stream->buf[2] != 'i' ||
	    stream->buf[3] != 'v' || stream->buf[4] != 'f')
		goto bad_format;
	memmove(stream->buf, stream->buf + 5, stream->ptr -= 5);
	stream->headlen = n + 5;

	enset_pixel_size(status, stream);

	return;
bad_format:
	enprintf(status, "%s: file format not supported\n", stream->file);
}


int
set_pixel_size(struct stream *stream)
{
	if (!strcmp(stream->pixfmt, "xyza"))
		stream->pixel_size = 4 * sizeof(double);
	else
		return -1;
	return 0;
}

void
enset_pixel_size(int status, struct stream *stream)
{
	if (set_pixel_size(stream))
		enprintf(status, "file %s uses unsupported pixel format: %s\n",
			 stream->file, stream->pixfmt);
}


void
fprint_stream_head(FILE *fp, struct stream *stream)
{
	fprintf(fp, "%zu %zu %zu %s\n%cuivf",
		stream->frames, stream->width, stream->height, stream->pixfmt, 0);
}


size_t
enread_stream(int status, struct stream *stream, size_t n)
{
	ssize_t r = read(stream->fd, stream->buf + stream->ptr,
			 sizeof(stream->buf) - stream->ptr < n ?
			 sizeof(stream->buf) - stream->ptr : n);
	if (r < 0)
		enprintf(status, "read %s:", stream->file);
	stream->ptr += (size_t)r;
	return (size_t)r;
}


void
eninf_check_fd(int status, int fd, const char *file)
{
	struct stat st;
	if (fstat(fd, &st))
		enprintf(status, "fstat %s:", file);
	if (S_ISREG(st.st_mode))
		enprintf(status, "%s is a regular file, refusing infinite write\n");
}


int
check_frame_size(size_t width, size_t height, size_t pixel_size)
{
	if (!width || !height || !pixel_size)
		return 1;
	if (width > SIZE_MAX / height)
		return 0;
	if (width * height > SIZE_MAX / pixel_size)
		return 0;
	return 1;
}

void
encheck_frame_size(int status, size_t width, size_t height, size_t pixel_size, const char *prefix, const char *fname)
{
	if (!check_frame_size(width, height, pixel_size))
		enprintf(status, "%s: %s%svideo frame is too large\n",
			 prefix ? prefix : "", (prefix && *prefix) ? " " : "", fname);
}


void
encheck_compat(int status, const struct stream *a, const struct stream *b)
{
	if (a->width != b->width || a->height != b->height)
		enprintf(status, "videos do not have the same geometry\n");
	if (strcmp(a->pixfmt, b->pixfmt))
		enprintf(status, "videos use incompatible pixel formats\n");
}


int
enread_frame(int status, struct stream *stream, void *buf, size_t n)
{
	char *buffer = buf;
	ssize_t r;
	for (; stream->ptr < n; stream->ptr += (size_t)r) {
		r = read(stream->fd, buffer + stream->ptr, n - stream->ptr);
		if (r < 0) {
			enprintf(status, "read %s:", stream->file);
		} else if (r == 0) {
			if (!stream->ptr)
				break;
			enprintf(status, "%s: incomplete frame", stream->file);
		}
	}
	if (!stream->ptr)
		return 0;
	stream->ptr -= n;
	return 1;
}


void
nprocess_each_frame_segmented(int status, struct stream *stream, int output_fd, const char* output_fname,
			      void (*process)(struct stream *stream, size_t n, size_t frame))
{
	size_t frame_size, frame, r, n;

	encheck_frame_size(status, stream->width, stream->height, stream->pixel_size, 0, stream->file);
	frame_size = stream->height * stream->width * stream->pixel_size;

	for (frame = 0; frame < stream->frames; frame++) {
		for (n = frame_size; n; n -= r) {
			if (!enread_stream(status, stream, n))
				enprintf(status, "%s: file is shorter than expected\n", stream->file);
			r = stream->ptr - (stream->ptr % stream->pixel_size);
			(process)(stream, r, frame);
			enwriteall(status, output_fd, stream->buf, r, output_fname);
			memmove(stream->buf, stream->buf + r, stream->ptr -= r);
		}
	}
}


void
nprocess_two_streams(int status, struct stream *left, struct stream *right, int output_fd, const char* output_fname,
		     void (*process)(struct stream *left, struct stream *right, size_t n))
{
	size_t n;

	encheck_compat(status, left, right);

	for (;;) {
		if (left->ptr < sizeof(left->buf) && !enread_stream(status, left, SIZE_MAX)) {
			close(left->fd);
			left->fd = -1;
			break;
		}
		if (right->ptr < sizeof(right->buf) && !enread_stream(status, right, SIZE_MAX)) {
			close(right->fd);
			right->fd = -1;
			break;
		}

		n = left->ptr < right->ptr ? left->ptr : right->ptr;
		n -= n % left->pixel_size;
		left->ptr -= n;
		right->ptr -= n;

		process(left, right, n);

		enwriteall(status, output_fd, left->buf, n, output_fname);
		if ((n & 3) || left->ptr != right->ptr) {
			memmove(left->buf,  left->buf  + n, left->ptr);
			memmove(right->buf, right->buf + n, right->ptr);
		}
	}

	if (right->fd >= 0)
		close(right->fd);

	enwriteall(status, output_fd, left->buf, left->ptr, output_fname);

	if (left->fd >= 0) {
		for (;;) {
			left->ptr = 0;
			if (!enread_stream(status, left, SIZE_MAX)) {
				close(left->fd);
				left->fd = -1;
				break;
			}
			enwriteall(status, output_fd, left->buf, left->ptr, output_fname);
		}
	}
}


void
nprocess_multiple_streams(int status, struct stream *streams, size_t n_streams, int output_fd, const char* output_fname,
			  void (*process)(struct stream *streams, size_t n_streams, size_t n))
{
	size_t closed, i, j, n;

	for (i = 1; i < n_streams; i++)
		encheck_compat(status, streams + i, streams);

	while (n_streams) {
		n = SIZE_MAX;
		for (i = 0; i < n_streams; i++) {
			if (streams[i].ptr < sizeof(streams->buf) && !eread_stream(streams + i, SIZE_MAX)) {
				close(streams[i].fd);
				streams[i].fd = -1;
			}
			if (streams[i].ptr && streams[i].ptr < n)
				n = streams[i].ptr;
		}
		n -= n % streams->pixel_size;

		process(streams, n_streams, n);
		enwriteall(status, output_fd, streams->buf, n, output_fname);

		closed = SIZE_MAX;
		for (i = 0; i < n_streams; i++) {
			memmove(streams[i].buf, streams[i].buf + n, streams[i].ptr -= n);
			if (streams[i].ptr < streams->pixel_size && streams[i].fd < 0 && closed == SIZE_MAX)
				closed = i;
		}
		if (closed != SIZE_MAX) {
			for (i = (j = closed) + 1; i < n_streams; i++)
				if (streams[i].ptr < streams->pixel_size && streams[i].fd < 0)
					streams[j++] = streams[i];
			n_streams = j;
		}
	}
}


void
nprocess_each_frame_two_streams(int status, struct stream *left, struct stream *right, int output_fd, const char* output_fname,
				void (*process)(char *restrict output, char *restrict lbuf, char *restrict rbuf,
						struct stream *left, struct stream *right, size_t ln, size_t rn))
{
	size_t lframe_size, rframe_size;
	char *lbuf, *rbuf, *image;

	encheck_frame_size(status, left->width,  left->height,  left->pixel_size,  0, left->file);
	encheck_frame_size(status, right->width, right->height, right->pixel_size, 0, right->file);
	lframe_size = left->height  * left->width  * left->pixel_size;
	rframe_size = right->height * right->width * right->pixel_size;

	if (lframe_size > SIZE_MAX - lframe_size || 2 * lframe_size > SIZE_MAX - rframe_size)
		enprintf(status, "video frame is too large\n");

	image = mmap(0, 2 * lframe_size + lframe_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (image == MAP_FAILED)
		enprintf(status, "mmap:");
	lbuf = image + 1 * lframe_size;
	rbuf = image + 2 * lframe_size;

	for (;;) {
		if (!enread_frame(status, left, lbuf, lframe_size)) {
			close(left->fd);
			left->fd = -1;
			break;
		}
		if (!enread_frame(status, right, rbuf, rframe_size)) {
			close(right->fd);
			right->fd = -1;
			break;
		}

		process(image, lbuf, rbuf, left, right, lframe_size, rframe_size);
		enwriteall(status, output_fd, image, lframe_size, output_fname);
	}

	if (right->fd >= 0)
		close(right->fd);

	if (left->fd >= 0) {
		memcpy(image, lbuf, left->ptr);
		while (enread_frame(status, left, lbuf, lframe_size))
			enwriteall(status, output_fd, image, lframe_size, output_fname);
	}

	free(lbuf);
	free(rbuf);
	munmap(image, 2 * lframe_size + rframe_size);
}
