# CacheIt

**CacheIt** is a lightweight, single-header C++ caching library built with modern C++ in mind. It provides a high-performance, thread-safe cache with two modes of operation:

- **ID Mode (Default):**  
  It uses a dense vector keyed by entity ID for fast updates and iteration over all cached entities.

- **Grouping Mode:**  
  When supplied with a categorizer function, CacheIt groups entities by a category (e.g. actor type).
  This enables extremely fast per-category iteration, but at the cost of a slightly slower update.
  
- ID Mode has faster update times and slightly slower iteration times compared to grouping mode
- Grouping Mode has faster iteration times and slower update times than ID Mode.

## Features

- **Single Header:** Easy integration—simply drop `CacheIt.hpp` into your project.
- **Modern C++:** Utilizes C++17/20 features, including `std::shared_mutex` for thread safety.
- **Flexible Caching Modes:**  
  - **ID Mode:** Fast update and iteration when rendering or processing all entities.
  - **Grouping Mode:** Fast per-category iteration when you need to filter or render subsets of entities.
- **Thread-Safe:** All operations are protected by shared mutexes for safe concurrent access.
- **Minimal Overhead:** Optimized for high frame rates, ensuring that the cache updates quickly to reflect dynamic changes in the data.

## Installation
- Self explanatory lol

## Requirements
- C++17 or later

## ID Mode 
- If you don't need category-based grouping, use the default constructor.
- In this mode, CacheIt expects your entity type to have an id member (int)
```cpp
#include "CacheIt.hpp"

struct AActor {
    int id;               // Unique actor ID.
    float ActorHealth;
    std::string ActorType;
    // position, etc
};

CacheIt<AActor> cache;  // uses default ctor, thus ID mode
std::vector<AActor*> actors = //stuff
cache.update(actors);

// iterate over all cached actors
cache.for_each_all([](AActor* actor) {
    // do something with it
    // render(actor)
});

```

## Grouping Mode 
- If you need to render or process only a specific category (for example, only players or dropped items)
- Construct a CacheIt instance with a categorizer function:
```cpp
CacheIt<AActor, std::string> grouped_cache([](const AActor* actor) {
    return actor->ActorType;  // group by actor type
});

grouped_cache.update(actors);

// Iterate over a specific group (e.g., "Player"):
grouped_cache.for_each("Player", [](AActor* actor) {
    // process only players
});

// you can also iterate over all actors in grouping mode:
grouped_cache.for_each_all([](AActor* actor) {
    // process all actors
});

```

## Size
- Returns total number of entities that's currently cached
```cpp
size_t total = cache.size();
```

## License
- This project is licensed under the MIT License.

## Contributions
- Feel free to contribute as this was my first attempt on trying various caching methods and finding the one which suited my needs the most.
