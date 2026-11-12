// solver.h - optimal Rubik's Cube solver: IDA* guided by pattern databases.
//
// IDA* repeats a depth-first search with a growing cost bound. A branch is cut
// as soon as  moves_so_far + heuristic  exceeds the bound. The heuristic is the
// largest of the three pattern-database distances, which never overestimates,
// so the first solution found is a shortest one (in the half-turn metric,
// where every one of the 18 face turns costs 1).
#pragma once

#include "pdb.h"

#include <algorithm>
#include <chrono>
#include <climits>

namespace rubix {

struct SolveResult {
    bool solved = false;
    bool timedOut = false;
    std::vector<int> moves;
    uint64_t nodes = 0;
    double seconds = 0.0;
};

class IDAStarSolver {
public:
    bool verbose = false;  // print each new depth bound to stderr

    explicit IDAStarSolver(const PatternDatabases& db)
        : db_(db), cm_(cornerMoves()), em_(edgeMoves()) {}

    // Lower bound on the number of moves needed to solve `cube`.
    int heuristic(const Cube& cube) const { return h(makeNode(cube)); }

    // Finds a shortest solution. timeLimitSec <= 0 means no limit.
    SolveResult solve(const Cube& cube, int maxDepth = 20, double timeLimitSec = 0.0) {
        SolveResult res;
        start_ = Clock::now();
        limit_ = timeLimitSec;
        nodes_ = 0;
        timedOut_ = false;

        const Node root = makeNode(cube);
        int bound = h(root);
        while (bound <= maxDepth) {
            if (verbose) std::fprintf(stderr, "  searching depth %d ...\n", bound);
            nextBound_ = INT_MAX;
            path_.clear();
            if (dfs(root, 0, bound, -1)) {
                res.solved = true;
                res.moves = path_;
                break;
            }
            if (timedOut_ || nextBound_ == INT_MAX) break;
            bound = nextBound_;
        }
        res.timedOut = timedOut_;
        res.nodes = nodes_;
        res.seconds = std::chrono::duration<double>(Clock::now() - start_).count();
        return res;
    }

private:
    using Clock = std::chrono::steady_clock;

    // Everything the heuristic needs, kept incrementally up to date.
    struct Node {
        uint16_t cperm, twist;     // corner coordinates
        uint8_t pa[EDGE_K];        // slots of the tracked edges of subset A
        uint8_t pb[EDGE_K];        // ... and subset B
        uint8_t oa, ob;            // flip bit k = flip of tracked edge k
    };

    static void trackEdges(const Cube& c, const EdgeSubset& s, uint8_t* pos, uint8_t& ori) {
        ori = 0;
        for (int k = 0; k < EDGE_K; k++) {
            for (int p = 0; p < 12; p++) {
                if (c.ep[p] == s.ids[k]) {
                    pos[k] = uint8_t(p);
                    ori |= uint8_t(c.eo[p] << k);
                    break;
                }
            }
        }
    }

    Node makeNode(const Cube& c) const {
        Node n{};
        n.cperm = uint16_t(rankPerm8(c.cp.data()));
        n.twist = uint16_t(encodeTwist(c.co.data()));
        trackEdges(c, EDGE_SUBSET_A, n.pa, n.oa);
        trackEdges(c, EDGE_SUBSET_B, n.pb, n.ob);
        return n;
    }

    int h(const Node& n) const {
        int a = db_.corner.get(uint64_t(n.cperm) * TWIST_N + n.twist);
        int b = db_.edgeA.get(uint64_t(rankPartial(n.pa)) * 64 + n.oa);
        int c = db_.edgeB.get(uint64_t(rankPartial(n.pb)) * 64 + n.ob);
        return std::max(a, std::max(b, c));
    }

    Node step(const Node& n, int m) const {
        Node c;
        c.cperm = cm_.perm[size_t(n.cperm) * NUM_MOVES + m];
        c.twist = cm_.twist[size_t(n.twist) * NUM_MOVES + m];
        c.oa = c.ob = 0;
        for (int k = 0; k < EDGE_K; k++) {
            uint8_t p = n.pa[k];
            c.pa[k] = em_.newPos[m][p];
            c.oa |= uint8_t((((n.oa >> k) & 1) ^ em_.flip[m][p]) << k);
            uint8_t q = n.pb[k];
            c.pb[k] = em_.newPos[m][q];
            c.ob |= uint8_t((((n.ob >> k) & 1) ^ em_.flip[m][q]) << k);
        }
        return c;
    }

    bool dfs(const Node& n, int g, int bound, int lastFace) {
        nodes_++;
        const int hv = h(n);
        if (g + hv > bound) {
            nextBound_ = std::min(nextBound_, g + hv);
            return false;
        }
        // h == 0 means corners and both edge sets are in place: solved.
        if (hv == 0) return true;

        if (limit_ > 0 && (nodes_ & 0xFFFF) == 0 &&
            std::chrono::duration<double>(Clock::now() - start_).count() > limit_) {
            timedOut_ = true;
            return false;
        }
        for (int m = 0; m < NUM_MOVES; m++) {
            const int face = m / 3;
            if (redundantAfter(lastFace, face)) continue;
            path_.push_back(m);
            if (dfs(step(n, m), g + 1, bound, face)) return true;
            path_.pop_back();
            if (timedOut_) return false;
        }
        return false;
    }

    const PatternDatabases& db_;
    const CornerMoves& cm_;
    const EdgeMoves& em_;

    std::vector<int> path_;
    uint64_t nodes_ = 0;
    int nextBound_ = INT_MAX;
    bool timedOut_ = false;
    double limit_ = 0.0;
    Clock::time_point start_;
};

}  // namespace rubix
