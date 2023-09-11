// Copyright (c) 2023 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>

#include "paddle/fluid/ir/dialect/paddle_dialect/ir/pd_type.h"
#include "paddle/fluid/ir/drr/api/tensor_interface.h"
#include "paddle/ir/core/value.h"

namespace ir {
namespace drr {

class IrShape {
 public:
  explicit IrShape(const phi::DDim* dims) : dims_(dims) {}

  bool operator==(const IrShape& other) const { return *dims_ == *other.dims_; }

  int size() const { return dims_->size(); }

  int64_t at(int idx) const { return dims_->at(idx); }

 private:
  const phi::DDim* dims_;
};

class IrDtype {
 public:
  explicit IrDtype(const ir::Type* dtype) : dtype_(dtype) {}

  bool operator==(const IrDtype& other) const {
    return *dtype_ == *other.dtype_;
  }

 private:
  const ir::Type* dtype_;
};

class IrValue : public TensorInterface {
 public:
  explicit IrValue(const ir::Value& value)
      : value_(value),
        shape_(value ? &value.type()
                            .dyn_cast<paddle::dialect::DenseTensorType>()
                            .dims()
                     : nullptr),
        dtype_(value ? &value.type()
                            .dyn_cast<paddle::dialect::DenseTensorType>()
                            .dtype()
                     : nullptr) {}

  ShapeInterface Shape() const override { return ShapeInterface(&shape_); }
  DtypeInterface Dtype() const override { return DtypeInterface(&dtype_); }

  const Value& get() const { return value_; }

 private:
  const Value value_;
  const IrShape shape_;
  const IrDtype dtype_;
};

class IrAttr;

}  // namespace drr
}  // namespace ir
