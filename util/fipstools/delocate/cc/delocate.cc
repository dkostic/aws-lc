// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0 OR ISC

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "transform.h"

static void usage() {
  fprintf(stderr,
          "Usage: delocate [flags] [input files...]\n"
          "  -a <file>       Path to a .a file containing assembly sources\n"
          "  -o <file>       Path to output assembly (required)\n"
          "  -cc <path>      Path to the C compiler for preprocessing inputs\n"
          "  -cc-flags <f>   Flags for the C compiler when preprocessing\n"
          "  -s2n-bignum-include <dir>  Directory with s2n-bignum header files\n"
          "  -no-se-debug-directives    Disables .file/.loc on boundary symbols\n");
}

static std::vector<std::string> splitFields(const std::string &s) {
  std::vector<std::string> result;
  size_t pos = 0;
  while (pos < s.size()) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
    if (pos >= s.size()) break;
    size_t start = pos;
    while (pos < s.size() && s[pos] != ' ' && s[pos] != '\t') pos++;
    result.push_back(s.substr(start, pos - start));
  }
  return result;
}

int main(int argc, char **argv) {
  std::string arInput;
  std::string outFile;
  std::string ccPath;
  std::string ccFlags;
  std::string s2nBignumInclude;
  bool noStartEndDebugDirectives = false;

  std::vector<std::string> positionalArgs;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
      arInput = argv[++i];
    } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
      outFile = argv[++i];
    } else if (strcmp(argv[i], "-cc") == 0 && i + 1 < argc) {
      ccPath = argv[++i];
    } else if (strcmp(argv[i], "-cc-flags") == 0 && i + 1 < argc) {
      ccFlags = argv[++i];
    } else if (strcmp(argv[i], "-s2n-bignum-include") == 0 && i + 1 < argc) {
      s2nBignumInclude = argv[++i];
    } else if (strcmp(argv[i], "-no-se-debug-directives") == 0) {
      noStartEndDebugDirectives = true;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown flag: %s\n", argv[i]);
      usage();
      return 1;
    } else {
      positionalArgs.push_back(argv[i]);
    }
  }

  if (outFile.empty()) {
    fprintf(stderr, "Must give argument to -o.\n");
    return 1;
  }

  std::vector<InputFile> inputs;
  if (!arInput.empty()) {
    inputs.push_back({arInput, 0, true, "", nullptr});
  }

  std::vector<std::string> includes;
  std::set<std::string> includePaths;

  for (size_t i = 0; i < positionalArgs.size(); i++) {
    const std::string &path = positionalArgs[i];
    if (path.empty()) continue;

    if (path.size() > 2 && path.substr(path.size() - 2) == ".h") {
      std::string err;
      std::string dir = includePathFromHeaderFilePath(path, err);
      if (dir.empty()) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
      }
      includes.push_back(path);
      includePaths.insert(dir);
      continue;
    }

    inputs.push_back({path, (int)(i + 1), false, "", nullptr});
  }

  if (!s2nBignumInclude.empty()) {
    includePaths.insert(s2nBignumInclude);
  }

  std::vector<std::string> cppCommand;
  if (!ccPath.empty()) {
    cppCommand.push_back(ccPath);
    auto flags = splitFields(ccFlags);
    cppCommand.insert(cppCommand.end(), flags.begin(), flags.end());
    cppCommand.push_back("-Wno-unused-command-line-argument");
    for (const auto &p : includePaths) {
      cppCommand.push_back("-I" + p);
    }
    cppCommand.push_back("-E");
    cppCommand.push_back("-dI");
  }

  std::string errOut;
  if (!Delocation::parseInputs(inputs, cppCommand, errOut)) {
    fprintf(stderr, "%s\n", errOut.c_str());
    return 1;
  }

  std::string output;
  Delocation d;
  if (!d.transform(output, includes, inputs, !noStartEndDebugDirectives,
                   errOut)) {
    fprintf(stderr, "%s\n", errOut.c_str());
    return 1;
  }

  std::ofstream out(outFile, std::ios::binary | std::ios::trunc);
  if (!out) {
    fprintf(stderr, "cannot open output file %s\n", outFile.c_str());
    return 1;
  }
  out.write(output.data(), output.size());
  return 0;
}
