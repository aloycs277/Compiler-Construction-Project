# Developer report (complete with your own implementation evidence)

1. Language profile and exclusions: cite the C Minor SRS sections and Appendix A.
2. Lexer: token representation, longest-match policy, line/column tracking, comments, unsupported syntax diagnostics.
3. Parser and AST: recursive-descent grammar mapping, precedence/associativity, source spans, error recovery.
4. Semantic analysis: function signature table, lexical scopes, name resolution, type/lvalue rules, returns and loop control.
5. Student IR: instruction set, temporary/local identity model, labels and control-flow representation.
6. LLVM lowering: mapping each student IR instruction to LLVM textual IR, i32/i1 handling, short circuit and ABI.
7. Backend integration: exact `llc`, assembler and linker versions/commands; explain why GCC receives only generated `.s`.
8. Diagnostics and failure atomicity.
9. Testing: >=50 positive/negative and >=100 malformed/mutated cases; expected vs observed outcomes.
10. AI/tool disclosure: follow the university's academic-integrity policy and be prepared to explain every compiler stage.
