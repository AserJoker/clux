# clux

> **C, but with LUX** — a minimal, statically-typed, C-like language.

clux is a small systems-language project experimenting with a clean, modern take on
C syntax: `var` type inference, `as` casts (no `(T)x`), strict `bool`, no implicit
conversions, and `{}` block statements. The compiler front-end is written in C11
using a hand-rolled "C-style OOP" style (opaque structs + vtable, all memory via a
custom `allocator_t`).

```
func main(): i32 {
    var name = "clux";
    printf("hello, %s\n", name);
    return 0;
}
```

## Features

Implemented (M1 subset, interpretable end-to-end):

- Basic types: `i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool str void`
- `var` declarations with type inference and explicit `: T` annotation
- Numeric / float type suffixes (`7i8`, `2.5f32`), multi-base literals (`0xFF`, `0o17`, `0b1010`)
- Functions (`func name(params): ret { ... }`), no forward declarations required
- Control flow: `if` / `else`, `while`, `for`, `break` / `continue`, `return`
- Operators: arithmetic, comparison, logical (`&&` / `||` / `!`), bitwise, assignment (+ compound)
- Explicit `as` casts; **no implicit cross-type conversions**
- `printf` builtin (hard-bound to C `printf`)
- Duck-typing layout compatibility; TDZ (definite-assignment) analysis
- Compile-time evaluation (CTFE) for constant slots

Designed but not yet implemented: pointers / ownership, struct / enum / arrays /
tuples, modules, error handling. See `docs/milestones.md` and `docs/m2-design.md`.

## Build

Requirements:

- CMake ≥ 3.14
- A C11 / C++20 toolchain (tested with Clang / GCC; MSVC-style drivers also work)
- ICU (Unicode support, pulled in via `third_party/icu`)
- GoogleTest (git submodule at `third_party/gtest`)

```sh
git submodule update --init --recursive   # fetch GoogleTest
cmake -S . -B build
cmake --build build
```

Produces:

- `build/clux` — the compiler / runner driver
- `build/clux_test` — the unit + integration test executable

> **Note:** do **not** run `ninja clean` / `--clean-first`. A generated ICU data
> file lives in the build tree and has no rebuild rule; cleaning it breaks the
> next build. Use a fresh build directory for clean rebuilds instead.

## Usage

```
clux run <file>                       # run a .cx source (or .cxb bytecode by content)
clux format <file.cx> [-o PATH]       # format source in place (or to PATH / stdout)
clux bc <emit|asm|disasm> <file>     # bytecode tools (debug / distribution)
clux test                             # run the test suite
clux version                          # version info
clux build <file.cx>                  # native backend — not implemented yet
```

Examples live in `examples/` (`.cx` sources and `.cxs` bytecode-assembly). Try:

```sh
./build/clux run examples/hello/hello.cx
```

## Project layout

```
src/core/        allocator, arena, vec, rbtree, strmap, omap, stream, string
src/parser/      lexer + recursive-descent / Pratt parser (+ source formatter)
src/vm/          value engine: type/vtable, value, scope, function, bytecode
src/sema/        semantic analysis (shadow-value driven type checking)
src/ctfe/        compile-time evaluation
src/compiler/    AST -> bytecode
src/diag/        shared diagnostic collector
src/driver/      pipeline orchestration (load -> lex -> parse -> sema -> run)
src/cmd/         subcommand dispatch (run / format / bc / build / test / ...)
include/         public headers (mirrors src/)
tests/           C++20 + GoogleTest suites
examples/        sample programs
docs/            design & specification documents
```

## Documentation

- `docs/language-spec-m1.md` — the M1 language reference (lexical, types, grammar, semantics)
- `docs/architecture-m1.md` — compiler pipeline and module responsibilities
- `docs/parse-paradigm.md` — parser design decisions (mismatch/error, fail-fast, Pratt)
- `docs/format-rules.md` — rules enforced by `clux format` (spacing, indentation, comments)
- `docs/bytecode-mnemonics.md` — bytecode instruction set and `.cxs` assembly syntax
- `docs/m2-design.md` — M2 design (struct / enum / arrays / tuples / CTFE / bytecode tools)
- `docs/milestones.md` — milestone roadmap

## Testing

```sh
cd build && ctest --output-on-failure    # or: ./build/clux_test
```

Unit tests cover Lexer / Parser / VM / bytecode; integration tests execute the
`examples/*.cx` programs end-to-end.

## License

See repository license terms.
