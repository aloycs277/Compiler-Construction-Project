#ifndef CMINOR_IR_HPP
#define CMINOR_IR_HPP

#include "cminor/frontend.hpp"

#include <string>
#include <vector>

namespace cminor {

enum class IROp {
    Label, Constant, Load, Store, Unary, Binary, Normalize,
    Call, Jump, Branch, Return
};

struct IRInstruction {
    IROp op;
    std::string label;
    std::string name;
    TokenKind tokenOp;
    int destination;
    int local;
    int left;
    int right;
    int condition;
    int trueLabel;
    int falseLabel;
    std::int32_t constant;
    bool returnsVoid;
    bool isPrintf;
    std::string format;
    std::vector<int> arguments;

    IRInstruction();
};

struct IRLocal {
    int id;
    std::string name;
    int parameterIndex;
};

struct IRFunction {
    std::string name;
    bool returnsVoid;
    int parameterCount;
    int temporaryCount;
    std::vector<IRLocal> locals;
    std::vector<IRInstruction> instructions;
};

struct IRProgram {
    std::vector<IRFunction> functions;
};

IRProgram lowerToIR(const Program& program);
std::string dumpIR(const IRProgram& program);
std::string emitLLVM(const IRProgram& program);

} // namespace cminor

#endif
