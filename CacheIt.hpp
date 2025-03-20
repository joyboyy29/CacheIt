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
#include <unordered_map>
#include <functional>
#include <shared_mutex>
#include <mutex>
#include <stdexcept>

// if a categorizer fn is provided we group entities by that category
// if not, we us the default ctor which uses ID based caching
template <typename T, typename Category = int>
class CacheIt {
public:
    using categorizer_fn = std::function<Category(const T*)>;

    // default ctor that uses ID based cache
    CacheIt() : enable_grouping_(false) {}

    // ctor with categorizer (grouping)
    explicit CacheIt(categorizer_fn categorizer)
        : categorizer_(std::move(categorizer)), enable_grouping_(true) {}

    // update cache (either ID or grouping)
    void update(const std::vector<T*>& entities) {
        if (enable_grouping_) {
            // grouping mode: we build an umap keyed by the category
            std::unordered_map<Category, std::vector<T*>> local_cache;
            for (auto* entity : entities) {
                Category cat = categorizer_(entity);
                local_cache[cat].push_back(entity);
            }
            {
                std::unique_lock lock(mutex_);
                cache_ = std::move(local_cache);
            }
        } else {
            /* ID mode: we build a dense table indexed with entity->id
             * a struct of AActor or whatever struct user of the class uses
             * should have an ID member
             */
            std::vector<T*> local_table;
            std::vector<u64> local_active_ids;
            // in the table, the idx = entity->id;
            for (auto* entity : entities) {
                u64 id = entity->id;
                if (id >= static_cast<u64>(local_table.size()))
                    local_table.resize(id + 1, nullptr);
                local_table[id] = entity;
                local_active_ids.push_back(id);
            }
            {
                std::unique_lock lock(mutex_);
                table_.swap(local_table);
                active_ids_.swap(local_active_ids);
            }
        }
    }
    
    //iterate over a certain category
    // only usable in grouping mode
    template <typename Fn>
    void for_each(const Category& cat, Fn func) const {
        if (!enable_grouping_) {
            throw std::runtime_error("oopsie! for_each called in ID mode");
        }
        std::vector<T*> local_entities;
        {
            std::shared_lock lock(mutex_);
            if (auto it = cache_.find(cat); it != cache_.end()) {
                local_entities = it->second;
            }
        }
        for (auto* entity : local_entities) {
            func(entity);
        }
    }

    // iterate over all entities
    template <typename Fn>
    void for_each_all(Fn func) const {
        if (enable_grouping_) {
            // grouping: iterate over each group.
            std::unordered_map<Category, std::vector<T*>> local_cache;
            {
                std::shared_lock lock(mutex_);
                local_cache = cache_;
            }
            for (const auto& pair : local_cache) {
                for (auto* entity : pair.second)
                    func(entity);
            }
        } else {
            // ID: iterate over the table
            std::vector<T*> local_table;
            {
                std::shared_lock lock(mutex_);
                local_table = table_;
            }
            for (auto* entity : local_table) {
                if (entity)
                    func(entity);
            }
        }
    }

    // grouping mode: return copy of the cache
    std::unordered_map<Category, std::vector<T*>> get_cache() const {
        if (!enable_grouping_) {
            throw std::runtime_error("oopsie! get_cache() called in ID mode");
        }
        std::unordered_map<Category, std::vector<T*>> local_cache;
        {
            std::shared_lock lock(mutex_);
            local_cache = cache_;
        }
        return local_cache;
    }

    // return number of currently cached entities
    size_t size() const {
        if (enable_grouping_) {
            size_t total = 0;
            std::unordered_map<Category, std::vector<T*>> local_cache;
            {
                std::shared_lock lock(mutex_);
                local_cache = cache_;
            }
            for (const auto& pair : local_cache)
                total += pair.second.size();
            return total;
        } else {
            size_t count = 0;
            std::vector<T*> local_table;
            {
                std::shared_lock lock(mutex_);
                local_table = table_;
            }
            for (auto* entity : local_table)
                if (entity)
                    count++;
            return count;
        }
    }

private:
    using u64 = uint64_t;
    
    mutable std::shared_mutex mutex_;

    // used in grouping mode
    std::unordered_map<Category, std::vector<T*>> cache_;

    // used in ID mode
    std::vector<T*> table_;
    std::vector<u64> active_ids_;

    // mode flags
    bool enable_grouping_;
    categorizer_fn categorizer_;
};
