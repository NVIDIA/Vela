// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <string>
#include <string_view>

namespace vsr::core {

struct DataNode;

// Text Encoding //////////////////////////////////////////////////////////////

// The Text Encoding is the human-readable and human-editable form of a Data
// Tree, a full peer of the Binary Encoding (ADR 0039). Its grammar and every
// literal rule are specified in DataTreeText.md beside this header; the code
// here is the writer and the parser for that document.

// Every text file begins with this token followed by the encoding version.
// Detection is "does the input start with the header token": anything else
// is the Binary Encoding, and the file extension is never consulted.
constexpr std::string_view DATA_TREE_TEXT_HEADER = "vsr-text";
constexpr int DATA_TREE_TEXT_VERSION = 1;

// True when the leading bytes of some input are the Text Encoding header.
// Only the header token is inspected; the version is validated by the reader.
bool isDataTreeText(std::string_view leadingBytes);

// True when the named file begins with the Text Encoding header. A file that
// cannot be opened is not text.
bool isDataTreeTextFile(const char *filename);

/*
 * The Text Encoding writer and parser, reached from DataNode's serialization
 * methods. It is a friend of DataNode and DataTree for the same reason the
 * binary loader is a member: reading is one Subtree Replacement, so the
 * individual edits that rebuild the node are made silently and reported once.
 *
 * Nothing here is meant to be called directly; use DataNode::toText(),
 * DataNode::fromText(), or the Encoding argument of write()/save().
 */
struct DataTreeTextCodec
{
  // Append the Text Encoding of the tree that node roots to text. The node's
  // own name and value are not part of it (ADR 0027); a detached node or a
  // leaf writes an empty tree, which is the header alone.
  static void write(const DataNode &node, std::string &text);

  // Replace node's contents with the tree described by text. The reader
  // contract is identical to the binary one: the node is emptied before
  // decoding, left empty on failure, and exactly one signalSubtreeReplaced()
  // is delivered either way. Every failure logs a warning naming the line
  // and column. A node with no tree behind it refuses to read.
  static bool read(DataNode &node, std::string_view text);

  // read(), with the text taken from a file.
  static bool load(DataNode &node, const char *filename);

 private:
  struct Writer;
  struct Parser;
};

} // namespace vsr::core
