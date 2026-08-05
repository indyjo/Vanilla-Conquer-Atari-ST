#include "ste_stream_memory_source.h"

#include <string.h>

SteStreamMemorySource::SteStreamMemorySource() : base_(0), size_(0UL), pos_(0UL) {}

int SteStreamMemorySource::bind(unsigned char const *data, unsigned long len)
{
	reset();
	if (!data || len == 0UL) {
		return 0;
	}
	base_ = data;
	size_ = len;
	pos_ = 0UL;
	return 1;
}

void SteStreamMemorySource::reset()
{
	base_ = 0;
	size_ = 0UL;
	pos_ = 0UL;
}

void SteStreamMemorySource::rewind()
{
	pos_ = 0UL;
}

unsigned long SteStreamMemorySource::size() const
{
	return size_;
}

int SteStreamMemorySource::at_end() const
{
	return size_ == 0UL || pos_ >= size_;
}

unsigned long SteStreamMemorySource::read(unsigned char *dst, unsigned long n)
{
	if (!base_ || !dst || n == 0UL || pos_ >= size_) {
		return 0UL;
	}
	unsigned long const left = size_ - pos_;
	if (n > left) {
		n = left;
	}
	memcpy(dst, base_ + pos_, (size_t)n);
	pos_ += n;
	return n;
}

unsigned long SteStreamMemorySource::skip(unsigned long n)
{
	if (!base_ || n == 0UL || pos_ >= size_) {
		return 0UL;
	}
	unsigned long const left = size_ - pos_;
	if (n > left) {
		n = left;
	}
	pos_ += n;
	return n;
}
