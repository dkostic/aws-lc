// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0 OR ISC

#ifndef AR_H
#define AR_H

#include <map>
#include <string>
#include <vector>

// ParseAR parses an archive file and returns a map from filename to contents.
// Returns false on error, with err_out set to an error message.
bool ParseAR(const std::string &data, std::map<std::string, std::string> &out,
             std::string &err_out);

#endif // AR_H
