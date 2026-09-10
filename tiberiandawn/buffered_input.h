#ifndef BUFFERED_INPUT_H
#define BUFFERED_INPUT_H

#include <string.h>

/*
 * 1 KiB read-ahead decorator. Stream needs only:
 *   int Read(void *buffer, int size);
 * Prefetch is sequential; Seek/Write on the underlying stream is not tracked.
 */
class BufferedInput
{
public:
	enum { BUF_BYTES = 1024 };

	BufferedInput(void)
	    : src_(0)
	    , read_(0)
	    , pos_(0)
	    , end_(0)
	{
	}

	template <typename Stream> void Bind(Stream& stream)
	{
		src_ = &stream;
		read_ = &BufferedInput::read_thunk<Stream>;
		pos_ = 0;
		end_ = 0;
	}

	void Unbind(void)
	{
		src_ = 0;
		read_ = 0;
		pos_ = 0;
		end_ = 0;
	}

	int Read(void* buffer, int size)
	{
		if (!buffer || size <= 0 || !src_ || !read_)
			return 0;

		unsigned char* out = (unsigned char*)buffer;
		int got = 0;

		while (got < size) {
			if (pos_ >= end_) {
				end_ = read_(src_, buf_, BUF_BYTES);
				pos_ = 0;
				if (end_ <= 0) {
					end_ = 0;
					break;
				}
			}

			int n = end_ - pos_;
			int want = size - got;
			if (n > want)
				n = want;
			memcpy(out + got, buf_ + pos_, (size_t)n);
			pos_ += n;
			got += n;
		}

		return got;
	}

private:
	typedef int (*ReadFn)(void* src, void* buffer, int size);

	template <typename Stream> static int read_thunk(void* src, void* buffer, int size)
	{
		return ((Stream*)src)->Read(buffer, size);
	}

	void* src_;
	ReadFn read_;
	unsigned char buf_[BUF_BYTES];
	int pos_;
	int end_;
};

#endif
