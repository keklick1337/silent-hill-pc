#include "import/ui/console.h"

#include <cctype>
#include <istream>
#include <ostream>

namespace sh::import::ui {

namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    std::size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
        ++first;
    }
    return s.substr(first);
}

bool is_quit_word(const std::string& s) { return s == "q" || s == "Q" || s == "quit" || s == "exit"; }

void print_header(std::ostream& out, const ViewModel& v) {
    out << v.title << '\n';
    for (const std::string& paragraph : v.body) {
        out << paragraph << '\n';
    }
    if (!v.message.empty()) {
        out << v.message << '\n';
    }
}

}  // namespace

int run_console(Controller& controller, std::istream& in, std::ostream& out, const ConsoleOptions& options) {
    controller.start();

    bool arg_pending = !options.image_arg.empty();
    bool intro_shown = false;
    bool cancel_sent = false;
    int last_percent = -1;
    std::string last_stage_text;

    for (;;) {
        const ViewModel v = controller.snapshot();
        switch (v.phase) {
        case Phase::Ready:
            print_header(out, v);
            return kConsoleOk;

        case Phase::AskPath: {
            if (!intro_shown) {
                intro_shown = true;
                out << v.title << '\n';
                for (const std::string& paragraph : v.body) {
                    out << paragraph << '\n';
                }
            }
            std::string line;
            if (arg_pending) {
                arg_pending = false;
                line = options.image_arg;
            } else {
                out << v.input_label;
                if (!v.input_hint.empty()) {
                    out << " (" << v.input_hint << ")";
                }
                out << ": " << std::flush;
                if (!std::getline(in, line)) {
                    out << '\n';
                    controller.quit();
                    break;
                }
            }
            line = trimmed(line);
            if (is_quit_word(line)) {
                controller.quit();
                break;
            }
            // An empty answer keeps the suggested path.
            if (!line.empty()) {
                controller.set_text(line);
            }
            if (!controller.submit()) {
                const ViewModel after = controller.snapshot();
                if (after.phase == Phase::AskPath && !after.message.empty()) {
                    out << after.message << '\n';
                }
            }
            break;
        }

        case Phase::Importing:
        case Phase::Cancelling:
            if (v.percent != last_percent || v.stage_text != last_stage_text) {
                last_percent = v.percent;
                last_stage_text = v.stage_text;
                out << '[' << (v.percent < 10 ? "  " : v.percent < 100 ? " " : "") << v.percent << "%] " << v.stage_text
                    << ", " << v.eta_text << '\n'
                    << std::flush;
            }
            if (!cancel_sent && options.interrupted && options.interrupted()) {
                cancel_sent = true;
                controller.cancel();
            }
            controller.wait_for_change(v.revision, options.poll);
            break;

        case Phase::Done:
            print_header(out, v);
            return kConsoleOk;

        case Phase::Failed:
            print_header(out, v);
            return kConsoleFailed;

        case Phase::Cancelled:
        case Phase::Aborted:
            print_header(out, v);
            return kConsoleCancelled;
        }
    }
}

}  // namespace sh::import::ui
