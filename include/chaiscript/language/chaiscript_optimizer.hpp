// This file is distributed under the BSD License.
// See "license.txt" for details.
// Copyright 2009-2012, Jonathan Turner (jonathan@emptycrate.com)
// Copyright 2009-2016, Jason Turner (jason@emptycrate.com)
// http://www.chaiscript.com

#ifndef CHAISCRIPT_OPTIMIZER_HPP_
#define CHAISCRIPT_OPTIMIZER_HPP_

#include "chaiscript_eval.hpp"


namespace chaiscript {
  namespace optimizer {

    template<typename ... T>
      struct Optimizer : T...
    {
      Optimizer() = default;
      Optimizer(T ... t)
        : T(std::move(t))...
      {
      }

      template<typename Tracer>
      auto optimize(eval::AST_Node_Impl_Ptr<Tracer> p) {
        (void)std::initializer_list<int>{ (p = T::optimize(p), 0)... };
        return p;
      }
    };

    template<typename T>
      auto child_at(const eval::AST_Node_Impl_Ptr<T> &node, const size_t offset) {
        if (node->identifier == AST_Node_Type::Compiled) {
          return dynamic_cast<const eval::Compiled_AST_Node<T>&>(*node).m_original_node->children[offset];
        } else {
          return node->children[offset];
        }
      }

    template<typename T>
      auto child_count(const eval::AST_Node_Impl_Ptr<T> &node) {
        if (node->identifier == AST_Node_Type::Compiled) {
          return dynamic_cast<const eval::Compiled_AST_Node<T>&>(*node).m_original_node->children.size();
        } else {
          return node->children.size();
        }
      }

    template<typename T, typename Callable>
      auto make_compiled_node(const eval::AST_Node_Impl_Ptr<T> &original_node, std::vector<eval::AST_Node_Impl_Ptr<T>> children, Callable callable)
      {
        return chaiscript::make_shared<eval::AST_Node_Impl<T>, eval::Compiled_AST_Node<T>>(original_node, std::move(children), std::move(callable));
      }


    struct Return {
      template<typename T>
      auto optimize(const eval::AST_Node_Impl_Ptr<T> &p)
      {
        if (p->identifier == AST_Node_Type::Def
            && !p->children.empty())
        {
          auto &last_child = p->children.back();
          if (last_child->identifier == AST_Node_Type::Block) {
            auto &block_last_child = last_child->children.back();
            if (block_last_child->identifier == AST_Node_Type::Return) {
              if (block_last_child->children.size() == 1) {
                last_child->children.back() = block_last_child->children[0];
              }
            }
          }
        }

        return p;
      }
    };

    template<typename T>
    bool contains_var_decl_in_scope(const T &node)
    {
      if (node->identifier == AST_Node_Type::Var_Decl) {
        return true;
      }

      const auto num = child_count(node);

      for (size_t i = 0; i < num; ++i) {
        const auto &child = child_at(node, i);
        if (child->identifier != AST_Node_Type::Block
            && contains_var_decl_in_scope(child)) {
          return true;
        }
      }

      return false;
    }

    struct Block {
      template<typename T>
      auto optimize(const eval::AST_Node_Impl_Ptr<T> &node) {
        if (node->identifier == AST_Node_Type::Block
            && node->children.size() == 1
            && !contains_var_decl_in_scope(node))
        {
          return node->children[0];
        } else {
          return node;
        }
      }
    };

    struct If {
      template<typename T>
      auto optimize(const eval::AST_Node_Impl_Ptr<T> &node) {
        if ((node->identifier == AST_Node_Type::If || node->identifier == AST_Node_Type::Ternary_Cond)
             && node->children.size() >= 2
             && node->children[0]->identifier == AST_Node_Type::Constant)
        {
          const auto condition = std::dynamic_pointer_cast<eval::Constant_AST_Node<T>>(node->children[0])->m_value;
          if (condition.get_type_info().bare_equal_type_info(typeid(bool))) {
            if (boxed_cast<bool>(condition)) {
              return node->children[1];
            } else if (node->children.size() == 3) {
              return node->children[2];
            }
          }
        }

        return node;
      }
    };

    struct Constant_Fold {
      template<typename T>
      auto optimize(const eval::AST_Node_Impl_Ptr<T> &node) {

        if (node->identifier == AST_Node_Type::Binary
            && node->children.size() == 2
            && node->children[0]->identifier == AST_Node_Type::Constant
            && node->children[1]->identifier == AST_Node_Type::Constant)
        {
          try {
            const auto &oper = node->text;
            const auto parsed = Operators::to_operator(oper);
            if (parsed != Operators::Opers::invalid) {
              const auto lhs = std::dynamic_pointer_cast<eval::Constant_AST_Node<T>>(node->children[0])->m_value;
              const auto rhs = std::dynamic_pointer_cast<eval::Constant_AST_Node<T>>(node->children[1])->m_value;
              if (lhs.get_type_info().is_arithmetic() && rhs.get_type_info().is_arithmetic()) {
                const auto val  = Boxed_Number::do_oper(parsed, lhs, rhs);
                const auto match = node->children[0]->text + " " + oper + " " + node->children[1]->text;
                return chaiscript::make_shared<eval::AST_Node_Impl<T>, eval::Constant_AST_Node<T>>(std::move(match), node->location, std::move(val));
              }
            }
          } catch (const std::exception &) {
            //failure to fold, that's OK
          }
        } else if (node->identifier == AST_Node_Type::Fun_Call
                   && node->children.size() == 2
                   && node->children[0]->identifier == AST_Node_Type::Id
                   && node->children[1]->identifier == AST_Node_Type::Arg_List
                   && node->children[1]->children.size() == 1
                   && node->children[1]->children[0]->identifier == AST_Node_Type::Constant) {

          const auto arg = std::dynamic_pointer_cast<eval::Constant_AST_Node<T>>(node->children[1]->children[0])->m_value;
          if (arg.get_type_info().is_arithmetic()) {
            const auto &fun_name = node->children[0]->text;

            const auto make_constant = [&node, &fun_name](auto val){
              const auto match = fun_name + "(" + node->children[1]->children[0]->text + ")";
              return chaiscript::make_shared<eval::AST_Node_Impl<T>, eval::Constant_AST_Node<T>>(std::move(match), node->location, Boxed_Value(val));
            };

            if (fun_name == "double") {
              return make_constant(Boxed_Number(arg).get_as<double>());
            } else if (fun_name == "int") {
              return make_constant(Boxed_Number(arg).get_as<int>());
            } else if (fun_name == "float") {
              return make_constant(Boxed_Number(arg).get_as<float>());
            } else if (fun_name == "long") {
              return make_constant(Boxed_Number(arg).get_as<long>());
            }

          }

        }

        return node;
      }
    };

    struct For_Loop {
      template<typename T>
      auto optimize(const eval::AST_Node_Impl_Ptr<T> &for_node) {

        if (for_node->identifier != AST_Node_Type::For) {
          return for_node;
        }

        const auto eq_node = child_at(for_node, 0);
        const auto binary_node = child_at(for_node, 1);
        const auto prefix_node = child_at(for_node, 2);

        if (eq_node->identifier == AST_Node_Type::Equation
            && child_count(eq_node) == 2
            && child_at(eq_node, 0)->identifier == AST_Node_Type::Var_Decl
            && child_at(eq_node, 1)->identifier == AST_Node_Type::Constant
            && binary_node->identifier == AST_Node_Type::Binary
            && binary_node->text == "<"
            && child_count(binary_node) == 2
            && child_at(binary_node, 0)->identifier == AST_Node_Type::Id
            && child_at(binary_node, 0)->text == child_at(child_at(eq_node,0), 0)->text
            && child_at(binary_node, 1)->identifier == AST_Node_Type::Constant
            && prefix_node->identifier == AST_Node_Type::Prefix
            && prefix_node->text == "++"
            && child_count(prefix_node) == 1
            && child_at(prefix_node, 0)->identifier == AST_Node_Type::Id
            && child_at(prefix_node, 0)->text == child_at(child_at(eq_node,0), 0)->text)
        {
          const Boxed_Value &begin = std::dynamic_pointer_cast<const eval::Constant_AST_Node<T>>(child_at(eq_node, 1))->m_value;
          const Boxed_Value &end = std::dynamic_pointer_cast<const eval::Constant_AST_Node<T>>(child_at(binary_node, 1))->m_value;
          const std::string &id = child_at(prefix_node, 0)->text;

          if (begin.get_type_info().bare_equal(user_type<int>()) 
              && end.get_type_info().bare_equal(user_type<int>())) {

            const auto start_int = boxed_cast<int>(begin);
            const auto end_int = boxed_cast<int>(end);

            const auto body = child_at(for_node, 3);

            return make_compiled_node(for_node, {body}, 
                [id, start_int, end_int](const std::vector<eval::AST_Node_Impl_Ptr<T>> &children, const chaiscript::detail::Dispatch_State &t_ss) {
                  assert(children.size() == 1);
                  chaiscript::eval::detail::Scope_Push_Pop spp(t_ss);

                  int i = start_int;
                  t_ss.add_object(id, var(&i));

                  try {
                    for (; i < end_int; ++i) {
                      try {
                        // Body of Loop
                        children[0]->eval(t_ss);
                      } catch (eval::detail::Continue_Loop &) {
                        // we got a continue exception, which means all of the remaining 
                        // loop implementation is skipped and we just need to continue to
                        // the next iteration step
                      }
                    }
                  } catch (eval::detail::Break_Loop &) {
                    // loop broken
                  }

                  return void_var();
                }
            );
          } else {
            return for_node;
          }
        } else {
          return for_node;
        }
      }
    };


  }
}


#endif