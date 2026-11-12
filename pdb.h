// pdb.h - coordinates, move tables and pattern databases (Korf, 1997).
//
// A pattern database stores, for every configuration of a *subset* of the
// cube's pieces, the exact number of moves needed to solve just that subset.
// That number never overestimates the true distance, so the maximum over
// several databases is an admissible heuristic for IDA*.
//
// Three databases are used (4 bits per entry, 43 MB + 2 x 21 MB):
//   corners : all 8 corners (position + twist)            88,179,840 states
//   edgesA  : edges UR UF UL UB DR DF (position + flip)   42,577,920 states
//   edgesB  : edges DL DB FR FL BL BR (position + flip)   42,577,920 states
#pragma once

#include "cube.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace rubix {

constexpr int FACT[9] = {1, 1, 2, 6, 24, 120, 720, 5040, 40320};
constexpr int TWIST_N = 2187;    // 3^7
constexpr int CPERM_N = 40320;   // 8!
constexpr uint64_t CORNER_N = uint64_t(TWIST_N) * CPERM_N;
constexpr int EDGE_K = 6;        // edges tracked per edge database
constexpr uint32_t EDGE_PERM_N = 12u * 11 * 10 * 9 * 8 * 7;  // 665,280
constexpr uint64_t EDGE_N = uint64_t(EDGE_PERM_N) * 64;      // x 2^6 flips

// ------------------------------------------------------------- coordinates

inline int rankPerm8(const uint8_t* p) {
    int r = 0;
    for (int i = 0; i < 8; i++) {
        int smaller = 0;
        for (int j = i + 1; j < 8; j++)
            if (p[j] < p[i]) smaller++;
        r += smaller * FACT[7 - i];
    }
    return r;
}

inline void unrankPerm8(int r, uint8_t* p) {
    bool used[8] = {};
    for (int i = 0; i < 8; i++) {
        int f = FACT[7 - i];
        int d = r / f;
        r %= f;
        for (int v = 0; v < 8; v++) {
            if (used[v]) continue;
            if (d == 0) { p[i] = uint8_t(v); used[v] = true; break; }
            d--;
        }
    }
}

// Corner twist coordinate: the twist of the 8th corner is implied by the rest.
inline int encodeTwist(const uint8_t* co) {
    int t = 0;
    for (int i = 0; i < 7; i++) t = t * 3 + co[i];
    return t;
}

inline void decodeTwist(int t, uint8_t* co) {
    int sum = 0;
    for (int i = 6; i >= 0; i--) {
        co[i] = uint8_t(t % 3);
        sum += co[i];
        t /= 3;
    }
    co[7] = uint8_t((3 - sum % 3) % 3);
}

// Rank of an ordered selection of 6 distinct slots out of 12.
inline uint32_t rankPartial(const uint8_t* pos) {
    uint32_t r = 0, used = 0;
    for (int i = 0; i < EDGE_K; i++) {
        uint32_t below = uint32_t(__builtin_popcount(used & ((1u << pos[i]) - 1)));
        r = r * uint32_t(12 - i) + (pos[i] - below);
        used |= 1u << pos[i];
    }
    return r;
}

inline void unrankPartial(uint32_t r, uint8_t* pos) {
    uint32_t digit[EDGE_K];
    for (int i = EDGE_K - 1; i >= 0; i--) {
        digit[i] = r % uint32_t(12 - i);
        r /= uint32_t(12 - i);
    }
    uint32_t used = 0;
    for (int i = 0; i < EDGE_K; i++) {
        uint32_t d = digit[i];
        for (int v = 0; v < 12; v++) {
            if (used & (1u << v)) continue;
            if (d == 0) { pos[i] = uint8_t(v); used |= 1u << v; break; }
            d--;
        }
    }
}

// ------------------------------------------------------------- move tables

// How the two corner coordinates change under each of the 18 moves.
struct CornerMoves {
    std::vector<uint16_t> perm;   // perm[rank * 18 + move]
    std::vector<uint16_t> twist;  // twist[t * 18 + move]

    CornerMoves()
        : perm(size_t(CPERM_N) * NUM_MOVES), twist(size_t(TWIST_N) * NUM_MOVES) {
        const auto& mt = moveTable();
        for (int r = 0; r < CPERM_N; r++) {
            uint8_t p[8], q[8];
            unrankPerm8(r, p);
            for (int m = 0; m < NUM_MOVES; m++) {
                for (int i = 0; i < 8; i++) q[i] = p[mt[m].cp[i]];
                perm[size_t(r) * NUM_MOVES + m] = uint16_t(rankPerm8(q));
            }
        }
        for (int t = 0; t < TWIST_N; t++) {
            uint8_t co[8], nco[8];
            decodeTwist(t, co);
            for (int m = 0; m < NUM_MOVES; m++) {
                for (int i = 0; i < 8; i++)
                    nco[i] = uint8_t((co[mt[m].cp[i]] + mt[m].co[i]) % 3);
                twist[size_t(t) * NUM_MOVES + m] = uint16_t(encodeTwist(nco));
            }
        }
    }
};

inline const CornerMoves& cornerMoves() {
    static const CornerMoves cm;
    return cm;
}

// Where does an edge cubie that sits in slot p end up after a move, and how
// much does its flip change?
struct EdgeMoves {
    uint8_t newPos[NUM_MOVES][12];
    uint8_t flip[NUM_MOVES][12];

    EdgeMoves() {
        const auto& mt = moveTable();
        for (int m = 0; m < NUM_MOVES; m++) {
            for (int q = 0; q < 12; q++) newPos[m][mt[m].ep[q]] = uint8_t(q);
            for (int p = 0; p < 12; p++) flip[m][p] = mt[m].eo[newPos[m][p]];
        }
    }
};

inline const EdgeMoves& edgeMoves() {
    static const EdgeMoves em;
    return em;
}

// ---------------------------------------------------------- compact storage

class NibbleTable {
public:
    NibbleTable() = default;
    explicit NibbleTable(uint64_t n, uint8_t fill = 0xF)
        : n_(n), d_((n + 1) / 2, uint8_t(fill | (fill << 4))) {}

    uint8_t get(uint64_t i) const { return (d_[i >> 1] >> ((i & 1) << 2)) & 0xF; }
    void set(uint64_t i, uint8_t v) {
        unsigned s = unsigned(i & 1) << 2;
        uint8_t& b = d_[i >> 1];
        b = uint8_t((b & ~(0xFu << s)) | (unsigned(v) << s));
    }
    uint64_t size() const { return n_; }
    std::vector<uint8_t>& raw() { return d_; }
    const std::vector<uint8_t>& raw() const { return d_; }

private:
    uint64_t n_ = 0;
    std::vector<uint8_t> d_;
};

// ----------------------------------------------------------- BFS generation

// Layer-by-layer breadth-first search over a table of `n` states. `expand(i,
// push)` must call push(j) for every neighbour j of state i.
template <class Expand>
inline void bfsFill(NibbleTable& t, uint64_t n, uint64_t start, const char* label,
                    bool verbose, Expand expand) {
    auto t0 = std::chrono::steady_clock::now();
    t.set(start, 0);
    uint64_t total = 1;
    for (int depth = 0; depth < 14; depth++) {
        uint64_t added = 0;
        const uint8_t d = uint8_t(depth), nd = uint8_t(depth + 1);
        for (uint64_t i = 0; i < n; i++) {
            if (t.get(i) != d) continue;
            expand(i, [&](uint64_t j) {
                if (t.get(j) == 0xF) { t.set(j, nd); added++; }
            });
        }
        total += added;
        if (verbose && added)
            std::fprintf(stderr, "  [%s] depth %2d: %11llu states\n", label, depth + 1,
                         (unsigned long long)added);
        if (added == 0) break;
    }
    if (verbose) {
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "  [%s] done: %llu of %llu states in %.1fs%s\n", label,
                     (unsigned long long)total, (unsigned long long)n, s,
                     total == n ? "" : "  (WARNING: not all states reached)");
    }
}

inline void buildCornerTable(NibbleTable& t, bool verbose) {
    const CornerMoves& cm = cornerMoves();
    bfsFill(t, CORNER_N, 0, "corners", verbose, [&](uint64_t idx, auto&& push) {
        const uint32_t perm = uint32_t(idx / TWIST_N), twist = uint32_t(idx % TWIST_N);
        for (int m = 0; m < NUM_MOVES; m++)
            push(uint64_t(cm.perm[perm * NUM_MOVES + m]) * TWIST_N +
                 cm.twist[twist * NUM_MOVES + m]);
    });
}

struct EdgeSubset {
    uint8_t ids[EDGE_K];
};
inline const EdgeSubset EDGE_SUBSET_A = {{UR, UF, UL, UB, DR, DF}};
inline const EdgeSubset EDGE_SUBSET_B = {{DL, DB, FR, FL, BL, BR}};

inline void buildEdgeTable(NibbleTable& t, const EdgeSubset& s, const char* label,
                           bool verbose) {
    const EdgeMoves& em = edgeMoves();
    uint8_t startPos[EDGE_K];
    for (int k = 0; k < EDGE_K; k++) startPos[k] = s.ids[k];
    uint64_t start = uint64_t(rankPartial(startPos)) * 64;
    bfsFill(t, EDGE_N, start, label, verbose, [&](uint64_t idx, auto&& push) {
        uint8_t pos[EDGE_K];
        unrankPartial(uint32_t(idx >> 6), pos);
        const uint32_t ori = uint32_t(idx & 63);
        for (int m = 0; m < NUM_MOVES; m++) {
            uint8_t np[EDGE_K];
            uint32_t no = 0;
            for (int k = 0; k < EDGE_K; k++) {
                np[k] = em.newPos[m][pos[k]];
                no |= (((ori >> k) & 1u) ^ em.flip[m][pos[k]]) << k;
            }
            push(uint64_t(rankPartial(np)) * 64 + no);
        }
    });
}

// ------------------------------------------------------------ the databases

struct PdbHeader {
    char magic[8];
    uint32_t version;
    uint32_t reserved;
    uint64_t entries;
};

inline bool saveTable(const std::string& path, const NibbleTable& t) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    PdbHeader h{};
    std::memcpy(h.magic, "RUBIXPDB", 8);
    h.version = 1;
    h.entries = t.size();
    out.write(reinterpret_cast<const char*>(&h), sizeof h);
    out.write(reinterpret_cast<const char*>(t.raw().data()), std::streamsize(t.raw().size()));
    return bool(out);
}

inline bool loadTable(const std::string& path, NibbleTable& t, uint64_t expectedEntries) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    PdbHeader h{};
    in.read(reinterpret_cast<char*>(&h), sizeof h);
    if (!in || std::memcmp(h.magic, "RUBIXPDB", 8) != 0 || h.version != 1 ||
        h.entries != expectedEntries)
        return false;
    NibbleTable tmp(expectedEntries);
    in.read(reinterpret_cast<char*>(tmp.raw().data()), std::streamsize(tmp.raw().size()));
    if (!in) return false;
    t = std::move(tmp);
    return true;
}

class PatternDatabases {
public:
    NibbleTable corner, edgeA, edgeB;

    // Builds all three tables (one thread each).
    void build(bool verbose = true) {
        corner = NibbleTable(CORNER_N);
        edgeA = NibbleTable(EDGE_N);
        edgeB = NibbleTable(EDGE_N);
        // Force the shared lookup tables into existence before threads start.
        cornerMoves();
        edgeMoves();
        std::thread tc([&] { buildCornerTable(corner, verbose); });
        std::thread ta([&] { buildEdgeTable(edgeA, EDGE_SUBSET_A, "edgesA", verbose); });
        std::thread tb([&] { buildEdgeTable(edgeB, EDGE_SUBSET_B, "edgesB", verbose); });
        tc.join();
        ta.join();
        tb.join();
    }

    bool load(const std::string& dir) {
        return loadTable(dir + "/corners.pdb", corner, CORNER_N) &&
               loadTable(dir + "/edgesA.pdb", edgeA, EDGE_N) &&
               loadTable(dir + "/edgesB.pdb", edgeB, EDGE_N);
    }

    bool save(const std::string& dir) const {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return saveTable(dir + "/corners.pdb", corner) &&
               saveTable(dir + "/edgesA.pdb", edgeA) &&
               saveTable(dir + "/edgesB.pdb", edgeB);
    }

    // Loads cached tables from `dir`, or builds and caches them.
    void loadOrBuild(const std::string& dir, bool verbose = true) {
        if (load(dir)) {
            if (verbose) std::fprintf(stderr, "Loaded pattern databases from %s/\n", dir.c_str());
            return;
        }
        if (verbose)
            std::fprintf(stderr,
                         "Pattern databases not found in %s/ - building them (one-off, a few minutes)...\n",
                         dir.c_str());
        build(verbose);
        if (save(dir)) {
            if (verbose) std::fprintf(stderr, "Saved pattern databases to %s/\n", dir.c_str());
        } else if (verbose) {
            std::fprintf(stderr, "Warning: could not write pattern databases to %s/\n", dir.c_str());
        }
    }
};

}  // namespace rubix
