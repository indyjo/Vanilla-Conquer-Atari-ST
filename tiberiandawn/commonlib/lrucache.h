//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/*
 * lrucache.h - Fixed-capacity LRU cache (standard library only, C++11).
 *
 * Map lookup is average O(1); list maintains recency. A capacity of zero
 * means put() stores nothing and get() always misses.
 */

#ifndef LRUCACHE_H
#define LRUCACHE_H

#include <stddef.h>
#include <list>
#include <unordered_map>
#include <utility>

template <typename Key,
          typename Value,
          typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key> >
class LruCache {
public:
	typedef Key key_type;
	typedef Value mapped_type;
	typedef Hash hasher;
	typedef KeyEqual key_equal;

	explicit LruCache(size_t capacity) : capacity_(capacity) {}

	size_t capacity() const { return capacity_; }
	size_t size() const { return list_.size(); }
	bool empty() const { return list_.empty(); }

	void clear()
	{
		list_.clear();
		map_.clear();
	}

	bool get(const Key& key, Value& value_out)
	{
		typename MapType::iterator i = map_.find(key);
		if (i == map_.end()) {
			return false;
		}
		value_out = i->second->second;
		list_.splice(list_.begin(), list_, i->second);
		return true;
	}

	void put(const Key& key, Value value)
	{
		typename MapType::iterator i = map_.find(key);
		if (i != map_.end()) {
			i->second->second = std::move(value);
			list_.splice(list_.begin(), list_, i->second);
			return;
		}
		if (capacity_ == 0) {
			return;
		}
		if (list_.size() >= capacity_) {
			const Key& evict_key = list_.back().first;
			map_.erase(evict_key);
			list_.pop_back();
		}
		list_.emplace_front(key, std::move(value));
		map_[list_.front().first] = list_.begin();
	}

	/*
	 * Replace the LRU entry's key while keeping its Value payload (typically a fixed
	 * slot id into a parallel buffer slab). Preconditions: capacity > 0, cache holds
	 * at least capacity entries (prefilled). Undefined if map already contains new_key.
	 */
	bool retarget_oldest_slot(const Key& new_key, Value& slot_index_out)
	{
		if (capacity_ == 0 || list_.empty()) {
			return false;
		}
		if (map_.find(new_key) != map_.end()) {
			return false;
		}
		if (list_.size() < capacity_) {
			return false;
		}
		const Key old_k = list_.back().first;
		slot_index_out = list_.back().second;
		map_.erase(old_k);
		list_.pop_back();
		list_.emplace_front(new_key, slot_index_out);
		map_[list_.front().first] = list_.begin();
		return true;
	}

private:
	typedef std::pair<Key, Value> Item;
	typedef std::list<Item> ListType;
	typedef std::unordered_map<Key, typename ListType::iterator, Hash, KeyEqual> MapType;

	ListType list_;
	MapType map_;
	size_t capacity_;
};

#endif /* LRUCACHE_H */
