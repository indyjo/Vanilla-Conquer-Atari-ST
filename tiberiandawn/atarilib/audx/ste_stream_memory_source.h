#ifndef ATARILIB_AUDX_STE_STREAM_MEMORY_SOURCE_H_
#define ATARILIB_AUDX_STE_STREAM_MEMORY_SOURCE_H_

#include "ste_stream_source.h"

class SteStreamMemorySource final : public SteStreamSource {
public:
	SteStreamMemorySource();

	/* Bind to an external buffer (not owned). Returns 0 on failure. */
	int bind(unsigned char const *data, unsigned long len);

	void reset() override;
	void rewind() override;
	unsigned long size() const override;
	int at_end() const override;
	unsigned long read(unsigned char *dst, unsigned long n) override;
	unsigned long skip(unsigned long n) override;

private:
	unsigned char const *base_;
	unsigned long size_;
	unsigned long pos_;
};

#endif /* ATARILIB_AUDX_STE_STREAM_MEMORY_SOURCE_H_ */
