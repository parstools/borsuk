#include "coge/PropertyExpression.h"

int main() {
  using coge::PropertyExprKind;
  using coge::PropertyExpression;
  using coge::PropertyValueKind;
  const PropertyExpression left{PropertyExprKind::Binding,
                                PropertyValueKind::Scalar, "a", {}};
  const PropertyExpression right{PropertyExprKind::IntegerLiteral,
                                 PropertyValueKind::Scalar, "1", {}};
  const PropertyExpression sum{PropertyExprKind::Oracle,
                               PropertyValueKind::Mathematical, "+",
                               {left, right}};
  const PropertyExpression comparison{PropertyExprKind::Comparison,
                                      PropertyValueKind::Boolean, "<=",
                                      {sum, right}};
  if (coge::emitPropertyExpressionRust(comparison) !=
          "((i128::from(a) + 1_i128) <= 1_i128)" ||
      coge::emitPropertyArgumentRust(sum) !=
          "i128::from(a) + 1_i128")
    return 1;
  return 0;
}
