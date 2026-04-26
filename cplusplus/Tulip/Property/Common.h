// Copyright (c) 2018 ASMlover. All rights reserved.
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
#pragma once

#include <unordered_map>
#include "../Utility.h"

namespace tulip {

struct DescItem {
  std::uint32_t flag{};
  py::object type;
  py::object sub_type;

  DescItem(std::uint32_t f = 0)
    : flag(f) {
  }
};

class PropertyDesc {
  using PropertyDescPtr = boost::shared_ptr<PropertyDesc>;
  using PropertyDescMap = std::unordered_map<std::string, PropertyDescPtr>;

  bool is_item_{};
  DescItem item_{};
  PropertyDescMap desc_;
  PropertyDescPtr sub_desc_;
public:
  PropertyDesc(void) {
  }

  ~PropertyDesc(void) {
  }

  inline void clear(void) {
    desc_.clear();
  }

  inline void set_flag(std::uint32_t flag) {
    item_.flag = flag;
  }

  inline void set_flag(const std::string& name, std::uint32_t flag) {
    auto it = desc_.find(name);
    if (it != desc_.end())
      it->second->set_flag(flag);
  }

  inline std::uint32_t get_flag(void) const {
    return item_.flag;
  }

  inline std::uint32_t get_flag(const std::string& name) const {
    auto it = desc_.find(name);
    if (it != desc_.end())
      return it->second->get_flag();
    return 0;
  }

  inline std::uint32_t recursive_flag(void) const {
    auto flag = item_.flag;
    for (auto& x : desc_)
      flag |= x.second->recursive_flag();
    return flag;
  }

  inline bool is_valid(const std::string& name) const {
    return desc_.find(name) != desc_.end();
  }

  inline void add_desc(const std::string& name, const PropertyDescPtr& desc) {
    desc_[name] = desc;
  }

  inline PropertyDescPtr get_desc(const std::string& name) const {
    auto pos = desc_.find(name);
    if (pos != desc_.end())
      return pos->second;
    if (sub_desc_)
      return sub_desc_;
    return nullptr;
  }

  inline py::object& type(void) {
    return item_.type;
  }

  inline py::object& sub_type(void) {
    if (item_.sub_type.is_none() && sub_desc_)
      return sub_desc_->type();
    return item_.sub_type;
  }

  inline void set_type(const py::object& t) {
    item_.type = t;
  }

  inline void set_sub_type(const py::object& t) {
    item_.sub_type = t;
  }

  inline PropertyDescPtr& sub_desc(void) {
    if (!sub_desc_)
      sub_desc_ = boost::make_shared<PropertyDesc>();
    return sub_desc_;
  }

  inline bool is_item(void) const {
    return is_item_;
  }

  inline void set_is_item(bool b) {
    is_item_ = b;
  }
};

}
