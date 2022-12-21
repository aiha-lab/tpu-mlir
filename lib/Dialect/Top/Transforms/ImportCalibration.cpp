//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// TPU-MLIR is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Dialect/Top/Transforms/Passes.h"
#include "tpu_mlir/Support/MathUtils.h"
#include "tpu_mlir/Support/Helper/Module.h"

#include "mlir/IR/PatternMatch.h"
#include "mlir/Dialect/Quant/QuantTypes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include <sstream>
#include <fstream>
#include <regex>

using namespace llvm;
using namespace mlir;
using namespace tpu_mlir::helper;
namespace tpu_mlir {
namespace top {

typedef struct {
  double threshold;
  double min;
  double max;
} cali_info;

class ImportCalibrationTablePass
    : public ImportCalibrationTableBase<ImportCalibrationTablePass> {
public:
  ImportCalibrationTablePass() {}
  void runOnOperation() override {
    // llvm::errs() << "import calibration table:" << this->tableFile
    //              << ", is asymmetric " << this->isAsymmetric << "\n";
    auto module = getOperation();
    if (!Module::isState(module, Module::State::TOP_F32)) {
      module.dump();
      llvm_unreachable("wrong mlir state");
    }
    OpBuilder builder(module);
    std::map<std::string, cali_info> calibration_map;
    std::map<std::string, std::shared_ptr<std::vector<double>>>
        per_chan_scales_map;
    std::ifstream infile(this->tableFile);
    if (!infile) {
      llvm_unreachable("can't open calibration table file!");
    }
    std::string line;
    std::regex cali_pattern("\\S+\\s+[-0-9.e]+\\s+[-0-9.e]+\\s+[-0-9.e]+");
    std::regex info_pattern("#.*");
    bool weight_scale_meeted = false;
    while (std::getline(infile, line)) {
      if (line.back() == '\r') {
        line.pop_back();
      }

      if (weight_scale_meeted && std::string::npos != line.find("_weight")) {
        std::string name;
        double value;
        int num = 0;
        std::istringstream iss(line);
        iss >> name;
        iss >> num;
        auto vScales = std::make_shared<std::vector<double>>(num);
        for (int i = 0; i < num; i++) {
          iss >> value;
          vScales->data()[i] = value;
        }
        per_chan_scales_map[name] = vScales;
      } else {
        std::istringstream iss(line);
        std::string name;
        if (std::regex_match(line, cali_pattern)) {
          cali_info info = {0, 0, 0};
          if (!(iss >> name >> info.threshold >> info.min >> info.max)) {
            break;
          }
          calibration_map[name] = info;
        } else if (std::regex_match(line, info_pattern)) {
          if (std::string::npos != line.find("#weight_scale"))
            weight_scale_meeted = true;
        } else {
          // Format of threshold table error
          llvm::errs() << line;
          llvm_unreachable("\n  => not match required format\n");
        }
      }
    }
    double min, max;
    for (auto func : module.getOps<FuncOp>()) {
      func.walk([&](Operation *op) {
        if (isa<tpu_mlir::InferenceInterface>(op) || isa<InputOp>(op)) {
          for (auto value : op->getResults()) {
            if (value.getType().isa<mlir::NoneType>()) {
              continue;
            }
            auto type = value.getType().cast<RankedTensorType>();
            if (type.getElementType().isIntOrIndex()) {
              continue;
            }

            auto name = Module::getName(value).str();
            if (calibration_map.find(name) != calibration_map.end()) {
              auto &info = calibration_map[name];
              getMinMax(op, info, min, max);
              auto quant_type = quant::CalibratedQuantizedType::get(
                  type.getElementType(), min, max);
              auto new_type =
                  RankedTensorType::get(type.getShape(), quant_type);
              value.setType(new_type);
            }
          }
        } else if (isa<WeightOp>(op)) {
          auto user = op->getUsers().begin();
          std::string str = Module::getName(*user).str() + "_weight";
          if (per_chan_scales_map.count(str)) {
            op->setAttr("scale", builder.getF64ArrayAttr(ArrayRef<double>{
                                     *per_chan_scales_map[str]}));
          }
        }
      });
    }
    Module::updateModuleTypes(module);
    Module::setState(module, Module::State::TOP_CALIBRATED);
  }
  void getMinMax(Operation *op, const cali_info &info, double &min,
                 double &max) {
    if (isa<top::AbsOp>(op)) {
      min = -info.threshold;
      max = info.threshold;
    } else if (isa<top::SigmoidOp>(op)) {
      min = 0;
      max = 1;
    } else if (isAsymmetric == false) {
      min = info.min < 0 ? (-info.threshold) : 0;
      max = info.threshold;
    } else {
      min = info.min;
      max = info.max;
    }
  }
};

std::unique_ptr<OperationPass<ModuleOp>> createImportCalibrationTablePass() {
  return std::make_unique<ImportCalibrationTablePass>();
}

} // namespace top
} // namespace tpu_mlir
