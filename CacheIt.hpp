/*
 * Created by joyboyy29
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * I would appreciate if you left this part as is.
 */

#pragma once

#include <vector>
#include <functional>
#include <shared_mutex>
#include <mutex>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <type_traits>
#include <utility>

// ID mode uses a dense table and has add/remove option (didn't add remove_if and add_if as you can just do that by looping and using add or remove)
// Grouping mode uses a vector of buckets (changed from umap to remove hash overhead)

template<typename T, typename Category = int, typename Categorizer = void>
class CacheIt {
public:
    using u64 = uint64_t;
    static constexpr bool grouping_enabled = !std::is_same_v<Categorizer, void>;

    // id mode ctor
    CacheIt() {
        static_assert(!grouping_enabled, "Default constructor only valid for ID mode");
    }

    // grouping-mode ctor (only if Categorizer is not void)
    template<typename U = Categorizer,
             typename = std::enable_if_t<!std::is_same_v<U, void>>>
    explicit CacheIt(U categorizer)
        : categorizer_(std::move(categorizer)) {}

    // full rebuild
    void update(const std::vector<T*>& entities) {
        if constexpr (grouping_enabled) {
            // grouping mode:
            // changed from umap to vector of buckets
            std::unordered_map<Category, size_t> local_index;
            std::vector<Category> local_categories;
            local_index.reserve(entities.size());

            for (auto* e : entities) {
                Category c = categorizer_(e);
                if (!local_index.count(c)) {
                    local_index[c] = local_categories.size();
                    local_categories.push_back(c);
                }
            }

            std::vector<std::vector<T*>> local_buckets(local_categories.size());
            size_t avg = local_categories.empty() ? 0 : entities.size() / local_categories.size();
            for (auto& b : local_buckets) b.reserve(avg);

            for (auto* e : entities) {
                size_t idx = local_index[categorizer_(e)];
                local_buckets[idx].push_back(e);
            }

            std::unique_lock lock(grouping_mutex_);
            category_to_index_.swap(local_index);
            categories_.swap(local_categories);
            buckets_.swap(local_buckets);
        } else {
            // ID mode:
            std::vector<T*> local_table;
            std::vector<u64> local_ids;
            std::unordered_map<u64, size_t> local_idx_map;
            local_table.reserve(entities.size());
            local_ids.reserve(entities.size());
            local_idx_map.reserve(entities.size());

            for (auto* e : entities) {
                u64 id = static_cast<u64>(e->id);
                if (id >= local_table.size())
                    local_table.resize(id + 1, nullptr);
                local_table[id] = e;
                size_t pos = local_ids.size();
                local_ids.push_back(id);
                local_idx_map[id] = pos;
            }

            std::unique_lock lock(id_mutex_);
            table_.swap(local_table);
            active_ids_.swap(local_ids);
            id_to_index_.swap(local_idx_map);
        }
    }

    /*
    * You can use add and remove by doing something like
        diff_snapshots(prev_actors, curr_actors, to_add, to_remove); // sorts + set_difference
        for (auto* e : to_add)    cache.add(e);
        for (auto* e : to_remove) cache.remove(e);
        prev_actors.swap(curr_actors);
        curr_actors.clear();
    */

    // O(1) add
    void add(T* e) {
        u64 id = static_cast<u64>(e->id);
        if constexpr (grouping_enabled) {
            Category c = categorizer_(e);
            std::unique_lock lock(grouping_mutex_);
            auto it = category_to_index_.find(c);
            if (it == category_to_index_.end()) {
                size_t idx = categories_.size();
                category_to_index_[c] = idx;
                categories_.push_back(c);
                buckets_.emplace_back();
            }
            buckets_[category_to_index_[c]].push_back(e);
        } else {
            std::unique_lock lock(id_mutex_);
            if (id_to_index_.count(id)) return; // avoid duplicates
            if (id >= table_.size())
                table_.resize(id + 1, nullptr);
            table_[id] = e;
            size_t pos = active_ids_.size();
            active_ids_.push_back(id);
            id_to_index_[id] = pos;
        }
    }

    // O(1) remove
    void remove(T* e) {
        u64 id = static_cast<u64>(e->id);
        if constexpr (grouping_enabled) {
            Category c = categorizer_(e);
            std::unique_lock lock(grouping_mutex_);
            auto it = category_to_index_.find(c);
            if (it == category_to_index_.end()) return;
            auto& vec = buckets_[it->second];
            if (auto pos = std::find(vec.begin(), vec.end(), e); pos != vec.end()) {
                std::iter_swap(pos, vec.end() - 1);
                vec.pop_back();
            }
        } else {
            std::unique_lock lock(id_mutex_);
            auto it = id_to_index_.find(id);
            if (it == id_to_index_.end()) return;
            size_t idx = it->second;
            size_t last = active_ids_.size() - 1;
            u64 back_id = active_ids_[last];
            std::swap(active_ids_[idx], active_ids_[last]);
            active_ids_.pop_back();
            id_to_index_[back_id] = idx;
            id_to_index_.erase(it);
            if (id < table_.size()) table_[id] = nullptr;
        }
    }

    void clear() {
        if constexpr (grouping_enabled) {
            std::unique_lock lock(grouping_mutex_);
            category_to_index_.clear();
            categories_.clear();
            buckets_.clear();
        } else {
            std::unique_lock lock(id_mutex_);
            table_.clear();
            active_ids_.clear();
            id_to_index_.clear();
        }
    }

    size_t size() const {
        if constexpr (grouping_enabled) {
            std::shared_lock lock(grouping_mutex_);
            size_t total = 0;
            for (auto const& b : buckets_) total += b.size();
            return total;
        } else {
            std::shared_lock lock(id_mutex_);
            return active_ids_.size();
        }
    }

    std::vector<T*> get_all() const {
        std::vector<T*> result;
        if constexpr (grouping_enabled) {
            std::shared_lock lock(grouping_mutex_);
            size_t total = 0;
            for (auto const& b : buckets_) total += b.size();
            result.reserve(total);
            for (auto const& b : buckets_)
                for (auto* e : b)
                    result.push_back(e);
        } else {
            std::shared_lock lock(id_mutex_);
            result.reserve(active_ids_.size());
            for (auto id : active_ids_)
                if (id < table_.size() && table_[id])
                    result.push_back(table_[id]);
        }
        return result;
    }

    // iterate single category (grouping only)
    template<typename Fn>
    void for_each(const Category& cat, Fn func) const {
        static_assert(grouping_enabled, "for_each only in grouping mode");
        std::vector<T*> local;
        {
            std::shared_lock lock(grouping_mutex_);
            auto it = category_to_index_.find(cat);
            if (it != category_to_index_.end())
                local = buckets_[it->second];
        }
        for (auto* e : local) func(e);
    }

    // iterate all
    template<typename Fn>
    void for_each_all(Fn func) const {
        if constexpr (grouping_enabled) {
            std::shared_lock lock(grouping_mutex_);
            for (auto const& b : buckets_)
                for (auto* e : b) func(e);
        } else {
            std::shared_lock lock(id_mutex_);
            for (auto* e : table_) if (e) func(e);
        }
    }

    // access active ids (ID mode only)
    const std::vector<u64>& active_ids() const {
        static_assert(!grouping_enabled, "active_ids only in ID mode");
        std::shared_lock lock(id_mutex_);
        return active_ids_;
    }

private:
    // functor
    std::conditional_t<std::is_same_v<Categorizer, void>, char, Categorizer> categorizer_;
    
    // grouping
    mutable std::shared_mutex grouping_mutex_;
    std::unordered_map<Category, size_t> category_to_index_;
    std::vector<Category> categories_;
    std::vector<std::vector<T*>> buckets_;
    
    // ID
    mutable std::shared_mutex id_mutex_;
    std::vector<T*> table_;
    std::vector<u64> active_ids_;
    std::unordered_map<u64, size_t> id_to_index_;
};
