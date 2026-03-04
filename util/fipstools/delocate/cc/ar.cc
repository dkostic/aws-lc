// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0 OR ISC

#include "ar.h"

#include <cstdlib>
#include <cstring>

bool ParseAR(const std::string &data, std::map<std::string, std::string> &out,
             std::string &err_out) {
  // See https://en.wikipedia.org/wiki/Ar_(Unix)#File_format_details
  const char *expectedMagic = "!<arch>\n";
  const size_t magicLen = 8;

  if (data.size() < magicLen || data.compare(0, magicLen, expectedMagic) != 0) {
    err_out = "ar: not an archive file";
    return false;
  }

  std::string longFilenameTable;
  bool hasLongFilenameTable = false;
  size_t pos = magicLen;

  while (pos < data.size()) {
    if (pos + 60 > data.size()) {
      err_out = "ar: error reading file header";
      return false;
    }

    std::string name(data, pos, 16);
    // Trim trailing spaces from name
    size_t nameEnd = name.find_last_not_of(' ');
    if (nameEnd != std::string::npos) {
      name = name.substr(0, nameEnd + 1);
    }

    std::string sizeStr(data, pos + 48, 10);
    // Trim trailing spaces and NULs
    size_t sizeEnd = sizeStr.find_last_not_of(" \0", std::string::npos);
    if (sizeEnd != std::string::npos) {
      sizeStr = sizeStr.substr(0, sizeEnd + 1);
    }

    char *endptr;
    unsigned long long size = strtoull(sizeStr.c_str(), &endptr, 10);
    if (*endptr != '\0') {
      err_out = "ar: failed to parse file size";
      return false;
    }

    pos += 60;

    // File contents are padded to a multiple of two bytes.
    unsigned long long storedSize = size;
    if (storedSize % 2 == 1) {
      storedSize++;
    }

    if (pos + storedSize > data.size()) {
      err_out = "ar: error reading file contents";
      return false;
    }

    std::string contents(data, pos, size);
    pos += storedSize;

    if (name == "//") {
      if (hasLongFilenameTable) {
        err_out = "ar: two filename tables found";
        return false;
      }
      longFilenameTable = contents;
      hasLongFilenameTable = true;
      continue;
    }

    if (name == "/") {
      continue;
    }

    if (name.size() > 1 && name[0] == '/') {
      if (!hasLongFilenameTable) {
        err_out = "ar: long filename reference found before filename table";
        return false;
      }
      char *endp;
      unsigned long long offset = strtoull(name.c_str() + 1, &endp, 10);
      if (*endp != '\0') {
        err_out = "ar: failed to parse filename offset";
        return false;
      }
      if (offset > longFilenameTable.size()) {
        err_out = "ar: filename offset out of bounds";
        return false;
      }

      size_t end = longFilenameTable.find_first_of("/\0", offset);
      if (end == std::string::npos) {
        err_out = "ar: unterminated filename in table";
        return false;
      }
      name = longFilenameTable.substr(offset, end - offset);
    } else {
      // Trim trailing /
      if (!name.empty() && name.back() == '/') {
        name.pop_back();
      }
    }

    // BSD variant: #1/XXX
    unsigned int namelen = 0;
    if (sscanf(name.c_str(), "#1/%u", &namelen) == 1 &&
        contents.size() >= namelen) {
      name = contents.substr(0, namelen);
      contents = contents.substr(namelen);
      // Trim NUL padding from name
      size_t nullPos = name.find('\0');
      if (nullPos != std::string::npos) {
        name = name.substr(0, nullPos);
      }
    }

    if (name == "__.SYMDEF" || name == "__.SYMDEF SORTED") {
      continue;
    }

    out[name] = std::move(contents);
  }

  return true;
}
