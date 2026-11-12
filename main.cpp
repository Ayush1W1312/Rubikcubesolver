// main.cpp - command line interface for the Rubik's Cube solver.
#include "cube.h"
#include "pdb.h"
#include "solver.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>

using namespace rubix;

static void usage() {
    std::cout <<
R"(Rubik's Cube optimal solver (IDA* + pattern databases)

Usage:
  rubix_solver build-db [--db DIR]
      Build the pattern databases and save them in DIR (default: ./pdb).

  rubix_solver solve --moves "R U R' U'"  [options]
  rubix_solver solve --facelets <54 chars> [options]
      Solve the cube reached by applying the given moves to a solved cube, or
      the cube described by a facelet string (faces U R F D L B, 9 stickers each).

  rubix_solver random [--length N] [--count K] [--seed S] [options]
      Scramble a solved cube with N random moves (default 12) and solve it.

Options:
  --db DIR        directory holding the pattern databases (default: pdb)
  --timeout SEC   give up on a solve after SEC seconds (default: no limit)
  --verbose 1     show search progress

Notes:
  * The first run builds the databases (a few minutes, ~86 MB on disk).
  * Solutions are optimal in the half-turn metric (U, U2, U' each count as 1).
    Cubes that need 17+ moves can take a long time; use --timeout.
  * Moves: U R F D L B, with 2 for a half turn and ' for counter-clockwise.
)";
}

static void report(const Cube& scrambled, const SolveResult& r) {
    if (!r.solved) {
        std::cout << (r.timedOut ? "No solution found before the timeout.\n"
                                 : "No solution found within 20 moves (is the cube valid?).\n");
        std::cout << "Nodes    : " << r.nodes << "\n";
        return;
    }
    Cube check = scrambled;
    check.apply(r.moves);
    std::cout << "Solution : " << (r.moves.empty() ? "(already solved)" : movesToString(r.moves)) << "\n";
    std::cout << "Length   : " << r.moves.size() << " moves (optimal)\n";
    std::cout << "Nodes    : " << r.nodes << "\n";
    std::cout << std::fixed << std::setprecision(2) << "Time     : " << r.seconds << " s\n";
    std::cout << "Verified : " << (check.isSolved() ? "yes, applying it solves the cube" : "NO - bug!") << "\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    const std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") { usage(); return 0; }

    std::map<std::string, std::string> opt;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0 && i + 1 < argc) {
            opt[a.substr(2)] = argv[++i];
        } else {
            std::cerr << "Unexpected argument: " << a << "\n\n";
            usage();
            return 1;
        }
    }
    const std::string dir = opt.count("db") ? opt["db"] : "pdb";

    try {
        if (cmd == "build-db") {
            PatternDatabases db;
            db.build(true);
            if (!db.save(dir)) { std::cerr << "Could not write to " << dir << "/\n"; return 1; }
            std::cout << "Saved pattern databases to " << dir << "/\n";
            return 0;
        }

        if (cmd != "solve" && cmd != "random") {
            std::cerr << "Unknown command: " << cmd << "\n\n";
            usage();
            return 1;
        }

        const double timeout = opt.count("timeout") ? std::stod(opt["timeout"]) : 0.0;
        const bool verbose = opt.count("verbose") && opt["verbose"] != "0";

        // Build the cube(s) to solve before the (slow) database load, so that
        // bad input is reported immediately.
        std::vector<std::pair<std::string, Cube>> jobs;
        if (cmd == "solve") {
            Cube c;
            std::string label;
            if (opt.count("moves")) {
                std::vector<int> mv = parseMoves(opt["moves"]);
                c.apply(mv);
                label = opt["moves"];
            } else if (opt.count("facelets")) {
                std::string err;
                if (!fromFacelets(opt["facelets"], c, &err)) {
                    std::cerr << "Invalid facelet string: " << err << "\n";
                    return 1;
                }
                label = opt["facelets"];
            } else {
                std::cerr << "solve needs --moves or --facelets\n\n";
                usage();
                return 1;
            }
            jobs.emplace_back(label, c);
        } else {
            int length = opt.count("length") ? std::stoi(opt["length"]) : 12;
            int count = opt.count("count") ? std::stoi(opt["count"]) : 1;
            uint64_t seed = opt.count("seed") ? std::stoull(opt["seed"]) : std::random_device{}();
            std::mt19937_64 rng(seed);
            for (int i = 0; i < count; i++) {
                Cube c;
                std::vector<int> mv = randomScramble(length, rng);
                c.apply(mv);
                jobs.emplace_back(movesToString(mv), c);
            }
        }

        PatternDatabases db;
        db.loadOrBuild(dir, true);
        IDAStarSolver solver(db);
        solver.verbose = verbose;

        for (size_t i = 0; i < jobs.size(); i++) {
            if (i) std::cout << "\n";
            std::cout << "Scramble : " << jobs[i].first << "\n";
            std::cout << "Lower bnd: " << solver.heuristic(jobs[i].second) << " moves\n";
            SolveResult r = solver.solve(jobs[i].second, 20, timeout);
            report(jobs[i].second, r);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
