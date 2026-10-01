#include "database.h"

#include "clock.h"

const char* type_name(const Value& value) {
    // The index follows the order of the types in the std::variant.
    switch (value.index()) {
        case 0: return "string";
        case 1: return "list";
        case 2: return "hash";
        case 3: return "zset";
    }
    return "none";
}

Database::Database() : clock_(now_ms) {}

bool Database::is_expired(const Entry& entry) const {
    return entry.expire_at != NO_EXPIRY && entry.expire_at <= clock_();
}

Entry* Database::find(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) return nullptr;
    if (is_expired(it->second)) {  // lazy expiry
        remove(key);
        return nullptr;
    }
    return &it->second;
}

Entry& Database::create(const std::string& key) {
    remove(key);
    // try_emplace builds the Entry directly inside the map (no copy or move).
    return data_.try_emplace(key).first->second;
}

bool Database::remove(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    if (it->second.expire_at != NO_EXPIRY) {
        expiry_index_.erase({it->second.expire_at, key});
    }
    data_.erase(it);
    return true;
}

bool Database::rename(const std::string& from, const std::string& to) {
    Entry* source = find(from);
    if (source == nullptr) return false;
    if (from == to) return true;

    int64_t expire_at = source->expire_at;
    if (expire_at != NO_EXPIRY) expiry_index_.erase({expire_at, from});
    remove(to);

    // extract() takes the map node out without copying the value, so even a
    // huge list is renamed in O(1). We change its key and put it back.
    auto node = data_.extract(from);
    node.key() = to;
    data_.insert(std::move(node));

    if (expire_at != NO_EXPIRY) expiry_index_.insert({expire_at, to});
    return true;
}

bool Database::set_expire(const std::string& key, int64_t at_ms) {
    Entry* entry = find(key);
    if (entry == nullptr) return false;
    if (at_ms <= clock_()) {  // already in the past: delete now
        remove(key);
        return true;
    }
    if (entry->expire_at != NO_EXPIRY) expiry_index_.erase({entry->expire_at, key});
    entry->expire_at = at_ms;
    expiry_index_.insert({at_ms, key});
    return true;
}

bool Database::persist(const std::string& key) {
    Entry* entry = find(key);
    if (entry == nullptr || entry->expire_at == NO_EXPIRY) return false;
    expiry_index_.erase({entry->expire_at, key});
    entry->expire_at = NO_EXPIRY;
    return true;
}

int Database::remove_expired(int max_keys) {
    int64_t now = clock_();
    int removed = 0;
    // The index is sorted by time, so expired keys are all at the front.
    while (removed < max_keys && !expiry_index_.empty()) {
        auto first = expiry_index_.begin();
        if (first->first > now) break;  // the soonest key is still alive: done
        std::string key = first->second;  // copy: remove() erases this element
        remove(key);
        removed++;
    }
    return removed;
}

void Database::clear() {
    data_.clear();
    expiry_index_.clear();
}

std::vector<std::string> Database::keys() const {
    std::vector<std::string> result;
    result.reserve(data_.size());
    for (const auto& [key, entry] : data_) {
        if (!is_expired(entry)) result.push_back(key);
    }
    return result;
}
