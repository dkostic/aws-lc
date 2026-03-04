# Implementation Plan: Rewrite `delocate` from Go to C++

## Overview

Rewrite the `delocate` build tool (~2,900 lines of Go logic + ~8,200 lines of auto-generated PEG parser) as a standalone C++ executable. The tool transforms assembly source files for the FIPS module boundary. The C++ version must produce byte-identical output to the Go version for all existing test cases.

## Constraints

- The project sets `CMAKE_CXX_STANDARD 11` by default, but since `delocate` is a build-time host tool (not a library linked into the product), it can set its own standard. Target C++17 for the delocate executable only — this gives us `std::optional`, `std::string_view`, structured bindings, and `std::filesystem`.
- No external dependencies beyond the C++ standard library. The PEG parser will be hand-written (recursive descent), not generated from a third-party tool.
- The existing `testdata/` directory (18 test cases) serves as the acceptance test suite.

## File Structure

```
util/fipstools/delocate/
├── delocate.peg              # (keep, reference only)
├── delocate.go               # (keep during transition, remove after)
├── delocate.peg.go           # (keep during transition, remove after)
├── delocate_test.go          # (keep during transition, remove after)
├── CMakeLists.txt            # NEW — builds the C++ executable + test runner
├── delocate.cc               # NEW — main(), CLI arg parsing, file I/O
├── parser.h                  # NEW — PEG rule enum, AST node, parser class declaration
├── parser.cc                 # NEW — recursive descent parser implementation
├── transform.h               # NEW — delocation struct, transform() declaration
├── transform.cc              # NEW — core transformation logic (processInput, processDirective, etc.)
├── arch_x86_64.cc            # NEW — processIntelInstruction, x86-specific helpers
├── arch_aarch64.cc           # NEW — processAarch64Instruction, aarch64-specific helpers
├── arch_ppc64le.cc           # NEW — processPPCInstruction, ppc64-specific helpers
├── ar.h                      # NEW — ParseAR declaration
├── ar.cc                     # NEW — archive file parser
├── fips_const.h              # NEW — UninitHashValue constant
├── delocate_test.cc          # NEW — test runner using the existing testdata/
└── testdata/                 # (unchanged)
```

## Steps

### Step 1: Scaffold — CMakeLists.txt, fips_const.h, ar.h/ar.cc

Set up the build. Create `CMakeLists.txt` that builds a `delocate_cc` executable (distinct name during transition so both can coexist). Port the trivial pieces:

- `fips_const.h`: The 32-byte `UninitHashValue` array.
- `ar.h` / `ar.cc`: Port `ParseAR()` — reads `.a` archive files, returns `std::map<std::string, std::vector<char>>`. ~150 lines of C++. The Go version is ~140 lines; it's a straightforward binary format parser.

Validate: compile and link successfully.

### Step 2: Parser — parser.h / parser.cc

Port the PEG grammar as a hand-written recursive descent parser.

`parser.h`:
- Define `enum class PegRule : uint8_t` with all 63 rule constants (matching the Go `pegRule` enum exactly).
- Define `struct Node` (equivalent to Go's `node32`): `PegRule rule; uint32_t begin, end; Node* up; Node* next;`.
- Define `class Parser` with `bool parse(const std::string& input)` and `Node* ast()`.
- Use an arena allocator (a `std::deque<Node>` or similar) for node allocation to avoid per-node `new`/`delete`.

`parser.cc`:
- One method per grammar rule, returning `bool` (success/failure) and building AST nodes on match.
- The grammar has 63 rules but many are simple (e.g., `WS`, `Comment`, `Label`). The complex ones are `MemoryRef`, `RegisterOrConstant`, `Offset`, and `Instruction`.
- Implement PEG semantics: ordered choice (`/`), sequence, zero-or-more (`*`), not-predicate (`!`), character classes.
- Key detail: the Go parser operates on `[]rune` (Unicode codepoints). Assembly files are ASCII, so `char` is fine.

Estimated size: ~800-1,200 lines.

Validate: write a small test that parses each `testdata/*/in*.s` file and verifies the parse succeeds without errors. Compare AST structure against the Go parser by adding a tree-dump mode to both.

### Step 3: Core transformation framework — transform.h / transform.cc

Port the `delocation` struct and the architecture-independent logic.

`transform.h`:
- `enum class ProcessorType { PPC64LE, X86_64, AARCH64 };`
- `struct InputFile { std::string path; int index; bool isArchive; std::string contents; Node* ast; };`
- `class Delocation` with all the state fields from the Go `delocation` struct (symbol maps, redirectors, BSS accessors, GOT sets, etc.) using `std::unordered_set<std::string>` and `std::unordered_map<std::string, std::string>`.

`transform.cc` — port these functions:
- `detectProcessor()`, `sortedSet()`
- `contents()`, `writeNode()`, `writeCommentedNode()`, `locateError()`
- `processInput()` — the main per-file AST walk dispatching to labels/directives/instructions
- `processLabel()`, `processDirective()`, `processLabelContainingDirective()`, `processSymbolExpr()`
- `skipRelroSection()`, `maybeSkipRelroStatement()`, `skippedLine()`
- `handleBSS()`
- AST helpers: `assertNodeType()`, `skipWS()`, `skipNodes()`, `forEachPath()`, `matchPatternOneLine()`, `matchPatternSearchSubtree()`, `instructionArgs()`
- Relro logic: `isEndOfRelroSection()`, `isNewLine()`, `isProbablyAValidSymbol()`, `findLocalLabelsForRelro()`, `relroLocalLabelToFuncMapping()`
- Naming helpers: `redirectorName()`, `accessorName()`, `localTargetName()`, `localEntryName()`, `gotHelperName()`, `loadTOCFuncName()`, `isFipsScopeMarkers()`, `isSynthesized()`, `mapLocalSymbol()`, `sectionType()`
- `wrapperStack` — port as `std::vector<std::function<void(std::function<void()>)>>` with a `do()` method.
- `transform()` — the top-level function that orchestrates everything: symbol collection, file number tracking, per-input processing, then emitting all the helper functions (redirectors, BSS accessors, GOT entries, ia32cap functions, etc.)
- `parseInputs()`, `preprocess()`
- Include path helpers: `relativeHeaderIncludePath()`, `includePathFromHeaderFilePath()`

Estimated size: ~1,500-2,000 lines.

Validate: at this point the generic test cases (`generic-FileDirectives`, `generic-FileDirectives-no-start-end`, `generic-Includes`) should pass since they don't exercise architecture-specific instruction rewriting.

### Step 4: x86-64 instruction processing — arch_x86_64.cc

Port the x86-64 specific logic. This is the largest architecture handler.

Functions to port:
- `processIntelInstruction()` (~380 lines of Go) — the main dispatcher handling RIP-relative addressing, PLT calls, GOTPCREL references, `OPENSSL_ia32cap_P` accesses, and instruction rewriting.
- `isRIPRelative()`
- `classifyInstruction()` and `instructionType` enum
- Wrapper/helper functions: `push()`, `compare()`, `twoArgOp()`, `loadFromGOT()`, `saveRegister()`, `moveTo()`, `finalTransform()`, `combineOp()`, `threeArgCombineOp()`, `fourArgCombineOp()`, `memoryVectorCombineOp()`, `undoConditionalMove()`, `isValidLEATarget()`
- `cpuCapUniqueSymbol` struct and its methods

Estimated size: ~600-800 lines.

Validate: x86_64 test cases should pass: `x86_64-Basic`, `x86_64-BSS`, `x86_64-GOTRewrite`, `x86_64-LargeMemory`, `x86_64-LabelRewrite`, `x86_64-Sections`, `x86_64-ThreeArg`, `x86_64-FourArg`, `x86_64-Relro`.

### Step 5: aarch64 instruction processing — arch_aarch64.cc

Port:
- `processAarch64Instruction()` (~220 lines of Go)
- `loadAarch64Address()`
- `writeAarch64Function()`

Estimated size: ~300-400 lines.

Validate: `aarch64-Basic` test case should pass.

### Step 6: ppc64le instruction processing — arch_ppc64le.cc

Port:
- `processPPCInstruction()` (~210 lines of Go)
- `isPPC64LEAPair()`, `establishTOC()`, `loadFromTOC()`, `loadTOCFuncName()`, `gatherOffsets()`, `parseMemRef()`

Estimated size: ~350-450 lines.

Validate: ppc64le test cases should pass: `ppc64le-GlobalEntry`, `ppc64le-LoadToR0`, `ppc64le-Sample`, `ppc64le-Sample2`, `ppc64le-TOCWithOffset`.

### Step 7: Main entry point — delocate.cc

Port `main()`:
- CLI argument parsing (replace Go's `flag` package with manual `argc`/`argv` parsing or a simple helper — the tool only has 6 flags).
- File I/O: reading inputs, writing output.
- Preprocessor invocation via `popen()`.

Estimated size: ~150 lines.

Validate: the compiled binary should be invocable with the same CLI interface as the Go version.

### Step 8: CMake integration

Update `crypto/fipsmodule/CMakeLists.txt`:
- Replace `go_executable(delocate ...)` with `add_subdirectory()` pointing to the new C++ source, or inline `add_executable(delocate ...)`.
- Keep the same `add_custom_command` that invokes `./delocate` with the same flags.

### Step 9: Cleanup

Once all tests pass and the FIPS build succeeds end-to-end:
- Remove `delocate.go`, `delocate.peg.go`, `delocate_test.go`
- Remove the Go `ar` package if nothing else uses it (check first)
- Remove `delocate.peg` or keep it as documentation

---

## Testing Strategy

### Level 1: Unit test — parser correctness

Write a test that parses every `testdata/*/in*.s` file and asserts the parse succeeds. Optionally dump the AST and compare against the Go parser's output (add a `-dump-ast` flag to both).

### Level 2: Golden file tests (primary validation)

This is the most important test. Port the exact logic from `delocate_test.go`:

```
For each of the 18 test cases:
  1. Read input files from testdata/<name>/in*.s
  2. Call parseInputs() then transform()
  3. Compare output byte-for-byte against testdata/<name>/out.s
```

Create `delocate_test.cc` that does this. It can be a simple `main()` that iterates the test table, runs each case, and reports pass/fail with a diff on failure. No test framework dependency needed — just `assert` and `diff`-style output.

The test table (matching the Go version exactly):

| Test name | Includes | Inputs | startEndDebugDirectives |
|---|---|---|---|
| generic-FileDirectives | none | in.s | true |
| generic-FileDirectives-no-start-end | none | in.s | false |
| generic-Includes | /some/include/path/openssl/foo.h, bar.h | in.s | true |
| ppc64le-GlobalEntry | none | in.s | true |
| ppc64le-LoadToR0 | none | in.s | true |
| ppc64le-Sample2 | none | in.s | true |
| ppc64le-Sample | none | in.s | true |
| ppc64le-TOCWithOffset | none | in.s | true |
| x86_64-Basic | none | in.s | true |
| x86_64-BSS | none | in.s | true |
| x86_64-GOTRewrite | none | in.s | true |
| x86_64-LargeMemory | none | in.s | true |
| x86_64-LabelRewrite | none | in1.s, in2.s | true |
| x86_64-Sections | none | in.s | true |
| x86_64-ThreeArg | none | in.s | true |
| x86_64-FourArg | none | in.s | true |
| x86_64-Relro | none | in.s | true |
| aarch64-Basic | none | in.s | true |

Run with: `./delocate_test --testdata=<path-to-testdata>`

### Level 3: Side-by-side comparison on real build

Run a full FIPS build twice — once with the Go delocator, once with the C++ delocator — and `diff` the generated `bcm-delocated.S` files. They must be identical.

```bash
# Build with Go delocator
mkdir build-go && cd build-go
cmake -GNinja -DFIPS=1 -DFIPS_DELOCATE=1 ..
ninja bcm-delocated.S
cp crypto/fipsmodule/bcm-delocated.S ../bcm-delocated-go.S

# Build with C++ delocator
cd .. && mkdir build-cc && cd build-cc
cmake -GNinja -DFIPS=1 -DFIPS_DELOCATE=1 -DUSE_CPP_DELOCATE=ON ..
ninja bcm-delocated.S
cp crypto/fipsmodule/bcm-delocated.S ../bcm-delocated-cc.S

# Compare
diff ../bcm-delocated-go.S ../bcm-delocated-cc.S
```

### Level 4: Full FIPS build + test suite

Build the entire project with the C++ delocator and run the full test suite:

```bash
cmake -GNinja -DFIPS=1 -DFIPS_DELOCATE=1 -DUSE_CPP_DELOCATE=ON ..
ninja
ninja run_tests
```

This validates that the delocated assembly actually assembles, links, and passes all crypto tests including the FIPS integrity check.

### Level 5: Cross-architecture validation

If CI supports it, run Level 3 and Level 4 on all three target architectures:
- x86-64 (Linux)
- aarch64 (Linux, e.g. Graviton)
- ppc64le (Linux)

This catches any architecture-specific codegen differences.
