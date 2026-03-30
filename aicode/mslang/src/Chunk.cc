// Copyright (c) 2026 ASMlover. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list ofconditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materialsprovided with the
//    distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#include "Chunk.hh"

namespace ms {

void Chunk::write(Instruction instr, int line, int column, int token_length) noexcept {
  code_.push_back(instr);
  if (!lines_.empty() && lines_.back().line == line
      && lines_.back().column == column
      && lines_.back().token_length == token_length) {
    lines_.back().count++;
  } else {
    lines_.push_back({line, column, token_length, 1});
  }
}

sz_t Chunk::add_constant(Value value) noexcept {
  constants_.push_back(value);
  return constants_.size() - 1;
}

Instruction Chunk::code_at(sz_t offset) const noexcept {
  return code_[offset];
}

const Value& Chunk::constant_at(sz_t index) const noexcept {
  return constants_[index];
}

int Chunk::line_at(sz_t offset) const noexcept {
  sz_t cumulative = 0;
  for (const auto& run : lines_) {
    cumulative += static_cast<sz_t>(run.count);
    if (offset < cumulative) {
      return run.line;
    }
  }
  return 0;
}

int Chunk::column_at(sz_t offset) const noexcept {
  sz_t cumulative = 0;
  for (const auto& run : lines_) {
    cumulative += static_cast<sz_t>(run.count);
    if (offset < cumulative) {
      return run.column;
    }
  }
  return 0;
}

int Chunk::token_length_at(sz_t offset) const noexcept {
  sz_t cumulative = 0;
  for (const auto& run : lines_) {
    cumulative += static_cast<sz_t>(run.count);
    if (offset < cumulative) {
      return run.token_length;
    }
  }
  return 0;
}

sz_t Chunk::count() const noexcept {
  return code_.size();
}

void Chunk::compact_nops() noexcept {
  sz_t n = code_.size();
  if (n == 0) return;

  bool has_nop = false;
  for (sz_t i = 0; i < n; ++i) {
    if (decode_op(code_[i]) == OpCode::OP_NOP) { has_nop = true; break; }
  }
  if (!has_nop) return;

  // Expand RLE lines_ to per-instruction arrays
  std::vector<int> per_line, per_col, per_tok;
  per_line.reserve(n);
  per_col.reserve(n);
  per_tok.reserve(n);
  for (const auto& run : lines_) {
    for (int j = 0; j < run.count; ++j) {
      per_line.push_back(run.line);
      per_col.push_back(run.column);
      per_tok.push_back(run.token_length);
    }
  }

  // Pass 1: build offset_map[old] = new index for each instruction position
  std::vector<sz_t> offset_map(n + 1, 0);
  sz_t new_size = 0;
  for (sz_t i = 0; i < n; ++i) {
    if (decode_op(code_[i]) != OpCode::OP_NOP)
      offset_map[i] = new_size++;
  }
  offset_map[n] = new_size;
  // For NOPs, map to next non-NOP's new position (for jump target fixup)
  for (sz_t i = n; i-- > 0; ) {
    if (decode_op(code_[i]) == OpCode::OP_NOP)
      offset_map[i] = offset_map[i + 1];
  }

  // Pass 2: copy non-NOPs, fix sBx jump offsets, rebuild RLE lines
  std::vector<Instruction> new_code;
  new_code.reserve(new_size);
  std::vector<SourceRun> new_lines;

  for (sz_t i = 0; i < n; ++i) {
    if (decode_op(code_[i]) == OpCode::OP_NOP) continue;

    Instruction instr = code_[i];
    OpCode op = decode_op(instr);

    if (op == OpCode::OP_JMP || op == OpCode::OP_TRY || op == OpCode::OP_FORITER) {
      int old_sbx = decode_sBx(instr);
      sz_t old_target = static_cast<sz_t>(static_cast<int>(i) + 1 + old_sbx);
      sz_t new_cur = new_code.size();
      int new_sbx = static_cast<int>(offset_map[old_target]) - static_cast<int>(new_cur) - 1;
      instr = encode_AsBx(op, decode_A(instr), new_sbx);
    }

    new_code.push_back(instr);

    int line = per_line[i], col = per_col[i], tok = per_tok[i];
    if (!new_lines.empty() && new_lines.back().line == line
        && new_lines.back().column == col
        && new_lines.back().token_length == tok) {
      new_lines.back().count++;
    } else {
      new_lines.push_back({line, col, tok, 1});
    }
  }

  code_ = std::move(new_code);
  lines_ = std::move(new_lines);
}

void Chunk::truncate(sz_t new_count) noexcept {
  sz_t to_remove = code_.size() - new_count;
  while (to_remove > 0 && !lines_.empty()) {
    auto& back = lines_.back();
    sz_t take = std::min(to_remove, static_cast<sz_t>(back.count));
    back.count -= static_cast<int>(take);
    to_remove -= take;
    if (back.count == 0) lines_.pop_back();
  }
  code_.resize(new_count);
}

Instruction& Chunk::operator[](sz_t offset) noexcept {
  return code_[offset];
}

const Instruction* Chunk::code_data() const noexcept {
  return code_.data();
}

Instruction* Chunk::code_data() noexcept {
  return code_.data();
}

const std::vector<Value>& Chunk::constants() const noexcept {
  return constants_;
}

std::vector<Value>& Chunk::constants() noexcept {
  return constants_;
}

const std::vector<SourceRun>& Chunk::lines() const noexcept {
  return lines_;
}

std::vector<SourceRun>& Chunk::lines() noexcept {
  return lines_;
}

std::vector<Instruction>& Chunk::code() noexcept {
  return code_;
}

const std::vector<Instruction>& Chunk::code() const noexcept {
  return code_;
}

} // namespace ms
