// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/commands/command_kit.h"

#include "engine/debug/overlay/debug_overlay.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace sh::debug::commands {

void say_text(Context& ctx, std::string_view line) {
    print_command_line(ctx.out, line.substr(0, kLineLimit));
}

void say(Context& ctx, const char* format, ...) {
    char line[kLineLimit + 1];
    va_list values;
    va_start(values, format);
    std::vsnprintf(line, sizeof(line), format, values);
    va_end(values);
    say_text(ctx, line);
}

std::string upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool same_word(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool contains_word(std::string_view text, std::string_view part) {
    if (part.empty()) return true;
    for (std::size_t at = 0; at + part.size() <= text.size(); ++at) {
        if (same_word(text.substr(at, part.size()), part)) return true;
    }
    return false;
}

std::vector<std::string> spaced_words(const Args& args) {
    std::vector<std::string> words;
    const std::string text = args.joined(0);
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t start = text.find_first_not_of(' ', at);
        if (start == std::string::npos) break;
        const std::size_t end = text.find(' ', start);
        words.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        at = end == std::string::npos ? text.size() : end;
    }
    return words;
}

int whole(const std::string& word) { return std::atoi(word.c_str()); }

double real(const std::string& word) { return std::atof(word.c_str()); }

bool switched(const Args& args, std::size_t index, bool current) {
    return args.has(index) ? args.boolean(index) : !current;
}

ArgSpec arg(std::string name, ArgType type, bool optional, std::string help, std::vector<std::string> values,
            CompleteFn complete) {
    ArgSpec spec;
    spec.name = std::move(name);
    spec.type = type;
    spec.optional = optional;
    spec.help = std::move(help);
    spec.values = std::move(values);
    spec.complete = std::move(complete);
    return spec;
}

ArgSpec opt_number(std::string name, std::string help) {
    return arg(std::move(name), ArgType::Float, true, std::move(help));
}

ArgSpec opt_integer(std::string name, std::string help) {
    return arg(std::move(name), ArgType::Int, true, std::move(help));
}

ArgSpec opt_switch(std::string help) { return arg("state", ArgType::Bool, true, std::move(help)); }

ArgSpec opt_word(std::string name, std::string help, std::vector<std::string> suggestions) {
    return arg(std::move(name), ArgType::String, true, std::move(help), {}, words_completion(std::move(suggestions)));
}

CompleteFn words_completion(std::vector<std::string> words) {
    return [words = std::move(words)](std::string_view, std::vector<Candidate>& out) {
        for (const std::string& word : words) out.push_back({word, {}});
    };
}

void add(Registry& registry, std::string name, std::string group, std::string help, std::vector<ArgSpec> args,
         RunFn run, std::vector<std::string> aliases, std::string details) {
    CommandSpec spec;
    spec.name = std::move(name);
    spec.group = std::move(group);
    spec.help = std::move(help);
    spec.details = std::move(details);
    spec.aliases = std::move(aliases);
    spec.args = std::move(args);
    spec.run = std::move(run);
    (void)registry.add(std::move(spec));
}

RunFn number_setting(Setting<float> value, const char* line) {
    return [value, line](Context& ctx, const Args& args) {
        if (args.has(0)) value() = static_cast<float>(real(args.str(0)));
        say(ctx, line, static_cast<double>(value()));
        return done();
    };
}

RunFn whole_setting(Setting<int> value, const char* line) {
    return [value, line](Context& ctx, const Args& args) {
        if (args.has(0)) value() = whole(args.str(0));
        say(ctx, line, value());
        return done();
    };
}

RunFn switch_setting(Setting<int> value, const char* line, const char* on, const char* off) {
    return [value, line, on, off](Context& ctx, const Args& args) {
        value() = switched(args, 0, value() != 0) ? 1 : 0;
        say(ctx, line, value() != 0 ? on : off);
        return done();
    };
}

LineWrapper::LineWrapper(Context& ctx, std::size_t limit, std::size_t slack, std::string_view separator)
    : ctx_(ctx), limit_(limit), slack_(slack), separator_(separator) {}

void LineWrapper::add(std::string_view item) { add(item, item.size()); }

void LineWrapper::add(std::string_view item, std::size_t measure) {
    if (line_.size() + measure + slack_ >= limit_) flush();
    if (!line_.empty()) line_ += separator_;
    line_ += item;
}

void LineWrapper::flush() {
    if (line_.empty()) return;
    say_text(ctx_, line_);
    line_.clear();
}

}  // namespace sh::debug::commands
