#!/usr/bin/env python3
"""Run C Minor frontend acceptance and malformed-input tests."""

import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parent.parent
TIMEOUT_SECONDS = 5


def invoke(compiler, source_path):
    try:
        result = subprocess.run(
            [str(compiler), "--check", str(source_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(f"compiler timed out on {source_path}") from error
    if result.returncode < 0:
        raise AssertionError(f"compiler terminated abnormally on {source_path}")
    return result


def expect_success(compiler, path):
    result = invoke(compiler, path)
    if result.returncode != 0:
        raise AssertionError(
            f"expected success for {path}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def expect_failure(compiler, path):
    result = invoke(compiler, path)
    if result.returncode == 0:
        raise AssertionError(f"expected a source error for {path}")
    if "error[" not in result.stderr or not re.search(r":\d+:\d+: error\[", result.stderr):
        raise AssertionError(
            f"expected a located diagnostic for {path}\n{result.stderr}"
        )


def write_source(directory, name, source):
    path = directory / name
    path.write_text(source, encoding="ascii")
    return path


def generated_positive_cases(directory):
    cases = []
    for value in range(1, 51):
        name = f"generated_positive_{value}.cm"
        source = (
            f"int main(void) {{ int value_{value} = {value}; "
            f"if (value_{value} + 1 <= {value + 1}) "
            f"{{ value_{value} = value_{value} * 2; }} "
            "return 0; }\n"
        )
        cases.append(write_source(directory, name, source))
    return cases


def boundary_positive_cases(directory):
    cases = []
    locals_source = "int main(void) {\n"
    for index in range(64):
        locals_source += f"int local_{index} = {index};\n"
    locals_source += "return 0;\n}\n"
    cases.append(write_source(directory, "boundary_64_locals.cm", locals_source))

    six_parameters = (
        "int sum6(int a, int b, int c, int d, int e, int f) "
        "{ return a + b + c + d + e + f; }\n"
        "int main(void) { if (sum6(1, 2, 3, 4, 5, 6) != 21) return 1; "
        "return 0; }\n"
    )
    cases.append(write_source(directory, "boundary_6_parameters.cm", six_parameters))

    functions = []
    for index in range(32):
        functions.append(f"int helper_{index}(void) {{ return {index}; }}\n")
    functions.append("int main(void) { return 0; }\n")
    cases.append(write_source(directory, "boundary_32_helpers.cm", "".join(functions)))

    nested_blocks = (
        "int main(void) {\n" + "{\n" * 16 + ";\n" + "}\n" * 16 +
        "return 0;\n}\n"
    )
    cases.append(write_source(directory, "boundary_16_blocks.cm", nested_blocks))

    nested_expression = "(" * 16 + "1" + ")" * 16
    cases.append(
        write_source(
            directory, "boundary_16_expression_levels.cm",
            f"int main(void) {{ return {nested_expression}; }}\n"
        )
    )

    long_comments = ("//" + "x" * 1900 + "\n") * 18
    cases.append(
        write_source(
            directory, "boundary_32k_source.cm",
            long_comments + "int main(void) { return 0; }\n"
        )
    )

    token_lines = (";" * 100 + "\n") * 40
    cases.append(
        write_source(
            directory, "boundary_4000_tokens.cm",
            "int main(void) {\n" + token_lines + "return 0;\n}\n"
        )
    )

    cases.append(
        write_source(
            directory, "boundary_2048_column_line.cm",
            "//" + "x" * 2046 + "\nint main(void) { return 0; }\n"
        )
    )
    return cases


def negative_cases(directory):
    snippets = [
        "int main(void) { return missing; }\n",
        "int main(void) { int x = 1; int x = 2; return 0; }\n",
        "int main(void) { break; return 0; }\n",
        "int main(void) { continue; return 0; }\n",
        "int main(void) { return; }\n",
        "void f(void) { return 1; } int main(void) { return 0; }\n",
        "void main(void) { return; }\n",
        "int main(int x) { return 0; }\n",
        "int main(void) { (1 = 2); return 0; }\n",
        "int main(void) { return absent(); }\n",
        "int f(int x); int main(void) { return f(); }\n",
        "void f(void) { return; } int main(void) { return f(); }\n",
        "int f(void); int main(void) { return 0; }\n",
        "int main(void) { return later(); } int later(void) { return 0; }\n",
        "int main(void) { x = 1; return 0; }\n",
        "int f(int x) { int x = 1; return x; } int main(void) { return 0; }\n",
        "int f(void) { return; } int main(void) { return 0; }\n",
        "int f(); int main(void) { return 0; }\n",
        "int global; int main(void) { return 0; }\n",
        "int main(void) { int x = 1 return 0; }\n",
        "int main(void) { int *x; return 0; }\n",
        "int main(void) { " + "".join(
            f"int local_{index} = 0; " for index in range(65)
        ) + "return 0; }\n",
        "int seven(int a, int b, int c, int d, int e, int f, int g) "
        "{ return a; } int main(void) { return 0; }\n",
        "#include <stdio.h>\nint main(void) { return 0; }\n",
        "int main(void) { return 012; }\n",
        "int main(void) { return 2147483648; }\n",
        "int main(void) { int x = 0; x++; return 0; }\n",
        "int main(void) { int x = \"not an int\"; return 0; }\n",
        "int main(void) { return printf(); }\n",
        "int main(void) { return printf(1); }\n",
        "int main(void) { printf(\"%d\", \"not an int\"); return 0; }\n",
        "int printf(void) { return 0; } int main(void) { return 0; }\n",
        "int main(void) { if (1) return 0; }\n",
        "int main(void) { return 0; } int main(void) { return 0; }\n",
    ]
    return [
        write_source(directory, f"generated_negative_{index}.cm", source)
        for index, source in enumerate(snippets, 1)
    ]


def malformed_cases(directory):
    cases = []
    for index in range(100):
        if index < 20:
            literal = ["0x10", "0b10", "012", "4u", "1.0",
                       "2147483648"][index % 6]
            source = (
                f"int main(void) {{ int broken_{index} = {literal}; return 0; }}\n"
            )
        elif index < 40:
            operator = ["++", "--", "+=", "-=", "*=", "/=", "%=", "<<",
                        ">>", "&=", "|=", "^=", "?", "->", "&", "|", "^",
                        ">>=", "<<=", "++"][index - 20]
            source = (
                f"int main(void) {{ int broken_{index} = 1; "
                f"broken_{index} {operator} 2; return 0; }}\n"
            )
        elif index < 60:
            endings = [")", "}", ";", "=", "(", "&&", "||", "+", "<",
                       "return", "if (", "for (", "while (", "int", "void",
                       "else", "break", "continue", "return 1", "int x ="]
            source = (
                f"int main(void) {{ int value_{index} = {index}; "
                f"return value_{index}; }} {endings[index - 40]}"
            )
        elif index < 80:
            source = (
                f"#define CMINOR_{index} {index}\n"
                "int main(void) { return 0; }\n"
            )
        else:
            source = (
                f"int main(void) {{ int broken_{index} = {index}; "
                f"@ return broken_{index}; }}\n"
            )
        cases.append(write_source(directory, f"mutated_{index:03}.cm", source))
    return cases


def run_execution_cases(compiler, directory):
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        raise AssertionError("execution acceptance tests require Linux x86-64")
    gcc = shutil.which("gcc")
    if gcc is None:
        raise AssertionError("GCC is required to assemble and link generated .s files")

    sources = [
        ROOT / "examples" / "basic.cm",
        ROOT / "tests" / "positive" / "recursion.cm",
        ROOT / "tests" / "positive" / "control_flow.cm",
        ROOT / "tests" / "positive" / "void_functions.cm",
        ROOT / "tests" / "positive" / "printf.cm",
    ]
    for index, source in enumerate(sources):
        assembly = directory / f"execution_{index}.s"
        object_file = directory / f"execution_{index}.o"
        executable = directory / f"execution_{index}"
        commands = [
            [str(compiler), str(source), "-o", str(assembly)],
            [gcc, "-c", str(assembly), "-o", str(object_file)],
            [gcc, "-no-pie", str(object_file), "-o", str(executable)],
        ]
        for command in commands:
            result = subprocess.run(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=TIMEOUT_SECONDS,
            )
            if result.returncode != 0:
                raise AssertionError(
                    f"command failed ({result.returncode}): {command}\n"
                    f"{result.stderr}"
                )
        result = subprocess.run(
            [str(executable)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=TIMEOUT_SECONDS,
        )
        if result.returncode != 0:
            raise AssertionError(
                f"{source.name} exited with {result.returncode}; expected 0"
            )
        if source.name == "printf.cm" and result.stdout != b"Hello World\nvalue=7 char=A\n1234567\n":
            raise AssertionError(
                f"printf produced unexpected output: {result.stdout!r}"
            )


def run_determinism_checks(compiler, directory):
    source = ROOT / "tests" / "positive" / "control_flow.cm"
    stage_directory = directory / "stage-dumps"
    stage_directory.mkdir()
    for mode in (
        "--dump-tokens",
        "--dump-ast",
        "--dump-symbols",
        "--dump-ir",
        "--emit-backend-ir",
    ):
        first = subprocess.run(
            [str(compiler), mode, str(source)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(stage_directory),
            timeout=TIMEOUT_SECONDS,
        )
        second = subprocess.run(
            [str(compiler), mode, str(source)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(stage_directory),
            timeout=TIMEOUT_SECONDS,
        )
        if first.returncode != 0 or second.returncode != 0:
            raise AssertionError(f"{mode} did not complete successfully")
        if first.stdout != second.stdout or first.stderr != second.stderr:
            raise AssertionError(f"{mode} output is not deterministic")
    if list(stage_directory.iterdir()):
        raise AssertionError("stage dump unexpectedly created output files")


def run_lexer_checks(compiler, directory):
    source = "int main(void) {\r\n  return 3;\r\n}\r\n"
    path = directory / "lexer_crlf.cm"
    path.write_bytes(source.encode("ascii"))
    result = subprocess.run(
        [str(compiler), "--dump-tokens", str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=TIMEOUT_SECONDS,
    )
    expected = (
        '1:1 int "int"\n'
        '1:5 identifier "main"\n'
        '1:9 ( "("\n'
        '1:10 void "void"\n'
        '1:14 ) ")"\n'
        '1:16 { "{"\n'
        '2:3 return "return"\n'
        '2:10 integer "3"\n'
        '2:11 ; ";"\n'
        '3:1 } "}"\n'
        '4:1 end of file\n'
    )
    if result.returncode != 0 or result.stdout != expected:
        raise AssertionError(
            "Flex scanner produced unexpected CRLF token locations\n"
            f"expected:\n{expected}\nactual:\n{result.stdout}\n{result.stderr}"
        )

    cases = [
        ("directive", b"#define X 1\nint main(void) { return 0; }\n",
         "UNSUPPORTED_PREPROCESSING_DIRECTIVE", ":1:1:"),
        ("line_splicing", b"int\\\nmain(void) { return 0; }\n",
         "UNSUPPORTED_LINE_SPLICING", ":1:4:"),
        ("unterminated_comment", b"int main(void) { /* unfinished",
         "LEXICAL_ERROR", ":1:18:"),
        ("non_ascii_comment", b"// \xff\nint main(void) { return 0; }\n",
         "LEXICAL_ERROR", ":1:4:"),
        ("nul_byte", b"int\x00main(void) { return 0; }\n",
         "LEXICAL_ERROR", ":1:4:"),
        ("multi_character", b"int main(void) { return 'ab'; }\n",
         "LEXICAL_ERROR", ":1:25:"),
        ("unknown_escape", b'int main(void) { printf("\\q"); return 0; }\n',
         "LEXICAL_ERROR", ":1:25:"),
    ]
    for name, contents, diagnostic_code, location in cases:
        case_path = directory / f"lexer_{name}.cm"
        case_path.write_bytes(contents)
        result = subprocess.run(
            [str(compiler), "--check", str(case_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
        if result.returncode == 0:
            raise AssertionError(f"expected scanner diagnostic for {name}")
        if f"error[{diagnostic_code}]" not in result.stderr or location not in result.stderr:
            raise AssertionError(
                f"unexpected scanner diagnostic for {name}: {result.stderr}"
            )


def run_cli_checks(compiler):
    source = ROOT / "examples" / "basic.cm"
    successful = (
        ["--help"],
        ["--version"],
        ["--capabilities"],
        ["--check", str(source)],
    )
    for arguments in successful:
        result = subprocess.run(
            [str(compiler)] + arguments,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
        if result.returncode != 0:
            raise AssertionError(f"expected CLI success for {arguments}: {result.stderr}")
    if subprocess.run(
        [str(compiler), "--check", "--dump-ir", str(source)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=TIMEOUT_SECONDS,
    ).returncode != 2:
        raise AssertionError("mutually exclusive compiler actions must exit with status 2")
    if subprocess.run(
        [str(compiler), "--backend=qbe", str(source)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=TIMEOUT_SECONDS,
    ).returncode != 2:
        raise AssertionError("an unsupported backend selection must exit with status 2")
    if subprocess.run(
        [str(compiler), "--unknown-option", str(source)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=TIMEOUT_SECONDS,
    ).returncode != 2:
        raise AssertionError("an unknown CLI option must exit with status 2")


def main():
    if len(sys.argv) not in (2, 3) or (
        len(sys.argv) == 3 and sys.argv[2] != "--execution"
    ):
        print("usage: run_tests.py PATH_TO_CMINORCC [--execution]", file=sys.stderr)
        return 2
    compiler = pathlib.Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler executable not found: {compiler}", file=sys.stderr)
        return 2

    positive_files = sorted((ROOT / "tests" / "positive").glob("*.cm"))
    negative_files = sorted((ROOT / "tests" / "negative").glob("*.cm"))
    with tempfile.TemporaryDirectory(prefix="cminor-tests-") as temp:
        directory = pathlib.Path(temp)
        positives = (
            positive_files
            + generated_positive_cases(directory)
            + boundary_positive_cases(directory)
        )
        negatives = negative_files + negative_cases(directory)
        malformed = malformed_cases(directory)

        for path in positives:
            expect_success(compiler, path)
        for path in negatives + malformed:
            expect_failure(compiler, path)
        run_lexer_checks(compiler, directory)
        run_determinism_checks(compiler, directory)
        run_cli_checks(compiler)
        if len(sys.argv) == 3:
            run_execution_cases(compiler, directory)

    print(
        f"PASS: {len(positives)} positive, {len(negatives)} negative, "
        f"{len(malformed)} malformed/mutated cases"
        + (", 4 end-to-end execution cases" if len(sys.argv) == 3 else "")
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
