# How the Sift compiler works: a guide for the viva

This guide explains, in plain words, what every part of the project does and why it was built
that way. Read it alongside the source: each file starts with a comment that says what it does,
and the important functions have a comment above them.

---

## 1. The big picture

A compiler is a chain of stages. Each stage turns the program into a form that is easier for the
next stage to work with.

```
source text --lexer--> tokens --parser--> AST --sema--> checked AST --+--> interpreter (run it)
                                                                      |
                                                                      +--> code generator --> LLVM IR
                                                                           --> llc --> object file
                                                                           --> clang --> executable
```

- **Front end** (shared by both phases): `lexer.l`, `parser.y`, `ast.c`, `sema.c`.
- **Phase 1** (`./sift run`): `interp.c` walks the tree and does what it says.
- **Phase 2** (`./sift compile`): `codegen.c` writes LLVM IR; LLVM's tools make machine code.

Both phases use exactly the same checked tree, which is why they give the same results. The test
script checks this on every test.

We follow one tiny program through every stage:

```
process main gives val
begin
  declare x: val = 2 + 3 * 4;
  check x > 10 and x < 20
  begin
    show x;
  end
  give back 0;
end
```

---

## 2. Lexer (`src/lexer.l`, Flex)

The lexer reads characters and groups them into **tokens**: keywords, names, numbers, operators.
It throws away spaces and `--` comments. `./sift tokens` shows the result:

```
1   PROCESS  process
1   IDENT    main
1   GIVES    gives
1   VAL      val
2   BEGIN    begin
3   DECLARE  declare
3   IDENT    x
3   COLON    :
3   VAL      val
3   EQ       =
3   INT      2
3   PLUS     +
...
```

Points to know:

- A Flex file is a list of *pattern { action }* rules. Flex turns it into a C function `yylex()`
  that returns the next token's number each time it is called.
- **Longest match wins**; on a tie, the earlier rule wins. That is why keywords are listed before
  the identifier rule (`process` is a keyword) but `processes` is still an identifier (longer match),
  and why `<=` is one token rather than `<` then `=`.
- Identifiers and numbers also carry a value in `yylval` (the name string, the integer).
- `%option yylineno` makes Flex count lines. `YY_USER_ACTION` copies that line into `yylloc`, so
  the parser knows the line of every token (`@1.first_line` in `parser.y`). Every error message
  uses these line numbers.
- Errors: an unknown character, or a number too big for 32 bits, is reported and the program stops.
- The token numbers come from Bison: `parser.y` declares the tokens, Bison writes them to
  `parser.h`, and the lexer includes that file.

---

## 3. Parser (`src/parser.y`, Bison) and the AST (`src/ast.h`, `src/ast.c`)

The parser checks that the tokens form a correct program according to the grammar, and builds
an **abstract syntax tree (AST)**: a tree of C structs that represents the program's structure
without the punctuation. `./sift ast` prints it:

```
program
  process main gives val  (line 1)
    block
      declare x: val  (line 3)
        binary +
          int 2
          binary *
            int 3
            int 4
      check  (line 4)
        if
          binary and
            binary >
              var x
              int 10
            binary <
              var x
              int 20
          block
            show  (line 6)
              var x
      give back  (line 8)
        int 0
```

Points to know:

- Bison makes an **LALR(1)** parser: it reads tokens left to right, pushing them on a stack
  (*shift*), and when the top of the stack matches the right-hand side of a rule it replaces it
  with the rule's left-hand side (*reduce*) and runs the rule's C action. Our actions call the
  constructors in `ast.c` (`expr_binary`, `stmt_new`, ...) to build the tree.
- **Why build a tree at all in Phase 1?** Bison actions run once, while parsing. A loop body must
  run many times and a process runs every time it is called, so we cannot "execute in the
  actions". We build the tree once and the interpreter walks it as often as needed. This is still
  "Flex + Bison + C" and there is no intermediate code.
- **Precedence.** The expression grammar is written simply (`expr: expr PLUS expr | expr STAR expr
  | ...`), which on its own is ambiguous. The declarations at the top of `parser.y`
  (`%left`, `%nonassoc`, `%precedence`, lowest first) tell Bison how to resolve each choice.
  That is why `2 + 3 * 4` became `+` with `*` underneath. `|>` has the lowest precedence;
  comparisons, `within` and `is` are `%nonassoc`, so `a < b < c` is a syntax error.
  Unary minus uses `%prec UMINUS` so `-2 * 3` is `(-2) * 3`.
- **Zero conflicts.** Bison reports a *shift/reduce* or *reduce/reduce conflict* when the grammar
  leaves a choice open. Ours has none; the Makefile passes `-Werror=conflicts-sr
  -Werror=conflicts-rr` so the build would fail if one appeared.
- **`check ... else check ... otherwise`** is built by the `check_head` rule, which adds one
  condition and one block to the same node for each `else check`.
- **Pipeline desugaring.** `a |> f(x)` is rewritten into the call `f(a, x)` right in the parser
  action (`expr_pipe` in `ast.c`), so later stages only ever see calls. `a |> f |> g(5)` becomes
  `g(f(a), 5)`. Desugaring means: a convenient syntax that is turned into syntax the language
  already had.
- **Syntax errors**: `%define parse.error verbose` makes Bison say what it found and what it
  expected; `yyerror` prints the first error and stops.

---

## 4. Semantic analysis (`src/sema.c`)

A program can be grammatically correct but still meaningless: using a variable that was never
declared, adding a flag to a number, `stop` outside a loop. Sema finds these. It also prepares the
tree for the back ends.

How it works:

- **Two passes.** Pass 1 looks only at process headers (names, parameters, `gives` types): checks
  duplicate names and that `main` is `process main gives val`. Because all signatures are known
  before any body is checked, a process can be called before it is defined. Pass 2 checks each
  process body.
- **Scopes and the symbol table.** Each `begin ... end` block opens a new *scope* (a list of the
  names declared in it). Scopes form a stack: looking up a name searches from the innermost scope
  outward, so an inner `declare x` hides (shadows) an outer `x`. Declaring a name twice in the
  *same* scope is an error. When a block ends, its scope is removed, so its names are no longer
  visible.
- **Types.** `check_expr` works out the type of every expression bottom-up and stores it in the
  node (`e->type`): `+` needs two `val`s and gives a `val`; `<` gives a `flag`; `=` needs two
  values of the same type, and so on.
- **Avoiding error floods.** Sema reports *every* error, not just the first. After an error the
  expression gets type `TY_ERROR`, which is accepted everywhere, so one mistake does not cause a
  chain of follow-up messages.
- **Slots.** Every variable of a process (parameters first, then each `declare` and loop variable)
  gets a number, its *slot*. Sema writes the slot into every node that uses the variable. The
  interpreter keeps an array of values per call and finds a variable by `slots[n]`; the code
  generator makes one stack slot (`alloca`) per number. Names are no longer needed after sema.
- **Other rules checked**: loop variables are read-only; `stop`/`skip` only inside loops
  (`loop_depth`); argument count and types; a process without `gives` cannot be used inside an
  expression; buffer declaration rules (list length = size, and so on); indexing only on buffers;
  `show` of a whole buffer is not allowed.
- **"Every path must give back".** For a `gives` process, the last statement must be `give back`,
  or a `check` with an `otherwise` whose every branch ends that way (checked recursively by
  `ends_with_give`). This is *conservative*: it may reject a program that would actually always
  return (for example one ending with an endless loop), but it never accepts one that can fall off
  the end.

---

