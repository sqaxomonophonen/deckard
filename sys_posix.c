#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "sys.h"

int sys_mmap_file_ro(struct sys_mmap_file* mf, const char* path)
{
	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		return -1;
	}

	struct stat st;
	if (fstat(fd, &st) == -1) {
		return -1;
	}

	mf->sz = st.st_size;
	mf->ptr = mmap(NULL, mf->sz, PROT_READ, MAP_PRIVATE, fd, 0);

	if (mf->ptr == MAP_FAILED) {
		return -1;
	}

	if (close(fd) == -1) {
		return -1;
	}

	return 0;
}

void sys_munmap_file(struct sys_mmap_file* mf)
{
	munmap(mf->ptr, mf->sz);
}
