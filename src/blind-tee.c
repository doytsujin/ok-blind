/* See LICENSE file for copyright and license details. */
#include "common.h"

USAGE("[file ...]")

#if !defined(PIPE_BUF)
# define PIPE_BUF BUFSIZ
#endif

int
main(int argc, char *argv[])
{
	char buf[PIPE_BUF];
	int *fds = alloca(argc * sizeof(*fds));
	size_t i, n = 0, done;
	ssize_t r, w, *ps;

	UNOFLAGS(0);

	fds[n++] = STDOUT_FILENO;
	while (argc--)
		fds[n++] = eopen(*argv++, O_WRONLY | O_CREAT | O_TRUNC, 0666);

	ps = alloca(n * sizeof(*ps));

	while (n) {
		memset(ps, 0, n * sizeof(*ps));
		r = read(STDIN_FILENO, buf, sizeof(buf));
		if (r < 0)
			eprintf("read <stdin>:");
		if (!r)
			break;
		for (done = 0; done < n;) {
			for (i = 0; i < n; i++) {
				if (ps[i] == r)
					continue;
				w = write(fds[i], buf + ps[i], r - ps[i]);
				if (w < 0) {
					close(fds[i]);
					n--;
					memmove(fds + i, fds + i + 1, (n - i) * sizeof(*fds));
					memmove(ps  + i, ps  + i + 1, (n - i) * sizeof(*ps));
				}
				ps[i] += w;
				if (ps[i] == r)
					done++;
			}
		}
	}

	return 0;
}
