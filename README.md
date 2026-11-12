# Rubik's Cube Optimal Solver

A C++17 solver for the 3x3x3 Rubik's Cube that finds **shortest solutions** (in the
half-turn metric, where `U`, `U2` and `U'` each count as one move). It uses
**IDA\*** (iterative-deepening A\*) guided by **pattern databases**, the approach
Richard Korf introduced in 1997 for optimal cube solving.

## Build and run

Requires `g++` (C++17) and `make`.

```bash
make                 # builds bin/rubix_solver
make db              # builds + caches the pattern databases in ./pdb (~15 s, ~86 MB)

./bin/rubix_solver solve --moves "R U R' U'"
./bin/rubix_solver solve --facelets UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB
./bin/rubix_solver random --length 12 --count 5 --seed 1
./bin/rubix_solver random --length 15 --timeout 60
```

If the databases are missing, any `solve`/`random` command builds them first.

Example:

```
Scramble : R U R' U'
Lower bnd: 4 moves
Solution : U R U' R'
Length   : 4 moves (optimal)
Nodes    : 9
Verified : yes, applying it solves the cube
```

**Facelet input** is 54 characters: faces in the order `U R F D L B`, nine stickers
each, read row by row as you look at the face (the usual unfolded-cross layout).
Physically impossible cubes (a twisted corner, a flipped edge, two swapped pieces)
are rejected with an explanation.

## Tests

```bash
make test            # 128k checks on the cube model, facelets, coordinates, move tables
make test-solver     # heuristic admissibility + solving, needs the databases
```

The unit tests include physical checks (e.g. after an `R` turn the right-hand column
of U must show F stickers), the known orders of well-known algorithms
(`R U` has order 105, `R U2 D' B D'` has order 1260), round-trips through facelets,
and that the incremental search coordinates always agree with the real cube.
The corner database's level sizes (1, 18, 243, 2 874, 28 000, 205 416, ...) match the
published distribution for the corner cube.

## How it works

**Cube model (`src/cube.h`)** - the cube is four small arrays: which corner/edge piece
is in each slot and how it is twisted/flipped. A move is a permutation of the same
shape, and applying a move is `compose(state, move)`. All 18 moves are generated from
the six clockwise quarter turns.

**Pattern databases (`src/pdb.h`)** - for a *subset* of pieces, a breadth-first search
from the solved state records the exact number of moves needed to solve just that
subset. Three tables are stored at 4 bits per entry:

| Table   | Pieces tracked          | States     | Size  |
|---------|-------------------------|-----------:|------:|
| corners | all 8 corners           | 88,179,840 | 42 MB |
| edgesA  | UR UF UL UB DR DF       | 42,577,920 | 20 MB |
| edgesB  | DL DB FR FL BL BR       | 42,577,920 | 20 MB |

Solving a subset can never take more moves than solving the whole cube, so each table
is a lower bound; the maximum of the three is the heuristic.

**IDA\* (`src/solver.h`)** - repeated depth-first search with a rising bound. A branch
is cut when `moves so far + heuristic > bound`. Because the heuristic never
overestimates, the first solution found is optimal. Search state is updated
incrementally (corner coordinates via move tables, tracked edges via small position
maps), so each node costs a few table lookups. Redundant move pairs (same face twice,
or opposite faces in the wrong order) are skipped.

## Performance

Measured on a 2-core cloud VM, single-threaded search:

| Scramble length | Optimal length | Typical time        |
|----------------:|---------------:|---------------------|
| 12              | 12             | ~0.01-0.03 s        |
| 14              | 14             | 3-16 s              |
| 16+             | 16+            | minutes or longer   |

The search runs at roughly 6 million nodes per second. Fully random cubes need 17-18
moves on average to solve optimally, which can take minutes to hours with this
heuristic, so use `--timeout`. (Non-optimal solvers such as Kociemba's two-phase
algorithm solve any cube in milliseconds; this project deliberately targets optimality.)

## Project layout

```
src/cube.h         cube state, moves, move parsing, facelet conversion
src/pdb.h          coordinates, move tables, pattern database build/save/load
src/solver.h       IDA* search
src/main.cpp       command line interface
tests/             unit tests and solver tests
Makefile
```

## Ideas for extending it

- Search both directions of a scramble in parallel threads (the two edge tables are
  independent, and IDA* iterations split naturally by first move).
- Add symmetry reduction to the databases to make lookups cache friendlier.
- Add a fast non-optimal mode (two-phase search) and compare solution length vs. time.
- Add a 7-edge database and measure how much the node count drops.

## Credits

- Richard E. Korf, *Finding Optimal Solutions to Rubik's Cube Using Pattern Databases*
  (AAAI 1997) - the technique.
- Piece numbering and the six base face-turn definitions follow the convention used in
  Herbert Kociemba's two-phase solver, so states are comparable with other tools.
- This code was written with the help of Claude (Anthropic) as an AI coding assistant.
