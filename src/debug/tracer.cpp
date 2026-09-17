#include "retroemu/debug/tracer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace retroemu {
namespace {

// FNV-1a. Not cryptography: just a compact, stable fingerprint of a trace, so
// a change in CPU behaviour can be spotted without storing gigabytes.
struct Fnv1a {
    u64 value = 1469598103934665603ULL;

    void feed(const std::string &text)
    {
        for (char c : text) {
            value ^= static_cast<u8>(c);
            value *= 1099511628211ULL;
        }
    }
};

// Pull one "NAME:VALUE" field out of a trace line. Returns false if absent.
bool field(const std::string &line, const char *name, std::string &out)
{
    const std::string key = std::string(name) + ":";
    const std::size_t at  = line.find(key);
    if (at == std::string::npos) return false;

    const std::size_t start = at + key.size();
    std::size_t end = line.find(' ', start);
    if (end == std::string::npos) end = line.size();
    out = line.substr(start, end - start);
    return true;
}

std::string flags_text(const std::string &hex_f)
{
    const unsigned f = std::strtoul(hex_f.c_str(), nullptr, 16);
    std::string s;
    s += (f & FlagZ) ? 'Z' : '-';
    s += (f & FlagN) ? 'N' : '-';
    s += (f & FlagH) ? 'H' : '-';
    s += (f & FlagC) ? 'C' : '-';
    return s;
}

std::vector<std::string> read_lines(const std::string &path, std::string &error)
{
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file) { error = "cannot open '" + path + "'"; return lines; }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

// ---------------------------------------------------------------------------
std::string trace_line(const GameBoy &gb)
{
    const Registers &r = gb.cpu().regs();
    const Bus       &b = gb.bus();

    char buffer[160];
    std::snprintf(buffer, sizeof(buffer),
                  "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X "
                  "SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X",
                  r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l, r.sp, r.pc,
                  b.peek(r.pc),
                  b.peek(static_cast<u16>(r.pc + 1)),
                  b.peek(static_cast<u16>(r.pc + 2)),
                  b.peek(static_cast<u16>(r.pc + 3)));
    return buffer;
}

u64 run_trace(GameBoy &gb, std::FILE *out, const TraceOptions &options)
{
    gb.bus().set_ly_stub(options.ly_stub);

    u64 lines = 0;
    while (lines < options.max_instructions &&
           gb.bus().clock().t_cpu() < options.max_cycles) {

        const std::string line = trace_line(gb);
        std::fputs(line.c_str(), out);
        std::fputc('\n', out);
        ++lines;

        gb.step();
        if (gb.cpu().illegal() || gb.cpu().stopped()) break;
    }
    return lines;
}

std::string trace_digest(GameBoy &gb, u64 instructions)
{
    Fnv1a hash;
    for (u64 i = 0; i < instructions; ++i) {
        hash.feed(trace_line(gb));
        gb.step();
        if (gb.cpu().illegal() || gb.cpu().stopped()) break;
    }

    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llX",
                  static_cast<unsigned long long>(hash.value));
    return buffer;
}

// ---------------------------------------------------------------------------
//  The diff. Only the FIRST divergence matters: everything after it is a
//  consequence, not a cause.
// ---------------------------------------------------------------------------
int diff_traces(const std::string &path_a, const std::string &path_b, int context)
{
    std::string error;
    const std::vector<std::string> a = read_lines(path_a, error);
    if (!error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    const std::vector<std::string> b = read_lines(path_b, error);
    if (!error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }

    const std::size_t shared = a.size() < b.size() ? a.size() : b.size();

    std::size_t at = 0;
    while (at < shared && a[at] == b[at]) ++at;

    if (at == shared) {
        if (a.size() == b.size()) {
            std::printf("  identical: %zu lines match\n", a.size());
            return 0;
        }
        std::printf("  the first %zu lines match, then one trace stops\n", shared);
        std::printf("    %s : %zu lines\n", path_a.c_str(), a.size());
        std::printf("    %s : %zu lines\n", path_b.c_str(), b.size());
        return 1;
    }

    std::printf("  first divergence at instruction %zu\n\n", at + 1);

    const std::size_t from = (at >= static_cast<std::size_t>(context))
                                 ? at - static_cast<std::size_t>(context) : 0;
    for (std::size_t i = from; i < at; ++i)
        std::printf("    %8zu  %s\n", i + 1, a[i].c_str());

    std::printf("\n  \033[1;33m%8zu  %s\033[0m   <- %s\n", at + 1, a[at].c_str(), path_a.c_str());
    std::printf("  \033[1;33m%8zu  %s\033[0m   <- %s\n\n", at + 1, b[at].c_str(), path_b.c_str());

    // Name exactly what differs, so the divergence can be read at a glance.
    const char *names[] = {"A", "F", "B", "C", "D", "E", "H", "L", "SP", "PC", "PCMEM"};
    for (const char *name : names) {
        std::string va, vb;
        if (!field(a[at], name, va) || !field(b[at], name, vb)) continue;
        if (va == vb) continue;

        if (std::strcmp(name, "F") == 0) {
            std::printf("  %-6s differs: %s (%s) versus %s (%s)\n",
                        name, va.c_str(), flags_text(va).c_str(),
                        vb.c_str(), flags_text(vb).c_str());
        } else {
            std::printf("  %-6s differs: %s versus %s\n", name, va.c_str(), vb.c_str());
        }
    }
    return 1;
}

}  // namespace retroemu
