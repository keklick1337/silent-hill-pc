#include "engine/hooks/hook_bus.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <exception>
#include <tuple>

namespace sh::hooks {

Event::Field* Event::find(std::string_view key) {
    for (Field& f : fields_) {
        if (f.key == key) {
            return &f;
        }
    }
    return nullptr;
}

const Event::Field* Event::find(std::string_view key) const {
    for (const Field& f : fields_) {
        if (f.key == key) {
            return &f;
        }
    }
    return nullptr;
}

Event& Event::set(std::string_view key, Value value, bool read_only) {
    if (Field* f = find(key)) {
        f->value = std::move(value);
        f->read_only = read_only;
    } else {
        fields_.push_back(Field{std::string(key), std::move(value), read_only});
    }
    return *this;
}

bool Event::update(std::string_view key, Value value) {
    Field* f = find(key);
    if (f != nullptr && f->read_only) {
        return false;
    }
    if (f == nullptr) {
        // A new field: handlers may pass results back to the emitter or to later handlers.
        fields_.push_back(Field{std::string(key), std::move(value), false});
    } else {
        f->value = std::move(value);
    }
    return true;
}

const Value* Event::get(std::string_view key) const {
    const Field* f = find(key);
    return f == nullptr ? nullptr : &f->value;
}

std::int64_t Event::get_int(std::string_view key, std::int64_t fallback) const {
    const Value* v = get(key);
    if (v == nullptr) {
        return fallback;
    }
    if (const auto* i = std::get_if<std::int64_t>(v)) {
        return *i;
    }
    if (const auto* d = std::get_if<double>(v)) {
        return static_cast<std::int64_t>(*d);
    }
    if (const auto* b = std::get_if<bool>(v)) {
        return *b ? 1 : 0;
    }
    return fallback;
}

std::string Event::get_string(std::string_view key, std::string_view fallback) const {
    const Value* v = get(key);
    if (const auto* s = v != nullptr ? std::get_if<std::string>(v) : nullptr) {
        return *s;
    }
    return std::string(fallback);
}

bool Event::get_bool(std::string_view key, bool fallback) const {
    const Value* v = get(key);
    if (v == nullptr) {
        return fallback;
    }
    if (const auto* b = std::get_if<bool>(v)) {
        return *b;
    }
    if (const auto* i = std::get_if<std::int64_t>(v)) {
        return *i != 0;
    }
    return fallback;
}

bool Event::read_only(std::string_view key) const {
    const Field* f = find(key);
    return f != nullptr && f->read_only;
}

bool Event::cancel(std::string_view by) {
    if (!cancellable_) {
        return false;
    }
    if (!cancelled_) {
        cancelled_ = true;
        cancelled_by_ = std::string(by);
    }
    return true;
}

Token Bus::on(std::string_view event, Handler handler, HandlerOptions options, std::string_view group) {
    std::lock_guard lock(mutex_);
    std::vector<Entry>& list = handlers_[std::string(event)];
    if (!options.id.empty()) {
        for (Entry& e : list) {
            if (e.info.owner == options.owner && e.info.id == options.id && e.info.group == group) {
                e.handler = std::move(handler);
                e.info.priority = options.priority;
                return e.info.token;
            }
        }
    }
    Entry e;
    e.info.event = std::string(event);
    e.info.owner = std::move(options.owner);
    e.info.id = std::move(options.id);
    e.info.priority = options.priority;
    e.info.group = std::string(group);
    e.info.token = next_token_++;
    e.handler = std::move(handler);
    e.sequence = next_sequence_++;
    list.push_back(std::move(e));
    return list.back().info.token;
}

bool Bus::off(Token token) {
    std::lock_guard lock(mutex_);
    for (auto& [name, list] : handlers_) {
        const auto it = std::find_if(list.begin(), list.end(), [&](const Entry& e) { return e.info.token == token; });
        if (it != list.end()) {
            list.erase(it);
            return true;
        }
    }
    return false;
}

bool Bus::off(std::string_view owner, std::string_view event, std::string_view id) {
    std::lock_guard lock(mutex_);
    const auto found = handlers_.find(event);
    if (found == handlers_.end()) {
        return false;
    }
    std::vector<Entry>& list = found->second;
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&](const Entry& e) { return e.info.owner == owner && e.info.id == id; });
    if (it == list.end()) {
        return false;
    }
    list.erase(it);
    return true;
}

std::size_t Bus::off_owner(std::string_view owner) {
    std::lock_guard lock(mutex_);
    std::size_t removed = 0;
    for (auto& [name, list] : handlers_) {
        removed += static_cast<std::size_t>(
            std::erase_if(list, [&](const Entry& e) { return e.info.owner == owner; }));
    }
    return removed;
}

std::size_t Bus::off_group(std::string_view group) {
    std::lock_guard lock(mutex_);
    std::size_t removed = 0;
    for (auto& [name, list] : handlers_) {
        removed += static_cast<std::size_t>(
            std::erase_if(list, [&](const Entry& e) { return e.info.group == group; }));
    }
    return removed;
}

void Bus::clear() {
    std::lock_guard lock(mutex_);
    handlers_.clear();
}

bool Bus::has(std::string_view event) const {
    std::lock_guard lock(mutex_);
    const auto it = handlers_.find(event);
    return it != handlers_.end() && !it->second.empty();
}

std::vector<Bus::Entry> Bus::ordered(std::string_view event) const {
    std::vector<Entry> out;
    const auto it = handlers_.find(event);
    if (it == handlers_.end()) {
        return out;
    }
    out = it->second;
    const auto rank = [&](const std::string& owner) {
        if (owner == "silenthill") {
            return -1;
        }
        const int r = order_ ? order_(owner) : INT_MAX;
        return r < 0 ? INT_MAX : r;
    };
    std::stable_sort(out.begin(), out.end(), [&](const Entry& a, const Entry& b) {
        return std::make_tuple(-a.info.priority, rank(a.info.owner), a.sequence) <
               std::make_tuple(-b.info.priority, rank(b.info.owner), b.sequence);
    });
    return out;
}

bool Bus::emit(Event& event) {
    std::vector<Entry> list;
    std::function<void(const std::string&)> log;
    {
        std::lock_guard lock(mutex_);
        ++counts_[event.name()];
        list = ordered(event.name());
        log = log_;
    }
    for (Entry& e : list) {
        try {
            e.handler(event);
        } catch (const std::exception& ex) {
            const std::string message = "hook " + event.name() + " of " + e.info.owner + ": " + ex.what();
            if (log) {
                log(message);
            } else {
                std::fprintf(stderr, "warning: [HOOKS] %s\n", message.c_str());
            }
        }
        if (event.cancelled()) {
            break;
        }
    }
    return !event.cancelled();
}

void Bus::set_owner_order(std::function<int(std::string_view owner)> order) {
    std::lock_guard lock(mutex_);
    order_ = std::move(order);
}

void Bus::set_logger(std::function<void(const std::string& message)> log) {
    std::lock_guard lock(mutex_);
    log_ = std::move(log);
}

std::vector<HandlerInfo> Bus::handlers(std::string_view event) const {
    std::lock_guard lock(mutex_);
    std::vector<HandlerInfo> out;
    const auto add = [&](std::string_view name) {
        for (const Entry& e : ordered(name)) {
            out.push_back(e.info);
        }
    };
    if (!event.empty()) {
        add(event);
    } else {
        for (const auto& [name, list] : handlers_) {
            add(name);
        }
    }
    return out;
}

std::uint64_t Bus::emitted(std::string_view event) const {
    std::lock_guard lock(mutex_);
    const auto it = counts_.find(event);
    return it == counts_.end() ? 0 : it->second;
}

Bus& bus() {
    // Never destroyed: script hosts held in other statics leave the bus while the process exits, after a
    // function-local Bus would already be gone (a crash on quit with a mod's hooks registered).
    static Bus* const b = new Bus;
    return *b;
}

}  // namespace sh::hooks
