// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0 OR ISC

#include "transform.h"
#include "ar.h"
#include "fips_const.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

static std::string trimStr(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

static bool startsWith(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static bool endsWith(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool contains(const std::string &s, const std::string &sub) {
  return s.find(sub) != std::string::npos;
}

static std::string join(const std::vector<std::string> &v,
                        const std::string &sep) {
  std::string result;
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) result += sep;
    result += v[i];
  }
  return result;
}

static std::vector<std::string> splitFields(const std::string &s) {
  std::vector<std::string> result;
  std::istringstream iss(s);
  std::string token;
  while (iss >> token) result.push_back(token);
  return result;
}

// CpuCapUniqueSymbol
CpuCapUniqueSymbol newCpuCapUniqueSymbol(int uniqueness,
                                          const std::string &registerName) {
  std::string rn = registerName;
  // Remove leading/trailing %
  while (!rn.empty() && rn.front() == '%') rn.erase(rn.begin());
  while (!rn.empty() && rn.back() == '%') rn.pop_back();
  return {rn, std::to_string(uniqueness)};
}

// WrapperStack
void WrapperStack::doIt(std::function<void()> baseCase) {
  if (stack.empty()) {
    baseCase();
    return;
  }
  WrapperFunc wrapper = stack.front();
  stack.erase(stack.begin());
  wrapper([this, baseCase]() { this->doIt(baseCase); });
}

// Delocation
Delocation::Delocation()
    : processor_(ProcessorType::X86_64),
      output_(nullptr),
      gotDeltaNeeded_(false) {}

void Delocation::writeStr(const std::string &s) { *output_ += s; }

std::string Delocation::contents(const Node *node) const {
  return currentInput_.contents.substr(node->begin, node->end - node->begin);
}

void Delocation::writeNode(const Node *node) {
  writeStr(contents(node));
}

void Delocation::writeCommentedNode(const Node *node) {
  std::string line = contents(node);
  writeStr(commentIndicator_ + " WAS " + trimStr(line) + "\n");
}

Node *Delocation::skipWS(Node *node) {
  return skipNodes(node, PegRule::WS);
}

Node *Delocation::skipNodes(Node *node, PegRule ruleToSkip) {
  while (node != nullptr && node->rule == ruleToSkip) {
    node = node->next;
  }
  return node;
}

void Delocation::assertNodeType(const Node *node, PegRule expected) {
  if (node->rule != expected) {
    throw std::runtime_error(
        std::string("node was ") + pegRuleName(node->rule) + ", but wanted " +
        pegRuleName(expected));
  }
}

std::vector<Node *> Delocation::instructionArgs(Node *node) {
  std::vector<Node *> argNodes;
  for (node = skipWS(node); node != nullptr; node = skipWS(node->next)) {
    assertNodeType(node, PegRule::InstructionArg);
    argNodes.push_back(node->up);
  }
  return argNodes;
}

void Delocation::forEachPath(Node *node, std::function<void(Node *)> cb,
                              std::vector<PegRule> rules) {
  if (node == nullptr) return;
  if (rules.empty()) {
    cb(node);
    return;
  }
  PegRule rule = rules[0];
  std::vector<PegRule> childRules(rules.begin() + 1, rules.end());

  for (; node != nullptr; node = node->next) {
    if (node->rule != rule) continue;
    if (childRules.empty()) {
      cb(node);
    } else {
      forEachPath(node->up, cb, childRules);
    }
  }
}

bool Delocation::matchPatternSearchSubtree(
    Node *node, std::function<bool(Node *)> matchNode,
    const std::vector<PegRule> &rules) {
  if (node == nullptr) return false;
  PegRule rule = rules[0];
  std::vector<PegRule> childRules(rules.begin() + 1, rules.end());

  for (; node != nullptr; node = node->next) {
    if (rule != node->rule) continue;
    if (childRules.empty()) {
      return matchNode(node);
    }
    if (matchPatternSearchSubtree(node->up, matchNode, childRules))
      return true;
  }
  return false;
}

bool Delocation::matchPatternOneLine(
    Node *lineRootNode, std::function<bool(Node *)> matchNode,
    const std::vector<PegRule> &rules) {
  if (lineRootNode == nullptr || rules.empty()) return false;
  PegRule rule = rules[0];
  std::vector<PegRule> childRules(rules.begin() + 1, rules.end());
  if (rule != lineRootNode->rule) return false;
  if (childRules.empty()) return matchNode(lineRootNode);
  return matchPatternSearchSubtree(lineRootNode->up, matchNode, childRules);
}

std::string Delocation::mapLocalSymbol(const std::string &symbol) const {
  if (currentInput_.index == 0) return symbol;
  return symbol + "_BCM_" + std::to_string(currentInput_.index);
}

std::string Delocation::localTargetName(const std::string &name) {
  return ".L" + name + "_local_target";
}

std::string Delocation::localEntryName(const std::string &name) {
  return ".L" + name + "_local_entry";
}

std::string Delocation::redirectorName(const std::string &symbol) {
  return ".Lbcm_redirector_" + symbol;
}

std::string Delocation::accessorName(const std::string &name) {
  return name + "_bss_get";
}

bool Delocation::isSynthesized(const std::string &symbol,
                                ProcessorType proc) {
  bool synth = endsWith(symbol, "_bss_get") ||
               symbol == "OPENSSL_ia32cap_get";
  if (proc != ProcessorType::AARCH64) {
    synth = synth || startsWith(symbol, "BORINGSSL_bcm_text_");
  }
  return synth;
}

bool Delocation::isFipsScopeMarkers(const std::string &symbol) {
  return symbol == "BORINGSSL_bcm_text_start" ||
         symbol == "BORINGSSL_bcm_text_end" ||
         symbol == "BORINGSSL_bcm_text_hash";
}

std::pair<std::string, bool> Delocation::sectionType(
    const std::string &section) {
  if (section.empty() || section[0] != '.') return {"", false};
  size_t i = section.find('.', 1);
  std::string result = section;
  if (i != std::string::npos) {
    result = section.substr(0, i);
  }
  if (startsWith(result, ".debug_")) return {".debug", true};
  return {result, true};
}

ProcessorType Delocation::detectProcessor(const InputFile &input) {
  for (Node *statement = input.ast->up; statement != nullptr;
       statement = statement->next) {
    Node *node = skipNodes(statement->up, PegRule::WS);
    if (node == nullptr || node->rule != PegRule::Instruction) continue;

    Node *instruction = node->up;
    std::string instrName =
        input.contents.substr(instruction->begin,
                              instruction->end - instruction->begin);

    if (instrName == "movq" || instrName == "call" || instrName == "leaq")
      return ProcessorType::X86_64;
    if (instrName == "addis" || instrName == "addi" || instrName == "mflr")
      return ProcessorType::PPC64LE;
    if (instrName == "str" || instrName == "bl" || instrName == "ldr" ||
        instrName == "st1")
      return ProcessorType::AARCH64;
  }
  throw std::runtime_error(
      "processed entire input and didn't recognise any instructions.");
}

bool Delocation::isNewLine(const std::string &file, const Node *node) {
  return file.substr(node->begin, node->end - node->begin) == "\n";
}

bool Delocation::isEndOfRelroSection(const std::string &file,
                                      const Node *lineRootNode) {
  if (isNewLine(file, lineRootNode)) return false;

  const Node *nodeNext = lineRootNode->up;

  // .align directive
  if (matchPatternSearchSubtree(
          const_cast<Node *>(nodeNext),
          [&file](Node *node) {
            return file.substr(node->begin, node->end - node->begin) == "align";
          },
          {PegRule::Directive, PegRule::DirectiveName}))
    return false;

  // Label with LocalSymbol
  if (matchPatternSearchSubtree(
          const_cast<Node *>(nodeNext),
          [&file](Node *node) {
            std::string sym =
                file.substr(node->begin, node->end - node->begin);
            return startsWith(sym, ".L");
          },
          {PegRule::Label, PegRule::LocalSymbol}))
    return false;

  // LabelContainingDirective with .quad
  if (matchPatternSearchSubtree(
          const_cast<Node *>(nodeNext),
          [&file](Node *node) {
            return file.substr(node->begin, node->end - node->begin) == ".quad";
          },
          {PegRule::LabelContainingDirective,
           PegRule::LabelContainingDirectiveName}))
    return false;

  return true;
}

bool Delocation::isProbablyAValidSymbol(const std::string &symbol) {
  if (symbol.empty()) return false;
  if (symbol.size() > 255) return false;
  if (!std::isalpha((unsigned char)symbol[0]) && symbol[0] != '_')
    return false;
  for (size_t i = 0; i < symbol.size(); i++) {
    char c = symbol[i];
    if (!std::isalpha((unsigned char)c) && !std::isdigit((unsigned char)c) &&
        c != '_' && c != '.')
      return false;
  }
  if (startsWith(symbol, ".")) return false;
  if (contains(symbol, "@@")) return false;
  return true;
}

bool Delocation::findLocalLabelsForRelro(
    const std::string &file, Node *node,
    std::map<std::string, std::string> &relroMap) {
  Node *currentLineRootNode = node;
  for (; currentLineRootNode != nullptr;
       currentLineRootNode = currentLineRootNode->next) {
    std::string localSymbolName;
    if (matchPatternSearchSubtree(
            currentLineRootNode->up,
            [&file, &relroMap, &localSymbolName](Node *node) {
              std::string sym =
                  file.substr(node->begin, node->end - node->begin);
              if (relroMap.count(sym))
                throw std::runtime_error("Duplicate symbol found: " + sym);
              if (!startsWith(sym, ".L"))
                throw std::runtime_error(
                    "Symbol name syntax is not what was expected: " + sym);
              localSymbolName = sym;
              return true;
            },
            {PegRule::Label, PegRule::LocalSymbol})) {
      currentLineRootNode = currentLineRootNode->next;
      if (isNewLine(file, currentLineRootNode)) {
        currentLineRootNode = currentLineRootNode->next;
      }

      if (!matchPatternSearchSubtree(
              currentLineRootNode->up,
              [&file, &relroMap, &localSymbolName](Node *node) {
                std::string funcSymName =
                    file.substr(node->begin, node->end - node->begin);
                if (!isProbablyAValidSymbol(funcSymName))
                  throw std::runtime_error("Invalid symbol: " + funcSymName);
                relroMap[localSymbolName] = funcSymName;
                return true;
              },
              {PegRule::LabelContainingDirective, PegRule::SymbolArgs})) {
        return false;
      }
      continue;
    }

    if (isEndOfRelroSection(file, currentLineRootNode)) break;
  }
  return true;
}

bool Delocation::relroLocalLabelToFuncMapping(
    const InputFile &input,
    std::map<std::string, std::string> &relroMap) {
  auto matchRelRoCb = [&input](Node *node) {
    std::string st =
        input.contents.substr(node->begin, node->end - node->begin);
    return startsWith(st, ".data.rel.ro") || startsWith(st, ".ldata.rel.ro");
  };

  Node *currentLineRootNode = input.ast->up;
  for (; currentLineRootNode != nullptr;
       currentLineRootNode = currentLineRootNode->next) {
    if (matchPatternOneLine(
            currentLineRootNode, matchRelRoCb,
            {PegRule::Statement, PegRule::Directive, PegRule::Args,
             PegRule::Arg})) {
      if (!findLocalLabelsForRelro(input.contents,
                                   currentLineRootNode->next, relroMap))
        return false;
      continue;
    }

    // Handle .set aliases
    if (matchPatternSearchSubtree(
            currentLineRootNode->up,
            [&input](Node *node) {
              return input.contents.substr(node->begin,
                                           node->end - node->begin) == ".set";
            },
            {PegRule::LabelContainingDirective,
             PegRule::LabelContainingDirectiveName})) {
      matchPatternSearchSubtree(
          currentLineRootNode->up,
          [&input, &relroMap](Node *node) {
            std::string argsStr =
                input.contents.substr(node->begin, node->end - node->begin);
            size_t comma = argsStr.find(',');
            if (comma == std::string::npos) return true;
            std::string left = argsStr.substr(0, comma);
            std::string right = argsStr.substr(comma + 1);
            // Trim whitespace
            while (!right.empty() && right[0] == ' ') right.erase(0, 1);
            if (relroMap.count(right)) {
              relroMap[left] = relroMap[right];
            }
            return true;
          },
          {PegRule::LabelContainingDirective, PegRule::SymbolArgs});
      continue;
    }
  }
  return true;
}

void Delocation::skippedLine(const Node *node) {
  if (isNewLine(currentInput_.contents, node)) {
    writeStr("# SKIPPED newline\n");
  } else {
    writeStr("# SKIPPED " +
             currentInput_.contents.substr(node->begin,
                                           node->end - node->begin) +
             "\n");
  }
}

bool Delocation::maybeSkipRelroStatement(const Node *node) {
  if (!isEndOfRelroSection(currentInput_.contents, node)) {
    skippedLine(node);
    return true;
  }
  return false;
}

Node *Delocation::skipRelroSection(Node *statement) {
  Node *previousStatement = statement;
  for (; statement != nullptr; statement = statement->next) {
    if (!maybeSkipRelroStatement(statement)) break;
    previousStatement = statement;
  }
  return previousStatement;
}

bool Delocation::processInput(const InputFile &input, std::string &errOut) {
  currentInput_ = input;

  for (Node *statement = input.ast->up; statement != nullptr;
       statement = statement->next) {
    assertNodeType(statement, PegRule::Statement);

    Node *node = skipWS(statement->up);
    if (node == nullptr) {
      writeNode(statement);
      continue;
    }

    bool ok = true;
    switch (node->rule) {
      case PegRule::GlobalDirective:
      case PegRule::Comment:
      case PegRule::LocationDirective:
      case PegRule::ZeroDirective:
        writeNode(statement);
        break;
      case PegRule::Directive:
        ok = processDirective(statement, node->up, errOut);
        break;
      case PegRule::LabelContainingDirective:
        ok = processLabelContainingDirective(statement, node->up, errOut);
        break;
      case PegRule::Label:
        ok = processLabel(statement, node->up, errOut);
        break;
      case PegRule::Instruction:
        switch (processor_) {
          case ProcessorType::X86_64:
            ok = processIntelInstruction(statement, node->up, errOut);
            break;
          default:
            errOut = "unsupported processor type";
            return false;
        }
        break;
      default:
        errOut = std::string("unknown top-level statement type ") +
                 pegRuleName(node->rule);
        return false;
    }

    if (!ok) return false;
  }
  return true;
}

bool Delocation::processDirective(Node *&statement, Node *directive,
                                   std::string &errOut) {
  assertNodeType(directive, PegRule::DirectiveName);
  std::string directiveName = contents(directive);

  std::vector<std::string> args;
  forEachPath(
      directive,
      [this, &args](Node *arg) {
        if (arg->up != nullptr) {
          Node *inner = arg->up;
          assertNodeType(inner, PegRule::QuotedArg);
          if (inner->up == nullptr) {
            args.push_back("");
            return;
          }
          inner = inner->up;
          assertNodeType(inner, PegRule::QuotedText);
          args.push_back(contents(inner));
        } else {
          args.push_back(contents(arg));
        }
      },
      {PegRule::Args, PegRule::Arg});

  if (directiveName == "comm" || directiveName == "lcomm") {
    if (args.empty()) {
      errOut = "comm directive has no arguments";
      return false;
    }
    bssAccessorsNeeded_[args[0]] = args[0];
    writeNode(statement);
  } else if (directiveName == "data") {
    errOut = ".data section found in module";
    return false;
  } else if (directiveName == "section") {
    std::string section = args[0];

    if (startsWith(section, ".data.rel.ro")) {
      skippedLine(statement);
      statement = skipRelroSection(statement->next);
      if (statement != nullptr) {
        return true;
      }
      errOut = "Failed to skip relro section " + section;
      return false;
    }

    auto st = sectionType(section);
    if (!st.second) {
      writeNode(statement);
      return true;
    }

    if (st.first == ".rodata" || st.first == ".text") {
      writeCommentedNode(statement);
      writeStr(".text\n");
    } else if (st.first == ".data") {
      errOut = ".data section found in module";
      return false;
    } else if (st.first == ".init_array" || st.first == ".fini_array" ||
               st.first == ".ctors" || st.first == ".dtors") {
      writeNode(statement);
    } else if (st.first == ".debug" || st.first == ".note" ||
               st.first == ".toc") {
      writeNode(statement);
    } else if (st.first == ".bss") {
      writeNode(statement);
      bool ok = true;
      statement = handleBSS(statement, errOut, ok);
      if (!ok) return false;
    } else {
      writeNode(statement);
    }
  } else {
    writeNode(statement);
  }
  return true;
}

bool Delocation::processSymbolExpr(Node *expr, std::string &b) {
  bool changed = false;
  assertNodeType(expr, PegRule::SymbolExpr);

  while (expr != nullptr) {
    Node *atom = expr->up;
    assertNodeType(atom, PegRule::SymbolAtom);

    for (Node *term = atom->up; term != nullptr; term = skipWS(term->next)) {
      if (term->rule == PegRule::SymbolExpr) {
        changed = processSymbolExpr(term, b) || changed;
        continue;
      }
      if (term->rule != PegRule::LocalSymbol) {
        b += contents(term);
        continue;
      }
      std::string oldSymbol = contents(term);
      std::string newSymbol = mapLocalSymbol(oldSymbol);
      if (newSymbol != oldSymbol) changed = true;
      b += newSymbol;
    }

    Node *next = skipWS(atom->next);
    if (next == nullptr) break;
    assertNodeType(next, PegRule::SymbolOperator);
    b += contents(next);
    next = skipWS(next->next);
    assertNodeType(next, PegRule::SymbolExpr);
    expr = next;
  }
  return changed;
}

bool Delocation::processLabelContainingDirective(Node *&statement,
                                                  Node *directive,
                                                  std::string &errOut) {
  bool changed = false;
  assertNodeType(directive, PegRule::LabelContainingDirectiveName);
  std::string name = contents(directive);

  Node *node = directive->next;
  assertNodeType(node, PegRule::WS);
  node = node->next;
  assertNodeType(node, PegRule::SymbolArgs);

  std::vector<std::string> args;
  for (node = skipWS(node->up); node != nullptr; node = skipWS(node->next)) {
    assertNodeType(node, PegRule::SymbolArg);
    Node *arg = node->up;
    assertNodeType(arg, PegRule::SymbolExpr);
    std::string b;
    changed = processSymbolExpr(arg, b) || changed;
    args.push_back(b);
  }

  if (!changed) {
    writeNode(statement);
  } else {
    writeCommentedNode(statement);
    writeStr("\t" + name + "\t" + join(args, ", ") + "\n");
  }

  if (name == ".localentry") {
    writeStr(localEntryName(args[0]) + ":\n");
  }

  return true;
}

bool Delocation::processLabel(Node *&statement, Node *label,
                               std::string &errOut) {
  std::string symbol = contents(label);

  switch (label->rule) {
    case PegRule::LocalLabel:
      writeStr(symbol + ":\n");
      break;
    case PegRule::LocalSymbol:
      writeStr(mapLocalSymbol(symbol) + ":\n");
      break;
    case PegRule::SymbolName:
      writeStr(localTargetName(symbol) + ":\n");
      writeNode(statement);
      break;
    default:
      errOut =
          std::string("unknown label type ") + pegRuleName(label->rule);
      return false;
  }
  return true;
}

Node *Delocation::handleBSS(Node *statement, std::string &errOut, bool &ok) {
  ok = true;
  Node *lastStatement = statement;
  for (statement = statement->next; statement != nullptr;
       lastStatement = statement, statement = statement->next) {
    Node *node = skipWS(statement->up);
    if (node == nullptr) {
      writeNode(statement);
      continue;
    }

    switch (node->rule) {
      case PegRule::GlobalDirective:
      case PegRule::Comment:
      case PegRule::Instruction:
      case PegRule::LocationDirective:
      case PegRule::ZeroDirective:
        writeNode(statement);
        break;

      case PegRule::Directive: {
        Node *dir = node->up;
        assertNodeType(dir, PegRule::DirectiveName);
        std::string dn = contents(dir);
        if (dn == "text" || dn == "section" || dn == "data") {
          return lastStatement;
        }
        writeNode(statement);
        break;
      }

      case PegRule::Label: {
        Node *label = node->up;
        writeNode(statement);
        if (label->rule != PegRule::LocalSymbol) {
          std::string symbol = contents(label);
          std::string localSym = localTargetName(symbol);
          writeStr("\n" + localSym + ":\n");
          bssAccessorsNeeded_[symbol] = localSym;
        }
        break;
      }

      case PegRule::LabelContainingDirective: {
        ok = processLabelContainingDirective(statement, node->up, errOut);
        if (!ok) return nullptr;
        break;
      }

      default:
        errOut = std::string("unknown BSS statement type ") +
                 pegRuleName(node->rule);
        ok = false;
        return nullptr;
    }
  }
  return lastStatement;
}

// x86-64 specific helpers

bool Delocation::isRIPRelative(const Node *node) const {
  return node != nullptr && node->rule == PegRule::BaseIndexScale &&
         contents(node) == "(%rip)";
}

Node *Delocation::gatherOffsets(Node *symRef, std::string &offsets) const {
  while (symRef != nullptr && symRef->rule == PegRule::Offset) {
    std::string offset = contents(symRef);
    if (offset[0] != '+' && offset[0] != '-') {
      offset = "+" + offset;
    }
    offsets += offset;
    symRef = symRef->next;
  }
  return symRef;
}

Delocation::MemRefResult Delocation::parseMemRef(Node *memRef) {
  MemRefResult result = {"", "", "", false, false, memRef};
  if (memRef->rule != PegRule::SymbolRef) return result;

  Node *symRef = memRef->up;
  result.nextRef = memRef->next;

  // (Offset* '+')?
  symRef = gatherOffsets(symRef, result.offset);

  // (LocalSymbol / SymbolName)
  result.symbol = contents(symRef);
  if (symRef->rule == PegRule::LocalSymbol) {
    result.symbolIsLocal = true;
    std::string mapped = mapLocalSymbol(result.symbol);
    if (mapped != result.symbol) {
      result.symbol = mapped;
      result.didChange = true;
    }
  }
  symRef = symRef->next;

  // Offset*
  symRef = gatherOffsets(symRef, result.offset);

  // ('@' Section Offset*)?
  if (symRef != nullptr) {
    assertNodeType(symRef, PegRule::Section);
    result.section = contents(symRef);
    symRef = symRef->next;
    symRef = gatherOffsets(symRef, result.offset);
  }

  if (symRef != nullptr) {
    throw std::runtime_error(
        std::string("unexpected token in SymbolRef: ") +
        pegRuleName(symRef->rule));
  }

  return result;
}

Delocation::InstructionType Delocation::classifyInstruction(
    const std::string &instr, const std::vector<Node *> &args) {
  if ((instr == "push" || instr == "pushq") && args.size() == 1)
    return instrPush;
  if ((instr == "mov" || instr == "movq" || instr == "vmovq" ||
       instr == "movsd" || instr == "vmovsd") &&
      args.size() == 2)
    return instrMove;
  if ((instr == "cmovneq" || instr == "cmoveq" || instr == "cmove") &&
      args.size() == 2)
    return instrConditionalMove;
  if ((instr == "call" || instr == "callq" || instr == "jmp" ||
       instr == "jo" || instr == "jno" || instr == "js" || instr == "jns" ||
       instr == "je" || instr == "jz" || instr == "jne" || instr == "jnz" ||
       instr == "jb" || instr == "jnae" || instr == "jc" || instr == "jnb" ||
       instr == "jae" || instr == "jnc" || instr == "jbe" || instr == "jna" ||
       instr == "ja" || instr == "jnbe" || instr == "jl" || instr == "jnge" ||
       instr == "jge" || instr == "jnl" || instr == "jle" || instr == "jng" ||
       instr == "jg" || instr == "jnle" || instr == "jp" || instr == "jpe" ||
       instr == "jnp" || instr == "jpo") &&
      args.size() == 1)
    return instrJump;
  if ((instr == "orq" || instr == "andq" || instr == "xorq") &&
      args.size() == 2)
    return instrCombine;
  if (instr == "cmpq" && args.size() == 2) return instrCompare;
  if ((instr == "sarxq" || instr == "shlxq" || instr == "shrxq" ||
       instr == "pinsrq") &&
      args.size() == 3)
    return instrThreeArg;
  if (instr == "vpinsrq" && args.size() == 4) return instrFourArg;
  if (instr == "vpbroadcastq" && args.size() == 2) return instrTransformingMove;
  if ((instr == "movlps" || instr == "movhps") && args.size() == 2)
    return instrMemoryVectorCombine;
  if (instr == "addq" && args.size() == 2) return instrTwoArg;
  return instrOther;
}

bool Delocation::isValidLEATarget(const std::string &reg) {
  return !startsWith(reg, "%xmm") && !startsWith(reg, "%ymm") &&
         !startsWith(reg, "%zmm");
}

WrapperFunc Delocation::loadFromGOT(const std::string &destination,
                                     const std::string &symbol,
                                     const std::string &section,
                                     bool redzoneCleared) {
  gotExternalsNeeded_.insert(symbol + "@" + section);
  std::string *out = output_;
  return [out, destination, symbol, section, redzoneCleared](
             std::function<void()> k) {
    if (!redzoneCleared) {
      *out += "\tleaq -128(%rsp), %rsp\n";
    }
    *out += "\tpushf\n";
    *out += "\tleaq " + symbol + "_" + section + "_external(%rip), " +
            destination + "\n";
    *out += "\taddq (" + destination + "), " + destination + "\n";
    *out += "\tmovq (" + destination + "), " + destination + "\n";
    *out += "\tpopf\n";
    if (!redzoneCleared) {
      *out += "\tleaq\t128(%rsp), %rsp\n";
    }
  };
}

std::pair<WrapperFunc, std::string> Delocation::saveRegister(
    std::string *w, const std::vector<std::string> &avoidRegs) {
  std::vector<std::string> candidates = {"%rax", "%rbx", "%rcx", "%rdx"};
  std::string reg;
  for (const auto &c : candidates) {
    bool found = false;
    for (const auto &a : avoidRegs) {
      if (c == a) { found = true; break; }
    }
    if (!found) { reg = c; break; }
  }
  if (reg.empty()) throw std::runtime_error("too many excluded registers");

  return {[w, reg](std::function<void()> k) {
            *w += "\tleaq -128(%rsp), %rsp\n";
            *w += "\tpushq " + reg + "\n";
            k();
            *w += "\tpopq " + reg + "\n";
            *w += "\tleaq 128(%rsp), %rsp\n";
          },
          reg};
}

bool Delocation::processIntelInstruction(Node *&statement, Node *instruction,
                                          std::string &errOut) {
  assertNodeType(instruction, PegRule::InstructionName);
  std::string instructionName = contents(instruction);

  auto argNodes = instructionArgs(instruction->next);

  WrapperStack wrappers;
  std::vector<std::string> args;
  bool changed = false;

  for (size_t i = 0; i < argNodes.size(); i++) {
    Node *arg = argNodes[i];
    Node *fullArg = arg;
    bool isIndirect = false;

    if (arg->rule == PegRule::IndirectionIndicator) {
      arg = arg->next;
      isIndirect = true;
    }

    switch (arg->rule) {
      case PegRule::RegisterOrConstant:
      case PegRule::LocalLabelRef:
        args.push_back(contents(fullArg));
        break;

      case PegRule::AVX512Token: {
        std::string &tail = args.back();
        tail += contents(fullArg);
        break;
      }

      case PegRule::MemoryRef: {
        auto mr = parseMemRef(arg->up);
        std::string symbol = mr.symbol;
        std::string offset = mr.offset;
        std::string section = mr.section;
        changed = mr.didChange;
        bool symbolIsLocal = mr.symbolIsLocal;
        Node *memRef = mr.nextRef;

        if (symbol == "OPENSSL_ia32cap_P" && section.empty()) {
          if (instructionName != "leaq") {
            errOut = "non-leaq instruction referenced OPENSSL_ia32cap_P directly";
            return false;
          }
          if (i != 0 || argNodes.size() != 2 || !isRIPRelative(memRef) ||
              !offset.empty()) {
            errOut = "invalid OPENSSL_ia32cap_P reference";
            return false;
          }
          Node *target = argNodes[1];
          assertNodeType(target, PegRule::RegisterOrConstant);
          std::string reg = contents(target);
          if (!startsWith(reg, "%r")) {
            errOut = "tried to load OPENSSL_ia32cap_P into non-standard register";
            return false;
          }

          auto uniqueSymbol = newCpuCapUniqueSymbol(
              (int)cpuCapUniqueSymbols_.size(), reg);
          std::string sym = uniqueSymbol.getx86Symbol();
          std::string symRet = uniqueSymbol.getx86SymbolReturn();
          std::string *out = output_;
          wrappers.stack.push_back(
              [out, sym, symRet](std::function<void()>) {
                *out += "\tjmp\t" + sym + "\n";
                *out += symRet + ":\n";
              });
          cpuCapUniqueSymbols_.push_back(uniqueSymbol);
          changed = true;
          goto endArgs;
        }

        if (section.empty()) {
          if (relroLocalLabelToFuncMap_.count(symbol)) {
            if (argNodes.size() != 2) {
              throw std::runtime_error("Expected only two arguments");
            }
            symbol = localTargetName(relroLocalLabelToFuncMap_[symbol]);
            std::string targetReg = contents(argNodes[1]);
            auto sr = saveRegister(output_, {targetReg});
            wrappers.stack.push_back(sr.first);
            std::string tempReg = sr.second;
            std::string *out = output_;
            wrappers.stack.push_back(
                [out, symbol, tempReg, targetReg](std::function<void()>) {
                  *out += "\tleaq\t" + symbol + "(%rip), " + tempReg + "\n";
                  *out += "\tmovq\t" + tempReg + ", " + targetReg + "\n";
                });
            changed = true;
          } else if (symbols_.count(symbol)) {
            symbol = localTargetName(symbol);
            changed = true;
          }
        } else if (section == "PLT") {
          if (classifyInstruction(instructionName, argNodes) != instrJump) {
            errOut = "Cannot rewrite PLT reference for non-jump instruction";
            return false;
          }
          if (symbols_.count(symbol)) {
            symbol = localTargetName(symbol);
            changed = true;
          } else if (!symbolIsLocal &&
                     !isSynthesized(symbol, ProcessorType::X86_64)) {
            redirectors_[symbol + "@" + section] = redirectorName(symbol);
            symbol = redirectorName(symbol);
          }
          changed = true;
        } else if (section == "GOTPCREL") {
          if (!offset.empty()) {
            errOut = "loading from GOT with offset is unsupported";
            return false;
          }
          if (!isRIPRelative(memRef)) {
            errOut = "GOT access must be IP-relative";
            return false;
          }

          bool useGOT = false;
          if (symbols_.count(symbol)) {
            symbol = localTargetName(symbol);
            changed = true;
          } else if (!isSynthesized(symbol, ProcessorType::X86_64)) {
            useGOT = true;
          }

          auto classification =
              classifyInstruction(instructionName, argNodes);
          if (classification != instrFourArg &&
              classification != instrThreeArg &&
              classification != instrCompare && i != 0) {
            errOut = "GOT access must be source operand";
            return false;
          }

          std::string targetReg;
          bool redzoneCleared = false;

          switch (classification) {
            case instrPush: {
              std::string *out = output_;
              wrappers.stack.push_back(
                  [out](std::function<void()> k) {
                    *out += "\tpushq %rax\n";
                    k();
                    *out += "\txchg %rax, (%rsp)\n";
                  });
              targetReg = "%rax";
              break;
            }
            case instrConditionalMove: {
              std::string inv;
              if (instructionName == "cmoveq" || instructionName == "cmove")
                inv = "ne";
              else if (instructionName == "cmovneq")
                inv = "e";
              else
                throw std::runtime_error(
                    "unknown conditional move: " + instructionName);
              std::string *out = output_;
              wrappers.stack.push_back(
                  [out, inv](std::function<void()> k) {
                    *out += "\tj" + inv + " 999f\n";
                    k();
                    *out += "999:\n";
                  });
              // fallthrough
            }
            // fallthrough
            case instrMove: {
              assertNodeType(argNodes[1], PegRule::RegisterOrConstant);
              targetReg = contents(argNodes[1]);
              break;
            }
            case instrCompare: {
              std::string otherSource = contents(argNodes[i ^ 1]);
              auto sr = saveRegister(output_, {otherSource});
              redzoneCleared = true;
              wrappers.stack.push_back(sr.first);
              std::string tempReg = sr.second;
              std::string *out = output_;
              std::string iName = instructionName;
              if (i == 0) {
                wrappers.stack.push_back(
                    [out, iName, tempReg,
                     otherSource](std::function<void()> k) {
                      k();
                      *out += "\t" + iName + " " + tempReg + ", " +
                              otherSource + "\n";
                    });
              } else {
                wrappers.stack.push_back(
                    [out, iName, otherSource,
                     tempReg](std::function<void()> k) {
                      k();
                      *out += "\t" + iName + " " + otherSource + ", " +
                              tempReg + "\n";
                    });
              }
              targetReg = tempReg;
              break;
            }
            case instrTransformingMove: {
              assertNodeType(argNodes[1], PegRule::RegisterOrConstant);
              targetReg = contents(argNodes[1]);
              std::string *out = output_;
              std::string iName = instructionName;
              std::string tr = targetReg;
              wrappers.stack.push_back(
                  [out, iName, tr](std::function<void()> k) {
                    k();
                    *out += "\t" + iName + " " + tr + ", " + tr + "\n";
                  });
              if (isValidLEATarget(targetReg)) {
                errOut = "transforming moves assumed to target XMM registers";
                return false;
              }
              break;
            }
            case instrCombine: {
              targetReg = contents(argNodes[1]);
              if (!isValidLEATarget(targetReg)) {
                errOut = "cannot handle combining instructions targeting non-general registers";
                return false;
              }
              auto sr = saveRegister(output_, {targetReg});
              redzoneCleared = true;
              wrappers.stack.push_back(sr.first);
              std::string tempReg = sr.second;
              std::string *out = output_;
              std::string iName = instructionName;
              std::string tr = targetReg;
              wrappers.stack.push_back(
                  [out, iName, tempReg, tr](std::function<void()> k) {
                    k();
                    *out += "\t" + iName + " " + tempReg + ", " + tr + "\n";
                  });
              targetReg = tempReg;
              break;
            }
            case instrMemoryVectorCombine: {
              assertNodeType(argNodes[1], PegRule::RegisterOrConstant);
              targetReg = contents(argNodes[1]);
              if (isValidLEATarget(targetReg)) {
                errOut = "target register must be an XMM register";
                return false;
              }
              auto sr = saveRegister(output_, {});
              wrappers.stack.push_back(sr.first);
              redzoneCleared = true;
              std::string tempReg = sr.second;
              std::string *out = output_;
              std::string iName = instructionName;
              std::string tr = targetReg;
              wrappers.stack.push_back(
                  [out, iName, tempReg, tr](std::function<void()> k) {
                    k();
                    *out += "\tpushq " + tempReg + "\n";
                    *out += "\t" + iName + " (%rsp), " + tr + "\n";
                    *out += "\tleaq 8(%rsp), %rsp\n";
                  });
              targetReg = tempReg;
              break;
            }
            case instrThreeArg: {
              if (argNodes.size() != 3) {
                errOut = "three-argument instruction has wrong number of args";
                return false;
              }
              if (i != 0 && i != 1) {
                errOut = "GOT access must be from source operand";
                return false;
              }
              targetReg = contents(argNodes[2]);
              std::string otherSource = contents(argNodes[1]);
              if (i == 1) otherSource = contents(argNodes[0]);

              auto sr = saveRegister(output_, {targetReg, otherSource});
              redzoneCleared = true;
              wrappers.stack.push_back(sr.first);
              std::string tempReg = sr.second;
              std::string *out = output_;
              std::string iName = instructionName;
              std::string tr = targetReg;
              if (i == 0) {
                wrappers.stack.push_back(
                    [out, iName, tempReg, otherSource,
                     tr](std::function<void()> k) {
                      k();
                      *out += "\t" + iName + " " + tempReg + ", " +
                              otherSource + ", " + tr + "\n";
                    });
              } else {
                wrappers.stack.push_back(
                    [out, iName, otherSource, tempReg,
                     tr](std::function<void()> k) {
                      k();
                      *out += "\t" + iName + " " + otherSource + ", " +
                              tempReg + ", " + tr + "\n";
                    });
              }
              targetReg = tempReg;
              break;
            }
            case instrFourArg: {
              if (argNodes.size() != 4) {
                errOut = "four-argument instruction has wrong number of args";
                return false;
              }
              if (i != 1) {
                errOut = "GOT access must be from source operand";
                return false;
              }
              targetReg = contents(argNodes[3]);
              std::string otherSource = contents(argNodes[2]);
              std::string gotSource = contents(argNodes[1]);
              std::string immediate = contents(argNodes[0]);

              auto sr = saveRegister(output_, {targetReg, gotSource});
              redzoneCleared = true;
              wrappers.stack.push_back(sr.first);
              std::string tempReg = sr.second;
              std::string *out = output_;
              std::string iName = instructionName;
              std::string tr = targetReg;
              wrappers.stack.push_back(
                  [out, iName, immediate, tempReg, otherSource,
                   tr](std::function<void()> k) {
                    k();
                    *out += "\t" + iName + " " + immediate + ", " +
                            tempReg + ", " + otherSource + ", " + tr + "\n";
                  });
              targetReg = tempReg;
              break;
            }
            case instrTwoArg: {
              if (argNodes.size() != 2) {
                errOut = "two-argument instruction has wrong number of args";
                return false;
              }
              assertNodeType(argNodes[1], PegRule::RegisterOrConstant);
              targetReg = contents(argNodes[1]);
              auto sr = saveRegister(output_, {targetReg});
              redzoneCleared = true;
              wrappers.stack.push_back(sr.first);
              std::string tempReg = sr.second;
              std::string *out = output_;
              std::string iName = instructionName;
              std::string tr = targetReg;
              wrappers.stack.push_back(
                  [out, iName, tempReg, tr](std::function<void()> k) {
                    k();
                    *out += "\t" + iName + " " + tempReg + ", " + tr + "\n";
                  });
              targetReg = tempReg;
              break;
            }
            default:
              errOut = "Cannot rewrite GOTPCREL reference for instruction " +
                       instructionName;
              return false;
          }

          if (!isValidLEATarget(targetReg)) {
            auto sr = saveRegister(output_, {});
            wrappers.stack.push_back(sr.first);
            bool isAVX = startsWith(instructionName, "v");
            std::string tempReg = sr.second;
            std::string *out = output_;
            std::string tr = targetReg;
            wrappers.stack.push_back(
                [out, isAVX, tempReg, tr](std::function<void()> k) {
                  k();
                  std::string prefix = isAVX ? "v" : "";
                  *out += "\t" + prefix + "movq " + tempReg + ", " + tr +
                          "\n";
                });
            targetReg = tempReg;
            if (redzoneCleared) {
              errOut = "internal error: Red Zone was already cleared";
              return false;
            }
            redzoneCleared = true;
          }

          if (symbol == "OPENSSL_ia32cap_P") {
            auto uniqueSymbol = newCpuCapUniqueSymbol(
                (int)cpuCapUniqueSymbols_.size(), targetReg);
            std::string sym = uniqueSymbol.getx86Symbol();
            std::string symRet = uniqueSymbol.getx86SymbolReturn();
            std::string *out = output_;
            wrappers.stack.push_back(
                [out, sym, symRet](std::function<void()>) {
                  *out += "\tjmp\t" + sym + "\n";
                  *out += symRet + ":\n";
                });
            cpuCapUniqueSymbols_.push_back(uniqueSymbol);
          } else if (useGOT) {
            wrappers.stack.push_back(
                loadFromGOT(targetReg, symbol, section, redzoneCleared));
          } else {
            std::string *out = output_;
            std::string s = symbol;
            std::string tr = targetReg;
            wrappers.stack.push_back(
                [out, s, tr](std::function<void()>) {
                  *out += "\tleaq\t" + s + "(%rip), " + tr + "\n";
                });
          }
          changed = true;
          goto endArgs;
        } else {
          errOut = "Unknown section type " + section;
          return false;
        }

        if (!changed && !section.empty()) {
          throw std::runtime_error("section was not handled");
        }
        section.clear();

        {
          std::string argStr;
          if (isIndirect) argStr += "*";
          argStr += symbol;
          argStr += offset;
          for (; memRef != nullptr; memRef = memRef->next) {
            argStr += contents(memRef);
          }
          args.push_back(argStr);
        }
        break;
      }

      case PegRule::GOTLocation: {
        if (instructionName != "movabsq") {
          errOut = "_GLOBAL_OFFSET_TABLE_ lookup didn't use movabsq";
          return false;
        }
        if (i != 0 || argNodes.size() != 2) {
          errOut = "movabs of _GLOBAL_OFFSET_TABLE_ didn't have expected form";
          return false;
        }
        gotDeltaNeeded_ = true;
        changed = true;
        instructionName = "movq";
        assertNodeType(arg->up, PegRule::LocalSymbol);
        std::string baseSymbol = mapLocalSymbol(contents(arg->up));
        std::string targetReg = contents(argNodes[1]);
        args.push_back(".Lboringssl_got_delta(%rip)");
        std::string *out = output_;
        wrappers.stack.push_back(
            [out, baseSymbol, targetReg](std::function<void()> k) {
              k();
              *out += "\taddq $.Lboringssl_got_delta-" + baseSymbol + ", " +
                      targetReg + "\n";
            });
        break;
      }

      case PegRule::GOTSymbolOffset: {
        if (instructionName != "movabsq") {
          errOut = "_GLOBAL_OFFSET_TABLE_ offset didn't use movabsq";
          return false;
        }
        if (i != 0 || argNodes.size() != 2) {
          errOut =
              "movabs of _GLOBAL_OFFSET_TABLE_ offset didn't have expected form";
          return false;
        }
        assertNodeType(arg->up, PegRule::SymbolName);
        std::string symbol = contents(arg->up);
        if (startsWith(symbol, ".L")) {
          symbol = mapLocalSymbol(symbol);
        }
        std::string targetReg = contents(argNodes[1]);
        std::string fullArgStr = contents(arg);
        bool isGOTOFF = endsWith(fullArgStr, "@GOTOFF");
        std::string prefix;
        if (isGOTOFF) {
          prefix = "gotoff";
          gotOffOffsetsNeeded_.insert(symbol);
        } else {
          prefix = "got";
          gotOffsetsNeeded_.insert(symbol);
        }
        changed = true;
        std::string *out = output_;
        wrappers.stack.push_back(
            [out, prefix, symbol, targetReg](std::function<void()>) {
              *out += "\tmovq .Lboringssl_" + prefix + "_" + symbol +
                      "(%rip), " + targetReg + "\n";
            });
        break;
      }

      default:
        throw std::runtime_error(
            std::string("unknown instruction argument type ") +
            pegRuleName(arg->rule));
    }
  }

endArgs:
  if (changed) {
    writeCommentedNode(statement);
    std::string replacement =
        "\t" + instructionName + "\t" + join(args, ", ") + "\n";
    wrappers.doIt([this, &replacement]() { writeStr(replacement); });
  } else {
    writeNode(statement);
  }
  return true;
}

// Transform

bool Delocation::transform(std::string &out,
                            const std::vector<std::string> &includes,
                            std::vector<InputFile> &inputs,
                            bool startEndDebugDirectives,
                            std::string &errOut) {
  output_ = &out;

  std::set<std::string> symbols;
  std::set<std::string> localEntrySyms;
  std::map<int, bool> fileNumbers;
  int maxObservedFileNumber = 0;
  bool fileDirectivesContainMD5 = false;
  std::map<std::string, std::string> relroLocalLabelToFuncMap;

  symbols.insert("OPENSSL_ia32cap_get");

  for (const auto &include : includes) {
    std::string err;
    std::string relative = relativeHeaderIncludePath(include, err);
    if (relative.empty()) {
      errOut = err;
      return false;
    }
    out += "#include <" + relative + ">\n";
  }

  ProcessorType proc = ProcessorType::X86_64;
  if (!inputs.empty()) {
    proc = detectProcessor(inputs[0]);
  }
  processor_ = proc;

  for (auto &input : inputs) {
    forEachPath(
        input.ast->up,
        [&input, &symbols](Node *node) {
          std::string symbol =
              input.contents.substr(node->begin, node->end - node->begin);
          if (symbols.count(symbol)) {
            throw std::runtime_error("Duplicate symbol found: " + symbol +
                                     " in " + input.path);
          }
          symbols.insert(symbol);
        },
        {PegRule::Statement, PegRule::Label, PegRule::SymbolName});

    forEachPath(
        input.ast->up,
        [&input, &localEntrySyms](Node *node) {
          node = node->up;
          assertNodeType(node, PegRule::LabelContainingDirectiveName);
          std::string directive =
              input.contents.substr(node->begin, node->end - node->begin);
          if (directive != ".localentry") return;
          node = skipWS(node->next);
          assertNodeType(node, PegRule::SymbolArgs);
          node = node->up;
          assertNodeType(node, PegRule::SymbolArg);
          std::string symbol =
              input.contents.substr(node->begin, node->end - node->begin);
          localEntrySyms.insert(symbol);
        },
        {PegRule::Statement, PegRule::LabelContainingDirective});

    forEachPath(
        input.ast->up,
        [&input, &fileNumbers, &maxObservedFileNumber,
         &fileDirectivesContainMD5](Node *node) {
          assertNodeType(node, PegRule::LocationDirective);
          std::string directive =
              input.contents.substr(node->begin, node->end - node->begin);
          if (!startsWith(directive, ".file")) return;
          auto parts = splitFields(directive);
          if (parts.size() == 2) return;
          int fileNo = std::stoi(parts[1]);
          if (fileNumbers.count(fileNo)) {
            throw std::runtime_error("Duplicate file number " +
                                     std::to_string(fileNo));
          }
          fileNumbers[fileNo] = true;
          if (fileNo > maxObservedFileNumber)
            maxObservedFileNumber = fileNo;
          for (size_t t = 2; t < parts.size(); t++) {
            if (parts[t] == "md5") fileDirectivesContainMD5 = true;
          }
        },
        {PegRule::Statement, PegRule::LocationDirective});

    if (proc == ProcessorType::X86_64) {
      if (!relroLocalLabelToFuncMapping(input, relroLocalLabelToFuncMap)) {
        errOut = "error processing relro section";
        return false;
      }
    }
  }

  commentIndicator_ = "#";
  if (proc == ProcessorType::AARCH64) {
    commentIndicator_ = "//";
  }

  symbols_ = symbols;
  localEntrySymbols_ = localEntrySyms;
  relroLocalLabelToFuncMap_ = relroLocalLabelToFuncMap;
  redirectors_.clear();
  bssAccessorsNeeded_.clear();
  tocLoaders_.clear();
  gotExternalsNeeded_.clear();
  gotDeltaNeeded_ = false;
  gotOffsetsNeeded_.clear();
  gotOffOffsetsNeeded_.clear();
  cpuCapUniqueSymbols_.clear();

  out += ".text\n";
  if (startEndDebugDirectives) {
    std::string fileTrailing;
    if (fileDirectivesContainMD5) {
      fileTrailing = " md5 0x00000000000000000000000000000000";
    }
    out += ".file " + std::to_string(maxObservedFileNumber + 1) +
           " \"inserted_by_delocate.c\"" + fileTrailing + "\n";
    out += ".loc " + std::to_string(maxObservedFileNumber + 1) + " 1 0\n";
  }

  if (proc == ProcessorType::AARCH64) {
    out += ".global BORINGSSL_bcm_text_hash\n";
    out += ".type BORINGSSL_bcm_text_hash, @function\n";
  } else {
    out += ".type BORINGSSL_bcm_text_hash, @object\n";
    out += ".size BORINGSSL_bcm_text_hash, 32\n";
  }
  out += "BORINGSSL_bcm_text_hash:\n";
  for (int b = 0; b < 32; b++) {
    char buf[16];
    snprintf(buf, sizeof(buf), ".byte 0x%x\n", UninitHashValue[b]);
    out += buf;
  }

  if (proc == ProcessorType::AARCH64) {
    out += ".global BORINGSSL_bcm_text_start\n";
    out += ".type BORINGSSL_bcm_text_start, @function\n";
  }
  out += "BORINGSSL_bcm_text_start:\n";

  for (auto &input : inputs) {
    if (!processInput(input, errOut)) return false;
  }

  out += ".text\n";
  if (startEndDebugDirectives) {
    out += ".loc " + std::to_string(maxObservedFileNumber + 1) + " 2 0\n";
    if (proc == ProcessorType::AARCH64) {
      out += ".global BORINGSSL_bcm_text_end\n";
      out += ".type BORINGSSL_bcm_text_end, @function\n";
    }
  }
  out += "BORINGSSL_bcm_text_end:\n";

  // Emit redirector functions
  std::vector<std::string> redirectorNames;
  for (const auto &p : redirectors_) redirectorNames.push_back(p.first);
  std::sort(redirectorNames.begin(), redirectorNames.end());

  for (const auto &name : redirectorNames) {
    std::string redirector = redirectors_[name];
    if (proc == ProcessorType::X86_64) {
      out += ".type " + redirector + ", @function\n";
      out += redirector + ":\n";
      out += "\tjmp\t" + name + "\n";
    }
  }

  // Emit BSS accessors
  std::vector<std::string> accessorNames;
  for (const auto &p : bssAccessorsNeeded_)
    accessorNames.push_back(p.first);
  std::sort(accessorNames.begin(), accessorNames.end());

  for (const auto &name : accessorNames) {
    std::string funcName = accessorName(name);
    std::string target = bssAccessorsNeeded_[name];
    if (proc == ProcessorType::X86_64) {
      out += ".type " + funcName + ", @function\n";
      out += funcName + ":\n";
      out += "\tleaq\t" + target + "(%rip), %rax\n\tret\n";
    }
  }

  // x86-64 specific epilogue
  if (proc == ProcessorType::X86_64) {
    std::vector<std::string> externalNames(gotExternalsNeeded_.begin(),
                                           gotExternalsNeeded_.end());
    std::sort(externalNames.begin(), externalNames.end());
    for (const auto &name : externalNames) {
      size_t atPos = name.find('@');
      std::string symbol = name.substr(0, atPos);
      std::string section = name.substr(atPos + 1);
      out += ".type " + symbol + "_" + section + "_external, @object\n";
      out += ".size " + symbol + "_" + section + "_external, 8\n";
      out += symbol + "_" + section + "_external:\n";
      out += "\t.long " + symbol + "@" + section + "\n";
      out += "\t.long 0\n";
    }

    out += ".type OPENSSL_ia32cap_get, @function\n";
    out += ".globl OPENSSL_ia32cap_get\n";
    out += localTargetName("OPENSSL_ia32cap_get") + ":\n";
    out += "OPENSSL_ia32cap_get:\n";
    out += "\tleaq OPENSSL_ia32cap_P(%rip), %rax\n";
    out += "\tret\n";

    for (const auto &us : cpuCapUniqueSymbols_) {
      out += ".type " + us.getx86Symbol() + ", @function\n";
      out += us.getx86Symbol() + ":\n";
      out += "\tleaq OPENSSL_ia32cap_P(%rip), %" + us.registerName + "\n";
      out += "\tjmp " + us.getx86SymbolReturn() + "\n";
    }

    if (gotDeltaNeeded_) {
      out += ".Lboringssl_got_delta:\n";
      out += "\t.quad _GLOBAL_OFFSET_TABLE_-.Lboringssl_got_delta\n";
    }

    for (const auto &name : std::set<std::string>(gotOffsetsNeeded_.begin(),
                                                   gotOffsetsNeeded_.end())) {
      out += ".Lboringssl_got_" + name + ":\n";
      out += "\t.quad " + name + "@GOT\n";
    }
    for (const auto &name :
         std::set<std::string>(gotOffOffsetsNeeded_.begin(),
                               gotOffOffsetsNeeded_.end())) {
      out += ".Lboringssl_gotoff_" + name + ":\n";
      out += "\t.quad " + name + "@GOTOFF\n";
    }
  }

  return true;
}

bool Delocation::parseInputs(std::vector<InputFile> &inputs,
                              const std::vector<std::string> &cppCommand,
                              std::string &errOut) {
  static thread_local std::vector<Parser> parsers;
  parsers.resize(inputs.size());

  for (size_t i = 0; i < inputs.size(); i++) {
    auto &input = inputs[i];

    if (input.isArchive) {
      std::ifstream f(input.path, std::ios::binary);
      if (!f) {
        errOut = "cannot open " + input.path;
        return false;
      }
      std::string data((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
      std::map<std::string, std::string> ar;
      if (!ParseAR(data, ar, errOut)) return false;
      if (ar.size() != 1) {
        errOut = "expected one file in archive, but found " +
                 std::to_string(ar.size());
        return false;
      }
      input.contents = ar.begin()->second;
    } else {
      if (!cppCommand.empty()) {
        // Run preprocessor
        std::string cmd;
        for (const auto &arg : cppCommand) {
          cmd += arg + " ";
        }
        cmd += input.path;
        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
          errOut = "failed to run preprocessor";
          return false;
        }
        std::string result;
        char buffer[4096];
        while (size_t n = fread(buffer, 1, sizeof(buffer), pipe)) {
          result.append(buffer, n);
        }
        if (pclose(pipe) != 0) {
          errOut = "preprocessor failed";
          return false;
        }
        input.contents = result;
      } else {
        std::ifstream f(input.path, std::ios::binary);
        if (!f) {
          errOut = "cannot open " + input.path;
          return false;
        }
        input.contents = std::string((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
      }
    }

    if (!parsers[i].parse(input.contents)) {
      errOut = "error while parsing " + input.path + ": " + parsers[i].error();
      return false;
    }
    input.ast = parsers[i].ast();
  }
  return true;
}

// Path helpers

std::string includePathFromHeaderFilePath(const std::string &path,
                                           std::string &errOut) {
  std::string dir = path;
  while (true) {
    size_t sep = dir.find_last_of('/');
    if (sep == std::string::npos) break;
    std::string file = dir.substr(sep + 1);
    dir = dir.substr(0, sep);
    if (file == "openssl") return dir + "/";
  }
  errOut = "failed to find 'openssl' path element in header file path " + path;
  return "";
}

std::string relativeHeaderIncludePath(const std::string &path,
                                       std::string &errOut) {
  std::string dir = includePathFromHeaderFilePath(path, errOut);
  if (dir.empty()) return "";
  // Make relative
  if (startsWith(path, dir)) {
    return path.substr(dir.size());
  }
  return path;
}
