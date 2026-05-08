#include <bits/stdc++.h>
using namespace std;

enum OperandType { REG, IMM, MEM, INVALID };

set<string> REGISTERS = {"AX", "BX", "CX", "DX"};

enum SymbolType { LABEL, VARIABLE };

struct Error {
    int line_no;
    string message;
};

struct Symbol {
    string name;
    int address;
    string segment;
    SymbolType type;
};

struct ICLine {
    int address;
    int line_no;
    string label;
    string opcode;
    string op1, op2;
};

map<string, int> OPTAB = {{"MOV", 1}, {"ADD", 2}, {"SUB", 3}, {"CMP", 4},
                          {"JMP", 5}, {"INC", 6}, {"DEC", 7}, {"HLT", 8}};

vector<string> tokenize(string line) {
    vector<string> tokens;
    string temp;

    for (char c : line) {
        if (c == ',' || isspace(c)) {
            if (!temp.empty()) {
                tokens.push_back(temp);
                temp.clear();
            }
        } else {
            temp += c;
        }
    }

    if (!temp.empty())
        tokens.push_back(temp);

    return tokens;
}

OperandType getOperandType(string op, map<string, Symbol>& SYMTAB) {
    if (op.empty())
        return INVALID;

    // Register
    if (REGISTERS.count(op))
        return REG;

    // Immediate (number)
    if (isdigit(op[0]))
        return IMM;

    // Symbol → memory
    if (SYMTAB.count(op))
        return MEM;

    return INVALID;
}

bool validateOperands(string opcode, OperandType t1, OperandType t2) {

    if (opcode == "MOV") {
        // MOV reg, reg/mem/imm
        if (t1 == REG && (t2 == REG || t2 == MEM || t2 == IMM))
            return true;
    }

    else if (opcode == "ADD" || opcode == "SUB" || opcode == "CMP") {
        // arithmetic: reg, reg/mem/imm
        if (t1 == REG && (t2 == REG || t2 == MEM || t2 == IMM))
            return true;
    }

    else if (opcode == "INC" || opcode == "DEC") {
        // must be exactly one operand
        if (t2 != INVALID) {
            return false;
        }

        if (t1 == REG || t1 == MEM) {
            return true;
        }

        return false;
    }

    else if (opcode == "JMP") {
        // jump: label (memory symbol)
        if (t1 == MEM)
            return true;
    }

    else if (opcode == "HLT") {
        return (t1 == INVALID && t2 == INVALID);
    }

    cout << "reached\n";
    return false;
}

void pass1(vector<string>& program, vector<ICLine>& IC, map<string, Symbol>& SYMTAB,
           vector<Error>& ERRORS) {

    int LC_code = 0, LC_data = 0;
    string current_segment = "";
    bool end_found{false};
    bool in_segment{false};

    for (int i = 0; i < program.size(); i++) {
        int& LC = (current_segment == "DATA") ? LC_data : LC_code;
        string line = program[i];

        if (line.empty())
            continue;

        auto tokens = tokenize(line);
        if (tokens.empty())
            continue;

        int idx = 0;
        string label = "";

        // Check if first token is a label
        if (!tokens.empty() && tokens[0].back() == ':') {
            label = tokens[0].substr(0, tokens[0].size() - 1);

            // Validate label (basic)
            if (!isalpha(label[0])) {
                // i + 1 is the line number
                ERRORS.push_back({i + 1, "Error: Invalid label name " + label});
            }

            // Check duplicate
            if (SYMTAB.count(label)) {
                ERRORS.push_back({i + 1, "Error: Duplicate label " + label});
            } else {
                SYMTAB[label] = {label, LC, current_segment, LABEL};
            }

            idx = 1; // move past label
        }

        if (idx >= tokens.size())
            continue;

        string op = tokens[idx];

        // Segment handling
        if (op == "DATA" && idx + 1 < tokens.size() && tokens[idx + 1] == "SEGMENT") {
            if (in_segment) {
                ERRORS.push_back({i + 1, "ERROR: Nested segment not allowed"});
            }
            current_segment = "DATA";
            in_segment = true;
            continue;
        }

        if (op == "CODE" && idx + 1 < tokens.size() && tokens[idx + 1] == "SEGMENT") {
            if (in_segment) {
                ERRORS.push_back({i + 1, "ERROR: Nested segment not allowed"});
            }
            current_segment = "CODE";
            in_segment = true;
            continue;
        }

        if (op == "ENDS" || op == "END") {
            if (op == "END")
                end_found = true;
            continue;
        }
        if (idx + 1 < tokens.size() && tokens[idx + 1] == "ENDS") {
            string seg = tokens[idx]; // DATA or CODE

            if (!in_segment) {
                ERRORS.push_back({i + 1, "ERROR: ENDS without active segment"});
            } else if (seg != current_segment) {
                ERRORS.push_back({i + 1, "ERROR: Mismatched ENDS for " + seg});
            }

            in_segment = false;
            current_segment = "";
            continue;
        }

        // DATA definition (DB / DW)
        if (idx + 1 < tokens.size()) {
            string directive = tokens[idx + 1];

            if (directive == "DB" || directive == "DW") {
                if (current_segment != "DATA") {
                    ERRORS.push_back({i + 1, "ERROR: Data declaration outside DATA segment"});
                }

                string var = tokens[idx];

                if (SYMTAB.count(var)) {
                    ERRORS.push_back({i + 1, "Error: Duplicate symbol " + var});
                } else {
                    SYMTAB[var] = {var, LC, current_segment, VARIABLE};
                }

                int size_per_unit = (directive == "DB") ? 1 : 2;
                int count = tokens.size() - (idx + 2);
                if (count == 0)
                    count = 1;

                // Create ICLine HERE (not before)
                ICLine ic;
                ic.address = LC;
                ic.opcode = directive;
                ic.op1 = var;
                ic.line_no = i + 1;

                IC.push_back(ic);

                LC += count * size_per_unit;

                continue;
            }
        }

        if (OPTAB.count(op)) {

            if (current_segment != "CODE") {
                ERRORS.push_back({i + 1, "ERROR: Instruction outside CODE segment"});
            }

            ICLine ic;
            ic.address = LC;
            ic.opcode = op;
            ic.line_no = i + 1;

            if (idx + 1 < tokens.size())
                ic.op1 = tokens[idx + 1];
            if (idx + 2 < tokens.size())
                ic.op2 = tokens[idx + 2];

            IC.push_back(ic);

            // LC update
            if (op == "JMP")
                LC += 3;
            else if (op == "HLT")
                LC += 1;
            else
                LC += 2;
        } else {
            ERRORS.push_back({i + 1, "ERROR: Unknown opcode or directive: " + op});
        }
    }
    if (!end_found) {
        ERRORS.push_back({0, "Missing END directive"});
    }
}

void pass2(vector<ICLine>& IC, map<string, Symbol>& SYMTAB, vector<Error>& ERRORS,
           ofstream& outputFile) {

    int i{0};
    for (auto& line : IC) {
        i++;

        // Handle directives FIRST
        if (line.opcode == "DB" || line.opcode == "DW") {
            outputFile << line.address << " " << line.opcode << " " << line.op1 << "\n";
            continue;
        }

        // Check opcode validity BEFORE anything else
        if (!OPTAB.count(line.opcode)) {
            ERRORS.push_back({line.line_no, "ERROR: Invalid opcode\n"});
            continue;
        }

        // Now compute operand types (only for instructions)
        OperandType t1 = getOperandType(line.op1, SYMTAB);
        OperandType t2 = getOperandType(line.op2, SYMTAB);

        // SEGMENT VALIDATION
        auto checkSymbol = [&](string op, OperandType type) {
            if (!SYMTAB.count(op))
                return; // not a symbol → ignore

            Symbol sym = SYMTAB[op];

            if (line.opcode == "JMP") {
                if (sym.type != LABEL) {
                    cout << "ERROR: JMP requires label\n";
                }
            } else {
                if (type == MEM) { // 🔥 ONLY check memory operands
                    if (sym.type != VARIABLE) {
                        cout << "ERROR: Expected data symbol\n";
                    }
                }
            }
        };

        checkSymbol(line.op1, t1);
        checkSymbol(line.op2, t2);

        // Operand validation
        if (!validateOperands(line.opcode, t1, t2)) {
            ERRORS.push_back({line.line_no, "ERROR: Invalid operands for " + line.opcode});
            continue;
        }

        // Output
        outputFile << line.address << " ";
        outputFile << OPTAB[line.opcode] << " ";

        auto resolve = [&](string op) {
            if (op.empty())
                return string("");

            if (SYMTAB.count(op)) {
                return "[" + to_string(SYMTAB[op].address) + "]"; // better semantics
            }

            if (isdigit(op[0]))
                return op;

            return op;
        };

        outputFile << resolve(line.op1) << " " << resolve(line.op2) << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: <program> <input file> <output file>\n";
        return 1;
    }
    ifstream inputFile{argv[1]};
    ofstream outputFile{argv[2]};
    vector<string> program{};
    if (!inputFile.is_open()) {
        cerr << "Failed to open file\n";
        return 1;
    }
    if (!outputFile.is_open()) {
        cerr << "Failed to open output file\n";
        return 1;
    }

    string line;
    while (getline(inputFile, line)) {
        program.push_back(line);
    }
    for (auto& line : program) {
        std::transform(line.begin(), line.end(), line.begin(),
                       [](unsigned char c) { return std::toupper(c); });
    }

    vector<ICLine> IC;
    map<string, Symbol> SYMTAB;
    vector<Error> ERRORS{};

    pass1(program, IC, SYMTAB, ERRORS);

    cout << "\n--- SYMBOL TABLE ---\n";
    for (auto& [k, v] : SYMTAB) {
        cout << k << " -> " << v.address << "\n";
    }

    pass2(IC, SYMTAB, ERRORS, outputFile);
    cout << "\nMachine code written to " << argv[2] << "\n";

    if (!ERRORS.empty()) {
        cout << "\nERRORS:\nOUTPUT COULD BE GARBAGE\n";
        for (auto& error : ERRORS) {
            cout << error.line_no << ' ' << error.message << '\n';
        }
    }

    return 0;
}
