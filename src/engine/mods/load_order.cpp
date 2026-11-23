#include "engine/mods/load_order.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace sh::mods {

namespace {

std::string describe(const ModRef& ref) {
    return ref.versions.is_any() ? "'" + ref.id + "'" : "'" + ref.id + "' " + ref.versions.text();
}

}  // namespace

const UserEntry* UserState::find(std::string_view id) const {
    for (const UserEntry& e : mods) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

std::vector<std::string> engine_problems(const Manifest& manifest, const Version& engine) {
    std::vector<std::string> out;
    const std::string this_is = ", this is " + engine.to_string();
    if (manifest.min_engine_version && engine < *manifest.min_engine_version) {
        out.push_back("needs engine " + manifest.min_engine_version->to_string() + " or newer" + this_is);
    }
    if (manifest.max_engine_version && engine > *manifest.max_engine_version) {
        out.push_back("needs engine " + manifest.max_engine_version->to_string() + " or older" + this_is);
    }
    if (manifest.engine && !manifest.engine->matches(engine)) {
        out.push_back("needs engine " + manifest.engine->text() + this_is);
    }
    return out;
}

const char* state_name(ModState state) {
    switch (state) {
    case ModState::Active: return "active";
    case ModState::Disabled: return "disabled";
    case ModState::Error: return "error";
    }
    return "?";
}

std::vector<const ResolvedMod*> Plan::active() const {
    std::vector<const ResolvedMod*> out;
    for (const ResolvedMod& m : mods) {
        if (m.state == ModState::Active) {
            out.push_back(&m);
        }
    }
    return out;
}

const ResolvedMod* Plan::find(std::string_view id) const {
    for (const ResolvedMod& m : mods) {
        if (m.candidate.manifest.id == id) {
            return &m;
        }
    }
    return nullptr;
}

namespace {

using Forced = std::map<std::string, std::string, std::less<>>;  // mod id -> why the engine switches it off

std::string lower_path(std::string_view path) {
    std::string out(path);
    for (char& c : out) {
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return out;
}

Plan resolve_pass(std::vector<Candidate> candidates, const UserState& user, const Environment& env,
                  const Forced& forced) {
    Plan plan;
    std::vector<ResolvedMod>& mods = plan.mods;
    std::map<std::string, std::size_t, std::less<>> by_id;  // the mod that owns each id

    for (Candidate& c : candidates) {
        ResolvedMod m;
        m.warnings = c.warnings;
        m.candidate = std::move(c);
        const Manifest& man = m.candidate.manifest;
        if (const auto taken = by_id.find(man.id); taken != by_id.end()) {
            m.state = ModState::Error;
            m.problems.push_back("id '" + man.id + "' is already used by folder '" +
                                 mods[taken->second].candidate.folder + "'");
        } else {
            by_id.emplace(man.id, mods.size());
            // A new mod stays off until the player switches it on: a clean install plays like the original.
            if (const UserEntry* e = user.find(man.id); e == nullptr || !e->enabled) {
                m.state = ModState::Disabled;
            }
        }
        // Not a matter of the player's switch: the mod cannot run here, and the Mods screen says so at once.
        if (std::vector<std::string> wrong = engine_problems(man, env.engine);
            !wrong.empty() && m.problems.empty()) {
            m.state = ModState::Error;
            m.problems = std::move(wrong);
        }
        if (m.state == ModState::Active) {
            if (man.api && !man.api->matches(env.api)) {
                m.state = ModState::Error;
                m.problems.push_back("needs mod API " + man.api->text() + ", this engine has " + env.api.to_string());
            }
            if (!man.games.empty() && !env.game.empty() &&
                std::find(man.games.begin(), man.games.end(), env.game) == man.games.end()) {
                m.state = ModState::Error;
                std::string list;
                for (const std::string& g : man.games) {
                    list += (list.empty() ? "" : ", ") + g;
                }
                m.problems.push_back("made for " + list + ", this disc is " + env.game);
            }
            if (const auto f = forced.find(man.id); f != forced.end() && m.state == ModState::Active) {
                m.state = ModState::Error;
                m.problems.push_back(f->second);
            }
        }
        mods.push_back(std::move(m));
    }

    const auto owner = [&](std::string_view id) -> ResolvedMod* {
        const auto it = by_id.find(id);
        return it == by_id.end() ? nullptr : &mods[it->second];
    };

    // Rule 4 until stable.
    for (bool changed = true; changed;) {
        changed = false;
        for (ResolvedMod& m : mods) {
            if (m.state != ModState::Active) {
                continue;
            }
            const Manifest& man = m.candidate.manifest;
            for (const ModRef& dep : man.dependencies) {
                const ResolvedMod* d = owner(dep.id);
                std::string problem;
                if (d == nullptr) {
                    problem = "needs mod " + describe(dep) + ", which is not installed";
                } else if (d->state == ModState::Disabled) {
                    problem = "needs mod " + describe(dep) + ", which is switched off";
                } else if (d->state == ModState::Error) {
                    problem = "needs mod " + describe(dep) + ", which could not be loaded";
                } else if (!dep.versions.matches(d->candidate.manifest.version)) {
                    problem = "needs mod " + describe(dep) + ", installed is " +
                              d->candidate.manifest.version.to_string();
                }
                if (!problem.empty()) {
                    m.problems.push_back(std::move(problem));
                    m.state = ModState::Error;
                }
            }
            for (const ModRef& con : man.conflicts) {
                const ResolvedMod* d = owner(con.id);
                if (d != nullptr && d != &m && d->state == ModState::Active &&
                    con.versions.matches(d->candidate.manifest.version)) {
                    m.problems.push_back("conflicts with mod '" + con.id + "' " +
                                         d->candidate.manifest.version.to_string() +
                                         ", which is enabled; switch one of them off");
                    m.state = ModState::Error;
                }
            }
            if (m.state != ModState::Active) {
                changed = true;
            }
        }
        if (changed) {
            continue;
        }

        // Cycles of required dependencies among the active mods (iterative DFS colouring).
        std::vector<int> colour(mods.size(), 0);  // 0 new, 1 on the path, 2 done
        std::vector<std::size_t> path;
        std::set<std::size_t> in_cycle;
        std::map<std::size_t, std::string> cycle_text;
        std::function<void(std::size_t)> visit = [&](std::size_t i) {
            colour[i] = 1;
            path.push_back(i);
            for (const ModRef& dep : mods[i].candidate.manifest.dependencies) {
                const auto it = by_id.find(dep.id);
                if (it == by_id.end() || mods[it->second].state != ModState::Active) {
                    continue;
                }
                const std::size_t j = it->second;
                if (colour[j] == 1) {
                    const auto from = std::find(path.begin(), path.end(), j);
                    std::string text;
                    for (auto p = from; p != path.end(); ++p) {
                        text += mods[*p].candidate.manifest.id + " -> ";
                    }
                    text += mods[j].candidate.manifest.id;
                    for (auto p = from; p != path.end(); ++p) {
                        if (in_cycle.insert(*p).second) {
                            cycle_text[*p] = text;
                        }
                    }
                } else if (colour[j] == 0) {
                    visit(j);
                }
            }
            path.pop_back();
            colour[i] = 2;
        };
        for (std::size_t i = 0; i < mods.size(); ++i) {
            if (mods[i].state == ModState::Active && colour[i] == 0) {
                visit(i);
            }
        }
        for (const std::size_t i : in_cycle) {
            mods[i].state = ModState::Error;
            mods[i].problems.push_back("dependency cycle: " + cycle_text[i]);
            changed = true;
        }
    }

    // Rule 5: order the active mods.
    std::vector<std::size_t> active;
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (mods[i].state == ModState::Active) {
            active.push_back(i);
        }
    }
    // Deterministic key: the player's order, then priority, then id.
    const auto key = [&](std::size_t i) {
        const Manifest& man = mods[i].candidate.manifest;
        int listed = INT_MAX;
        for (std::size_t k = 0; k < user.mods.size(); ++k) {
            if (user.mods[k].id == man.id) {
                listed = static_cast<int>(k);
                break;
            }
        }
        return std::make_tuple(listed == INT_MAX ? 1 : 0, listed == INT_MAX ? man.priority : listed, man.id);
    };
    std::sort(active.begin(), active.end(), [&](std::size_t a, std::size_t b) { return key(a) < key(b); });

    std::map<std::size_t, std::set<std::size_t>> after;  // edge u -> v: u loads before v
    const auto reaches = [&](std::size_t from, std::size_t to) {
        std::vector<std::size_t> stack{from};
        std::set<std::size_t> seen;
        while (!stack.empty()) {
            const std::size_t n = stack.back();
            stack.pop_back();
            if (n == to) {
                return true;
            }
            if (!seen.insert(n).second) {
                continue;
            }
            for (const std::size_t next : after[n]) {
                stack.push_back(next);
            }
        }
        return false;
    };
    const auto active_index = [&](std::string_view id) -> std::optional<std::size_t> {
        const auto it = by_id.find(id);
        if (it == by_id.end() || mods[it->second].state != ModState::Active) {
            return std::nullopt;
        }
        return it->second;
    };
    for (const std::size_t i : active) {
        for (const ModRef& dep : mods[i].candidate.manifest.dependencies) {
            if (const auto d = active_index(dep.id)) {
                after[*d].insert(i);
            }
        }
    }
    const auto soft = [&](std::size_t first, std::size_t second, std::size_t declarer, const std::string& why) {
        if (first == second || after[first].count(second) != 0) {
            return;
        }
        if (reaches(second, first)) {
            mods[declarer].warnings.push_back(why + " ignored: it contradicts the rest of the load order");
            return;
        }
        after[first].insert(second);
    };
    for (const std::size_t i : active) {
        const Manifest& man = mods[i].candidate.manifest;
        for (const ModRef& dep : man.optional_dependencies) {
            if (const auto d = active_index(dep.id)) {
                if (!dep.versions.matches(mods[*d].candidate.manifest.version)) {
                    mods[i].warnings.push_back("optional mod " + describe(dep) + " is version " +
                                               mods[*d].candidate.manifest.version.to_string());
                }
                soft(*d, i, i, "optional dependency '" + dep.id + "'");
            }
        }
        for (const std::string& id : man.load_after) {
            if (const auto d = active_index(id)) {
                soft(*d, i, i, "load_after '" + id + "'");
            }
        }
        for (const std::string& id : man.load_before) {
            if (const auto d = active_index(id)) {
                soft(i, *d, i, "load_before '" + id + "'");
            }
        }
    }

    // Kahn's algorithm; among the ready mods the smallest key goes first.
    std::map<std::size_t, int> incoming;
    for (const std::size_t i : active) {
        incoming[i] += 0;
        for (const std::size_t next : after[i]) {
            ++incoming[next];
        }
    }
    std::vector<std::size_t> order;
    std::vector<std::size_t> ready;
    for (const std::size_t i : active) {
        if (incoming[i] == 0) {
            ready.push_back(i);
        }
    }
    const auto rank = [&](std::size_t i) {
        return static_cast<std::size_t>(std::find(active.begin(), active.end(), i) - active.begin());
    };
    while (!ready.empty()) {
        const auto best = std::min_element(ready.begin(), ready.end(),
                                           [&](std::size_t a, std::size_t b) { return rank(a) < rank(b); });
        const std::size_t n = *best;
        ready.erase(best);
        order.push_back(n);
        for (const std::size_t next : after[n]) {
            if (--incoming[next] == 0) {
                ready.push_back(next);
            }
        }
    }

    std::vector<ResolvedMod> sorted;
    sorted.reserve(mods.size());
    for (const std::size_t i : order) {
        mods[i].load_index = static_cast<int>(sorted.size());
        sorted.push_back(std::move(mods[i]));
    }
    std::vector<std::size_t> rest;
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (mods[i].load_index < 0) {
            rest.push_back(i);
        }
    }
    // The same key as the active ones: a new mod switched on in the Mods screen keeps the place its priority gives.
    std::stable_sort(rest.begin(), rest.end(), [&](std::size_t a, std::size_t b) { return key(a) < key(b); });
    for (const std::size_t i : rest) {
        sorted.push_back(std::move(mods[i]));
    }
    mods = std::move(sorted);
    return plan;
}

}  // namespace

// Rule 6: two active mods replacing one layered file whole cannot both have it; the later one in the order is
// switched off and the order is resolved again (its dependents follow it, the indices close up).
Plan resolve_load_order(std::vector<Candidate> candidates, const UserState& user, const Environment& env) {
    Forced forced;
    for (;;) {
        Plan plan = resolve_pass(candidates, user, env, forced);
        std::map<std::string, std::string, std::less<>> taken;  // lower-cased path -> the first mod replacing it
        bool more = false;
        for (const ResolvedMod* m : plan.active()) {
            const Manifest& man = m->candidate.manifest;
            bool clash = false;
            for (const std::string& path : man.replace) {
                const auto it = taken.find(lower_path(path));
                if (it != taken.end() && it->second != man.id) {
                    forced.emplace(man.id, "replaces " + path + " which mod '" + it->second +
                                               "' also replaces; switch one of them off");
                    clash = more = true;
                    break;
                }
            }
            if (!clash) {  // a mod that goes off claims nothing
                for (const std::string& path : man.replace) {
                    taken.emplace(lower_path(path), man.id);
                }
            }
        }
        if (!more) {
            return plan;
        }
    }
}

}  // namespace sh::mods
