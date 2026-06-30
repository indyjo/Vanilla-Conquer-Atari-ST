/*
 * Minimal CCFileClass stub for remix ST16 builds (weights loaded via remix_st16, not disk).
 * Shadowed ahead of common/ccfile.h via -I. in tools/remix/makefile.
 */
#ifndef CCFILE_H
#define CCFILE_H

#ifndef READ
enum
{
	READ = 0,
	WRITE = 1
};
#endif

class CCFileClass
{
public:
	bool Open(char const *, int) { return false; }
	long Read(void *, long) { return 0; }
	void Close() {}
};

#endif /* CCFILE_H */
