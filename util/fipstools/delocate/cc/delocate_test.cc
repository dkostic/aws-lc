// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0 OR ISC

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "transform.h"

struct TestCase {
  const char *name;
  std::vector<std::string> includes;
  std::vector<std::string> inputs;
  const char *out;
  bool startEndDebugDirectives;
};

static std::string readFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path.c_str());
    exit(1);
  }
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

int main(int argc, char **argv) {
  std::string testDataDir = "testdata";
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--testdata") == 0 && i + 1 < argc) {
      testDataDir = argv[++i];
    }
  }

  // Test table matching the Go version (excluding ppc64le tests)
  std::vector<TestCase> tests = {
      {"generic-FileDirectives", {}, {"in.s"}, "out.s", true},
      {"generic-FileDirectives-no-start-end", {}, {"in.s"}, "out.s", false},
      {"generic-Includes",
       {"/some/include/path/openssl/foo.h",
        "/some/include/path/openssl/bar.h"},
       {"in.s"},
       "out.s",
       true},
      {"x86_64-Basic", {}, {"in.s"}, "out.s", true},
      {"x86_64-BSS", {}, {"in.s"}, "out.s", true},
      {"x86_64-GOTRewrite", {}, {"in.s"}, "out.s", true},
      {"x86_64-LargeMemory", {}, {"in.s"}, "out.s", true},
      {"x86_64-LabelRewrite", {}, {"in1.s", "in2.s"}, "out.s", true},
      {"x86_64-Sections", {}, {"in.s"}, "out.s", true},
      {"x86_64-ThreeArg", {}, {"in.s"}, "out.s", true},
      {"x86_64-FourArg", {}, {"in.s"}, "out.s", true},
      {"x86_64-Relro", {}, {"in.s"}, "out.s", true},
      {"aarch64-Basic", {}, {"in.s"}, "out.s", true},
  };

  int passed = 0;
  int failed = 0;

  for (const auto &test : tests) {
    std::string testPath = testDataDir + "/" + test.name + "/";

    std::vector<InputFile> inputs;
    for (size_t i = 0; i < test.inputs.size(); i++) {
      inputs.push_back(
          {testPath + test.inputs[i], (int)i, false, "", nullptr});
    }

    std::string errOut;
    if (!Delocation::parseInputs(inputs, {}, errOut)) {
      fprintf(stderr, "FAIL %s: parseInputs failed: %s\n", test.name,
              errOut.c_str());
      failed++;
      continue;
    }

    std::string output;
    Delocation d;
    if (!d.transform(output, test.includes, inputs,
                     test.startEndDebugDirectives, errOut)) {
      fprintf(stderr, "FAIL %s: transform failed: %s\n", test.name,
              errOut.c_str());
      failed++;
      continue;
    }

    std::string expected = readFile(testPath + test.out);

    if (output == expected) {
      printf("PASS %s\n", test.name);
      passed++;
    } else {
      fprintf(stderr, "FAIL %s: output differs\n", test.name);

      // Find first difference
      std::istringstream gotStream(output);
      std::istringstream expStream(expected);
      std::string gotLine, expLine;
      int lineNo = 0;
      while (std::getline(expStream, expLine)) {
        lineNo++;
        if (!std::getline(gotStream, gotLine)) {
          fprintf(stderr, "  First difference at line %d:\n", lineNo);
          fprintf(stderr, "  Expected: %s\n", expLine.c_str());
          fprintf(stderr, "  Got:      <EOF>\n");
          break;
        }
        if (gotLine != expLine) {
          fprintf(stderr, "  First difference at line %d:\n", lineNo);
          fprintf(stderr, "  Expected: %s\n", expLine.c_str());
          fprintf(stderr, "  Got:      %s\n", gotLine.c_str());
          break;
        }
      }
      if (std::getline(gotStream, gotLine) && gotLine != expLine) {
        // Extra lines in output
        lineNo++;
        fprintf(stderr, "  Extra output at line %d: %s\n", lineNo,
                gotLine.c_str());
      }

      failed++;
    }
  }

  printf("\n%d passed, %d failed\n", passed, failed);
  return failed > 0 ? 1 : 0;
}
