/*
 * rankcache.h — Fixed-capacity ranked ring cache (no STL containers).
 *
 * Directory nodes are ordered as a ring: head is MRU (logical rank 0),
 * head+capacity-1 is LRU. Lookup is a linear scan. A hit bubbles the
 * entry one step toward MRU (neighbor swap). A miss rekeys the LRU node
 * and rotates it to MRU so consecutive inserts do not thrash one slot.
 *
 * Value payloads (e.g. slab pointers) stay with the node; only directory
 * order changes — pointed-to memory is never moved by the cache.
 *
 * get / retarget_oldest assign Value by copy (pointers remain valid for
 * in-place updates through the pointee). Optional Pred continues the scan
 * when Key matches but the value is rejected (e.g. hash collision).
 */

#ifndef ATARILIB_RANKCACHE_H_
#define ATARILIB_RANKCACHE_H_

#include <stddef.h>
#include <stdint.h>
#include <new>

template <typename Key, typename Value>
class RankCache {
public:
	struct Node {
		Key key;
		Value value;
	};

	explicit RankCache(uint16_t capacity)
	    : nodes_(nullptr)
	    , capacity_(capacity)
	    , head_(0)
	{
		if (capacity_ == 0)
			return;
		nodes_ = new (std::nothrow) Node[(size_t)capacity_];
		if (!nodes_)
			capacity_ = 0;
	}

	~RankCache()
	{
		delete[] nodes_;
		nodes_ = nullptr;
		capacity_ = 0;
		head_ = 0;
	}

	uint16_t capacity() const { return capacity_; }
	bool valid() const { return nodes_ != nullptr && capacity_ > 0; }

	/* Physical index write (init / reseed). Does not change head. */
	void set(uint16_t phys_i, const Key &key, const Value &value)
	{
		if (!nodes_ || phys_i >= capacity_)
			return;
		nodes_[phys_i].key = key;
		nodes_[phys_i].value = value;
	}

	void reset_head() { head_ = 0; }

	/*
	 * Linear scan from MRU. On hit at logical rank i>0, swap with predecessor.
	 * value_out receives a copy of the live node Value after any promote.
	 */
	bool get(const Key &key, Value &value_out)
	{
		return get(key, value_out, AcceptAll());
	}

	template <typename Pred>
	bool get(const Key &key, Value &value_out, Pred accept)
	{
		if (!nodes_ || capacity_ == 0)
			return false;

		for (uint16_t logical = 0; logical < capacity_; ++logical) {
			const uint16_t p = phys(logical);
			if (!(nodes_[p].key == key))
				continue;
			if (!accept(nodes_[p].value))
				continue;
			if (logical > 0) {
				const uint16_t pred = phys((uint16_t)(logical - 1));
				Node tmp = nodes_[p];
				nodes_[p] = nodes_[pred];
				nodes_[pred] = tmp;
				value_out = nodes_[pred].value;
			} else {
				value_out = nodes_[p].value;
			}
			return true;
		}
		return false;
	}

	/*
	 * Rekey the LRU node (same Value / slab) and make it MRU via ring rotate.
	 * Returns false if empty or if key is already present (Key match + already_has).
	 */
	bool retarget_oldest(const Key &key, Value &value_out)
	{
		return retarget_oldest(key, value_out, AcceptAll());
	}

	template <typename Pred>
	bool retarget_oldest(const Key &key, Value &value_out, Pred already_has)
	{
		if (!nodes_ || capacity_ == 0)
			return false;

		for (uint16_t logical = 0; logical < capacity_; ++logical) {
			const uint16_t p = phys(logical);
			if (nodes_[p].key == key && already_has(nodes_[p].value))
				return false;
		}

		head_ = (head_ == 0) ? (uint16_t)(capacity_ - 1) : (uint16_t)(head_ - 1);
		nodes_[head_].key = key;
		value_out = nodes_[head_].value;
		return true;
	}

	Node const *nodes() const { return nodes_; }
	Node *nodes() { return nodes_; }

private:
	struct AcceptAll {
		template <typename V>
		bool operator()(const V &) const
		{
			return true;
		}
	};

	uint16_t phys(uint16_t logical) const
	{
		uint16_t p = (uint16_t)(head_ + logical);
		if (p >= capacity_)
			p = (uint16_t)(p - capacity_);
		return p;
	}

	Node *nodes_;
	uint16_t capacity_;
	uint16_t head_;
};

#endif /* ATARILIB_RANKCACHE_H_ */
