#pragma once

#include <string>
#include <vector>

#include "simple_executor.h"

enum class PhysicalPlanKind {
  Simple,
  Complex
};

struct PhysicalPlan {
  PhysicalPlanKind kind = PhysicalPlanKind::Simple;
  std::string raw;
  SimplePlan simple;
  bool has_function_call = false;
};

struct PlanPartition {
  std::vector<PhysicalPlan> plans;
  bool has_function_call = false;
};
