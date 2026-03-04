// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0 OR ISC

#ifndef PARSER_H
#define PARSER_H

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// PEG rule enum matching the Go pegRule constants exactly.
enum class PegRule : uint8_t {
  Unknown = 0,
  AsmFile,
  Statement,
  GlobalDirective,
  Directive,
  DirectiveName,
  LocationDirective,
  ZeroDirective,
  FileDirective,
  LocDirective,
  Args,
  Arg,
  QuotedArg,
  QuotedText,
  LabelContainingDirective,
  LabelContainingDirectiveName,
  SymbolArgs,
  SymbolArg,
  SymbolExpr,
  SymbolAtom,
  SymbolOperator,
  OpenParen,
  CloseParen,
  SymbolType,
  Dot,
  TCMarker,
  EscapedChar,
  WS,
  Comment,
  Label,
  SymbolName,
  LocalSymbol,
  LocalLabel,
  LocalLabelRef,
  Instruction,
  InstructionName,
  InstructionArg,
  GOTLocation,
  GOTSymbolOffset,
  AVX512Token,
  TOCRefHigh,
  TOCRefLow,
  IndirectionIndicator,
  RegisterOrConstant,
  ARMConstantTweak,
  ARMRegister,
  ARMVectorRegister,
  SVE2PredicateRegister,
  ARMRegisterBoundary,
  MemoryRef,
  SymbolRef,
  Low12BitsSymbolRef,
  ARMBaseIndexScale,
  ARMGOTLow12,
  ARMPostincrement,
  BaseIndexScale,
  Operator,
  OffsetOperator,
  S2nBignumHelper,
  Offset,
  Section,
  SegmentRegister,
};

const char *pegRuleName(PegRule r);

// AST node.
struct Node {
  PegRule rule;
  uint32_t begin;
  uint32_t end;
  Node *up;    // first child
  Node *next;  // next sibling
};

// Parser implements a recursive descent PEG parser for the assembly grammar.
class Parser {
 public:
  bool parse(const std::string &input);
  Node *ast() { return root_; }
  const std::string &error() const { return error_; }

 private:
  std::deque<Node> arena_;
  Node *root_ = nullptr;
  const char *buf_ = nullptr;
  size_t len_ = 0;
  size_t pos_ = 0;
  std::string error_;

  Node *allocNode(PegRule rule, uint32_t begin) {
    arena_.push_back({rule, begin, 0, nullptr, nullptr});
    return &arena_.back();
  }

  // Save/restore state for backtracking
  struct SavePoint {
    size_t pos;
    size_t arenaSize;
  };
  SavePoint save() { return {pos_, arena_.size()}; }
  void restore(SavePoint sp) {
    pos_ = sp.pos;
    while (arena_.size() > sp.arenaSize) arena_.pop_back();
  }

  char peek() const { return pos_ < len_ ? buf_[pos_] : '\0'; }
  bool atEnd() const { return pos_ >= len_; }

  // Build a node for a rule. If the parse function returns true, the node
  // is finalized. If false, the state is restored.
  // Children are collected as a linked list of nodes built during the parse.

  // Helper: check character classes
  // The PEG uses [[A-Z]] which in the peg tool means case-insensitive [A-Za-z].
  static bool isCIAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  }
  static bool isCIAlphaNum(char c) {
    return isCIAlpha(c) || (c >= '0' && c <= '9');
  }
  static bool isCIHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
  }
  static bool isDigit(char c) { return c >= '0' && c <= '9'; }

  bool matchStr(const char *s);
  bool matchChar(char c) {
    if (pos_ < len_ && buf_[pos_] == c) { pos_++; return true; }
    return false;
  }

  // Node builder helper: creates a node, runs a parse function, and on
  // success links children. On failure, restores state.
  // The parse function populates `children` vector.
  Node *buildNode(PegRule rule, bool (Parser::*parseFn)(std::vector<Node*>&));

  // Grammar rules - each returns true on success and appends child nodes.
  bool rAsmFile(std::vector<Node*> &ch);
  bool rStatement(std::vector<Node*> &ch);
  bool rGlobalDirective(std::vector<Node*> &ch);
  bool rDirective(std::vector<Node*> &ch);
  bool rDirectiveName(std::vector<Node*> &ch);
  bool rLocationDirective(std::vector<Node*> &ch);
  bool rZeroDirective(std::vector<Node*> &ch);
  bool rFileDirective(std::vector<Node*> &ch);
  bool rLocDirective(std::vector<Node*> &ch);
  bool rArgs(std::vector<Node*> &ch);
  bool rArg(std::vector<Node*> &ch);
  bool rQuotedArg(std::vector<Node*> &ch);
  bool rQuotedText(std::vector<Node*> &ch);
  bool rLabelContainingDirective(std::vector<Node*> &ch);
  bool rLabelContainingDirectiveName(std::vector<Node*> &ch);
  bool rSymbolArgs(std::vector<Node*> &ch);
  bool rSymbolArg(std::vector<Node*> &ch);
  bool rSymbolExpr(std::vector<Node*> &ch);
  bool rSymbolAtom(std::vector<Node*> &ch);
  bool rSymbolOperator(std::vector<Node*> &ch);
  bool rSymbolType(std::vector<Node*> &ch);
  bool rDot(std::vector<Node*> &ch);
  bool rTCMarker(std::vector<Node*> &ch);
  bool rWS(std::vector<Node*> &ch);
  bool rComment(std::vector<Node*> &ch);
  bool rLabel(std::vector<Node*> &ch);
  bool rSymbolName(std::vector<Node*> &ch);
  bool rLocalSymbol(std::vector<Node*> &ch);
  bool rLocalLabel(std::vector<Node*> &ch);
  bool rLocalLabelRef(std::vector<Node*> &ch);
  bool rInstruction(std::vector<Node*> &ch);
  bool rInstructionName(std::vector<Node*> &ch);
  bool rInstructionArg(std::vector<Node*> &ch);
  bool rGOTLocation(std::vector<Node*> &ch);
  bool rGOTSymbolOffset(std::vector<Node*> &ch);
  bool rAVX512Token(std::vector<Node*> &ch);
  bool rTOCRefHigh(std::vector<Node*> &ch);
  bool rTOCRefLow(std::vector<Node*> &ch);
  bool rRegisterOrConstant(std::vector<Node*> &ch);
  bool rARMConstantTweak(std::vector<Node*> &ch);
  bool rARMRegister(std::vector<Node*> &ch);
  bool rARMVectorRegister(std::vector<Node*> &ch);
  bool rSVE2PredicateRegister(std::vector<Node*> &ch);
  bool rMemoryRef(std::vector<Node*> &ch);
  bool rSymbolRef(std::vector<Node*> &ch);
  bool rLow12BitsSymbolRef(std::vector<Node*> &ch);
  bool rARMBaseIndexScale(std::vector<Node*> &ch);
  bool rARMGOTLow12(std::vector<Node*> &ch);
  bool rBaseIndexScale(std::vector<Node*> &ch);
  bool rOffset(std::vector<Node*> &ch);
  bool rSection(std::vector<Node*> &ch);
  bool rSegmentRegister(std::vector<Node*> &ch);
  bool rS2nBignumHelper(std::vector<Node*> &ch);
  bool rOffsetOperator(std::vector<Node*> &ch);
};

#endif // PARSER_H
