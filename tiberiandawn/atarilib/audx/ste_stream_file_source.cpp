#include "ste_stream_file_source.h"

#include "audx.h"
#include "function.h"

#include <new>
#include <string.h>

SteStreamFileSource::SteStreamFileSource()
    : pool_id_(0), begin_(0), size_(0), pos_(0), file_(0), ahead_file_pos_(0), ahead_len_(0)
{
	named_[0] = '\0';
}

SteStreamFileSource::~SteStreamFileSource()
{
	close_();
}

void SteStreamFileSource::close_()
{
	if (file_) {
		CCFileClass *f = (CCFileClass *)file_;
		if (f->Is_Open())
			f->Close();
		delete f;
		file_ = 0;
	}
	ahead_len_ = 0;
}

int SteStreamFileSource::ensure_open_()
{
	char name[16];
	char const *open_name;
	CCFileClass *f;

	if (file_)
		return 1;
	if (named_[0] != '\0') {
		open_name = named_;
	} else {
		if (!AUDX_Format_Pool_Name(pool_id_, name, sizeof(name)))
			return 0;
		open_name = name;
	}
	f = new (std::nothrow) CCFileClass(open_name);
	if (!f)
		return 0;
	if (!f->Is_Available() || !f->Open(READ)) {
		delete f;
		return 0;
	}
	file_ = f;
	return 1;
}

int SteStreamFileSource::bind(uint16_t pool_id, uint32_t begin, uint32_t size)
{
	reset();
	if (pool_id == 0 || size == 0)
		return 0;
	pool_id_ = pool_id;
	named_[0] = '\0';
	begin_ = begin;
	size_ = size;
	pos_ = 0;
	if (!ensure_open_()) {
		reset();
		return 0;
	}
	return 1;
}

int SteStreamFileSource::bind_named(char const *filename, uint32_t begin, uint32_t size)
{
	reset();
	if (!filename || !filename[0] || size == 0)
		return 0;
	if (strlen(filename) >= sizeof(named_))
		return 0;
	strncpy(named_, filename, sizeof(named_) - 1);
	named_[sizeof(named_) - 1] = '\0';
	pool_id_ = 0;
	begin_ = begin;
	size_ = size;
	pos_ = 0;
	if (!ensure_open_()) {
		reset();
		return 0;
	}
	return 1;
}

void SteStreamFileSource::reset()
{
	close_();
	pool_id_ = 0;
	named_[0] = '\0';
	begin_ = 0;
	size_ = 0;
	pos_ = 0;
	ahead_file_pos_ = 0;
	ahead_len_ = 0;
}

void SteStreamFileSource::rewind()
{
	pos_ = 0;
	ahead_len_ = 0;
}

unsigned long SteStreamFileSource::size() const
{
	return (unsigned long)size_;
}

unsigned long SteStreamFileSource::read(unsigned char *dst, unsigned long n)
{
	unsigned long got = 0UL;
	CCFileClass *f;

	if (!dst || !file_ || n == 0UL || pos_ >= size_)
		return 0UL;
	if (n > (unsigned long)(size_ - pos_))
		n = (unsigned long)(size_ - pos_);

	f = (CCFileClass *)file_;
	while (got < n) {
		uint32_t const abs = begin_ + pos_ + (uint32_t)got;
		unsigned long take;

		if (!(ahead_len_ > 0 && abs >= ahead_file_pos_ && abs < ahead_file_pos_ + ahead_len_)) {
			uint32_t want = READAHEAD;
			uint32_t const rem = (begin_ + size_) - abs;
			if (want > rem)
				want = rem;
			if (want == 0)
				break;
			if (f->Seek((long)abs, SEEK_SET) != (long)abs)
				break;
			long const rd = f->Read(ahead_, (long)want);
			if (rd <= 0)
				break;
			ahead_file_pos_ = abs;
			ahead_len_ = (uint32_t)rd;
		}

		{
			uint32_t const off = abs - ahead_file_pos_;
			take = (unsigned long)(ahead_len_ - off);
			if (take > n - got)
				take = n - got;
			memcpy(dst + got, ahead_ + off, (size_t)take);
			got += take;
		}
	}
	pos_ += (uint32_t)got;
	return got;
}

unsigned long SteStreamFileSource::skip(unsigned long n)
{
	if (!file_ || n == 0UL || pos_ >= size_)
		return 0UL;
	if (n > (unsigned long)(size_ - pos_))
		n = (unsigned long)(size_ - pos_);
	pos_ += (uint32_t)n;
	ahead_len_ = 0;
	return n;
}
