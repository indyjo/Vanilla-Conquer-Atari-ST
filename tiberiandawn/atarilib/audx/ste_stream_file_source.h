#ifndef ATARILIB_AUDX_STE_STREAM_FILE_SOURCE_H_
#define ATARILIB_AUDX_STE_STREAM_FILE_SOURCE_H_

#include "ste_stream_source.h"

#include "ccfile.h"

#include <stdint.h>

class SteStreamFileSource final : public SteStreamSource {
public:
	SteStreamFileSource();
	~SteStreamFileSource() override;

	int bind(uint16_t pool_id, uint32_t begin, uint32_t size);

	/** Stream a named file span (e.g. MIX-hosted AUD payload) without loading it all. */
	int bind_named(char const *filename, uint32_t begin, uint32_t size);

	void reset() override;
	void rewind() override;
	unsigned long size() const override;
	int at_end() const override;
	unsigned long read(unsigned char *dst, unsigned long n) override;
	unsigned long skip(unsigned long n) override;

private:
	enum { READAHEAD = 2048, NAMED_CAP = 24 };

	int ensure_open_();
	void close_();

	uint16_t pool_id_;
	char named_[NAMED_CAP];
	uint32_t begin_;
	uint32_t size_;
	uint32_t pos_;
	int file_open_;
	CCFileClass file_;
	unsigned char ahead_[READAHEAD];
	uint32_t ahead_file_pos_; /* absolute file offset of ahead_[0] */
	uint32_t ahead_len_;
};

#endif /* ATARILIB_AUDX_STE_STREAM_FILE_SOURCE_H_ */
