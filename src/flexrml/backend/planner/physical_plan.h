#pragma once

#include <string>
#include <vector>

#include "complex_plan.h"
#include "simple_plan.h"

enum class PhysicalPlanKind {
  Simple,
  Complex
};

struct PhysicalPlan {
  PhysicalPlanKind kind = PhysicalPlanKind::Simple;
  SimplePlan simple;
  ComplexPlan complex;
  bool has_function_call = false;
};

struct PlanPartition {
  std::vector<PhysicalPlan> plans;
  bool has_function_call = false;
};
