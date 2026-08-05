#include "ste_stream_page_ring_source.h"

#include "audx_page_cache.h"
#include "audx_pool_file.h"

#include <string.h>

#if defined(__GNUC__) && (defined(__mc68000__) || defined(__M68K__) || defined(__m68k__))
static inline unsigned short page_ring_ipl5_lock(void)
{
	unsigned short t;
	__asm__ __volatile__(
	    "move.w %%sr,%0\n\t"
	    "move.w %0,%%d1\n\t"
	    "andi.w #0xF8FF,%%d1\n\t"
	    "ori.w #0x0500,%%d1\n\t"
	    "move.w %%d1,%%sr"
	    : "=d"(t)
	    :
	    : "d1", "cc", "memory");
	return t;
}
static inline void page_ring_ipl5_restore(unsigned short t)
{
	__asm__ __volatile__("move.w %0,%%sr" : : "d"(t) : "cc", "memory");
}
#else
static inline unsigned short page_ring_ipl5_lock(void)
{
	return 0;
}
static inline void page_ring_ipl5_restore(unsigned short) {}
#endif

SteStreamPageRingSource::SteStreamPageRingSource()
    : pool_id_(0), begin_(0), size_(0), fill_pos_(0), read_pos_(0), use_stream_(0), write_i_(0), read_i_(0), count_(0)
{
	memset(slots_, 0, sizeof(slots_));
}

void SteStreamPageRingSource::clear_slots_()
{
	unsigned short const sr = page_ring_ipl5_lock();
	for (unsigned i = 0; i < AUDX_PAGE_RING_SLOTS; ++i) {
		if (slots_[i].page) {
			AUDX_Page_Unpin(slots_[i].page);
			slots_[i].page = 0;
		}
		slots_[i].data_off = 0;
		slots_[i].valid_len = 0;
		slots_[i].consumed = 0;
	}
	write_i_ = 0;
	read_i_ = 0;
	count_ = 0;
	page_ring_ipl5_restore(sr);
}

int SteStreamPageRingSource::bind(uint16_t pool_id, uint32_t begin, uint32_t size, int use_stream)
{
	reset();
	if (pool_id == 0 || size == 0)
		return 0;
	if (!use_stream && size > AUDX_PAGE_CACHE_MAX)
		return 0;
	if (!AUDX_Page_Cache_Is_Inited())
		return 0;
	pool_id_ = pool_id;
	begin_ = begin;
	size_ = size;
	fill_pos_ = 0;
	read_pos_ = 0;
	use_stream_ = use_stream ? 1 : 0;
	return 1;
}

void SteStreamPageRingSource::reset()
{
	clear_slots_();
	pool_id_ = 0;
	begin_ = 0;
	size_ = 0;
	fill_pos_ = 0;
	read_pos_ = 0;
	use_stream_ = 0;
}

void SteStreamPageRingSource::rewind()
{
	clear_slots_();
	fill_pos_ = 0;
	read_pos_ = 0;
}

unsigned long SteStreamPageRingSource::size() const
{
	return (unsigned long)size_;
}

int SteStreamPageRingSource::at_end() const
{
	return size_ == 0 || read_pos_ >= size_;
}

int SteStreamPageRingSource::fill_one_slot_()
{
	uint32_t file_off;
	uint32_t page_off;
	uint32_t remain;
	uint32_t valid;
	uint8_t const *page = 0;
	uint8_t *stream_page;
	uint8_t wi;
	Slot *slot;

	if (pool_id_ == 0 || fill_pos_ >= size_ || count_ >= AUDX_PAGE_RING_SLOTS)
		return 0;

	file_off = begin_ + fill_pos_;
	page_off = file_off % AUDX_PAGE_SIZE;
	remain = size_ - fill_pos_;
	valid = AUDX_PAGE_SIZE - page_off;
	if (valid > remain)
		valid = remain;
	if (valid == 0)
		return 0;

	if (use_stream_) {
		uint32_t const page_begin = (file_off / AUDX_PAGE_SIZE) * AUDX_PAGE_SIZE;
		stream_page = AUDX_Stream_Page_Acquire();
		if (!stream_page)
			return 0;
		if (!AUDX_Pool_Read(pool_id_, page_begin, AUDX_PAGE_SIZE, stream_page)) {
			memset(stream_page, 0, AUDX_PAGE_SIZE);
			return 0;
		}
		page = stream_page;
	} else {
		page = AUDX_Page_Get(pool_id_, file_off);
		if (!page)
			return 0;
	}

	AUDX_Page_Pin(page);

	wi = write_i_;
	slot = &slots_[wi];
	/* Reclaim prior occupant of this physical slot (already consumed). */
	if (slot->page) {
		AUDX_Page_Unpin(slot->page);
		slot->page = 0;
	}
	slot->data_off = (uint16_t)page_off;
	slot->valid_len = (uint16_t)valid;
	slot->consumed = 0;
	/* Publish pointer last, then bump count under IPL-5. */
	slot->page = page;

	{
		unsigned short const sr = page_ring_ipl5_lock();
		write_i_ = (uint8_t)((wi + 1u) % AUDX_PAGE_RING_SLOTS);
		count_ = (uint8_t)(count_ + 1u);
		page_ring_ipl5_restore(sr);
	}

	fill_pos_ += valid;
	return 1;
}

void SteStreamPageRingSource::service()
{
	if (pool_id_ == 0)
		return;
	while (count_ < AUDX_PAGE_RING_SLOTS && fill_pos_ < size_) {
		if (!fill_one_slot_())
			break;
	}
}

unsigned long SteStreamPageRingSource::consume_(unsigned char *dst, unsigned long n)
{
	unsigned long got = 0UL;

	if (pool_id_ == 0 || n == 0UL || read_pos_ >= size_)
		return 0UL;
	if (n > (unsigned long)(size_ - read_pos_))
		n = (unsigned long)(size_ - read_pos_);

	while (got < n) {
		Slot *slot;
		uint16_t avail;
		unsigned long take;
		uint8_t ri;

		if (count_ == 0)
			break;

		ri = read_i_;
		slot = &slots_[ri];
		if (!slot->page || slot->consumed >= slot->valid_len) {
			/* Slot published empty or already drained — advance. */
			unsigned short const sr = page_ring_ipl5_lock();
			if (count_ == 0) {
				page_ring_ipl5_restore(sr);
				break;
			}
			read_i_ = (uint8_t)((ri + 1u) % AUDX_PAGE_RING_SLOTS);
			count_ = (uint8_t)(count_ - 1u);
			page_ring_ipl5_restore(sr);
			continue;
		}

		avail = (uint16_t)(slot->valid_len - slot->consumed);
		take = n - got;
		if (take > (unsigned long)avail)
			take = (unsigned long)avail;
		if (dst)
			memcpy(dst + got, slot->page + slot->data_off + slot->consumed, (size_t)take);
		slot->consumed = (uint16_t)(slot->consumed + (uint16_t)take);
		got += take;

		if (slot->consumed >= slot->valid_len) {
			unsigned short const sr = page_ring_ipl5_lock();
			read_i_ = (uint8_t)((read_i_ + 1u) % AUDX_PAGE_RING_SLOTS);
			if (count_ > 0)
				count_ = (uint8_t)(count_ - 1u);
			page_ring_ipl5_restore(sr);
		}
	}

	read_pos_ += (uint32_t)got;
	return got;
}

unsigned long SteStreamPageRingSource::read(unsigned char *dst, unsigned long n)
{
	if (!dst)
		return 0UL;
	return consume_(dst, n);
}

unsigned long SteStreamPageRingSource::skip(unsigned long n)
{
	return consume_(0, n);
}
