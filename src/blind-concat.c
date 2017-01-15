/* See LICENSE file for copyright and license details. */
#include "stream.h"
#include "util.h"

#if defined(HAVE_EPOLL)
# include <sys/epoll.h>
#endif
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

USAGE("[-o output-file [-j jobs]] first-stream ... last-stream")

static void
concat_to_stdout(int argc, char *argv[], const char *fname)
{
	struct stream *streams;
	size_t frames = 0;
	int i;

	streams = emalloc((size_t)argc * sizeof(*streams));

	for (i = 0; i < argc; i++) {
		streams[i].file = argv[i];
		streams[i].fd = eopen(streams[i].file, O_RDONLY);
		einit_stream(streams + i);
		if (i)
			echeck_compat(streams + i, streams);
		if (streams[i].frames > SIZE_MAX - frames)
			eprintf("resulting video is too long\n");
		frames += streams[i].frames;
	}

	streams->frames = frames;
	fprint_stream_head(stdout, streams);
	efflush(stdout, fname);

	for (i = 0; i < argc; i++) {
		for (; eread_stream(streams + i, SIZE_MAX); streams[i].ptr = 0)
			ewriteall(STDOUT_FILENO, streams[i].buf, streams[i].ptr, fname);
		close(streams[i].fd);
	}

	free(streams);
}

static void
concat_to_file(int argc, char *argv[], char *output_file)
{
	struct stream stream, refstream;
	int first = 1;
	int fd = eopen(output_file, O_RDWR | O_CREAT | O_TRUNC, 0666);
	char head[STREAM_HEAD_MAX];
	ssize_t headlen, size = 0;
	char *data;

	for (; argc--; argv++) {
		stream.file = *argv;
		stream.fd = eopen(stream.file, O_RDONLY);
		einit_stream(&stream);

		if (first) {
			refstream = stream;
			first = 1;
		} else {
			if (refstream.frames > SIZE_MAX - stream.frames)
				eprintf("resulting video is too long\n");
			refstream.frames += stream.frames;
			echeck_compat(&stream, &refstream);
		}

		for (; eread_stream(&stream, SIZE_MAX); stream.ptr = 0) {
			ewriteall(fd, stream.buf, stream.ptr, output_file);
			size += stream.ptr;
		}
		close(stream.fd);
	}

	sprintf(head, "%zu %zu %zu %s\n%cuivf%zn",
		stream.frames, stream.width, stream.height, stream.pixfmt, 0, &headlen);

	ewriteall(fd, head, (size_t)headlen, output_file);

	data = mmap(0, size + (size_t)headlen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED)
		eprintf("mmap %s:", output_file);
	memmove(data + headlen, data, size);
	memcpy(data, head, (size_t)headlen);
	munmap(data, size + (size_t)headlen);

	close(fd);
}

static void
concat_to_file_parallel(int argc, char *argv[], char *output_file, size_t jobs)
{
#if !defined(HAVE_EPOLL)
	int fd = eopen(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd != STDOUT_FILENO && dup2(fd, STDOUT_FILENO) == -1)
		eprintf("dup2:");
	concat_to_stdout(argc, argv, output_file);
#else
	struct epoll_event *events;
	struct stream *streams;
	size_t *ptrs;
	char head[STREAM_HEAD_MAX];
	size_t ptr, frame_size, frames = 0, next = 0, j;
	ssize_t headlen;
	int fd, i, n, pollfd;

	if (jobs > (size_t)argc)
		jobs = (size_t)argc;

	fd = eopen(output_file, O_RDWR | O_CREAT | O_TRUNC, 0666);
	events  = emalloc(jobs * sizeof(*events));
	streams = emalloc((size_t)argc * sizeof(*streams));
	ptrs    = emalloc((size_t)argc * sizeof(*ptrs));

	for (i = 0; i < argc; i++) {
		streams[i].file = argv[i];
		streams[i].fd = eopen(streams[i].file, O_RDONLY);
		einit_stream(streams + i);
		if (i)
			echeck_compat(streams + i, streams);
		if (streams[i].frames > SIZE_MAX - frames)
			eprintf("resulting video is too long\n");
		frames += streams[i].frames;
	}

	sprintf(head, "%zu %zu %zu %s\n%cuivf%zn",
		frames, streams->width, streams->height, streams->pixfmt, 0, &headlen);

	echeck_frame_size(streams->width, streams->height, streams->pixel_size, 0, output_file);
	frame_size = streams->width * streams->height * streams->pixel_size;
	ptr = (size_t)headlen;
	for (i = 0; i < argc; i++) {
		ptrs[i] = ptr;
		ptr += streams->frames * frame_size;
	}
	if (ftruncate(fd, (off_t)ptr))
		eprintf("ftruncate %s:", output_file);
#if defined(POSIX_FADV_RANDOM)
        posix_fadvise(fd, (size_t)headlen, 0, POSIX_FADV_RANDOM);
#endif

	pollfd = epoll_create1(0);
	if (pollfd == -1)
		eprintf("epoll_create1:");

	epwriteall(fd, head, (size_t)headlen, 0, output_file);
	for (i = 0; i < argc; i++) {
		epwriteall(fd, streams[i].buf, streams[i].ptr, ptrs[i], output_file);
		ptrs[i] += streams[i].ptr;
		streams[i].ptr = 0;
	}

	for (j = 0; j < jobs; j++, next++) {
		events->events = EPOLLIN;
		events->data.u64 = next;
		if (epoll_ctl(pollfd, EPOLL_CTL_ADD, streams[next].fd, events) == -1) {
			if ((errno == ENOMEM || errno == ENOSPC) && j)
				break;
			eprintf("epoll_ctl:");
		}
	}
	jobs = j;

	while (jobs) {
		n = epoll_wait(pollfd, events, jobs, -1);
		if (n < 0)
			eprintf("epoll_wait:");
		for (i = 0; i < n; i++) {
			j = events[i].data.u64;
			if (!eread_stream(streams + j, SIZE_MAX)) {
				close(streams[j].fd);
				if (next < (size_t)argc) {
					events->events = EPOLLIN;
					events->data.u64 = next;
					if (epoll_ctl(pollfd, EPOLL_CTL_ADD, streams[next].fd, events) == -1) {
						if ((errno == ENOMEM || errno == ENOSPC) && j)
							break;
						eprintf("epoll_ctl:");
					}
					next++;
				} else {
					jobs--;
				}
			} else {
				epwriteall(fd, streams[j].buf, streams[j].ptr, ptrs[j], output_file);
				ptrs[j] += streams[j].ptr;
				streams[j].ptr = 0;
			}
		}
	}

	close(pollfd);
	free(events);
	free(streams);
	free(ptrs);
#endif
}

int
main(int argc, char *argv[])
{
	char *output_file = NULL;
	size_t jobs = 0;

	ARGBEGIN {
	case 'o':
		output_file = EARG();
		break;
	case 'j':
		jobs = etozu_flag('j', EARG(), 1, SHRT_MAX);
		break;
	default:
		usage();
	} ARGEND;

	if (argc < 2 || (jobs && !output_file))
		usage();

	if (jobs)
		concat_to_file_parallel(argc, argv, output_file, jobs);
	else if (output_file)
		concat_to_file(argc, argv, output_file);
	else
		concat_to_stdout(argc, argv, "<stdout>");

	return 0;
}
