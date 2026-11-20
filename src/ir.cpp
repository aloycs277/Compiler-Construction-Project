#include "cminor/ir.hpp"

#include <map>
#include <set>
#include <sstream>

namespace cminor {

IRInstruction::IRInstruction()
    : op(IROp::Constant), tokenOp(TokenKind::End), destination(-1), local(-1),
      left(-1), right(-1), condition(-1), trueLabel(-1), falseLabel(-1),
      constant(0), returnsVoid(false), isPrintf(false) {}

namespace {

struct LoopTargets {
    int breakLabel;
    int continueLabel;
};

class FunctionLowerer {
public:
    explicit FunctionLowerer(const Function& function) : nextTemporary_(0),
        nextLabel_(0), terminated_(true) {
        output_.name = function.name;
        output_.returnsVoid = function.returnsVoid;
        output_.parameterCount = static_cast<int>(function.parameters.size());
        output_.temporaryCount = 0;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            addLocal(function.parameters[i].localId, function.parameters[i].name,
                     static_cast<int>(i));
        }
        collectStatementLocals(*function.body);
        emitLabel("entry");
        lowerStatement(*function.body);
        if (!terminated_) {
            IRInstruction instruction;
            instruction.op = IROp::Return;
            instruction.returnsVoid = true;
            output_.instructions.push_back(instruction);
            terminated_ = true;
        }
        output_.temporaryCount = nextTemporary_;
    }

    IRFunction take() { return output_; }

private:
    void addLocal(int id, const std::string& name, int parameterIndex) {
        if (id < 0 || !localIds_.insert(id).second) return;
        IRLocal local;
        local.id = id;
        local.name = name;
        local.parameterIndex = parameterIndex;
        output_.locals.push_back(local);
    }

    void collectStatementLocals(const Statement& statement) {
        if (statement.kind == StmtKind::Declaration) {
            addLocal(statement.localId, statement.name, -1);
        } else if (statement.kind == StmtKind::For && !statement.name.empty()) {
            addLocal(statement.localId, statement.name, -1);
        }
        for (std::size_t i = 0; i < statement.statements.size(); ++i) {
            collectStatementLocals(*statement.statements[i]);
        }
    }

    int newTemporary() { return nextTemporary_++; }
    int newLabel() { return nextLabel_++; }

    void emitLabel(const std::string& label) {
        IRInstruction instruction;
        instruction.op = IROp::Label;
        instruction.label = label;
        output_.instructions.push_back(instruction);
        terminated_ = false;
    }

    void emitLabel(int label) {
        std::ostringstream name;
        name << 'L' << label;
        emitLabel(name.str());
    }

    void emitJump(int label) {
        IRInstruction instruction;
        instruction.op = IROp::Jump;
        instruction.trueLabel = label;
        output_.instructions.push_back(instruction);
        terminated_ = true;
    }

    void ensureOpenBlock() {
        if (!terminated_) return;
        std::ostringstream dead;
        dead << "dead" << newLabel();
        emitLabel(dead.str());
    }

    int lowerExpression(const Expr& expression) {
        switch (expression.kind) {
        case ExprKind::Integer: {
            const int result = newTemporary();
            IRInstruction instruction;
            instruction.op = IROp::Constant;
            instruction.destination = result;
            instruction.constant = expression.integer;
            output_.instructions.push_back(instruction);
            return result;
        }
        case ExprKind::String:
            return -1;
        case ExprKind::Variable: {
            const int result = newTemporary();
            IRInstruction instruction;
            instruction.op = IROp::Load;
            instruction.destination = result;
            instruction.local = expression.localId;
            output_.instructions.push_back(instruction);
            return result;
        }
        case ExprKind::Call: {
            IRInstruction instruction;
            instruction.op = IROp::Call;
            instruction.name = expression.name;
            instruction.returnsVoid = expression.isVoid;
            instruction.isPrintf = expression.name == "printf";
            if (instruction.isPrintf) {
                instruction.format = expression.arguments[0]->value;
            }
            const std::size_t firstArgument = instruction.isPrintf ? 1 : 0;
            for (std::size_t i = firstArgument; i < expression.arguments.size(); ++i) {
                instruction.arguments.push_back(
                    lowerExpression(*expression.arguments[i]));
            }
            if (!instruction.returnsVoid) instruction.destination = newTemporary();
            output_.instructions.push_back(instruction);
            return instruction.destination;
        }
        case ExprKind::Unary: {
            const int operand = lowerExpression(*expression.right);
            if (expression.op == TokenKind::Plus) return operand;
            const int result = newTemporary();
            IRInstruction instruction;
            instruction.op = IROp::Unary;
            instruction.tokenOp = expression.op;
            instruction.destination = result;
            instruction.left = operand;
            output_.instructions.push_back(instruction);
            return result;
        }
        case ExprKind::Assignment: {
            const int value = lowerExpression(*expression.right);
            IRInstruction instruction;
            instruction.op = IROp::Store;
            instruction.local = expression.left->localId;
            instruction.left = value;
            output_.instructions.push_back(instruction);
            return value;
        }
        case ExprKind::Binary: {
            if (expression.op == TokenKind::AndAnd ||
                expression.op == TokenKind::OrOr) {
                return lowerShortCircuit(expression);
            }
            const int left = lowerExpression(*expression.left);
            const int right = lowerExpression(*expression.right);
            const int result = newTemporary();
            IRInstruction instruction;
            instruction.op = IROp::Binary;
            instruction.tokenOp = expression.op;
            instruction.destination = result;
            instruction.left = left;
            instruction.right = right;
            output_.instructions.push_back(instruction);
            return result;
        }
        }
        return -1;
    }

    int lowerShortCircuit(const Expr& expression) {
        const int left = lowerExpression(*expression.left);
        const int result = newTemporary();
        const int rightLabel = newLabel();
        const int shortLabel = newLabel();
        const int endLabel = newLabel();

        IRInstruction branch;
        branch.op = IROp::Branch;
        branch.condition = left;
        branch.trueLabel = expression.op == TokenKind::AndAnd
            ? rightLabel : shortLabel;
        branch.falseLabel = expression.op == TokenKind::AndAnd
            ? shortLabel : rightLabel;
        output_.instructions.push_back(branch);
        terminated_ = true;

        emitLabel(shortLabel);
        IRInstruction shortValue;
        shortValue.op = IROp::Constant;
        shortValue.destination = result;
        shortValue.constant = expression.op == TokenKind::AndAnd ? 0 : 1;
        output_.instructions.push_back(shortValue);
        emitJump(endLabel);

        emitLabel(rightLabel);
        const int right = lowerExpression(*expression.right);
        IRInstruction normalize;
        normalize.op = IROp::Normalize;
        normalize.destination = result;
        normalize.left = right;
        output_.instructions.push_back(normalize);
        emitJump(endLabel);

        emitLabel(endLabel);
        return result;
    }

    void lowerStatement(const Statement& statement) {
        ensureOpenBlock();
        switch (statement.kind) {
        case StmtKind::Block:
            for (std::size_t i = 0; i < statement.statements.size(); ++i) {
                lowerStatement(*statement.statements[i]);
            }
            break;
        case StmtKind::Declaration:
            if (statement.first) {
                const int value = lowerExpression(*statement.first);
                IRInstruction store;
                store.op = IROp::Store;
                store.local = statement.localId;
                store.left = value;
                output_.instructions.push_back(store);
            }
            break;
        case StmtKind::Expression:
            if (statement.first) lowerExpression(*statement.first);
            break;
        case StmtKind::If:
            lowerIf(statement);
            break;
        case StmtKind::While:
            lowerWhile(statement);
            break;
        case StmtKind::For:
            lowerFor(statement);
            break;
        case StmtKind::Break:
            emitJump(loopStack_.back().breakLabel);
            break;
        case StmtKind::Continue:
            emitJump(loopStack_.back().continueLabel);
            break;
        case StmtKind::Return: {
            IRInstruction instruction;
            instruction.op = IROp::Return;
            instruction.returnsVoid = statement.first == NULL;
            if (statement.first) instruction.left = lowerExpression(*statement.first);
            output_.instructions.push_back(instruction);
            terminated_ = true;
            break;
        }
        case StmtKind::Empty:
            break;
        }
    }

    void lowerIf(const Statement& statement) {
        const int condition = lowerExpression(*statement.first);
        const int yesLabel = newLabel();
        const int noLabel = newLabel();
        const int endLabel = newLabel();

        IRInstruction branch;
        branch.op = IROp::Branch;
        branch.condition = condition;
        branch.trueLabel = yesLabel;
        branch.falseLabel = noLabel;
        output_.instructions.push_back(branch);
        terminated_ = true;

        emitLabel(yesLabel);
        lowerStatement(*statement.statements[0]);
        if (!terminated_) emitJump(endLabel);

        emitLabel(noLabel);
        if (statement.statements.size() > 1) {
            lowerStatement(*statement.statements[1]);
        }
        if (!terminated_) emitJump(endLabel);

        emitLabel(endLabel);
    }

    void lowerWhile(const Statement& statement) {
        const int conditionLabel = newLabel();
        const int bodyLabel = newLabel();
        const int endLabel = newLabel();
        emitJump(conditionLabel);
        emitLabel(conditionLabel);

        const int condition = lowerExpression(*statement.first);
        IRInstruction branch;
        branch.op = IROp::Branch;
        branch.condition = condition;
        branch.trueLabel = bodyLabel;
        branch.falseLabel = endLabel;
        output_.instructions.push_back(branch);
        terminated_ = true;

        emitLabel(bodyLabel);
        LoopTargets targets;
        targets.breakLabel = endLabel;
        targets.continueLabel = conditionLabel;
        loopStack_.push_back(targets);
        lowerStatement(*statement.statements[0]);
        loopStack_.pop_back();
        if (!terminated_) emitJump(conditionLabel);

        emitLabel(endLabel);
    }

    void lowerFor(const Statement& statement) {
        if (!statement.name.empty() && statement.first) {
            const int initial = lowerExpression(*statement.first);
            IRInstruction store;
            store.op = IROp::Store;
            store.local = statement.localId;
            store.left = initial;
            output_.instructions.push_back(store);
        } else if (statement.name.empty() && statement.first) {
            lowerExpression(*statement.first);
        }

        const int conditionLabel = newLabel();
        const int bodyLabel = newLabel();
        const int updateLabel = newLabel();
        const int endLabel = newLabel();
        emitJump(conditionLabel);
        emitLabel(conditionLabel);

        int condition;
        if (statement.second) {
            condition = lowerExpression(*statement.second);
        } else {
            condition = newTemporary();
            IRInstruction alwaysTrue;
            alwaysTrue.op = IROp::Constant;
            alwaysTrue.destination = condition;
            alwaysTrue.constant = 1;
            output_.instructions.push_back(alwaysTrue);
        }
        IRInstruction branch;
        branch.op = IROp::Branch;
        branch.condition = condition;
        branch.trueLabel = bodyLabel;
        branch.falseLabel = endLabel;
        output_.instructions.push_back(branch);
        terminated_ = true;

        emitLabel(bodyLabel);
        LoopTargets targets;
        targets.breakLabel = endLabel;
        targets.continueLabel = updateLabel;
        loopStack_.push_back(targets);
        lowerStatement(*statement.statements[0]);
        loopStack_.pop_back();
        if (!terminated_) emitJump(updateLabel);

        emitLabel(updateLabel);
        if (statement.third) lowerExpression(*statement.third);
        emitJump(conditionLabel);
        emitLabel(endLabel);
    }

    IRFunction output_;
    std::set<int> localIds_;
    std::vector<LoopTargets> loopStack_;
    int nextTemporary_;
    int nextLabel_;
    bool terminated_;
};

const char* irOperator(TokenKind op) {
    return tokenName(op);
}

std::string tempName(int id) {
    std::ostringstream out;
    out << 't' << id;
    return out.str();
}

std::string localName(int id) {
    std::ostringstream out;
    out << 'v' << id;
    return out.str();
}

std::string labelName(int id) {
    std::ostringstream out;
    out << 'L' << id;
    return out.str();
}

std::string llvmRegister(int& nextRegister) {
    std::ostringstream out;
    out << "%r" << nextRegister++;
    return out.str();
}

std::string llvmCString(const std::string& value) {
    std::ostringstream out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(value[i]);
        if (byte >= 32 && byte <= 126 && byte != '"' && byte != '\\') {
            out << static_cast<char>(byte);
        } else {
            const char digits[] = "0123456789ABCDEF";
            out << '\\' << digits[byte >> 4] << digits[byte & 15];
        }
    }
    out << "\\00";
    return out.str();
}

std::string displayString(const std::string& value) {
    std::ostringstream out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(value[i]);
        switch (byte) {
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        default:
            if (byte >= 32 && byte <= 126) out << static_cast<char>(byte);
            else {
                const char digits[] = "0123456789ABCDEF";
                out << "\\x" << digits[byte >> 4] << digits[byte & 15];
            }
        }
    }
    return out.str();
}

} // namespace

IRProgram lowerToIR(const Program& program) {
    IRProgram result;
    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        const Function& function = program.functions[i];
        if (!function.definition) continue;
        FunctionLowerer lowerer(function);
        result.functions.push_back(lowerer.take());
    }
    return result;
}

std::string dumpIR(const IRProgram& program) {
    std::ostringstream out;
    for (std::size_t f = 0; f < program.functions.size(); ++f) {
        const IRFunction& function = program.functions[f];
        out << "function " << (function.returnsVoid ? "void " : "int ")
            << function.name << " (";
        for (int p = 0; p < function.parameterCount; ++p) {
            if (p) out << ", ";
            out << "i32";
        }
        out << ")\n";
        for (std::size_t i = 0; i < function.instructions.size(); ++i) {
            const IRInstruction& instruction = function.instructions[i];
            out << "  ";
            switch (instruction.op) {
            case IROp::Label:
                out << instruction.label << ':';
                break;
            case IROp::Constant:
                out << tempName(instruction.destination) << " = const "
                    << instruction.constant;
                break;
            case IROp::Load:
                out << tempName(instruction.destination) << " = load "
                    << localName(instruction.local);
                break;
            case IROp::Store:
                out << "store " << tempName(instruction.left) << " -> "
                    << localName(instruction.local);
                break;
            case IROp::Unary:
                out << tempName(instruction.destination) << " = "
                    << irOperator(instruction.tokenOp) << ' '
                    << tempName(instruction.left);
                break;
            case IROp::Binary:
                out << tempName(instruction.destination) << " = "
                    << tempName(instruction.left) << ' '
                    << irOperator(instruction.tokenOp) << ' '
                    << tempName(instruction.right);
                break;
            case IROp::Normalize:
                out << tempName(instruction.destination) << " = bool "
                    << tempName(instruction.left);
                break;
            case IROp::Call:
                if (!instruction.returnsVoid) {
                    out << tempName(instruction.destination) << " = ";
                }
                out << "call " << instruction.name << '(';
                if (instruction.isPrintf) {
                    out << "format=\"" << displayString(instruction.format) << '"';
                }
                if (instruction.isPrintf && !instruction.arguments.empty()) out << ", ";
                for (std::size_t a = 0; a < instruction.arguments.size(); ++a) {
                    if (a) out << ", ";
                    out << tempName(instruction.arguments[a]);
                }
                out << ')';
                break;
            case IROp::Jump:
                out << "jump " << labelName(instruction.trueLabel);
                break;
            case IROp::Branch:
                out << "branch " << tempName(instruction.condition) << " ? "
                    << labelName(instruction.trueLabel) << " : "
                    << labelName(instruction.falseLabel);
                break;
            case IROp::Return:
                if (instruction.returnsVoid) out << "return void";
                else out << "return " << tempName(instruction.left);
                break;
            }
            out << '\n';
        }
        out << "end function\n";
    }
    return out.str();
}

std::string emitLLVM(const IRProgram& program) {
    std::ostringstream out;
    out << "; Student-generated LLVM IR from C Minor three-address IR\n"
        << "target triple = \"x86_64-pc-linux-gnu\"\n\n";
    std::map<std::string, int> strings;
    for (std::size_t f = 0; f < program.functions.size(); ++f) {
        for (std::size_t i = 0; i < program.functions[f].instructions.size(); ++i) {
            const IRInstruction& instruction = program.functions[f].instructions[i];
            if (instruction.op == IROp::Call && instruction.isPrintf &&
                strings.find(instruction.format) == strings.end()) {
                strings.insert(std::make_pair(instruction.format,
                                               static_cast<int>(strings.size())));
            }
        }
    }
    for (std::map<std::string, int>::const_iterator it = strings.begin();
         it != strings.end(); ++it) {
        const std::string encoded = llvmCString(it->first);
        out << "@.str." << it->second << " = private unnamed_addr constant ["
            << (it->first.size() + 1) << " x i8] c\"" << encoded
            << "\", align 1\n";
    }
    if (!strings.empty()) {
        out << "declare i32 @printf(ptr, ...)\n\n";
    }
    for (std::size_t f = 0; f < program.functions.size(); ++f) {
        const IRFunction& function = program.functions[f];
        out << "define " << (function.returnsVoid ? "void" : "i32") << " @"
            << function.name << '(';
        int argument = 0;
        for (std::size_t l = 0; l < function.locals.size(); ++l) {
            if (function.locals[l].parameterIndex < 0) continue;
            if (argument) out << ", ";
            out << "i32 %arg" << argument++;
        }
        out << ") {\nentry:\n";

        for (std::size_t l = 0; l < function.locals.size(); ++l) {
            out << "  %" << localName(function.locals[l].id)
                << " = alloca i32\n";
        }
        for (int t = 0; t < function.temporaryCount; ++t) {
            out << "  %" << tempName(t) << " = alloca i32\n";
        }
        for (std::size_t l = 0; l < function.locals.size(); ++l) {
            if (function.locals[l].parameterIndex >= 0) {
                out << "  store i32 %arg" << function.locals[l].parameterIndex
                    << ", ptr %" << localName(function.locals[l].id) << '\n';
            }
        }

        int nextRegister = 0;
        for (std::size_t i = 0; i < function.instructions.size(); ++i) {
            const IRInstruction& instruction = function.instructions[i];
            if (instruction.op == IROp::Label) {
                if (instruction.label != "entry") out << instruction.label << ":\n";
                continue;
            }
            switch (instruction.op) {
            case IROp::Label:
                break;
            case IROp::Constant:
                out << "  store i32 " << instruction.constant << ", ptr %"
                    << tempName(instruction.destination) << '\n';
                break;
            case IROp::Load: {
                const std::string value = llvmRegister(nextRegister);
                out << "  " << value << " = load i32, ptr %"
                    << localName(instruction.local) << '\n'
                    << "  store i32 " << value << ", ptr %"
                    << tempName(instruction.destination) << '\n';
                break;
            }
            case IROp::Store: {
                const std::string value = llvmRegister(nextRegister);
                out << "  " << value << " = load i32, ptr %"
                    << tempName(instruction.left) << '\n'
                    << "  store i32 " << value << ", ptr %"
                    << localName(instruction.local) << '\n';
                break;
            }
            case IROp::Unary: {
                const std::string operand = llvmRegister(nextRegister);
                const std::string result = llvmRegister(nextRegister);
                out << "  " << operand << " = load i32, ptr %"
                    << tempName(instruction.left) << '\n';
                if (instruction.tokenOp == TokenKind::Minus) {
                    out << "  " << result << " = sub i32 0, " << operand << '\n';
                } else {
                    const std::string comparison = llvmRegister(nextRegister);
                    out << "  " << comparison << " = icmp eq i32 " << operand
                        << ", 0\n"
                        << "  " << result << " = zext i1 " << comparison
                        << " to i32\n";
                }
                out << "  store i32 " << result << ", ptr %"
                    << tempName(instruction.destination) << '\n';
                break;
            }
            case IROp::Binary: {
                const std::string left = llvmRegister(nextRegister);
                const std::string right = llvmRegister(nextRegister);
                const std::string result = llvmRegister(nextRegister);
                out << "  " << left << " = load i32, ptr %"
                    << tempName(instruction.left) << '\n'
                    << "  " << right << " = load i32, ptr %"
                    << tempName(instruction.right) << '\n';
                const char* operation = NULL;
                bool comparison = false;
                switch (instruction.tokenOp) {
                case TokenKind::Plus: operation = "add"; break;
                case TokenKind::Minus: operation = "sub"; break;
                case TokenKind::Star: operation = "mul"; break;
                case TokenKind::Slash: operation = "sdiv"; break;
                case TokenKind::Percent: operation = "srem"; break;
                case TokenKind::Less: operation = "icmp slt"; comparison = true; break;
                case TokenKind::LessEqual: operation = "icmp sle"; comparison = true; break;
                case TokenKind::Greater: operation = "icmp sgt"; comparison = true; break;
                case TokenKind::GreaterEqual: operation = "icmp sge"; comparison = true; break;
                case TokenKind::EqualEqual: operation = "icmp eq"; comparison = true; break;
                case TokenKind::NotEqual: operation = "icmp ne"; comparison = true; break;
                default: operation = "add"; break;
                }
                out << "  " << result << " = " << operation << " i32 "
                    << left << ", " << right << '\n';
                if (comparison) {
                    const std::string normalized = llvmRegister(nextRegister);
                    out << "  " << normalized << " = zext i1 " << result
                        << " to i32\n"
                        << "  store i32 " << normalized << ", ptr %"
                        << tempName(instruction.destination) << '\n';
                } else {
                    out << "  store i32 " << result << ", ptr %"
                        << tempName(instruction.destination) << '\n';
                }
                break;
            }
            case IROp::Normalize: {
                const std::string value = llvmRegister(nextRegister);
                const std::string comparison = llvmRegister(nextRegister);
                const std::string normalized = llvmRegister(nextRegister);
                out << "  " << value << " = load i32, ptr %"
                    << tempName(instruction.left) << '\n'
                    << "  " << comparison << " = icmp ne i32 " << value << ", 0\n"
                    << "  " << normalized << " = zext i1 " << comparison
                    << " to i32\n"
                    << "  store i32 " << normalized << ", ptr %"
                    << tempName(instruction.destination) << '\n';
                break;
            }
            case IROp::Call: {
                if (instruction.isPrintf) {
                    std::vector<std::string> arguments;
                    for (std::size_t a = 0; a < instruction.arguments.size(); ++a) {
                        const std::string value = llvmRegister(nextRegister);
                        out << "  " << value << " = load i32, ptr %"
                            << tempName(instruction.arguments[a]) << '\n';
                        arguments.push_back(value);
                    }
                    const std::string result = llvmRegister(nextRegister);
                    const int stringId = strings.find(instruction.format)->second;
                    out << "  " << result << " = call i32 (ptr, ...) @printf(ptr "
                        << "getelementptr inbounds ([" << (instruction.format.size() + 1)
                        << " x i8], ptr @.str." << stringId
                        << ", i64 0, i64 0)";
                    for (std::size_t a = 0; a < arguments.size(); ++a) {
                        out << ", i32 " << arguments[a];
                    }
                    out << ")\n"
                        << "  store i32 " << result << ", ptr %"
                        << tempName(instruction.destination) << '\n';
                    break;
                }
                std::vector<std::string> arguments;
                for (std::size_t a = 0; a < instruction.arguments.size(); ++a) {
                    const std::string value = llvmRegister(nextRegister);
                    out << "  " << value << " = load i32, ptr %"
                        << tempName(instruction.arguments[a]) << '\n';
                    arguments.push_back(value);
                }
                std::string result;
                if (!instruction.returnsVoid) result = llvmRegister(nextRegister);
                out << "  ";
                if (!instruction.returnsVoid) out << result << " = ";
                out << "call " << (instruction.returnsVoid ? "void" : "i32")
                    << " @" << instruction.name << '(';
                for (std::size_t a = 0; a < arguments.size(); ++a) {
                    if (a) out << ", ";
                    out << "i32 " << arguments[a];
                }
                out << ")\n";
                if (!instruction.returnsVoid) {
                    out << "  store i32 " << result << ", ptr %"
                        << tempName(instruction.destination) << '\n';
                }
                break;
            }
            case IROp::Jump:
                out << "  br label %" << labelName(instruction.trueLabel) << '\n';
                break;
            case IROp::Branch: {
                const std::string value = llvmRegister(nextRegister);
                const std::string condition = llvmRegister(nextRegister);
                out << "  " << value << " = load i32, ptr %"
                    << tempName(instruction.condition) << '\n'
                    << "  " << condition << " = icmp ne i32 " << value << ", 0\n"
                    << "  br i1 " << condition << ", label %"
                    << labelName(instruction.trueLabel) << ", label %"
                    << labelName(instruction.falseLabel) << '\n';
                break;
            }
            case IROp::Return:
                if (instruction.returnsVoid) {
                    out << "  ret void\n";
                } else {
                    const std::string value = llvmRegister(nextRegister);
                    out << "  " << value << " = load i32, ptr %"
                        << tempName(instruction.left) << '\n'
                        << "  ret i32 " << value << '\n';
                }
                break;
            }
        }
        out << "}\n\n";
    }
    return out.str();
}

} // namespace cminor
