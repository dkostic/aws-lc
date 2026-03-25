// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0 OR ISC

#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "parser.h"

enum class ProcessorType { PPC64LE = 1, X86_64, AARCH64 };

struct InputFile {
  std::string path;
  int index;
  bool isArchive;
  std::string contents;
  Node *ast;
};

struct CpuCapUniqueSymbol {
  std::string registerName;
  std::string suffixUniqueness;

  std::string getx86Symbol() const {
    return "LOPENSSL_ia32cap_P_" + registerName + suffixUniqueness;
  }
  std::string getx86SymbolReturn() const {
    return getx86Symbol() + "_return";
  }
};

CpuCapUniqueSymbol newCpuCapUniqueSymbol(int uniqueness,
                                          const std::string &registerName);

using WrapperFunc = std::function<void(std::function<void()>)>;

struct WrapperStack {
  std::vector<WrapperFunc> stack;
  void doIt(std::function<void()> baseCase);
};

class Delocation {
 public:
  Delocation();

  // Main entry points
  bool transform(std::string &out, const std::vector<std::string> &includes,
                 std::vector<InputFile> &inputs,
                 bool startEndDebugDirectives, std::string &errOut);

  static bool parseInputs(std::vector<InputFile> &inputs,
                           const std::vector<std::string> &cppCommand,
                           std::string &errOut);

 private:
  ProcessorType processor_;
  std::string *output_;
  std::string commentIndicator_;
  std::set<std::string> symbols_;
  std::set<std::string> localEntrySymbols_;
  std::vector<CpuCapUniqueSymbol> cpuCapUniqueSymbols_;
  std::map<std::string, std::string> relroLocalLabelToFuncMap_;
  std::map<std::string, std::string> redirectors_;
  std::map<std::string, std::string> bssAccessorsNeeded_;
  std::set<std::string> tocLoaders_;
  std::set<std::string> gotExternalsNeeded_;
  bool gotDeltaNeeded_;
  std::set<std::string> gotOffsetsNeeded_;
  std::set<std::string> gotOffOffsetsNeeded_;
  InputFile currentInput_;

  // Output helpers
  void writeStr(const std::string &s);
  std::string contents(const Node *node) const;
  void writeNode(const Node *node);
  void writeCommentedNode(const Node *node);

  // Processing
  bool processInput(const InputFile &input, std::string &errOut);
  bool processDirective(Node *&statement, Node *directive, std::string &errOut);
  bool processLabelContainingDirective(Node *&statement, Node *directive,
                                       std::string &errOut);
  bool processLabel(Node *&statement, Node *label, std::string &errOut);
  bool processSymbolExpr(Node *expr, std::string &b);
  bool processIntelInstruction(Node *&statement, Node *instruction,
                               std::string &errOut);
  Node *handleBSS(Node *statement, std::string &errOut, bool &ok);

  // Relro
  void skippedLine(const Node *node);
  bool maybeSkipRelroStatement(const Node *node);
  Node *skipRelroSection(Node *statement);
  static bool isNewLine(const std::string &file, const Node *node);
  static bool isEndOfRelroSection(const std::string &file,
                                  const Node *lineRootNode);
  static bool isProbablyAValidSymbol(const std::string &symbol);
  static bool findLocalLabelsForRelro(
      const std::string &file, Node *node,
      std::map<std::string, std::string> &relroMap);
  static bool relroLocalLabelToFuncMapping(
      const InputFile &input,
      std::map<std::string, std::string> &relroMap);

  // AST helpers
  static Node *skipWS(Node *node);
  static Node *skipNodes(Node *node, PegRule ruleToSkip);
  static void assertNodeType(const Node *node, PegRule expected);
  static std::vector<Node *> instructionArgs(Node *node);
  static void forEachPath(Node *node, std::function<void(Node *)> cb,
                          std::vector<PegRule> rules);
  static bool matchPatternSearchSubtree(
      Node *node, std::function<bool(Node *)> matchNode,
      const std::vector<PegRule> &rules);
  static bool matchPatternOneLine(
      Node *lineRootNode, std::function<bool(Node *)> matchNode,
      const std::vector<PegRule> &rules);

  // Symbol helpers
  std::string mapLocalSymbol(const std::string &symbol) const;
  static std::string localTargetName(const std::string &name);
  static std::string localEntryName(const std::string &name);
  static std::string redirectorName(const std::string &symbol);
  static std::string accessorName(const std::string &name);
  static bool isSynthesized(const std::string &symbol, ProcessorType proc);
  static bool isFipsScopeMarkers(const std::string &symbol);
  static std::pair<std::string, bool> sectionType(const std::string &section);
  static ProcessorType detectProcessor(const InputFile &input);

  // x86-64 specific
  bool isRIPRelative(const Node *node) const;
  Node *gatherOffsets(Node *symRef, std::string &offsets) const;
  struct MemRefResult {
    std::string symbol;
    std::string offset;
    std::string section;
    bool didChange;
    bool symbolIsLocal;
    Node *nextRef;
  };
  MemRefResult parseMemRef(Node *memRef);

  // x86-64 instruction helpers
  enum InstructionType {
    instrPush,
    instrMove,
    instrTransformingMove,
    instrJump,
    instrConditionalMove,
    instrCombine,
    instrMemoryVectorCombine,
    instrTwoArg,
    instrThreeArg,
    instrFourArg,
    instrCompare,
    instrOther,
  };
  static InstructionType classifyInstruction(const std::string &instr,
                                              const std::vector<Node *> &args);
  static bool isValidLEATarget(const std::string &reg);
  WrapperFunc loadFromGOT(const std::string &destination,
                           const std::string &symbol,
                           const std::string &section, bool redzoneCleared);
  static std::pair<WrapperFunc, std::string> saveRegister(
      std::string *w, const std::vector<std::string> &avoidRegs);

  // aarch64 specific
  bool processAarch64Instruction(Node *&statement, Node *instruction,
                                  std::string &errOut);
  bool loadAarch64Address(Node *statement, const std::string &targetReg,
                           const std::string &symbol,
                           const std::string &offsetStr, std::string &errOut);
  static void writeAarch64Function(
      std::string &out, const std::string &funcName,
      std::function<void(std::string &)> writeContents);
  static std::string gotHelperName(const std::string &symbol);
};

// Path helpers
std::string includePathFromHeaderFilePath(const std::string &path,
                                           std::string &errOut);
std::string relativeHeaderIncludePath(const std::string &path,
                                       std::string &errOut);

#endif // TRANSFORM_H
