// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0 OR ISC

#include "parser.h"

#include <cstring>

const char *pegRuleName(PegRule r) {
  switch (r) {
    case PegRule::Unknown: return "Unknown";
    case PegRule::AsmFile: return "AsmFile";
    case PegRule::Statement: return "Statement";
    case PegRule::GlobalDirective: return "GlobalDirective";
    case PegRule::Directive: return "Directive";
    case PegRule::DirectiveName: return "DirectiveName";
    case PegRule::LocationDirective: return "LocationDirective";
    case PegRule::ZeroDirective: return "ZeroDirective";
    case PegRule::FileDirective: return "FileDirective";
    case PegRule::LocDirective: return "LocDirective";
    case PegRule::Args: return "Args";
    case PegRule::Arg: return "Arg";
    case PegRule::QuotedArg: return "QuotedArg";
    case PegRule::QuotedText: return "QuotedText";
    case PegRule::LabelContainingDirective: return "LabelContainingDirective";
    case PegRule::LabelContainingDirectiveName: return "LabelContainingDirectiveName";
    case PegRule::SymbolArgs: return "SymbolArgs";
    case PegRule::SymbolArg: return "SymbolArg";
    case PegRule::SymbolExpr: return "SymbolExpr";
    case PegRule::SymbolAtom: return "SymbolAtom";
    case PegRule::SymbolOperator: return "SymbolOperator";
    case PegRule::OpenParen: return "OpenParen";
    case PegRule::CloseParen: return "CloseParen";
    case PegRule::SymbolType: return "SymbolType";
    case PegRule::Dot: return "Dot";
    case PegRule::TCMarker: return "TCMarker";
    case PegRule::EscapedChar: return "EscapedChar";
    case PegRule::WS: return "WS";
    case PegRule::Comment: return "Comment";
    case PegRule::Label: return "Label";
    case PegRule::SymbolName: return "SymbolName";
    case PegRule::LocalSymbol: return "LocalSymbol";
    case PegRule::LocalLabel: return "LocalLabel";
    case PegRule::LocalLabelRef: return "LocalLabelRef";
    case PegRule::Instruction: return "Instruction";
    case PegRule::InstructionName: return "InstructionName";
    case PegRule::InstructionArg: return "InstructionArg";
    case PegRule::GOTLocation: return "GOTLocation";
    case PegRule::GOTSymbolOffset: return "GOTSymbolOffset";
    case PegRule::AVX512Token: return "AVX512Token";
    case PegRule::TOCRefHigh: return "TOCRefHigh";
    case PegRule::TOCRefLow: return "TOCRefLow";
    case PegRule::IndirectionIndicator: return "IndirectionIndicator";
    case PegRule::RegisterOrConstant: return "RegisterOrConstant";
    case PegRule::ARMConstantTweak: return "ARMConstantTweak";
    case PegRule::ARMRegister: return "ARMRegister";
    case PegRule::ARMVectorRegister: return "ARMVectorRegister";
    case PegRule::SVE2PredicateRegister: return "SVE2PredicateRegister";
    case PegRule::ARMRegisterBoundary: return "ARMRegisterBoundary";
    case PegRule::MemoryRef: return "MemoryRef";
    case PegRule::SymbolRef: return "SymbolRef";
    case PegRule::Low12BitsSymbolRef: return "Low12BitsSymbolRef";
    case PegRule::ARMBaseIndexScale: return "ARMBaseIndexScale";
    case PegRule::ARMGOTLow12: return "ARMGOTLow12";
    case PegRule::ARMPostincrement: return "ARMPostincrement";
    case PegRule::BaseIndexScale: return "BaseIndexScale";
    case PegRule::Operator: return "Operator";
    case PegRule::OffsetOperator: return "OffsetOperator";
    case PegRule::S2nBignumHelper: return "S2nBignumHelper";
    case PegRule::Offset: return "Offset";
    case PegRule::Section: return "Section";
    case PegRule::SegmentRegister: return "SegmentRegister";
  }
  return "???";
}

bool Parser::matchStr(const char *s) {
  size_t slen = strlen(s);
  if (pos_ + slen > len_) return false;
  if (memcmp(buf_ + pos_, s, slen) != 0) return false;
  pos_ += slen;
  return true;
}

// Build a node: run parseFn, collect children, link them.
Node *Parser::buildNode(PegRule rule,
                        bool (Parser::*parseFn)(std::vector<Node *> &)) {
  SavePoint sp = save();
  Node *node = allocNode(rule, (uint32_t)pos_);
  std::vector<Node *> children;
  if (!(this->*parseFn)(children)) {
    restore(sp);
    return nullptr;
  }
  node->end = (uint32_t)pos_;
  // Link children
  Node *prev = nullptr;
  for (Node *c : children) {
    if (!prev) {
      node->up = c;
    } else {
      prev->next = c;
    }
    prev = c;
  }
  return node;
}

bool Parser::parse(const std::string &input) {
  buf_ = input.c_str();
  len_ = input.size();
  pos_ = 0;
  arena_.clear();
  root_ = nullptr;
  error_.clear();

  std::vector<Node *> ch;
  root_ = allocNode(PegRule::AsmFile, 0);
  if (!rAsmFile(ch)) {
    error_ = "parse error at position " + std::to_string(pos_);
    return false;
  }
  root_->end = (uint32_t)pos_;
  // Link statement children
  Node *prev = nullptr;
  for (Node *c : ch) {
    if (!prev) {
      root_->up = c;
    } else {
      prev->next = c;
    }
    prev = c;
  }
  return true;
}

// AsmFile <- Statement* !.
bool Parser::rAsmFile(std::vector<Node *> &ch) {
  while (!atEnd()) {
    Node *stmt = buildNode(PegRule::Statement, &Parser::rStatement);
    if (!stmt) break;
    ch.push_back(stmt);
  }
  return atEnd();
}

// Statement <- WS? (Label / ((GlobalDirective / LocationDirective /
//              LabelContainingDirective / ZeroDirective / Instruction /
//              Directive / Comment) WS? ((Comment? '\n') / ';')))
bool Parser::rStatement(std::vector<Node *> &ch) {
  // WS?
  {
    Node *ws = buildNode(PegRule::WS, &Parser::rWS);
    if (ws) ch.push_back(ws);
  }

  // Try Label
  {
    SavePoint sp = save();
    size_t chSize = ch.size();
    Node *lbl = buildNode(PegRule::Label, &Parser::rLabel);
    if (lbl) {
      ch.push_back(lbl);
      return true;
    }
    restore(sp);
    ch.resize(chSize);
  }

  // Try directive/instruction/comment alternatives
  {
    bool matched = false;

    // GlobalDirective
    if (!matched) {
      Node *n = buildNode(PegRule::GlobalDirective, &Parser::rGlobalDirective);
      if (n) { ch.push_back(n); matched = true; }
    }
    // LocationDirective
    if (!matched) {
      Node *n = buildNode(PegRule::LocationDirective, &Parser::rLocationDirective);
      if (n) { ch.push_back(n); matched = true; }
    }
    // LabelContainingDirective
    if (!matched) {
      Node *n = buildNode(PegRule::LabelContainingDirective, &Parser::rLabelContainingDirective);
      if (n) { ch.push_back(n); matched = true; }
    }
    // ZeroDirective
    if (!matched) {
      Node *n = buildNode(PegRule::ZeroDirective, &Parser::rZeroDirective);
      if (n) { ch.push_back(n); matched = true; }
    }
    // Instruction
    if (!matched) {
      Node *n = buildNode(PegRule::Instruction, &Parser::rInstruction);
      if (n) { ch.push_back(n); matched = true; }
    }
    // Directive
    if (!matched) {
      Node *n = buildNode(PegRule::Directive, &Parser::rDirective);
      if (n) { ch.push_back(n); matched = true; }
    }
    // Comment
    if (!matched) {
      Node *n = buildNode(PegRule::Comment, &Parser::rComment);
      if (n) { ch.push_back(n); matched = true; }
    }

    // The PEG grammar has an empty alternative after Comment, so
    // a Statement can be just "WS? WS? ((Comment? '\n') / ';')"
    // i.e. empty/whitespace-only lines are valid statements.
    // If !matched, we still try the trailing part.
  }

  // WS?
  {
    Node *ws = buildNode(PegRule::WS, &Parser::rWS);
    if (ws) ch.push_back(ws);
  }

  // ((Comment? '\n') / ';')
  {
    SavePoint sp = save();
    size_t chSize = ch.size();
    // Try Comment? '\n'
    {
      Node *cmt = buildNode(PegRule::Comment, &Parser::rComment);
      if (cmt) ch.push_back(cmt);
    }
    if (matchChar('\n')) return true;

    // Backtrack
    restore(sp);
    ch.resize(chSize);

    // Try ';'
    if (matchChar(';')) return true;

    return false;
  }
}

// GlobalDirective <- (".global" / ".globl") WS SymbolName
bool Parser::rGlobalDirective(std::vector<Node *> &ch) {
  if (!matchStr(".global") && !matchStr(".globl")) return false;
  Node *ws = buildNode(PegRule::WS, &Parser::rWS);
  if (!ws) return false;
  ch.push_back(ws);
  Node *sym = buildNode(PegRule::SymbolName, &Parser::rSymbolName);
  if (!sym) return false;
  ch.push_back(sym);
  return true;
}

// Directive <- '.' DirectiveName (WS Args)?
bool Parser::rDirective(std::vector<Node *> &ch) {
  if (!matchChar('.')) return false;
  Node *dn = buildNode(PegRule::DirectiveName, &Parser::rDirectiveName);
  if (!dn) return false;
  ch.push_back(dn);
  // (WS Args)?
  SavePoint sp = save();
  Node *ws = buildNode(PegRule::WS, &Parser::rWS);
  if (ws) {
    Node *args = buildNode(PegRule::Args, &Parser::rArgs);
    if (args) {
      ch.push_back(ws);
      ch.push_back(args);
    } else {
      restore(sp);
    }
  }
  return true;
}

// DirectiveName <- [[A-Z0-9_]]+
bool Parser::rDirectiveName(std::vector<Node *> &ch) {
  if (pos_ >= len_) return false;
  char c = buf_[pos_];
  if (!isCIAlpha(c) && !isDigit(c) && c != '_') return false;
  pos_++;
  while (pos_ < len_) {
    c = buf_[pos_];
    if (!isCIAlpha(c) && !isDigit(c) && c != '_') break;
    pos_++;
  }
  return true;
}

// LocationDirective <- FileDirective / LocDirective
bool Parser::rLocationDirective(std::vector<Node *> &ch) {
  Node *n = buildNode(PegRule::FileDirective, &Parser::rFileDirective);
  if (n) { ch.push_back(n); return true; }
  n = buildNode(PegRule::LocDirective, &Parser::rLocDirective);
  if (n) { ch.push_back(n); return true; }
  return false;
}

// ZeroDirective <- ".zero" WS [^#\n]+
bool Parser::rZeroDirective(std::vector<Node *> &ch) {
  if (!matchStr(".zero")) return false;
  // WS
  if (pos_ >= len_ || (buf_[pos_] != ' ' && buf_[pos_] != '\t')) return false;
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
  // [^#\n]+
  size_t start = pos_;
  while (pos_ < len_ && buf_[pos_] != '#' && buf_[pos_] != '\n') pos_++;
  return pos_ > start;
}

// FileDirective <- ".file" WS [^#\n]+
bool Parser::rFileDirective(std::vector<Node *> &ch) {
  if (!matchStr(".file")) return false;
  if (pos_ >= len_ || (buf_[pos_] != ' ' && buf_[pos_] != '\t')) return false;
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
  size_t start = pos_;
  while (pos_ < len_ && buf_[pos_] != '#' && buf_[pos_] != '\n') pos_++;
  return pos_ > start;
}

// LocDirective <- ".loc" WS [^#/\n]+
bool Parser::rLocDirective(std::vector<Node *> &ch) {
  if (!matchStr(".loc")) return false;
  if (pos_ >= len_ || (buf_[pos_] != ' ' && buf_[pos_] != '\t')) return false;
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
  size_t start = pos_;
  while (pos_ < len_ && buf_[pos_] != '#' && buf_[pos_] != '/' && buf_[pos_] != '\n') pos_++;
  return pos_ > start;
}

// Args <- Arg ((WS? ',' WS?) Arg)*
bool Parser::rArgs(std::vector<Node *> &ch) {
  Node *arg = buildNode(PegRule::Arg, &Parser::rArg);
  if (!arg) return false;
  ch.push_back(arg);
  while (true) {
    SavePoint sp = save();
    // WS? ',' WS?
    while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
    if (!matchChar(',')) { restore(sp); break; }
    while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
    Node *a = buildNode(PegRule::Arg, &Parser::rArg);
    if (!a) { restore(sp); break; }
    ch.push_back(a);
  }
  return true;
}

// Arg <- QuotedArg / [[0-9a-z%+\-*_@.]]*
bool Parser::rArg(std::vector<Node *> &ch) {
  Node *qa = buildNode(PegRule::QuotedArg, &Parser::rQuotedArg);
  if (qa) { ch.push_back(qa); return true; }
  // [[0-9a-z%+\-*_@.]]* — this can match zero chars
  while (pos_ < len_) {
    char c = buf_[pos_];
    if (isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        c == '%' || c == '+' || c == '-' || c == '*' || c == '_' ||
        c == '@' || c == '.') {
      pos_++;
    } else {
      break;
    }
  }
  return true;  // zero-or-more always succeeds
}

// QuotedArg <- '"' QuotedText '"'
bool Parser::rQuotedArg(std::vector<Node *> &ch) {
  if (!matchChar('"')) return false;
  Node *qt = buildNode(PegRule::QuotedText, &Parser::rQuotedText);
  if (qt) ch.push_back(qt);
  // QuotedText can be empty, so qt might be empty but that's okay
  // Actually rQuotedText always succeeds (zero-or-more), so we need to handle
  // the case where it creates a node with empty content.
  if (!qt) {
    // Should not happen since QuotedText is zero-or-more
    return false;
  }
  if (!matchChar('"')) return false;
  return true;
}

// QuotedText <- (EscapedChar / [^"])*
bool Parser::rQuotedText(std::vector<Node *> &ch) {
  while (pos_ < len_) {
    if (buf_[pos_] == '\\' && pos_ + 1 < len_) {
      pos_ += 2;  // EscapedChar
    } else if (buf_[pos_] != '"') {
      pos_++;
    } else {
      break;
    }
  }
  return true;
}

// LabelContainingDirective <- LabelContainingDirectiveName WS SymbolArgs
bool Parser::rLabelContainingDirective(std::vector<Node *> &ch) {
  Node *name = buildNode(PegRule::LabelContainingDirectiveName,
                         &Parser::rLabelContainingDirectiveName);
  if (!name) return false;
  ch.push_back(name);
  Node *ws = buildNode(PegRule::WS, &Parser::rWS);
  if (!ws) return false;
  ch.push_back(ws);
  Node *args = buildNode(PegRule::SymbolArgs, &Parser::rSymbolArgs);
  if (!args) return false;
  ch.push_back(args);
  return true;
}

// LabelContainingDirectiveName <- ".xword" / ".word" / ".hword" / ".long" /
//   ".set" / ".byte" / ".8byte" / ".4byte" / ".quad" / ".tc" /
//   ".localentry" / ".size" / ".type" / ".uleb128" / ".sleb128"
bool Parser::rLabelContainingDirectiveName(std::vector<Node *> &ch) {
  // Try longest matches first to avoid prefix matching issues
  static const char *names[] = {
      ".localentry", ".uleb128", ".sleb128", ".xword", ".hword",
      ".8byte",      ".4byte",   ".word",    ".long",  ".byte",
      ".quad",       ".size",    ".type",    ".set",   ".tc",
  };
  for (const char *n : names) {
    SavePoint sp = save();
    if (matchStr(n)) {
      // For ".tc", make sure it's not a prefix of something else like ".text"
      // Actually per the PEG, these are tried in order and the first match wins.
      // We need to check that the match is not a prefix of a longer directive.
      // But the PEG is ordered choice, so we need to respect the original order.
      return true;
    }
    restore(sp);
  }
  return false;
}

// SymbolArgs <- SymbolArg ((WS? ',' WS?) SymbolArg)*
bool Parser::rSymbolArgs(std::vector<Node *> &ch) {
  Node *arg = buildNode(PegRule::SymbolArg, &Parser::rSymbolArg);
  if (!arg) return false;
  ch.push_back(arg);
  while (true) {
    SavePoint sp = save();
    while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
    if (!matchChar(',')) { restore(sp); break; }
    while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
    Node *a = buildNode(PegRule::SymbolArg, &Parser::rSymbolArg);
    if (!a) { restore(sp); break; }
    ch.push_back(a);
  }
  return true;
}

// SymbolArg <- SymbolExpr
bool Parser::rSymbolArg(std::vector<Node *> &ch) {
  Node *n = buildNode(PegRule::SymbolExpr, &Parser::rSymbolExpr);
  if (!n) return false;
  ch.push_back(n);
  return true;
}

// SymbolExpr <- SymbolAtom (WS? SymbolOperator WS? SymbolExpr)?
bool Parser::rSymbolExpr(std::vector<Node *> &ch) {
  Node *atom = buildNode(PegRule::SymbolAtom, &Parser::rSymbolAtom);
  if (!atom) return false;
  ch.push_back(atom);
  // (WS? SymbolOperator WS? SymbolExpr)?
  SavePoint sp = save();
  size_t chSize = ch.size();
  // WS?
  Node *ws1 = buildNode(PegRule::WS, &Parser::rWS);
  if (ws1) ch.push_back(ws1);
  Node *op = buildNode(PegRule::SymbolOperator, &Parser::rSymbolOperator);
  if (op) {
    ch.push_back(op);
    Node *ws2 = buildNode(PegRule::WS, &Parser::rWS);
    if (ws2) ch.push_back(ws2);
    Node *expr = buildNode(PegRule::SymbolExpr, &Parser::rSymbolExpr);
    if (expr) {
      ch.push_back(expr);
    } else {
      restore(sp);
      ch.resize(chSize);
    }
  } else {
    restore(sp);
    ch.resize(chSize);
  }
  return true;
}

// SymbolAtom <- Offset / SymbolType / LocalLabelRef / LocalSymbol TCMarker? /
//               SymbolName Offset / SymbolName TCMarker? / Dot /
//               OpenParen WS? SymbolExpr WS? CloseParen
bool Parser::rSymbolAtom(std::vector<Node *> &ch) {
  // Try LocalLabelRef before Offset, because Offset would greedily match
  // the numeric prefix of a LocalLabelRef (e.g., matching "2" from "2f").
  {
    Node *n = buildNode(PegRule::LocalLabelRef, &Parser::rLocalLabelRef);
    if (n) { ch.push_back(n); return true; }
  }
  // Try Offset
  {
    Node *n = buildNode(PegRule::Offset, &Parser::rOffset);
    if (n) { ch.push_back(n); return true; }
  }
  // Try SymbolType
  {
    Node *n = buildNode(PegRule::SymbolType, &Parser::rSymbolType);
    if (n) { ch.push_back(n); return true; }
  }
  // Try LocalSymbol TCMarker?
  {
    SavePoint sp = save();
    Node *ls = buildNode(PegRule::LocalSymbol, &Parser::rLocalSymbol);
    if (ls) {
      ch.push_back(ls);
      Node *tc = buildNode(PegRule::TCMarker, &Parser::rTCMarker);
      if (tc) ch.push_back(tc);
      return true;
    }
    restore(sp);
  }
  // Try SymbolName Offset (must try before SymbolName TCMarker?)
  {
    SavePoint sp = save();
    Node *sn = buildNode(PegRule::SymbolName, &Parser::rSymbolName);
    if (sn) {
      Node *off = buildNode(PegRule::Offset, &Parser::rOffset);
      if (off) {
        ch.push_back(sn);
        ch.push_back(off);
        return true;
      }
    }
    restore(sp);
  }
  // Try SymbolName TCMarker?
  {
    SavePoint sp = save();
    Node *sn = buildNode(PegRule::SymbolName, &Parser::rSymbolName);
    if (sn) {
      ch.push_back(sn);
      Node *tc = buildNode(PegRule::TCMarker, &Parser::rTCMarker);
      if (tc) ch.push_back(tc);
      return true;
    }
    restore(sp);
  }
  // Try Dot
  {
    if (pos_ < len_ && buf_[pos_] == '.') {
      Node *dot = allocNode(PegRule::Dot, (uint32_t)pos_);
      pos_++;
      dot->end = (uint32_t)pos_;
      ch.push_back(dot);
      return true;
    }
  }
  // Try OpenParen WS? SymbolExpr WS? CloseParen
  {
    SavePoint sp = save();
    if (matchChar('(')) {
      Node *op = allocNode(PegRule::OpenParen, (uint32_t)(pos_ - 1));
      op->end = (uint32_t)pos_;
      ch.push_back(op);
      Node *ws1 = buildNode(PegRule::WS, &Parser::rWS);
      if (ws1) ch.push_back(ws1);
      Node *expr = buildNode(PegRule::SymbolExpr, &Parser::rSymbolExpr);
      if (expr) {
        ch.push_back(expr);
        Node *ws2 = buildNode(PegRule::WS, &Parser::rWS);
        if (ws2) ch.push_back(ws2);
        if (matchChar(')')) {
          Node *cp = allocNode(PegRule::CloseParen, (uint32_t)(pos_ - 1));
          cp->end = (uint32_t)pos_;
          ch.push_back(cp);
          return true;
        }
      }
    }
    restore(sp);
    // Remove any children we may have added
    // Since we're using a vector we can just check sizes
  }
  return false;
}

// SymbolOperator <- '+' / '-' / '|' / '<<' / '>>' / '/'
bool Parser::rSymbolOperator(std::vector<Node *> &ch) {
  if (matchStr("<<")) return true;
  if (matchStr(">>")) return true;
  if (pos_ < len_ && (buf_[pos_] == '+' || buf_[pos_] == '-' ||
                       buf_[pos_] == '|' || buf_[pos_] == '/')) {
    pos_++;
    return true;
  }
  return false;
}

// SymbolType <- [@%] ('function' / 'object')
bool Parser::rSymbolType(std::vector<Node *> &ch) {
  if (pos_ >= len_ || (buf_[pos_] != '@' && buf_[pos_] != '%')) return false;
  pos_++;
  if (matchStr("function")) return true;
  if (matchStr("object")) return true;
  pos_--;
  return false;
}

// Dot <- '.'
bool Parser::rDot(std::vector<Node *> &ch) {
  return matchChar('.');
}

// TCMarker <- '[TC]'
bool Parser::rTCMarker(std::vector<Node *> &ch) {
  return matchStr("[TC]");
}

// WS <- [ \t]+
bool Parser::rWS(std::vector<Node *> &ch) {
  if (pos_ >= len_ || (buf_[pos_] != ' ' && buf_[pos_] != '\t'))
    return false;
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
  return true;
}

// Comment <- ("//" / '#') [^\n]*
bool Parser::rComment(std::vector<Node *> &ch) {
  if (matchStr("//") || matchChar('#')) {
    while (pos_ < len_ && buf_[pos_] != '\n') pos_++;
    return true;
  }
  return false;
}

// Label <- (LocalSymbol / LocalLabel / SymbolName) ':'
bool Parser::rLabel(std::vector<Node *> &ch) {
  Node *n = nullptr;
  // Try LocalSymbol
  n = buildNode(PegRule::LocalSymbol, &Parser::rLocalSymbol);
  if (!n) {
    // Try LocalLabel
    n = buildNode(PegRule::LocalLabel, &Parser::rLocalLabel);
  }
  if (!n) {
    // Try SymbolName
    n = buildNode(PegRule::SymbolName, &Parser::rSymbolName);
  }
  if (!n) return false;
  if (!matchChar(':')) return false;
  ch.push_back(n);
  return true;
}

// SymbolName <- [[A-Z._]][[A-Z.0-9$_]]*
bool Parser::rSymbolName(std::vector<Node *> &ch) {
  if (pos_ >= len_) return false;
  char c = buf_[pos_];
  // [[A-Z._]] = case-insensitive [A-Za-z._]
  if (!isCIAlpha(c) && c != '.' && c != '_') return false;
  pos_++;
  // [[A-Z.0-9$_]]* = case-insensitive [A-Za-z.0-9$_]*
  while (pos_ < len_) {
    c = buf_[pos_];
    if (!isCIAlpha(c) && c != '.' && !isDigit(c) && c != '$' && c != '_')
      break;
    pos_++;
  }
  return true;
}

// LocalSymbol <- '.L' [[A-Za-z.0-9$_]]+
// Note: the PEG uses [[A-Za-z.0-9$_]] which is case-insensitive
bool Parser::rLocalSymbol(std::vector<Node *> &ch) {
  if (!matchStr(".L")) return false;
  if (pos_ >= len_) return false;
  char c = buf_[pos_];
  if (!isCIAlpha(c) && c != '.' && !isDigit(c) && c != '$' && c != '_')
    return false;
  pos_++;
  while (pos_ < len_) {
    c = buf_[pos_];
    if (!isCIAlpha(c) && c != '.' && !isDigit(c) && c != '$' && c != '_')
      break;
    pos_++;
  }
  return true;
}

// LocalLabel <- [0-9][0-9$]*
bool Parser::rLocalLabel(std::vector<Node *> &ch) {
  if (pos_ >= len_ || !isDigit(buf_[pos_])) return false;
  pos_++;
  while (pos_ < len_ && (isDigit(buf_[pos_]) || buf_[pos_] == '$')) pos_++;
  return true;
}

// LocalLabelRef <- [0-9][0-9$]*[bf]
bool Parser::rLocalLabelRef(std::vector<Node *> &ch) {
  if (pos_ >= len_ || !isDigit(buf_[pos_])) return false;
  size_t start = pos_;
  pos_++;
  while (pos_ < len_ && (isDigit(buf_[pos_]) || buf_[pos_] == '$')) pos_++;
  if (pos_ >= len_ || (buf_[pos_] != 'b' && buf_[pos_] != 'f')) {
    pos_ = start;
    return false;
  }
  pos_++;
  return true;
}

// Instruction <- InstructionName (WS InstructionArg ((WS? ','? WS?) InstructionArg)*)?
bool Parser::rInstruction(std::vector<Node *> &ch) {
  Node *name = buildNode(PegRule::InstructionName, &Parser::rInstructionName);
  if (!name) return false;
  ch.push_back(name);
  // (WS InstructionArg ...)?
  SavePoint sp = save();
  Node *ws = buildNode(PegRule::WS, &Parser::rWS);
  if (ws) {
    Node *arg = buildNode(PegRule::InstructionArg, &Parser::rInstructionArg);
    if (arg) {
      ch.push_back(ws);
      ch.push_back(arg);
      // ((WS? ','? WS?) InstructionArg)*
      while (true) {
        SavePoint sp2 = save();
        size_t chSize = ch.size();
        // WS?
        while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
          pos_++;
        // ','?
        matchChar(',');
        // WS?
        while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
          pos_++;
        Node *a2 = buildNode(PegRule::InstructionArg, &Parser::rInstructionArg);
        if (!a2) {
          restore(sp2);
          ch.resize(chSize);
          break;
        }
        ch.push_back(a2);
      }
    } else {
      restore(sp);
    }
  }
  return true;
}

// InstructionName <- [[A-Z]][[A-Z.0-9]]* [.+\-]?
bool Parser::rInstructionName(std::vector<Node *> &ch) {
  if (pos_ >= len_ || !isCIAlpha(buf_[pos_])) return false;
  pos_++;
  while (pos_ < len_ && (isCIAlpha(buf_[pos_]) || buf_[pos_] == '.' ||
                          isDigit(buf_[pos_])))
    pos_++;
  // [.+\-]?
  if (pos_ < len_ && (buf_[pos_] == '.' || buf_[pos_] == '+' ||
                       buf_[pos_] == '-'))
    pos_++;
  return true;
}

// InstructionArg <- IndirectionIndicator? (ARMConstantTweak / RegisterOrConstant /
//   LocalLabelRef / TOCRefHigh / TOCRefLow / GOTLocation / GOTSymbolOffset /
//   MemoryRef / AVX512Token)
bool Parser::rInstructionArg(std::vector<Node *> &ch) {
  // IndirectionIndicator?
  if (pos_ < len_ && buf_[pos_] == '*') {
    Node *ind = allocNode(PegRule::IndirectionIndicator, (uint32_t)pos_);
    pos_++;
    ind->end = (uint32_t)pos_;
    ch.push_back(ind);
  }

  // Try alternatives in order
  Node *n;

  n = buildNode(PegRule::ARMConstantTweak, &Parser::rARMConstantTweak);
  if (n) { ch.push_back(n); return true; }

  n = buildNode(PegRule::RegisterOrConstant, &Parser::rRegisterOrConstant);
  if (n) { ch.push_back(n); return true; }

  n = buildNode(PegRule::LocalLabelRef, &Parser::rLocalLabelRef);
  if (n) { ch.push_back(n); return true; }

  n = buildNode(PegRule::TOCRefHigh, &Parser::rTOCRefHigh);
  if (n) { ch.push_back(n); return true; }

  n = buildNode(PegRule::TOCRefLow, &Parser::rTOCRefLow);
  if (n) { ch.push_back(n); return true; }

  n = buildNode(PegRule::GOTLocation, &Parser::rGOTLocation);
  if (n) { ch.push_back(n); return true; }

  n = buildNode(PegRule::GOTSymbolOffset, &Parser::rGOTSymbolOffset);
  if (n) { ch.push_back(n); return true; }

  n = buildNode(PegRule::MemoryRef, &Parser::rMemoryRef);
  if (n) { ch.push_back(n); return true; }

  n = buildNode(PegRule::AVX512Token, &Parser::rAVX512Token);
  if (n) { ch.push_back(n); return true; }

  return false;
}

// GOTLocation <- '$_GLOBAL_OFFSET_TABLE_-' LocalSymbol
bool Parser::rGOTLocation(std::vector<Node *> &ch) {
  if (!matchStr("$_GLOBAL_OFFSET_TABLE_-")) return false;
  Node *ls = buildNode(PegRule::LocalSymbol, &Parser::rLocalSymbol);
  if (!ls) return false;
  ch.push_back(ls);
  return true;
}

// GOTSymbolOffset <- ('$' SymbolName '@GOT' 'OFF'?) / (":got:" SymbolName)
bool Parser::rGOTSymbolOffset(std::vector<Node *> &ch) {
  {
    SavePoint sp = save();
    if (matchChar('$')) {
      Node *sn = buildNode(PegRule::SymbolName, &Parser::rSymbolName);
      if (sn && matchStr("@GOT")) {
        ch.push_back(sn);
        matchStr("OFF");  // optional
        return true;
      }
    }
    restore(sp);
  }
  {
    SavePoint sp = save();
    if (matchStr(":got:")) {
      Node *sn = buildNode(PegRule::SymbolName, &Parser::rSymbolName);
      if (sn) {
        ch.push_back(sn);
        return true;
      }
    }
    restore(sp);
  }
  return false;
}

// AVX512Token <- WS? '{' '%'? [0-9a-z]* '}'
bool Parser::rAVX512Token(std::vector<Node *> &ch) {
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
  if (!matchChar('{')) return false;
  matchChar('%');
  while (pos_ < len_ && (isDigit(buf_[pos_]) ||
                          (buf_[pos_] >= 'a' && buf_[pos_] <= 'z')))
    pos_++;
  if (!matchChar('}')) return false;
  return true;
}

// TOCRefHigh <- '.TOC.-' ('0b' / ('.L' [a-zA-Z_0-9]+)) "@ha"
bool Parser::rTOCRefHigh(std::vector<Node *> &ch) {
  if (!matchStr(".TOC.-")) return false;
  if (matchStr("0b")) {
    // ok
  } else if (matchStr(".L")) {
    if (pos_ >= len_) return false;
    char c = buf_[pos_];
    if (!isCIAlpha(c) && c != '_' && !isDigit(c)) return false;
    pos_++;
    while (pos_ < len_) {
      c = buf_[pos_];
      if (!isCIAlpha(c) && c != '_' && !isDigit(c)) break;
      pos_++;
    }
  } else {
    return false;
  }
  if (!matchStr("@ha")) return false;
  return true;
}

// TOCRefLow <- '.TOC.-' ('0b' / ('.L' [a-zA-Z_0-9]+)) "@l"
bool Parser::rTOCRefLow(std::vector<Node *> &ch) {
  if (!matchStr(".TOC.-")) return false;
  if (matchStr("0b")) {
    // ok
  } else if (matchStr(".L")) {
    if (pos_ >= len_) return false;
    char c = buf_[pos_];
    if (!isCIAlpha(c) && c != '_' && !isDigit(c)) return false;
    pos_++;
    while (pos_ < len_) {
      c = buf_[pos_];
      if (!isCIAlpha(c) && c != '_' && !isDigit(c)) break;
      pos_++;
    }
  } else {
    return false;
  }
  if (!matchStr("@l")) return false;
  return true;
}

// RegisterOrConstant <- (('%'[[A-Z]][[A-Z0-9]]*) /
//   ('$' [0-9]+ WS? '*' WS? '(' [0-9]+ WS? '-' WS? [0-9]+ ')' ) /
//   ('$'? ((Offset Offset) / Offset)) /
//   ('#' '-'? [0-9]+ '.' [0-9]+ ([eE] [+\-]? [0-9]+)? ) /
//   ('#' Offset ('*' [0-9]+ ('-' [0-9] [0-9]*)?)? ) /
//   ('#' '~'? '(' [0-9] WS? "<<" WS? [0-9] [0-9]? ')' ) /
//   (('#' / '$') '~'? '0x'? [[0-9A-F]]+ ) /
//   ('$(-' [0-9]+ ')') /
//   ('#(' [0-9]+ ')') /
//   ARMRegister)
//   ![fb:(+\-]
bool Parser::rRegisterOrConstant(std::vector<Node *> &ch) {
  SavePoint startSp = save();

  // Try '%' register
  {
    SavePoint sp = save();
    if (matchChar('%') && pos_ < len_ && isCIAlpha(buf_[pos_])) {
      pos_++;
      while (pos_ < len_ && isCIAlphaNum(buf_[pos_])) pos_++;
      goto check_trailing;
    }
    restore(sp);
  }

  // Try '$' [0-9]+ WS? '*' WS? '(' [0-9]+ WS? '-' WS? [0-9]+ ')'
  {
    SavePoint sp = save();
    if (matchChar('$') && pos_ < len_ && isDigit(buf_[pos_])) {
      while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
      while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
      if (matchChar('*')) {
        while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
          pos_++;
        if (matchChar('(')) {
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
              pos_++;
            if (matchChar('-')) {
              while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
                pos_++;
              if (pos_ < len_ && isDigit(buf_[pos_])) {
                while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
                if (matchChar(')')) goto check_trailing;
              }
            }
          }
        }
      }
    }
    restore(sp);
  }

  // Try '$'? ((Offset Offset) / Offset)
  {
    SavePoint sp = save();
    matchChar('$');  // optional
    // Try Offset Offset
    {
      SavePoint sp2 = save();
      std::vector<Node *> dummy;
      if (rOffset(dummy)) {
        std::vector<Node *> dummy2;
        if (rOffset(dummy2)) goto check_trailing;
      }
      restore(sp2);
    }
    // Try single Offset
    {
      std::vector<Node *> dummy;
      if (rOffset(dummy)) goto check_trailing;
    }
    restore(sp);
  }

  // Try '#' '-'? [0-9]+ '.' [0-9]+ ([eE] [+\-]? [0-9]+)?
  {
    SavePoint sp = save();
    if (matchChar('#')) {
      matchChar('-');
      if (pos_ < len_ && isDigit(buf_[pos_])) {
        while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
        if (matchChar('.')) {
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            // ([eE] [+\-]? [0-9]+)?
            if (pos_ < len_ && (buf_[pos_] == 'e' || buf_[pos_] == 'E')) {
              pos_++;
              if (pos_ < len_ && (buf_[pos_] == '+' || buf_[pos_] == '-'))
                pos_++;
              if (pos_ < len_ && isDigit(buf_[pos_])) {
                while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
              }
            }
            goto check_trailing;
          }
        }
      }
    }
    restore(sp);
  }

  // Try '#' Offset ('*' [0-9]+ ('-' [0-9] [0-9]*)?)?
  {
    SavePoint sp = save();
    if (matchChar('#')) {
      std::vector<Node *> dummy;
      if (rOffset(dummy)) {
        // ('*' [0-9]+ ('-' [0-9] [0-9]*)?)?
        SavePoint sp2 = save();
        if (matchChar('*') && pos_ < len_ && isDigit(buf_[pos_])) {
          while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
          // ('-' [0-9] [0-9]*)?
          SavePoint sp3 = save();
          if (matchChar('-') && pos_ < len_ && isDigit(buf_[pos_])) {
            pos_++;
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
          } else {
            restore(sp3);
          }
        } else {
          restore(sp2);
        }
        goto check_trailing;
      }
    }
    restore(sp);
  }

  // Try '#' '~'? '(' [0-9] WS? "<<" WS? [0-9] [0-9]? ')'
  {
    SavePoint sp = save();
    if (matchChar('#')) {
      matchChar('~');
      if (matchChar('(') && pos_ < len_ && isDigit(buf_[pos_])) {
        pos_++;
        while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
          pos_++;
        if (matchStr("<<")) {
          while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
            pos_++;
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            pos_++;
            if (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            if (matchChar(')')) goto check_trailing;
          }
        }
      }
    }
    restore(sp);
  }

  // Try ('#' / '$') '~'? '0x'? [[0-9A-F]]+
  {
    SavePoint sp = save();
    if (matchChar('#') || matchChar('$')) {
      matchChar('~');
      matchStr("0x");
      if (pos_ < len_ && isCIHex(buf_[pos_])) {
        while (pos_ < len_ && isCIHex(buf_[pos_])) pos_++;
        goto check_trailing;
      }
    }
    restore(sp);
  }

  // Try '$(-' [0-9]+ ')'
  {
    SavePoint sp = save();
    if (matchStr("$(-")) {
      if (pos_ < len_ && isDigit(buf_[pos_])) {
        while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
        if (matchChar(')')) goto check_trailing;
      }
    }
    restore(sp);
  }

  // Try '#(' [0-9]+ ')'
  {
    SavePoint sp = save();
    if (matchStr("#(")) {
      if (pos_ < len_ && isDigit(buf_[pos_])) {
        while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
        if (matchChar(')')) goto check_trailing;
      }
    }
    restore(sp);
  }

  // Try ARMRegister
  {
    Node *arm = buildNode(PegRule::ARMRegister, &Parser::rARMRegister);
    if (arm) {
      ch.push_back(arm);
      goto check_trailing;
    }
  }

  restore(startSp);
  return false;

check_trailing:
  // ![fb:(+\-]
  if (pos_ < len_) {
    char c = buf_[pos_];
    if (c == 'f' || c == 'b' || c == ':' || c == '(' || c == '+' ||
        c == '-') {
      restore(startSp);
      return false;
    }
  }
  return true;
}

// ARMConstantTweak <- ((([us] "xt" [xwhb]) / ("lsl" / "lsr" / "ror" / "rol" / "asr" / "asl" / "msl") ![A-Za-z0-9_]) (WS '#'? Offset)?) /
//                     "mul vl"
bool Parser::rARMConstantTweak(std::vector<Node *> &ch) {
  if (matchStr("mul vl")) return true;

  SavePoint sp = save();
  bool matched = false;
  // [us] "xt" [xwhb]
  if (pos_ < len_ && (buf_[pos_] == 'u' || buf_[pos_] == 's')) {
    pos_++;
    if (matchStr("xt") && pos_ < len_ &&
        (buf_[pos_] == 'x' || buf_[pos_] == 'w' || buf_[pos_] == 'h' ||
         buf_[pos_] == 'b')) {
      pos_++;
      matched = true;
    } else {
      restore(sp);
    }
  }

  if (!matched) {
    static const char *shifts[] = {"lsl", "lsr", "ror", "rol",
                                    "asr", "asl", "msl"};
    for (const char *s : shifts) {
      SavePoint sp2 = save();
      if (matchStr(s)) {
        // ![A-Za-z0-9_]
        if (pos_ < len_ && (isCIAlpha(buf_[pos_]) || isDigit(buf_[pos_]) ||
                             buf_[pos_] == '_')) {
          restore(sp2);
          continue;
        }
        matched = true;
        break;
      }
      restore(sp2);
    }
  }

  if (!matched) {
    restore(sp);
    return false;
  }

  // (WS '#'? Offset)?
  {
    SavePoint sp2 = save();
    std::vector<Node *> dummy;
    if (rWS(dummy)) {
      matchChar('#');
      std::vector<Node *> dummy2;
      if (!rOffset(dummy2)) {
        restore(sp2);
      }
    }
  }
  return true;
}

// ARMRegister <- "sp" / ([xwdqshb] [0-9] [0-9]? !(ARMRegisterBoundary)) / "xzr" / "wzr" / "NZCV" / ARMVectorRegister / SVE2PredicateRegister /
//  ('{' WS? ARMVectorRegister WS? ([,\-] WS? ARMVectorRegister)* WS? '}' ('[' [0-9] [0-9]? ']')? )
bool Parser::rARMRegister(std::vector<Node *> &ch) {
  // "sp"
  {
    SavePoint sp = save();
    if (matchStr("sp")) {
      // Make sure it's not followed by alphanumeric (else it's a symbol)
      if (pos_ >= len_ || (!isCIAlpha(buf_[pos_]) && !isDigit(buf_[pos_]) &&
                            buf_[pos_] != '_'))
        return true;
      restore(sp);
    }
  }

  // [xwdqshb] [0-9] [0-9]? !(ARMRegisterBoundary)
  {
    SavePoint sp = save();
    if (pos_ < len_ && (buf_[pos_] == 'x' || buf_[pos_] == 'w' ||
                         buf_[pos_] == 'd' || buf_[pos_] == 'q' ||
                         buf_[pos_] == 's' || buf_[pos_] == 'h' ||
                         buf_[pos_] == 'b')) {
      pos_++;
      if (pos_ < len_ && isDigit(buf_[pos_])) {
        pos_++;
        if (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
        // !(ARMRegisterBoundary) = ![a-zA-Z0-9_]
        if (pos_ >= len_ || (!isCIAlpha(buf_[pos_]) && !isDigit(buf_[pos_]) &&
                              buf_[pos_] != '_'))
          return true;
      }
    }
    restore(sp);
  }

  if (matchStr("xzr")) return true;
  if (matchStr("wzr")) return true;
  if (matchStr("NZCV")) return true;

  // ARMVectorRegister
  {
    Node *n = buildNode(PegRule::ARMVectorRegister, &Parser::rARMVectorRegister);
    if (n) { ch.push_back(n); return true; }
  }

  // SVE2PredicateRegister
  {
    Node *n = buildNode(PegRule::SVE2PredicateRegister, &Parser::rSVE2PredicateRegister);
    if (n) { ch.push_back(n); return true; }
  }

  // '{' WS? ARMVectorRegister WS? ([,\-] WS? ARMVectorRegister)* WS? '}' ('[' [0-9] [0-9]? ']')?
  {
    SavePoint sp = save();
    if (matchChar('{')) {
      while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
      std::vector<Node *> dummy;
      if (rARMVectorRegister(dummy)) {
        while (true) {
          SavePoint sp2 = save();
          while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
            pos_++;
          if (pos_ < len_ && (buf_[pos_] == ',' || buf_[pos_] == '-')) {
            pos_++;
            while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
              pos_++;
            std::vector<Node *> dummy2;
            if (!rARMVectorRegister(dummy2)) {
              restore(sp2);
              break;
            }
          } else {
            restore(sp2);
            break;
          }
        }
        while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
          pos_++;
        if (matchChar('}')) {
          // ('[' [0-9] [0-9]? ']')?
          SavePoint sp2 = save();
          if (matchChar('[') && pos_ < len_ && isDigit(buf_[pos_])) {
            pos_++;
            if (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            if (!matchChar(']')) restore(sp2);
          } else {
            restore(sp2);
          }
          return true;
        }
      }
    }
    restore(sp);
  }

  return false;
}

// ARMVectorRegister <- [vz] [0-9] [0-9]? ('.' [0-9]* [bsdhqBSDHQ] ('[' [0-9] [0-9]? ']')? )?
bool Parser::rARMVectorRegister(std::vector<Node *> &ch) {
  if (pos_ >= len_ || (buf_[pos_] != 'v' && buf_[pos_] != 'z')) return false;
  pos_++;
  if (pos_ >= len_ || !isDigit(buf_[pos_])) return false;
  pos_++;
  if (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
  // ('.' [0-9]* [bsdhqBSDHQ] ...)?
  if (pos_ < len_ && buf_[pos_] == '.') {
    pos_++;
    while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
    if (pos_ < len_) {
      char c = buf_[pos_];
      if (c == 'b' || c == 's' || c == 'd' || c == 'h' || c == 'q' ||
          c == 'B' || c == 'S' || c == 'D' || c == 'H' || c == 'Q') {
        pos_++;
        // ('[' [0-9] [0-9]? ']')?
        SavePoint sp = save();
        if (matchChar('[') && pos_ < len_ && isDigit(buf_[pos_])) {
          pos_++;
          if (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
          if (!matchChar(']')) restore(sp);
        } else {
          restore(sp);
        }
      } else {
        // No valid suffix letter, backtrack the '.'
        pos_--;
      }
    } else {
      pos_--;
    }
  }
  return true;
}

// SVE2PredicateRegister <- "p" [0-9] [0-9]? "/" [mMzZ]
bool Parser::rSVE2PredicateRegister(std::vector<Node *> &ch) {
  SavePoint sp = save();
  if (matchChar('p') && pos_ < len_ && isDigit(buf_[pos_])) {
    pos_++;
    if (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
    if (matchChar('/')) {
      if (pos_ < len_ && (buf_[pos_] == 'm' || buf_[pos_] == 'M' ||
                           buf_[pos_] == 'z' || buf_[pos_] == 'Z')) {
        pos_++;
        return true;
      }
    }
  }
  restore(sp);
  return false;
}

// MemoryRef <- (SymbolRef BaseIndexScale / SymbolRef / Low12BitsSymbolRef /
//   Offset* BaseIndexScale / SegmentRegister Offset BaseIndexScale /
//   SegmentRegister BaseIndexScale / SegmentRegister Offset /
//   ARMBaseIndexScale / BaseIndexScale)
bool Parser::rMemoryRef(std::vector<Node *> &ch) {
  // SymbolRef BaseIndexScale
  {
    SavePoint sp = save();
    Node *sr = buildNode(PegRule::SymbolRef, &Parser::rSymbolRef);
    if (sr) {
      Node *bis = buildNode(PegRule::BaseIndexScale, &Parser::rBaseIndexScale);
      if (bis) {
        ch.push_back(sr);
        ch.push_back(bis);
        return true;
      }
    }
    restore(sp);
  }

  // SymbolRef
  {
    Node *sr = buildNode(PegRule::SymbolRef, &Parser::rSymbolRef);
    if (sr) { ch.push_back(sr); return true; }
  }

  // Low12BitsSymbolRef
  {
    Node *n = buildNode(PegRule::Low12BitsSymbolRef, &Parser::rLow12BitsSymbolRef);
    if (n) { ch.push_back(n); return true; }
  }

  // Offset* BaseIndexScale
  {
    SavePoint sp = save();
    std::vector<Node *> offsets;
    while (true) {
      Node *off = buildNode(PegRule::Offset, &Parser::rOffset);
      if (!off) break;
      offsets.push_back(off);
    }
    Node *bis = buildNode(PegRule::BaseIndexScale, &Parser::rBaseIndexScale);
    if (bis) {
      for (Node *o : offsets) ch.push_back(o);
      ch.push_back(bis);
      return true;
    }
    restore(sp);
  }

  // SegmentRegister Offset BaseIndexScale
  {
    SavePoint sp = save();
    Node *seg = buildNode(PegRule::SegmentRegister, &Parser::rSegmentRegister);
    if (seg) {
      Node *off = buildNode(PegRule::Offset, &Parser::rOffset);
      if (off) {
        Node *bis = buildNode(PegRule::BaseIndexScale, &Parser::rBaseIndexScale);
        if (bis) {
          ch.push_back(seg);
          ch.push_back(off);
          ch.push_back(bis);
          return true;
        }
      }
    }
    restore(sp);
  }

  // SegmentRegister BaseIndexScale
  {
    SavePoint sp = save();
    Node *seg = buildNode(PegRule::SegmentRegister, &Parser::rSegmentRegister);
    if (seg) {
      Node *bis = buildNode(PegRule::BaseIndexScale, &Parser::rBaseIndexScale);
      if (bis) {
        ch.push_back(seg);
        ch.push_back(bis);
        return true;
      }
    }
    restore(sp);
  }

  // SegmentRegister Offset
  {
    SavePoint sp = save();
    Node *seg = buildNode(PegRule::SegmentRegister, &Parser::rSegmentRegister);
    if (seg) {
      Node *off = buildNode(PegRule::Offset, &Parser::rOffset);
      if (off) {
        ch.push_back(seg);
        ch.push_back(off);
        return true;
      }
    }
    restore(sp);
  }

  // ARMBaseIndexScale
  {
    Node *n = buildNode(PegRule::ARMBaseIndexScale, &Parser::rARMBaseIndexScale);
    if (n) { ch.push_back(n); return true; }
  }

  // BaseIndexScale
  {
    Node *n = buildNode(PegRule::BaseIndexScale, &Parser::rBaseIndexScale);
    if (n) { ch.push_back(n); return true; }
  }

  return false;
}

// SymbolRef <- (Offset* '+')? (LocalSymbol / SymbolName) Offset* ('@' Section Offset*)?
bool Parser::rSymbolRef(std::vector<Node *> &ch) {
  // (Offset* '+')?
  {
    SavePoint sp = save();
    while (true) {
      Node *off = buildNode(PegRule::Offset, &Parser::rOffset);
      if (!off) break;
      ch.push_back(off);
    }
    if (!matchChar('+')) {
      restore(sp);
      // Remove any offset children that were added
      ch.clear();
    }
  }

  // (LocalSymbol / SymbolName)
  Node *sym = buildNode(PegRule::LocalSymbol, &Parser::rLocalSymbol);
  if (!sym) sym = buildNode(PegRule::SymbolName, &Parser::rSymbolName);
  if (!sym) return false;
  ch.push_back(sym);

  // Offset*
  while (true) {
    Node *off = buildNode(PegRule::Offset, &Parser::rOffset);
    if (!off) break;
    ch.push_back(off);
  }

  // ('@' Section Offset*)?
  {
    SavePoint sp = save();
    size_t chSize = ch.size();
    if (matchChar('@')) {
      Node *sec = buildNode(PegRule::Section, &Parser::rSection);
      if (sec) {
        ch.push_back(sec);
        while (true) {
          Node *off = buildNode(PegRule::Offset, &Parser::rOffset);
          if (!off) break;
          ch.push_back(off);
        }
      } else {
        restore(sp);
        ch.resize(chSize);
      }
    }
  }

  return true;
}

// Low12BitsSymbolRef <- ":lo12:" (LocalSymbol / SymbolName) Offset?
bool Parser::rLow12BitsSymbolRef(std::vector<Node *> &ch) {
  if (!matchStr(":lo12:")) return false;
  Node *sym = buildNode(PegRule::LocalSymbol, &Parser::rLocalSymbol);
  if (!sym) sym = buildNode(PegRule::SymbolName, &Parser::rSymbolName);
  if (!sym) return false;
  ch.push_back(sym);
  Node *off = buildNode(PegRule::Offset, &Parser::rOffset);
  if (off) ch.push_back(off);
  return true;
}

// ARMBaseIndexScale <- '[' ARMRegister (',' WS? (('#'? Offset ...) / ...) ...)? ']' ARMPostincrement?
bool Parser::rARMBaseIndexScale(std::vector<Node *> &ch) {
  if (!matchChar('[')) return false;
  Node *reg = buildNode(PegRule::ARMRegister, &Parser::rARMRegister);
  if (!reg) return false;
  ch.push_back(reg);

  // Optional second part
  SavePoint sp = save();
  size_t chSize = ch.size();
  if (matchChar(',')) {
    while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;

    bool gotSecondArg = false;

    // Try '#'? Offset ...
    {
      SavePoint sp2 = save();
      matchChar('#');
      // Try ARMGOTLow12
      {
        Node *got = buildNode(PegRule::ARMGOTLow12, &Parser::rARMGOTLow12);
        if (got) {
          ch.push_back(got);
          gotSecondArg = true;
          goto arm_check_tweak;
        }
      }
      restore(sp2);
    }

    // Try '#'? Low12BitsSymbolRef
    {
      SavePoint sp2 = save();
      matchChar('#');
      Node *lo12 = buildNode(PegRule::Low12BitsSymbolRef, &Parser::rLow12BitsSymbolRef);
      if (lo12) {
        ch.push_back(lo12);
        gotSecondArg = true;
        goto arm_check_tweak;
      }
      restore(sp2);
    }

    // Try '#'? Offset with optional multiplier
    {
      SavePoint sp2 = save();
      matchChar('#');
      std::vector<Node *> dummy;
      if (rOffset(dummy)) {
        // ('*' [0-9]+) / ('*' '(' [0-9]+ Operator [0-9]+ ')') / (('+' [0-9]+)*)?
        SavePoint sp3 = save();
        if (matchChar('*')) {
          if (matchChar('(')) {
            // [0-9]+ Operator [0-9]+ ')'
            if (pos_ < len_ && isDigit(buf_[pos_])) {
              while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
              if (pos_ < len_ && (buf_[pos_] == '+' || buf_[pos_] == '-')) {
                pos_++;
                if (pos_ < len_ && isDigit(buf_[pos_])) {
                  while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
                  matchChar(')');
                }
              }
            }
          } else if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
          } else {
            restore(sp3);
          }
        } else {
          // (('+' [0-9]+)*)?
          while (true) {
            SavePoint sp4 = save();
            if (matchChar('+') && pos_ < len_ && isDigit(buf_[pos_])) {
              while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            } else {
              restore(sp4);
              break;
            }
          }
        }
        gotSecondArg = true;
        goto arm_check_tweak;
      }
      restore(sp2);
    }

    // Try ARMRegister
    {
      Node *reg2 = buildNode(PegRule::ARMRegister, &Parser::rARMRegister);
      if (reg2) {
        ch.push_back(reg2);
        gotSecondArg = true;
        goto arm_check_tweak;
      }
    }

    if (!gotSecondArg) {
      restore(sp);
      ch.resize(chSize);
    }

  arm_check_tweak:
    if (gotSecondArg) {
      // (',' WS? ARMConstantTweak)?
      SavePoint sp3 = save();
      if (matchChar(',')) {
        while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
          pos_++;
        std::vector<Node *> dummy;
        if (!rARMConstantTweak(dummy)) {
          restore(sp3);
        }
      } else {
        restore(sp3);
      }
    }
  }

  if (!matchChar(']')) return false;

  // ARMPostincrement?
  matchChar('!');

  return true;
}

// ARMGOTLow12 <- ":got_lo12:" SymbolName
bool Parser::rARMGOTLow12(std::vector<Node *> &ch) {
  if (!matchStr(":got_lo12:")) return false;
  Node *sym = buildNode(PegRule::SymbolName, &Parser::rSymbolName);
  if (!sym) return false;
  ch.push_back(sym);
  return true;
}

// BaseIndexScale <- '(' RegisterOrConstant? WS? (',' WS? RegisterOrConstant WS? (',' [0-9]+)? )? ')'
bool Parser::rBaseIndexScale(std::vector<Node *> &ch) {
  if (!matchChar('(')) return false;

  // RegisterOrConstant?
  {
    Node *rc = buildNode(PegRule::RegisterOrConstant, &Parser::rRegisterOrConstant);
    if (rc) ch.push_back(rc);
  }

  // WS?
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;

  // (',' WS? RegisterOrConstant WS? (',' [0-9]+)? )?
  {
    SavePoint sp = save();
    if (matchChar(',')) {
      while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
      Node *rc = buildNode(PegRule::RegisterOrConstant, &Parser::rRegisterOrConstant);
      if (rc) {
        ch.push_back(rc);
        while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
        // (',' [0-9]+)?
        SavePoint sp2 = save();
        if (matchChar(',')) {
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
          } else {
            restore(sp2);
          }
        }
      } else {
        restore(sp);
      }
    }
  }

  if (!matchChar(')')) return false;
  return true;
}

// OffsetOperator <- '+' / '-' / '*'
bool Parser::rOffsetOperator(std::vector<Node *> &ch) {
  if (pos_ < len_ && (buf_[pos_] == '+' || buf_[pos_] == '-' ||
                       buf_[pos_] == '*')) {
    pos_++;
    return true;
  }
  return false;
}

// S2nBignumHelper <- '(' [0-9]+ WS? OffsetOperator WS? [0-9]+ ')' WS? OffsetOperator? WS?
bool Parser::rS2nBignumHelper(std::vector<Node *> &ch) {
  if (!matchChar('(')) return false;
  if (pos_ >= len_ || !isDigit(buf_[pos_])) return false;
  while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
  std::vector<Node *> dummy;
  if (!rOffsetOperator(dummy)) return false;
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
  if (pos_ >= len_ || !isDigit(buf_[pos_])) return false;
  while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
  if (!matchChar(')')) return false;
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
  // OffsetOperator? WS?
  {
    std::vector<Node *> dummy2;
    rOffsetOperator(dummy2);
  }
  while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t')) pos_++;
  return true;
}

// Offset <- '+'? '-'? (("0b" [01]+) /
//                      ("0x" [[0-9A-F]]+) /
//                      ([0-9]+ WS OffsetOperator [0-9]+ /
//                       [0-9]+ ( OffsetOperator '(' [0-9]+ OffsetOperator [0-9]+ ')' )? /
//                       [0-9]+ ( OffsetOperator [0-9]+ OffsetOperator [0-9]+ )? /
//                       [0-9]+ ( OffsetOperator [0-9]+ )? /
//                       S2nBignumHelper S2nBignumHelper (S2nBignumHelper ([0-9]+ OffsetOperator)? [0-9]+ OffsetOperator)? [0-9]+ /
//                       S2nBignumHelper [0-9]+ ((WS? OffsetOperator [0-9]+ (WS? OffsetOperator [0-9]+)?) / (!'x')) /
//                       S2nBignumHelper /
//                       '(' [0-9]+ WS? OffsetOperator WS? [0-9]+ WS? OffsetOperator WS? [0-9]+')')![[A-Z]]
//                     )
bool Parser::rOffset(std::vector<Node *> &ch) {
  SavePoint startSp = save();

  // '+'? '-'?
  matchChar('+');
  matchChar('-');

  // "0b" [01]+
  {
    SavePoint sp = save();
    if (matchStr("0b") && pos_ < len_ &&
        (buf_[pos_] == '0' || buf_[pos_] == '1')) {
      while (pos_ < len_ && (buf_[pos_] == '0' || buf_[pos_] == '1')) pos_++;
      return true;
    }
    restore(sp);
    // Restore sign chars
    matchChar('+');
    matchChar('-');
  }

  // Restart from after signs
  pos_ = startSp.pos;
  matchChar('+');
  matchChar('-');

  // "0x" [[0-9A-F]]+
  {
    SavePoint sp = save();
    if (matchStr("0x") && pos_ < len_ && isCIHex(buf_[pos_])) {
      while (pos_ < len_ && isCIHex(buf_[pos_])) pos_++;
      return true;
    }
    restore(sp);
  }

  // S2nBignumHelper S2nBignumHelper ... (complex S2n alternatives)
  // Try the S2n alternatives first since they start with '('
  {
    SavePoint sp = save();
    std::vector<Node *> dummy;
    if (rS2nBignumHelper(dummy)) {
      // S2nBignumHelper S2nBignumHelper ...
      SavePoint sp2 = save();
      std::vector<Node *> dummy2;
      if (rS2nBignumHelper(dummy2)) {
        // (S2nBignumHelper ([0-9]+ OffsetOperator)? [0-9]+ OffsetOperator)?
        SavePoint sp3 = save();
        std::vector<Node *> dummy3;
        if (rS2nBignumHelper(dummy3)) {
          // ([0-9]+ OffsetOperator)?
          SavePoint sp4 = save();
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            std::vector<Node *> dummy4;
            if (!rOffsetOperator(dummy4)) restore(sp4);
          }
          // [0-9]+ OffsetOperator
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            std::vector<Node *> dummy5;
            rOffsetOperator(dummy5);
          } else {
            restore(sp3);
          }
        }
        // [0-9]+
        if (pos_ < len_ && isDigit(buf_[pos_])) {
          while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
          return true;
        }
      }
      restore(sp2);

      // S2nBignumHelper [0-9]+ ((WS? OffsetOperator [0-9]+ ...) / (!'x'))
      if (pos_ < len_ && isDigit(buf_[pos_])) {
        while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
        // Try WS? OffsetOperator [0-9]+ ...
        SavePoint sp3 = save();
        while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
          pos_++;
        std::vector<Node *> dummy3;
        if (rOffsetOperator(dummy3)) {
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            // (WS? OffsetOperator [0-9]+)?
            SavePoint sp4 = save();
            while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
              pos_++;
            std::vector<Node *> dummy4;
            if (rOffsetOperator(dummy4) && pos_ < len_ && isDigit(buf_[pos_])) {
              while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            } else {
              restore(sp4);
            }
            return true;
          }
        }
        restore(sp3);
        // (!'x')
        if (pos_ >= len_ || buf_[pos_] != 'x') {
          return true;
        }
        // Fall through
      }

      // Just S2nBignumHelper alone
      return true;
    }
    restore(sp);
  }

  // '(' [0-9]+ WS? OffsetOperator WS? [0-9]+ WS? OffsetOperator WS? [0-9]+')' ![[A-Z]]
  {
    SavePoint sp = save();
    if (matchChar('(')) {
      if (pos_ < len_ && isDigit(buf_[pos_])) {
        while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
        while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
          pos_++;
        std::vector<Node *> dummy;
        if (rOffsetOperator(dummy)) {
          while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
            pos_++;
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
              pos_++;
            std::vector<Node *> dummy2;
            if (rOffsetOperator(dummy2)) {
              while (pos_ < len_ && (buf_[pos_] == ' ' || buf_[pos_] == '\t'))
                pos_++;
              if (pos_ < len_ && isDigit(buf_[pos_])) {
                while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
                if (matchChar(')')) {
                  // ![[A-Z]]
                  if (pos_ >= len_ || !isCIAlpha(buf_[pos_])) {
                    return true;
                  }
                }
              }
            }
          }
        }
      }
    }
    restore(sp);
  }

  // [0-9]+ with various optional suffixes
  if (pos_ < len_ && isDigit(buf_[pos_])) {
    while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;

    // Try: WS OffsetOperator [0-9]+
    {
      SavePoint sp = save();
      std::vector<Node *> dummy;
      if (rWS(dummy)) {
        std::vector<Node *> dummy2;
        if (rOffsetOperator(dummy2)) {
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            return true;
          }
        }
      }
      restore(sp);
    }

    // Try: OffsetOperator '(' [0-9]+ OffsetOperator [0-9]+ ')'
    {
      SavePoint sp = save();
      std::vector<Node *> dummy;
      if (rOffsetOperator(dummy)) {
        if (matchChar('(')) {
          if (pos_ < len_ && isDigit(buf_[pos_])) {
            while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
            std::vector<Node *> dummy2;
            if (rOffsetOperator(dummy2)) {
              if (pos_ < len_ && isDigit(buf_[pos_])) {
                while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
                if (matchChar(')')) return true;
              }
            }
          }
        }
      }
      restore(sp);
    }

    // Try: OffsetOperator [0-9]+ OffsetOperator [0-9]+
    {
      SavePoint sp = save();
      std::vector<Node *> dummy;
      if (rOffsetOperator(dummy)) {
        if (pos_ < len_ && isDigit(buf_[pos_])) {
          while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
          std::vector<Node *> dummy2;
          SavePoint sp2 = save();
          if (rOffsetOperator(dummy2)) {
            if (pos_ < len_ && isDigit(buf_[pos_])) {
              while (pos_ < len_ && isDigit(buf_[pos_])) pos_++;
              return true;
            }
          }
          restore(sp2);
          // Just OffsetOperator [0-9]+
          return true;
        }
      }
      restore(sp);
    }

    // Plain [0-9]+
    return true;
  }

  restore(startSp);
  return false;
}

// Section <- [[A-Z@]]+
bool Parser::rSection(std::vector<Node *> &ch) {
  if (pos_ >= len_) return false;
  char c = buf_[pos_];
  if (!isCIAlpha(c) && c != '@') return false;
  pos_++;
  while (pos_ < len_ && (isCIAlpha(buf_[pos_]) || buf_[pos_] == '@'))
    pos_++;
  return true;
}

// SegmentRegister <- '%' [c-gs] 's:'
bool Parser::rSegmentRegister(std::vector<Node *> &ch) {
  SavePoint sp = save();
  if (matchChar('%') && pos_ < len_ &&
      buf_[pos_] >= 'c' && buf_[pos_] <= 'g') {
    pos_++;
    if (matchStr("s:")) return true;
  }
  // Also check 's' which is in [c-gs]
  restore(sp);
  if (matchChar('%') && pos_ < len_ && buf_[pos_] == 's') {
    pos_++;
    if (matchStr("s:")) return true;
  }
  restore(sp);
  return false;
}
