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

## 5. Phase 1: the interpreter (`src/interp.c`)

The interpreter walks the checked tree and does what each node says.

- `eval(frame, expr)` returns the value of an expression. For `binary +` it evaluates the left
  side, then the right side, then adds. `exec_stmt(frame, stmt)` runs a statement.
- **Values.** A `Value` holds a number (for `val` and `flag`, where flag is 0 or 1) and a `Buffer`
  (a pointer to the elements plus the length).
- **Frames.** Each *call* of a process gets a new `Frame` with its own array of slots. That is what
  makes recursion work: `factorial(5)` and the `factorial(4)` it calls each have their own `n`.
- **Control flow with `Flow`.** Every statement returns a `Flow` value: `NORMAL`, `STOP`, `SKIP` or
  `RETURN`. A block stops running its statements as soon as one returns something other than
  `NORMAL` and passes it up. A loop that receives `STOP` ends, `SKIP` goes to the next iteration,
  and `RETURN` is passed further up until the process call receives it. This is how `stop`,
  `skip` and `give back` jump out of nested statements without any `goto`.
- **Short-circuit.** For `a and b`, if `a` is `no` the answer is `no` and `b` is not evaluated.
  This is what makes `j >= 0 and data[j] > current` in the insertion sort safe.
- **Arithmetic.** In C, signed overflow is undefined behaviour, so `+ - *` are done on unsigned
  32-bit numbers and converted back; this gives the same wrap-around as LLVM's `add`. Division by
  zero is a runtime error; `INT_MIN / -1` (which would crash a C program) is defined to give
  `INT_MIN`.
- **Runtime errors** (bad index, division by zero) print `runtime error: line N: ...` and exit
  with code 1. Output printed before the error is flushed first.
- **Deep recursion.** Each Sift call uses about 1 KB of C stack in the interpreter (eval calls
  call_process calls exec_block ...). The default 8 MB stack ran out around depth 7000, so
  `interp_run` runs the program on a thread with a 1 GiB stack.

---

## 6. Phase 2: generating LLVM IR (`src/codegen.c`)

**LLVM IR** is a low-level, typed, assembly-like language. LLVM can optimise it (`opt`) and turn
it into machine code for many CPUs (`llc`). We print it as text with `fprintf`. Here is our tiny
program's IR:

```
define i32 @sift_main() {
entry:
  %v0.x = alloca i32
  %t1 = mul i32 3, 4
  %t2 = add i32 2, %t1
  store i32 %t2, ptr %v0.x
  %t3 = load i32, ptr %v0.x
  %t4 = icmp sgt i32 %t3, 10
  br i1 %t4, label %L4, label %L5
L4:
  %t5 = load i32, ptr %v0.x
  %t6 = icmp slt i32 %t5, 20
  br label %L5
L5:
  %t7 = phi i1 [ false, %entry ], [ %t6, %L4 ]
  br i1 %t7, label %L2, label %L3
L2:
  %t8 = load i32, ptr %v0.x
  call void @sift_show_val(i32 %t8)
  br label %L1
L3:
  br label %L1
L1:
  ret i32 0
L6:
  call void @sift_missing_give_back(ptr @name.main, i32 9)
  unreachable
}
```

Ideas needed to read it:

- **SSA.** Every `%tN` is assigned exactly once (Static Single Assignment). `new_tmp()` hands out
  fresh names. `gen_expr` returns the name (or constant) that holds an expression's value.
- **Types.** `val` is `i32`, `flag` is `i1`, a buffer is `%buffer = type { ptr, i32 }` (pointer to
  the elements, and the length).
- **Variables** live in memory: `alloca` reserves a stack slot (one per sema slot, named like
  `%v0.x`), `store` writes it, `load` reads it. All allocas are placed at the start of the entry
  block; to do that, `codegen.c` writes each function's body into a memory buffer and the allocas
  into a second buffer, then prints the allocas first.
- **Basic blocks.** Code is split into blocks that start with a label (`L4:`) and end with exactly
  one *terminator*: `br` (jump, possibly conditional), `ret`, or `unreachable`. If code follows a
  terminator (for example statements after `give back`), `emit()` automatically starts a new,
  unreachable block so the IR stays valid.
- **`check`**: evaluate the condition, `br i1 cond, label %then, label %next`, each branch ends
  with `br` to a common end block.
- **Loops**: a *head* block tests the condition, the *body* runs the statements, a *latch* block
  (for counted loops) adds 1 to the counter, and an *exit* block follows. `stop` is `br exit`,
  `skip` is `br latch` (or `br head` for `repeat while`). A small stack (`break_label`,
  `continue_label`) remembers the labels of the enclosing loops.
- **Short-circuit `and`/`or` with `phi`.** Above, `x > 10 and x < 20`: if `x > 10` is false we
  jump straight to `L5` without computing `x < 20`. In `L5`, `phi` chooses a value according to
  which block we came from: `false` if from `entry`, `%t6` if from `L4`. `cur_block` remembers
  the current block's label because the right-hand side may itself create blocks.
- **Processes** become functions: `main` is `@sift_main`, others are `@sift.NAME` (so a process
  called `printf` cannot clash with C's `printf`). Parameters are copied into their allocas at the
  start, then used like any variable. `give back` is `ret`.
- **Buffers**: a new buffer calls `sift_new_buffer(n)` (calloc in the runtime) and builds the
  struct with `insertvalue`; `count` is `extractvalue %buffer %b, 1`; an element is reached with
  `getelementptr i32, ptr %data, i32 %index` and then `load`/`store`. Passing the struct by value
  copies only pointer + length, so buffers are shared (reference semantics), same as the
  interpreter.
- **Runtime checks**: before an index, `icmp ult index, length` (unsigned, so negative numbers
  fail too) branches to an error block that calls `sift_index_error` (marked `noreturn`), else to
  the block that does the access. Division does the same with `icmp eq divisor, 0`.
  *A bug we hit:* at first we called a check function and then did `sdiv` in the same block. `llc`
  moved the `idiv` instruction above the call (inside one block it may reorder instructions that
  have no dependency), so dividing by zero crashed with "Floating point exception" before our
  message could print. Putting the division in its own block, reachable only when the divisor is
  not zero, fixed it.
- **`INT_MIN / -1`** overflows and traps on x86, so the IR divides by 1 instead of -1 (`select`)
  and negates the result, matching the interpreter.
- **`void`** is the constant -2147483648; `is void` is `icmp eq`; `within` is two `icmp sle` and
  an `and`.

What `opt -O2` (`./sift compile -O`) makes of the same function: it computes everything at
compile time.

```
define noundef i32 @sift_main() local_unnamed_addr {
entry:
  tail call void @sift_show_val(i32 14)
  ret i32 0
}
```

### From IR to an executable (`src/main.c`, `src/runtime.c`)

`./sift compile` writes the IR into a temporary folder, then runs (with `fork` + `execvp`):

1. `opt -O2` (only with `-O`): LLVM's optimiser, the "middle end".
2. `llc -filetype=obj -relocation-model=pic`: the "back end", IR to an object file of machine code.
   `pic` (position-independent code) is needed because Ubuntu's clang builds position-independent
   executables.
3. `clang program.o build/sift_runtime.o -o prog`: links our code with the runtime and the C library.

`runtime.c` provides what the IR calls: `sift_show_val` (prints `void` for INT_MIN),
`sift_show_flag`, the error functions, `sift_new_buffer`, and the C `main`, which calls
`sift_main` and returns its value as the exit code.

---

## 7. Testing (`run_tests.sh`)

- `tests/NN_name.sift`: a program and its exact expected output (`.expected`), and optionally an
  expected exit code (`.exitcode`).
- `tests/err_NN_name.sift`: a program that must fail, with a piece of the expected error message.
- `tests/tok_*` / `tests/ast_*`: the output of `./sift tokens` / `./sift ast`.
- Program and error tests are run in **both modes**: `./sift run`, and `./sift compile` + running
  the executable. Both must give identical output and exit code. `./run_tests.sh run compile
  optimized` adds a third run with `opt -O2`.
- `make test` builds and runs everything; the script exits non-zero if anything fails.

---

## 8. Likely viva questions

**Why is there an AST in Phase 1 if no intermediate code is allowed?**
An AST is the parser's output, not intermediate code; the interpreter executes it directly.
Without it, loops and calls would be impossible because Bison actions run only once.

**What is a shift/reduce conflict, and how did you avoid them?**
A state where the parser could either shift the next token or reduce by a rule. For expressions
we used precedence and associativity declarations; the `check`/`else check` chain is written so
that `else` can only ever be followed by `check`, so there is no "dangling else".

**Where does the pipeline operator go after parsing?**
Nowhere: the parser action rewrites `a |> f(x)` into the call `f(a, x)`.

**How do scopes work?** A stack of scopes; lookup goes from innermost to outermost; redeclaring in
the same scope is an error; shadowing an outer name is allowed.

**How does `give back` get out of three nested loops in the interpreter?** Each statement returns
a `Flow` value; `RETURN` is passed up through every loop and block until the process call sees it.
In the compiler it is simply `ret`.

**Why `alloca` instead of building SSA directly?** It is much simpler: every variable is a memory
slot. LLVM's `mem2reg` pass (part of `opt -O2`) converts them into SSA registers automatically.

**What is `phi`?** An SSA instruction at the start of a block that picks a value depending on
which block jumped there. We use it for short-circuit `and`/`or`.

**How are buffers passed?** As a `{pointer, length}` pair, by value, so the elements are shared:
changes made by a process are seen by the caller.

**What is the trade-off of `void`?** It is stored as INT_MIN, so INT_MIN itself cannot be real
data; but no extra memory or flags are needed and `is void` is one comparison.

**What happens with `x / 0` in each mode?** Interpreter: checked in `do_arith`, runtime error.
Compiled: compare with 0 and branch to a block calling `sift_div_by_zero`. Same message, exit 1.

**Why does `main`'s result 300 give exit code 44?** Linux keeps only the lowest 8 bits of the exit
status (300 mod 256 = 44). Both modes behave the same.

**What does `llc` do vs `clang` vs `opt`?** `opt` optimises IR to IR; `llc` turns IR into machine
code (an object file); `clang` here only links object files into an executable.

---

## 9. Where to find things quickly

| Question | Look at |
|---|---|
| Which words are keywords? | `src/lexer.l` |
| Operator precedence | top of `src/parser.y` |
| How a pipeline is rewritten | `expr_pipe` in `src/ast.c` |
| Type rules of operators | `check_binary`, `check_expr` in `src/sema.c` |
| The four ways to declare a buffer | `check_buffer_declare` in `src/sema.c` |
| "Must end with give back" | `ends_with_give` in `src/sema.c` |
| How loops run in the interpreter | `exec_stmt`, `run_body` in `src/interp.c` |
| How a loop becomes IR | `gen_while`, `gen_repeat`, `gen_sweep` in `src/codegen.c` |
| Short-circuit in IR | `gen_logical` in `src/codegen.c` |
| Bounds checks in IR | `element_address` in `src/codegen.c` |
| How `compile` calls the LLVM tools | `cmd_compile` in `src/main.c` |
