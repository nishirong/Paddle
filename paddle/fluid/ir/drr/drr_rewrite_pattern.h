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

#include <memory>
#include <queue>
#include <unordered_set>
#include <vector>

#include "paddle/fluid/ir/drr/api/drr_pattern_context.h"
#include "paddle/fluid/ir/drr/api/match_context.h"
#include "paddle/fluid/ir/drr/ir_operation.h"
#include "paddle/fluid/ir/drr/ir_operation_creator.h"
#include "paddle/fluid/ir/drr/match_context_impl.h"
#include "paddle/fluid/ir/drr/pattern_graph.h"
#include "paddle/ir/core/enforce.h"
#include "paddle/ir/core/operation.h"
#include "paddle/ir/core/type_name.h"
#include "paddle/ir/pattern_rewrite/pattern_match.h"

namespace ir {
namespace drr {

template <typename DrrPattern>
class DrrRewritePattern : public ir::RewritePattern {
 public:
  explicit DrrRewritePattern(const DrrPatternContext& drr_context,
                             ir::IrContext* context,
                             ir::PatternBenefit benefit = 1)
      : ir::RewritePattern(
            drr_context.source_pattern_graph()->AnchorNode()->name(),
            benefit,
            context,
            {}),
        source_pattern_graph_(drr_context.source_pattern_graph()),
        constraints_(drr_context.constraints()),
        result_pattern_graph_(drr_context.result_pattern_graph()) {
    IR_ENFORCE(source_pattern_graph_->owned_op_call().size(),
               "source_pattern_graph is empty, please check the drr pattern "
               "define code.");
    source_pattern_graph_->Print();
    result_pattern_graph_->Print();
  }

  bool MatchAndRewrite(ir::Operation* op,
                       PatternRewriter& rewriter) const override {  // NOLINT
    std::shared_ptr<MatchContextImpl> src_match_ctx =
        std::make_shared<MatchContextImpl>();
    if (PatternGraphMatch(op, src_match_ctx)) {
      VLOG(4) << "DRR pattern (" << ir::get_type_name<DrrPattern>()
              << ") is matched in program.";
      PatternGraphRewrite(*src_match_ctx, rewriter);
      return true;
    }
    return false;
  }

 private:
  bool PatternGraphMatch(
      ir::Operation* op,
      const std::shared_ptr<MatchContextImpl>& source_pattern_match_ctx) const {
    // Match
    VLOG(6) << "PatternGraphMatch Start: op(" << op->name() << ")";
    const auto* anchor = source_pattern_graph_->AnchorNode();
    IR_ENFORCE(anchor);
    std::unordered_set<const OpCall*> drr_visited;
    std::unordered_set<Operation*> ir_visited;
    std::queue<const OpCall*> drr_q;
    std::queue<ir::Operation*> ir_q;
    drr_q.push(anchor);
    ir_q.push(op);
    drr_visited.insert(anchor);
    ir_visited.insert(op);
    bool matched = true;
    size_t step = 0;
    while (!drr_q.empty()) {
      if (!matched) break;

      IR_ENFORCE(drr_q.size() == ir_q.size());

      auto* drr_node = drr_q.front();
      auto* ir_node = ir_q.front();
      drr_q.pop();
      ir_q.pop();
      if (drr_node->name() != ir_node->name()) {
        matched = false;
        VLOG(6) << " --- match false: " << drr_node->name();
        break;
      } else {
        VLOG(6) << " --- match true: " << drr_node->name();
      }
      source_pattern_match_ctx->BindIrOperation(
          drr_node, std::make_shared<IrOperation>(ir_node));

      // op's inputs
      const auto& drr_input_tensors = drr_node->inputs();
      auto ir_input_value_size = ir_node->num_operands();
      // check input's size
      if (drr_input_tensors.size() != ir_input_value_size) {
        matched = false;
        VLOG(6) << " --- match false: " << drr_input_tensors.size()
                << " not equal " << ir_input_value_size;
        break;
      }
      for (size_t i = 0; i < drr_input_tensors.size(); ++i) {
        if (!matched) break;
        // check brother ops
        const auto& drr_brother_ops = drr_input_tensors[i]->consumers();
        auto ir_input_value = ir_node->operand(i).source();

        source_pattern_match_ctx->BindIrValue(
            drr_input_tensors[i]->name(),
            std::make_shared<IrValue>(ir_input_value));
        if (drr_brother_ops.size() != ir_input_value.use_count()) {
          matched = false;
          VLOG(6) << " --- match false: " << drr_brother_ops.size()
                  << " not equal " << ir_input_value.use_count();
          break;
        }

        for (auto* drr_brother_op : drr_brother_ops) {
          if (drr_visited.count(drr_brother_op)) {
            continue;
          }
          std::pair<bool, ir::Operation*> found{false, nullptr};
          for (auto it = ir_input_value.use_begin();
               it != ir_input_value.use_end();
               ++it) {
            auto* ir_op = it.owner();
            if (ir_visited.count(ir_op)) {
              continue;
            }
            // todo()
            if (drr_brother_op->name() == ir_op->name()) {
              found = std::make_pair(true, ir_op);
              break;
            }
          }
          if (found.first) {
            drr_q.push(drr_brother_op);
            ir_q.push(found.second);
            drr_visited.insert(drr_brother_op);
            ir_visited.insert(found.second);
          }
          //  else {
          //   VLOG(6) << " --- match false: brother op not same";
          //   matched = false;
          //   break;
          // }
        }

        if (source_pattern_graph_->input_tensors().count(
                drr_input_tensors[i]->name())) {
          continue;
        }

        // check ancestor op
        auto* drr_ancestor_op = drr_input_tensors[i]->producer();
        auto* ir_ancestor_op = ir_input_value.GetDefiningOp();
        if (drr_ancestor_op->name() != ir_ancestor_op->name()) {
          VLOG(6) << " --- match false: ancestor op not same";
          matched = false;
          break;
        }

        if (drr_visited.count(drr_ancestor_op) == 0) {
          drr_q.push(drr_ancestor_op);
          ir_q.push(ir_ancestor_op);
          drr_visited.insert(drr_ancestor_op);
          ir_visited.insert(ir_ancestor_op);
        }
      }

      // op's outputs
      const auto& drr_output_tensors = drr_node->outputs();
      auto ir_output_value_size = ir_node->num_results();
      // check output's size
      if (drr_output_tensors.size() != ir_output_value_size) {
        matched = false;
        VLOG(6) << " --- match false: " << drr_output_tensors.size()
                << " not equal " << ir_output_value_size;
        break;
      }

      for (size_t i = 0; i < drr_output_tensors.size(); ++i) {
        if (!matched) break;
        // check child ops
        const auto& drr_child_ops = drr_output_tensors[i]->consumers();
        auto ir_output_value = ir_node->result(i);
        source_pattern_match_ctx->BindIrValue(
            drr_output_tensors[i]->name(),
            std::make_shared<IrValue>(ir_output_value));
        if (source_pattern_graph_->output_tensors().count(
                drr_output_tensors[i]->name())) {
          continue;
        }
        if (drr_child_ops.size() != ir_output_value.use_count()) {
          matched = false;
          VLOG(6) << " --- match false: " << drr_child_ops.size()
                  << " not equal " << ir_output_value.use_count();
          break;
        }

        for (auto* drr_child_op : drr_child_ops) {
          if (drr_visited.count(drr_child_op)) {
            continue;
          }
          std::pair<bool, ir::Operation*> found{false, nullptr};
          for (auto it = ir_output_value.use_begin();
               it != ir_output_value.use_end();
               ++it) {
            auto* ir_op = it.owner();
            if (ir_visited.count(ir_op)) {
              continue;
            }
            // todo()
            if (drr_child_op->name() == ir_op->name()) {
              found = {true, ir_op};
              break;
            }
          }
          if (found.first) {
            drr_q.push(drr_child_op);
            ir_q.push(found.second);
            drr_visited.insert(drr_child_op);
            ir_visited.insert(found.second);
          }
          // else {
          //   matched = false;
          //   VLOG(6) << " --- match false: " << drr_child_op->name()
          //           << " not found.";
          //   break;
          // }
        }
      }

      step++;
    }

    if (matched) {
      VLOG(6) << "step: " << step
              << " CountOfOpCalls: " << source_pattern_graph_->CountOfOpCalls();
      IR_ENFORCE(step == source_pattern_graph_->CountOfOpCalls(),
                 "step not equal to count of opcalls");
    } else {
      VLOG(6) << " --- match false: " << op->name();
      return matched;
    }
    // matched = matched && step == source_pattern_graph_->CountOfOpCalls();

    // Constraints
    MatchContext match_context{source_pattern_match_ctx};
    for (const auto& constraint : constraints_) {
      matched = constraint(match_context);
      if (!matched) {
        VLOG(6) << " --- match false: constraint not satisfied";
        break;
      }
    }

    return matched;
  }

  bool Bottom2UpMatch(
      std::vector<OpCall*> drr_output_sequence_candidate,
      std::vector<ir::Operation*> ir_output_sequence,
      const std::shared_ptr<MatchContextImpl>& source_pattern_match_ctx) const {
    VLOG(6) << "Assert drr_output and ir_output have equal lengths" IR_ENFORCE(
        drr_output_sequence_candidate.size() == ir_output_sequence.size());
    // init
    std::unordered_set<const OpCall*> drr_visited;
    std::unordered_set<Operation*> ir_visited;
    std::queue<const OpCall*> drr_q;
    std::queue<ir::Operation*> ir_q;
    bool matched = true;
    for (size_t i = 0; i < ir_output_sequence.size(); ++i) {
      drr_q.push(drr_output_sequence_candidate[i]);
      drr_visited.insert(drr_output_sequence_candidate[i]);
      ir_q.push(ir_output_sequence[i]);
      ir_visited.insert(ir_output_sequence[i]);
      source_pattern_match_ctx->BindIrOperation(
          drr_output_sequence_candidate[i],
          std::make_shared<IrOperation>(ir_output_sequence[i]));
    }
    size_t step = 0;
    while (!drr_q.empty()) {
      if (!matched) break;
      auto* drr_node = drr_q.front();
      auto* ir_node = ir_q.front();
      drr_q.pop();
      ir_q.pop();
      if (drr_node->name() != ir_node->name()) {
        matched = false;
        break;
      }
      const auto& drr_input_tensors = drr_node->inputs();
      auto ir_input_value_size = ir_node->num_operands();
      // check input size
      if (drr_input_tensors.size() != ir_input_value_size) {
        VLOG(6) << "Match False! drr_node input size:"
                << drr_input_tensors.size()
                << "not equal ir_node input size:" << ir_input_value_size;
        matched = false;
        break;
      }
      // check output size
      if (drr_node->outputs().size() != ir_node->num_results()) {
        VLOG(6) << "Match False! drr_node output size:"
                << drr_node->outputs().size()
                << "not equal ir_node output size:" << ir_node->num_results();
        matched = false;
        break;
      }
      // check visited
      if (drr_visited.count(drr_node) && ir_visited.count(ir_node)) {
        continue;
      } else if (!(!drr_visited.count(drr_node) &&
                   !ir_visited.count(ir_node))) {
        VLOG(6) << "binding of ir_node and drr_node is not synchronized";
        matched = false;
        break;
      }
      source_pattern_match_ctx->BindIrOperation(
          drr_node, std::make_shared<IrOperation>(ir_node));
      // join the producerOp of input
      for (size_t i = 0; i < drr_input_tensors.size(); ++i) {
        auto* drr_producer_op = drr_input_tensors[i]->producer();
        auto* ir_producer_op = ir_node->operand(i).source().GetDefiningOp();
        if (drr_producer_op->name() != ir_producer_op->name()) {
          VLOG(6) << "Match False! drr_producer_op name:"
                  << drr_node->outputs().size()
                  << "not equal ir_producer_op node:" << ir_node->num_results();
          matched = false;
          break;
        } else {
          drr_q.push(drr_producer_op);
          ir_q.push(ir_producer_op);
          drr_visited.insert(drr_producer_op);
          ir_visited.insert(ir_producer_op);
        }
      }

      ++step;
    }

    if (matched) {
      IR_ENFORCE(step == source_pattern_graph_->CountOfOpCalls());
    } else {
      return matched;
    }

    MatchContext match_context{source_pattern_match_ctx};
    for (const auto& constraint : constraints_) {
      matched = constraint(match_context);
      if (!matched) break;
    }

    return matched;
  }

  void PatternGraphRewrite(const MatchContextImpl& source_pattern_match_ctx,
                           ir::PatternRewriter& rewriter) const {  // NOLINT
    VLOG(6) << "Create Operations in result_pattern_graph";
    MatchContextImpl res_match_ctx = CreateOperations(
        *result_pattern_graph_, source_pattern_match_ctx, rewriter);
    VLOG(6) << "Process Assign Tensor";
    RebindIrTensorForAssignTensor(*result_pattern_graph_, &res_match_ctx);
    VLOG(6) << "Replace Output Values in source_pattern_graph by Output Values "
               "in result_pattern_graph";
    ReplaceOutputTensor(source_pattern_match_ctx, res_match_ctx, rewriter);
    VLOG(6) << "Delete Operations in source_pattern_graph";
    DeleteSourcePatternOp(
        *source_pattern_graph_, source_pattern_match_ctx, rewriter);
  }

 private:
  MatchContextImpl CreateOperations(
      const ResultPatternGraph& result_pattern_graph,
      const MatchContextImpl& src_match_ctx,
      ir::PatternRewriter& rewriter) const {  // NOLINT
    MatchContextImpl res_match_ctx;
    // add input tensors info for res_match_ctx
    for (const auto& in_tensor : result_pattern_graph.input_tensors()) {
      IR_ENFORCE(result_pattern_graph.id2owend_tensor().count(in_tensor),
                 "Drr input tensor [%s] must exists in result pattern graph.",
                 in_tensor);
      if (!result_pattern_graph.id2owend_tensor().at(in_tensor)->is_none()) {
        res_match_ctx.BindIrValue(
            in_tensor,
            std::make_shared<IrValue>(src_match_ctx.GetIrValue(in_tensor)));
      }
    }

    // topo order visit result_pattern_graph
    GraphTopo graph_topo_visit(&result_pattern_graph);
    graph_topo_visit.WalkGraphNodesTopoOrder(
        [&src_match_ctx, &rewriter, &res_match_ctx](const OpCall& op_call) {
          CreateOperation(op_call, src_match_ctx, rewriter, &res_match_ctx);
        });

    return res_match_ctx;
  }

  void RebindIrTensorForAssignTensor(
      const ResultPatternGraph& result_pattern_graph,
      MatchContextImpl* res_match_ctx) const {
    const auto& tensor_assign_map = result_pattern_graph.tensor_assign_map();
    for (const auto& kv : tensor_assign_map) {
      const auto& src_tensor_name = kv.first;
      const auto& dst_tensor_name = kv.second;
      res_match_ctx->BindIrValue(
          src_tensor_name,
          std::make_shared<IrValue>(
              res_match_ctx->GetIrValue(dst_tensor_name)));
    }
  }

  void ReplaceOutputTensor(const MatchContextImpl& src_match_ctx,
                           const MatchContextImpl& res_match_ctx,
                           ir::PatternRewriter& rewriter) const {  // NOLINT
    for (const auto& output_name : source_pattern_graph_->output_tensors()) {
      if (result_pattern_graph_->output_tensors().count(output_name)) {
        const auto& src_ir_tensor = src_match_ctx.GetIrValue(output_name);
        const auto& res_ir_tensor = res_match_ctx.GetIrValue(output_name);
        rewriter.ReplaceAllUsesWith(src_ir_tensor.get(), res_ir_tensor.get());
      } else {
        LOG(WARNING) << "The output tensor (" << output_name
                     << ") in the source_pattern_graph is not the output "
                        "tensor in result_pattern_graph.";
      }
    }
  }

  void DeleteSourcePatternOp(const SourcePatternGraph& source_pattern_graph,
                             const MatchContextImpl& src_match_ctx,
                             ir::PatternRewriter& rewriter) const {  // NOLINT
    std::vector<const OpCall*> topo_order_ops;
    GraphTopo graph_topo_visit(&source_pattern_graph);
    graph_topo_visit.WalkGraphNodesTopoOrder(
        [&topo_order_ops](const OpCall& op_call) {
          topo_order_ops.push_back(&op_call);
        });
    // Delete Operation with topo order from output tensors.
    std::for_each(topo_order_ops.rbegin(),
                  topo_order_ops.rend(),
                  [&src_match_ctx, &rewriter](const OpCall* op_call) {
                    IR_ENFORCE(src_match_ctx.operation_map().count(op_call),
                               "Drr OpCall [%s] must exists in match context.",
                               op_call->name());
                    auto* op = src_match_ctx.operation_map().at(op_call)->get();
                    VLOG(6) << "Delete (" << op_call->name() << " @" << op_call
                            << " :@" << op << ") in source_pattern_graph ";
                    rewriter.EraseOp(op);
                  });
  }

  const std::shared_ptr<SourcePatternGraph> source_pattern_graph_;
  const std::vector<Constraint> constraints_;
  const std::shared_ptr<ResultPatternGraph> result_pattern_graph_;
};

}  // namespace drr
}  // namespace ir
