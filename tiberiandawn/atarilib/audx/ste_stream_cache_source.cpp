#include "ste_stream_cache_source.h"

#include "audx.h"
#include "audx_page_cache.h"

#include <string.h>

SteStreamCacheSource::SteStreamCacheSource() : pool_id_(0), begin_(0), size_(0), pos_(0) {}

int SteStreamCacheSource::bind(uint16_t pool_id, uint32_t begin, uint32_t size)
{
	reset();
	if (pool_id == 0 || size == 0 || size > AUDX_PAGE_CACHE_MAX)
		return 0;
	if (!AUDX_Page_Cache_Is_Inited())
		return 0;
	pool_id_ = pool_id;
	begin_ = begin;
	size_ = size;
	pos_ = 0;
	return 1;
}

void SteStreamCacheSource::reset()
{
	pool_id_ = 0;
	begin_ = 0;
	size_ = 0;
	pos_ = 0;
}

void SteStreamCacheSource::rewind()
{
	pos_ = 0;
}

unsigned long SteStreamCacheSource::size() const
{
	return (unsigned long)size_;
}

unsigned long SteStreamCacheSource::read(unsigned char *dst, unsigned long n)
{
	unsigned long got = 0UL;

	if (!dst || pool_id_ == 0 || n == 0UL || pos_ >= size_)
		return 0UL;
	if (n > (unsigned long)(size_ - pos_))
		n = (unsigned long)(size_ - pos_);

	while (got < n) {
		uint32_t const file_off = begin_ + pos_ + (uint32_t)got;
		uint32_t const page_off = file_off % AUDX_PAGE_SIZE;
		uint32_t const chunk = AUDX_PAGE_SIZE - page_off;
		unsigned long take = n - got;
		uint8_t const *page;

		if (take > (unsigned long)chunk)
			take = (unsigned long)chunk;
		page = AUDX_Page_Get(pool_id_, file_off);
		if (!page)
			break;
		memcpy(dst + got, page + page_off, (size_t)take);
		got += take;
	}
	pos_ += (uint32_t)got;
	return got;
}

unsigned long SteStreamCacheSource::skip(unsigned long n)
{
	if (pool_id_ == 0 || n == 0UL || pos_ >= size_)
		return 0UL;
	if (n > (unsigned long)(size_ - pos_))
		n = (unsigned long)(size_ - pos_);
	pos_ += (uint32_t)n;
	return n;
}
