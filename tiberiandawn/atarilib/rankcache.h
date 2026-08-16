/*
 * rankcache.h — Fixed-capacity ranked directory (no STL containers).
 *
 * Directory nodes are a packed array: index 0 is MRU, capacity-1 is LRU.
 * Lookup is a linear scan. A hit bubbles the entry one step toward MRU
 * (neighbor swap). A miss rekeys the LRU node and shifts it to index 0
 * so consecutive inserts do not thrash one slot.
 *
 * Value payloads (e.g. slab pointers) stay with the directory node that
 * owns them; pointed-to memory is never moved by the cache.
 *
 * get / retarget_oldest assign Value by copy (pointers remain valid for
 * in-place updates through the pointee). Optional Pred continues the scan
 * when Key matches but the value is rejected (e.g. hash collision).
 */

#ifndef ATARILIB_RANKCACHE_H_
#define ATARILIB_RANKCACHE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
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
	}

	uint16_t capacity() const { return capacity_; }
	bool valid() const { return nodes_ != nullptr && capacity_ > 0; }

	/* Index write (init / reseed). Index 0 is MRU. */
	void set(uint16_t i, const Key &key, const Value &value)
	{
		if (!nodes_ || i >= capacity_)
			return;
		nodes_[i].key = key;
		nodes_[i].value = value;
	}

	/*
	 * Linear scan from MRU. On hit at rank i>0, swap with predecessor.
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

		for (uint16_t i = 0; i < capacity_; ++i) {
			if (!(nodes_[i].key == key))
				continue;
			if (!accept(nodes_[i].value))
				continue;
			if (i > 0) {
				Node tmp = nodes_[i];
				nodes_[i] = nodes_[i - 1];
				nodes_[i - 1] = tmp;
				value_out = nodes_[i - 1].value;
			} else {
				value_out = nodes_[i].value;
			}
			return true;
		}
		return false;
	}

	/*
	 * Rekey the LRU node (same Value / slab) and make it MRU.
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

		for (uint16_t i = 0; i < capacity_; ++i) {
			if (nodes_[i].key == key && already_has(nodes_[i].value))
				return false;
		}

		promote_lru_to_mru(key, value_out);
		return true;
	}

	/*
	 * Like retarget_oldest, but only rekeys a node whose Value passes can_evict
	 * (e.g. pin_count == 0). Scans from LRU toward MRU; swaps the chosen node
	 * into the LRU slot, then promotes it to MRU.
	 */
	template <typename CanEvict>
	bool retarget_oldest_evictable(const Key &key, Value &value_out, CanEvict can_evict)
	{
		if (!nodes_ || capacity_ == 0)
			return false;

		for (uint16_t i = 0; i < capacity_; ++i) {
			if (nodes_[i].key == key)
				return false;
		}

		int chosen = -1;
		for (int i = (int)capacity_ - 1; i >= 0; --i) {
			if (can_evict(nodes_[i].value)) {
				chosen = i;
				break;
			}
		}
		if (chosen < 0)
			return false;

		const uint16_t last = (uint16_t)(capacity_ - 1);
		if ((uint16_t)chosen != last) {
			Node tmp = nodes_[chosen];
			nodes_[chosen] = nodes_[last];
			nodes_[last] = tmp;
		}

		promote_lru_to_mru(key, value_out);
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

	void promote_lru_to_mru(const Key &key, Value &value_out)
	{
		const uint16_t last = (uint16_t)(capacity_ - 1);
		const Value keep = nodes_[last].value;
		if (last > 0)
			memmove(&nodes_[1], &nodes_[0], (size_t)last * sizeof(Node));
		nodes_[0].key = key;
		nodes_[0].value = keep;
		value_out = keep;
	}

	Node *nodes_;
	uint16_t capacity_;
};

#endif /* ATARILIB_RANKCACHE_H_ */
