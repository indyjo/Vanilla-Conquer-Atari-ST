#ifndef ATARILIB_AUDX_STE_STREAM_CACHE_SOURCE_H_
#define ATARILIB_AUDX_STE_STREAM_CACHE_SOURCE_H_

#include "ste_stream_source.h"

#include <stdint.h>

class SteStreamCacheSource final : public SteStreamSource {
public:
	SteStreamCacheSource();

	/* Bind to an AUDX pool span (size should be <= AUDX_PAGE_CACHE_MAX). */
	int bind(uint16_t pool_id, uint32_t begin, uint32_t size);

	void reset() override;
	void rewind() override;
	unsigned long size() const override;
	unsigned long read(unsigned char *dst, unsigned long n) override;
	unsigned long skip(unsigned long n) override;

private:
	uint16_t pool_id_;
	uint32_t begin_;
	uint32_t size_;
	uint32_t pos_;
};

#endif /* ATARILIB_AUDX_STE_STREAM_CACHE_SOURCE_H_ */
