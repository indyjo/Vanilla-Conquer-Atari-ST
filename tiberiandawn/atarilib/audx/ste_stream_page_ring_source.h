#ifndef ATARILIB_AUDX_STE_STREAM_PAGE_RING_SOURCE_H_
#define ATARILIB_AUDX_STE_STREAM_PAGE_RING_SOURCE_H_

#include "audx.h"
#include "ste_stream_source.h"

#include <stdint.h>

/**
 * Page-pointer ring source: main thread pins/fills RankCache or stream slabs;
 * VBL read/skip only copies from already-published slots (no GEMDOS).
 */
class SteStreamPageRingSource final : public SteStreamSource {
public:
	SteStreamPageRingSource();

	/* Bind AUDX pool span. use_stream != 0 for payloads > AUDX_PAGE_CACHE_MAX. */
	int bind(uint16_t pool_id, uint32_t begin, uint32_t size, int use_stream);

	/** Main-thread refill: pin/fill until ring full or EOF. May call GEMDOS. */
	void service();

	void reset() override;
	void rewind() override;
	unsigned long size() const override;
	int at_end() const override;
	unsigned long read(unsigned char *dst, unsigned long n) override;
	unsigned long skip(unsigned long n) override;

private:
	struct Slot {
		uint8_t const *page;
		uint16_t data_off;
		uint16_t valid_len;
		uint16_t consumed; /* bytes already taken from this slot */
	};

	void clear_slots_();
	int fill_one_slot_();
	unsigned long consume_(unsigned char *dst, unsigned long n);

	uint16_t pool_id_;
	uint32_t begin_;
	uint32_t size_;
	uint32_t fill_pos_; /* next absolute span offset to fill into the ring */
	uint32_t read_pos_; /* logical consumer offset (for size accounting) */
	int use_stream_;
	Slot slots_[AUDX_PAGE_RING_SLOTS];
	uint8_t write_i_;
	volatile uint8_t read_i_;
	volatile uint8_t count_;
};

#endif /* ATARILIB_AUDX_STE_STREAM_PAGE_RING_SOURCE_H_ */
