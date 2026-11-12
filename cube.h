// cube.h - cubie-level model of the 3x3x3 Rubik's Cube.
//
// A cube state is stored as four arrays:
//   cp[i] : which corner cubie sits in corner slot i      (0..7)
//   co[i] : twist of the corner in slot i                 (0, 1, 2)
//   ep[i] : which edge cubie sits in edge slot i          (0..11)
//   eo[i] : flip of the edge in slot i                    (0, 1)
//
// Slot / cubie numbering and the six face-turn definitions follow the widely
// used convention from Herbert Kociemba's two-phase algorithm, so states can be
// compared against other solvers.
//
// Moves are numbered face * 3 + k with faces ordered U, R, F, D, L, B and
// k = 0 (clockwise quarter turn), 1 (half turn), 2 (counter-clockwise).
#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace rubix {

enum Corner : uint8_t { URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB };
enum Edge : uint8_t { UR, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR };
enum Face : int { U, R, F, D, L, B };

constexpr int NUM_MOVES = 18;

struct Cube {
    std::array<uint8_t, 8> cp, co;
    std::array<uint8_t, 12> ep, eo;

    Cube() {
        for (int i = 0; i < 8; i++) { cp[i] = uint8_t(i); co[i] = 0; }
        for (int i = 0; i < 12; i++) { ep[i] = uint8_t(i); eo[i] = 0; }
    }

    bool operator==(const Cube& o) const {
        return cp == o.cp && co == o.co && ep == o.ep && eo == o.eo;
    }
    bool operator!=(const Cube& o) const { return !(*this == o); }
    bool isSolved() const { return *this == Cube(); }

    void apply(int move);
    void apply(const std::vector<int>& moves) {
        for (int m : moves) apply(m);
    }
};

// "a followed by b". Works for cube states and for move definitions alike.
inline Cube compose(const Cube& a, const Cube& b) {
    Cube r;
    for (int i = 0; i < 8; i++) {
        r.cp[i] = a.cp[b.cp[i]];
        r.co[i] = uint8_t((a.co[b.cp[i]] + b.co[i]) % 3);
    }
    for (int i = 0; i < 12; i++) {
        r.ep[i] = a.ep[b.ep[i]];
        r.eo[i] = uint8_t((a.eo[b.ep[i]] + b.eo[i]) % 2);
    }
    return r;
}

// The 18 face turns, each expressed as a (cube-shaped) permutation.
inline const std::array<Cube, NUM_MOVES>& moveTable() {
    static const std::array<Cube, NUM_MOVES> table = [] {
        std::array<Cube, 6> base;  // clockwise quarter turns

        base[U].cp = {UBR, URF, UFL, ULB, DFR, DLF, DBL, DRB};
        base[U].ep = {UB, UR, UF, UL, DR, DF, DL, DB, FR, FL, BL, BR};

        base[R].cp = {DFR, UFL, ULB, URF, DRB, DLF, DBL, UBR};
        base[R].co = {2, 0, 0, 1, 1, 0, 0, 2};
        base[R].ep = {FR, UF, UL, UB, BR, DF, DL, DB, DR, FL, BL, UR};

        base[F].cp = {UFL, DLF, ULB, UBR, URF, DFR, DBL, DRB};
        base[F].co = {1, 2, 0, 0, 2, 1, 0, 0};
        base[F].ep = {UR, FL, UL, UB, DR, FR, DL, DB, UF, DF, BL, BR};
        base[F].eo = {0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0};

        base[D].cp = {URF, UFL, ULB, UBR, DLF, DBL, DRB, DFR};
        base[D].ep = {UR, UF, UL, UB, DF, DL, DB, DR, FR, FL, BL, BR};

        base[L].cp = {URF, ULB, DBL, UBR, DFR, UFL, DLF, DRB};
        base[L].co = {0, 1, 2, 0, 0, 2, 1, 0};
        base[L].ep = {UR, UF, BL, UB, DR, DF, FL, DB, FR, UL, DL, BR};

        base[B].cp = {URF, UFL, UBR, DRB, DFR, DLF, ULB, DBL};
        base[B].co = {0, 0, 1, 2, 0, 0, 2, 1};
        base[B].ep = {UR, UF, UL, BR, DR, DF, DL, BL, FR, FL, UB, DB};
        base[B].eo = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1};

        std::array<Cube, NUM_MOVES> t;
        for (int f = 0; f < 6; f++) {
            t[f * 3] = base[f];
            t[f * 3 + 1] = compose(base[f], base[f]);
            t[f * 3 + 2] = compose(t[f * 3 + 1], base[f]);
        }
        return t;
    }();
    return table;
}

inline void Cube::apply(int move) { *this = compose(*this, moveTable()[move]); }

// ---------------------------------------------------------------- move utils

inline int inverseMove(int m) { return (m / 3) * 3 + (2 - m % 3); }

// Search pruning: after a turn of `lastFace`, is turning `face` pointless?
// Same face twice always merges into one move; for opposite faces (which
// commute) only one of the two orders is allowed.
inline bool redundantAfter(int lastFace, int face) {
    if (lastFace < 0) return false;
    return face % 3 == lastFace % 3 && (face == lastFace || lastFace >= 3);
}

inline std::string moveName(int m) {
    std::string s(1, "URFDLB"[m / 3]);
    if (m % 3 == 1) s += '2';
    else if (m % 3 == 2) s += '\'';
    return s;
}

inline std::string movesToString(const std::vector<int>& moves) {
    std::string s;
    for (size_t i = 0; i < moves.size(); i++) {
        if (i) s += ' ';
        s += moveName(moves[i]);
    }
    return s;
}

// Parses e.g. "R U R' U2 F". Throws std::invalid_argument on bad input.
inline std::vector<int> parseMoves(const std::string& text) {
    static const std::string faces = "URFDLB";
    std::vector<int> out;
    size_t i = 0;
    while (i < text.size()) {
        char ch = text[i];
        if (std::isspace(static_cast<unsigned char>(ch))) { i++; continue; }
        size_t f = faces.find(ch);
        if (f == std::string::npos)
            throw std::invalid_argument(std::string("unknown move character '") + ch + "'");
        i++;
        int k = 0;
        if (i < text.size() && text[i] == '2') { k = 1; i++; }
        else if (i < text.size() && (text[i] == '\'' || text[i] == '3')) { k = 2; i++; }
        out.push_back(int(f) * 3 + k);
    }
    return out;
}

inline std::vector<int> randomScramble(int length, std::mt19937_64& rng) {
    std::vector<int> moves;
    int last = -1;
    while (int(moves.size()) < length) {
        int m = int(rng() % NUM_MOVES);
        if (redundantAfter(last, m / 3)) continue;
        moves.push_back(m);
        last = m / 3;
    }
    return moves;
}

// ------------------------------------------------------------------ facelets
//
// Facelet string: 54 characters, faces in the order U R F D L B, nine stickers
// each, read row by row as if looking straight at the face (the usual "unfolded
// cross" layout). Sticker characters are the face letters U R F D L B.

inline const int CORNER_FACELET[8][3] = {
    {8, 9, 20},  {6, 18, 38}, {0, 36, 47}, {2, 45, 11},
    {29, 26, 15}, {27, 44, 24}, {33, 53, 42}, {35, 17, 51}};

inline const int EDGE_FACELET[12][2] = {
    {5, 10}, {7, 19}, {3, 37}, {1, 46}, {32, 16}, {28, 25},
    {30, 43}, {34, 52}, {23, 12}, {21, 41}, {50, 39}, {48, 14}};

inline const char CORNER_COLOR[8][3] = {
    {'U', 'R', 'F'}, {'U', 'F', 'L'}, {'U', 'L', 'B'}, {'U', 'B', 'R'},
    {'D', 'F', 'R'}, {'D', 'L', 'F'}, {'D', 'B', 'L'}, {'D', 'R', 'B'}};

inline const char EDGE_COLOR[12][2] = {
    {'U', 'R'}, {'U', 'F'}, {'U', 'L'}, {'U', 'B'}, {'D', 'R'}, {'D', 'F'},
    {'D', 'L'}, {'D', 'B'}, {'F', 'R'}, {'F', 'L'}, {'B', 'L'}, {'B', 'R'}};

inline std::string toFacelets(const Cube& c) {
    std::string f(54, '?');
    for (int face = 0; face < 6; face++) f[face * 9 + 4] = "URFDLB"[face];
    for (int i = 0; i < 8; i++)
        for (int n = 0; n < 3; n++)
            f[CORNER_FACELET[i][(n + c.co[i]) % 3]] = CORNER_COLOR[c.cp[i]][n];
    for (int i = 0; i < 12; i++)
        for (int n = 0; n < 2; n++)
            f[EDGE_FACELET[i][(n + c.eo[i]) % 2]] = EDGE_COLOR[c.ep[i]][n];
    return f;
}

template <size_t N>
inline int permParity(const std::array<uint8_t, N>& p) {
    int inversions = 0;
    for (size_t i = 0; i < N; i++)
        for (size_t j = i + 1; j < N; j++)
            if (p[i] > p[j]) inversions++;
    return inversions & 1;
}

// Returns "" if the state is physically reachable, otherwise a reason.
inline std::string validate(const Cube& c) {
    int seenC = 0, seenE = 0, twist = 0, flip = 0;
    for (int i = 0; i < 8; i++) { seenC |= 1 << c.cp[i]; twist += c.co[i]; }
    for (int i = 0; i < 12; i++) { seenE |= 1 << c.ep[i]; flip += c.eo[i]; }
    if (seenC != 0xFF) return "some corner cubie appears twice or is missing";
    if (seenE != 0xFFF) return "some edge cubie appears twice or is missing";
    if (twist % 3 != 0) return "a corner is twisted in place";
    if (flip % 2 != 0) return "an edge is flipped in place";
    if (permParity(c.cp) != permParity(c.ep)) return "two pieces are swapped (parity error)";
    return "";
}

// Builds a cube from a facelet string. Returns false and sets *err on failure.
inline bool fromFacelets(const std::string& f, Cube& out, std::string* err = nullptr) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    if (f.size() != 54) return fail("facelet string must have exactly 54 characters");
    int count[256] = {};
    for (unsigned char ch : f) count[ch]++;
    for (char ch : std::string("URFDLB"))
        if (count[(unsigned char)ch] != 9)
            return fail(std::string("each of U R F D L B must appear 9 times; wrong count for ") + ch);
    for (int face = 0; face < 6; face++)
        if (f[face * 9 + 4] != "URFDLB"[face])
            return fail("centre stickers must be in the order U R F D L B");

    Cube c;
    for (int i = 0; i < 8; i++) {
        int ori = 0;
        while (ori < 3 && f[CORNER_FACELET[i][ori]] != 'U' && f[CORNER_FACELET[i][ori]] != 'D') ori++;
        if (ori == 3) return fail("a corner has no U or D sticker");
        char c1 = f[CORNER_FACELET[i][(ori + 1) % 3]];
        char c2 = f[CORNER_FACELET[i][(ori + 2) % 3]];
        bool found = false;
        for (int j = 0; j < 8 && !found; j++) {
            if (CORNER_COLOR[j][1] == c1 && CORNER_COLOR[j][2] == c2) {
                c.cp[i] = uint8_t(j);
                c.co[i] = uint8_t(ori);
                found = true;
            }
        }
        if (!found) return fail("a corner has an impossible colour combination");
    }
    for (int i = 0; i < 12; i++) {
        char a = f[EDGE_FACELET[i][0]], b = f[EDGE_FACELET[i][1]];
        bool found = false;
        for (int j = 0; j < 12 && !found; j++) {
            if (a == EDGE_COLOR[j][0] && b == EDGE_COLOR[j][1]) {
                c.ep[i] = uint8_t(j); c.eo[i] = 0; found = true;
            } else if (a == EDGE_COLOR[j][1] && b == EDGE_COLOR[j][0]) {
                c.ep[i] = uint8_t(j); c.eo[i] = 1; found = true;
            }
        }
        if (!found) return fail("an edge has an impossible colour combination");
    }
    std::string problem = validate(c);
    if (!problem.empty()) return fail(problem);
    out = c;
    return true;
}

}  // namespace rubix
