/*
 * ste_stream_source.h — Sequential byte source for SteStreamFormat pull/skip.
 */

#ifndef ATARILIB_AUDX_STE_STREAM_SOURCE_H_
#define ATARILIB_AUDX_STE_STREAM_SOURCE_H_

class SteStreamSource {
public:
	virtual ~SteStreamSource() = default;

	/* Read up to n bytes at the current logical offset; advance. Returns bytes read. */
	virtual unsigned long read(unsigned char *dst, unsigned long n) = 0;
	virtual unsigned long skip(unsigned long n) = 0;
	virtual unsigned long size() const = 0;
	virtual void rewind() = 0;
	virtual void reset() = 0;

	/*
	 * True when no more payload bytes will ever be available (logical EOF).
	 * False on a temporary stall (e.g. page-ring underrun with read_pos still in span).
	 */
	virtual int at_end() const = 0;

protected:
	SteStreamSource() = default;
};

#endif /* ATARILIB_AUDX_STE_STREAM_SOURCE_H_ */
