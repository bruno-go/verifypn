#include "PetriEngine/ExplicitColored/ExpressionCompilers/ExplicitQueryPropositionCompiler.h"
#include <charconv>
#include <functional>
#include <numeric>
#include "PetriEngine/ExplicitColored/FireabilityChecker.h"
#include "PetriEngine/PQL/Visitor.h"
#include "PetriEngine/PQL/Expressions.h"

namespace PetriEngine::ExplicitColored {
    MarkingCount_t minShortCircuit(
        const ColoredPetriNetMarking& marking,
        const std::vector<std::unique_ptr<ExplicitQueryProposition>>& expressions,
        const bool neg
    ) {
        auto min = std::numeric_limits<MarkingCount_t>::max();
        for (const auto& expression : expressions) {
            const auto dist = expression->distance(marking, neg);
            if (dist == 0) {
                return 0;
            }
            if (dist < min) {
                min = dist;
            }
        }
        return min;
    }

    class GammaQueryAndExpression final : public ExplicitQueryProposition {
    public:
        explicit GammaQueryAndExpression(std::vector<std::unique_ptr<ExplicitQueryProposition>> expressions)
            : _expressions(std::move(expressions)) {}

        [[nodiscard]] bool eval(const ColoredSuccessorGenerator& successorGenerator, const ColoredPetriNetMarking &marking, size_t id) const override {
            for (const auto& expression : _expressions) {
                if (!expression->eval(successorGenerator, marking, id)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] MarkingCount_t distance(const ColoredPetriNetMarking &marking,
            const bool neg) const override {
            if (neg) {
                return minShortCircuit(marking, _expressions, true);
            }
            return std::accumulate(_expressions.begin(), _expressions.end(), 0,
                [&](const MarkingCount_t sum, const std::unique_ptr<ExplicitQueryProposition> &expression) {
                    return sum + expression->distance(marking, false);
                }
            );
        }

    private:
        std::vector<std::unique_ptr<ExplicitQueryProposition>> _expressions;
    };

    class GammaQueryOrExpression final : public ExplicitQueryProposition {
    public:
        explicit GammaQueryOrExpression(std::vector<std::unique_ptr<ExplicitQueryProposition>> expressions)
            : _expressions(std::move(expressions)) {}

        [[nodiscard]] bool eval(const ColoredSuccessorGenerator& successorGenerator, const ColoredPetriNetMarking &marking, size_t id) const override {
            for (const auto& expression : _expressions) {
                if (expression->eval(successorGenerator, marking, id)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] MarkingCount_t distance(const ColoredPetriNetMarking &marking,
            const bool neg) const override {
            if (neg) {
                return std::accumulate(_expressions.begin(), _expressions.end(), 0,
                    [&](const MarkingCount_t sum, const std::unique_ptr<ExplicitQueryProposition> &expression) {
                        return sum + expression->distance(marking, true);
                    }
                );
            }
            return minShortCircuit(marking, _expressions, false);
        }

    private:
        std::vector<std::unique_ptr<ExplicitQueryProposition>> _expressions;
    };

    class GammaQueryNotExpression final : public ExplicitQueryProposition {
    public:
        explicit GammaQueryNotExpression(std::unique_ptr<ExplicitQueryProposition> inner)
            : _inner(std::move(inner)){}

        [[nodiscard]] bool eval(const ColoredSuccessorGenerator& successorGenerator, const ColoredPetriNetMarking &marking, size_t id) const override {
            return !_inner->eval(successorGenerator, marking, id);
        }

        [[nodiscard]] MarkingCount_t distance(const ColoredPetriNetMarking &marking,
            const bool neg) const override {
            return _inner->distance(marking, !neg);
        }

    private:
        std::unique_ptr<ExplicitQueryProposition> _inner;
    };

    class QueryValue {
    public:
        static QueryValue fromExpression(
            const PQL::Expr& expression,
            const std::unordered_map<std::string, uint32_t>& placeNameIndices,
            const ColoredPetriNet& net
        ) {
            switch (expression.type()) {
                case PQL::type_id<PQL::LiteralExpr>(): {
                    const auto& literalExpression = static_cast<const PQL::LiteralExpr&>(expression);
                    return QueryValue{[value = literalExpression.value()](const auto&) { return value; }};
                }
                case PQL::type_id<PQL::IdentifierExpr>(): {
                    const auto& identifierExpression = static_cast<const PQL::IdentifierExpr&>(expression);
                    return placeValue(*identifierExpression.name(), placeNameIndices, net);
                }
                case PQL::type_id<PQL::UnfoldedIdentifierExpr>(): {
                    const auto& identifierExpression = static_cast<const PQL::UnfoldedIdentifierExpr&>(expression);
                    return placeValue(*identifierExpression.name(), placeNameIndices, net);
                }
                case PQL::type_id<PQL::PlusExpr>(): {
                    const auto& plusExpression = static_cast<const PQL::PlusExpr&>(expression);
                    std::vector<QueryValue> values;
                    for (const auto& [_, name] : plusExpression.places()) {
                        values.push_back(placeValue(*name, placeNameIndices, net));
                    }

                    for (const auto& child : plusExpression.expressions()) {
                        values.push_back(fromExpression(*child, placeNameIndices, net));
                    }

                    return QueryValue{[constant = plusExpression.constant(), values = std::move(values)](const auto& marking) {
                        auto result = constant;
                        for (const auto& value : values) result += value.getCount(marking);
                        return result;
                    }};
                }
                case PQL::type_id<PQL::SubtractExpr>(): {
                    const auto& subtractExpression = static_cast<const PQL::SubtractExpr&>(expression);
                    if (subtractExpression.operands() == 0) {
                        throw base_error("Invalid subtraction expression");
                    }

                    std::vector<QueryValue> values;
                    values.push_back(fromExpression(*subtractExpression[0], placeNameIndices, net));
                    for (size_t i = 1; i < subtractExpression.operands(); ++i) {
                        values.push_back(fromExpression(*subtractExpression[i], placeNameIndices, net));
                    }

                    return QueryValue{[values = std::move(values)](const auto& marking) {
                        auto result = values[0].getCount(marking);
                        for (size_t i = 1; i < values.size(); ++i) result -= values[i].getCount(marking);
                        return result;
                    }};
                }
                case PQL::type_id<PQL::MinusExpr>(): {
                    const auto& minusExpression = static_cast<const PQL::MinusExpr&>(expression);
                    auto value = fromExpression(*minusExpression[0], placeNameIndices, net);
                    return QueryValue{[value = std::move(value)](const auto& marking) { return -value.getCount(marking); }};
                }
                case PQL::type_id<PQL::MultiplyExpr>(): {
                    const auto& multiplyExpression = static_cast<const PQL::MultiplyExpr&>(expression);
                    std::vector<QueryValue> values;
                    for (const auto& [_, name] : multiplyExpression.places()) {
                        values.push_back(placeValue(*name, placeNameIndices, net));
                    }

                    for (const auto& child : multiplyExpression.expressions()) {
                        values.push_back(fromExpression(*child, placeNameIndices, net));
                    }

                    return QueryValue{[constant = multiplyExpression.constant(), values = std::move(values)](const auto& marking) {
                        auto result = constant;
                        for (const auto& value : values) result *= value.getCount(marking);
                        return result;
                    }};
                }
                default:
                    throw base_error("Invalid expression type");
            }
        }

        [[nodiscard]] int64_t getCount(const ColoredPetriNetMarking& marking) const {
            return _evaluate(marking);
        }
    private:
        using Evaluator = std::function<int64_t(const ColoredPetriNetMarking&)>;

        explicit QueryValue(Evaluator evaluate) : _evaluate(std::move(evaluate)) {}

        static QueryValue placeValue(const std::string& name,
                                     const std::unordered_map<std::string, uint32_t>& placeNameIndices,
                                     const ColoredPetriNet& net) {
            if (const auto place = placeNameIndices.find(name); place != placeNameIndices.end()) {
                return QueryValue{[place = place->second](const auto& marking) { return marking.getPlaceCount(place); }};
            }

            const auto separator = name.rfind('_');
            if (separator == std::string::npos) {
                throw base_error("Unknown place '", name, "'");
            }

            const auto place = placeNameIndices.find(name.substr(0, separator));
            Color_t colorId;
            const auto colorName = std::string_view{name}.substr(separator + 1);
            const auto [end, error] = std::from_chars(colorName.begin(), colorName.end(), colorId);
            if (place == placeNameIndices.end() || error != std::errc{} || end != colorName.end()) {
                throw base_error("Unknown colored place '", name, "'");
            }

            const auto& colorType = *net.getPlaces()[place->second].colorType;
            if (colorId >= colorType.colorSize) {
                throw base_error("Unknown colored place '", name, "'");
            }

            std::vector<Color_t> colorSequence;
            colorSequence.reserve(colorType.basicColorSizes.size());
            for (const auto size : colorType.basicColorSizes) {
                colorSequence.push_back(colorId % size);
                colorId /= size;
            }

            const auto color = static_cast<Color_t>(ColorSequence{colorSequence, colorType}.encodedValue);
            return QueryValue{[place = place->second, color](const auto& marking) {
                return marking.markings[place].getCount(ColorSequence{color});
            }};
        }

        Evaluator _evaluate;
    };

    class GammaQueryLessThanExpression final : public ExplicitQueryProposition {
    public:
        GammaQueryLessThanExpression(QueryValue lhs, QueryValue rhs)
            : _lhs(std::move(lhs)), _rhs(std::move(rhs)) {}

        [[nodiscard]] bool eval(const ColoredSuccessorGenerator& successorGenerator, const ColoredPetriNetMarking &marking, size_t id) const override {
            return _lhs.getCount(marking) < _rhs.getCount(marking);
        }

        [[nodiscard]] MarkingCount_t distance(const ColoredPetriNetMarking &marking,
            const bool neg) const override {
            const auto lhs = _lhs.getCount(marking);
            const auto rhs = _rhs.getCount(marking);
            if (neg) {
                if (lhs >= rhs) {
                    return 0;
                }
                return rhs - lhs;
            }
            if (lhs < rhs) {
                return 0;
            }
            return lhs - rhs + 1;
        }

    private:
        QueryValue _lhs;
        QueryValue _rhs;
    };

    class GammaQueryLessThanOrEqualExpression final : public ExplicitQueryProposition {
    public:
        GammaQueryLessThanOrEqualExpression(QueryValue lhs, QueryValue rhs)
            : _lhs(std::move(lhs)), _rhs(std::move(rhs)) {}

        [[nodiscard]] bool eval(const ColoredSuccessorGenerator& successorGenerator, const ColoredPetriNetMarking &marking, size_t id) const override {
            return _lhs.getCount(marking) <= _rhs.getCount(marking);
        }

        [[nodiscard]] MarkingCount_t distance(const ColoredPetriNetMarking &marking,
            const bool neg) const override {
            const auto lhs = _lhs.getCount(marking);
            const auto rhs = _rhs.getCount(marking);
            if (neg) {
                if (lhs > rhs) {
                    return 0;
                }
                return rhs - lhs + 1;
            }
            if (lhs <= rhs) {
                return 0;
            }
            return lhs - rhs;
        }

    private:
        QueryValue _lhs;
        QueryValue _rhs;
    };

    class GammaQueryEqualExpression final : public ExplicitQueryProposition {
    public:
        GammaQueryEqualExpression(QueryValue lhs, QueryValue rhs)
            : _lhs(std::move(lhs)), _rhs(std::move(rhs)) {}

        [[nodiscard]] bool eval(const ColoredSuccessorGenerator& successorGenerator, const ColoredPetriNetMarking &marking, size_t id) const override {
            return _lhs.getCount(marking) == _rhs.getCount(marking);
        }

        [[nodiscard]] MarkingCount_t distance(const ColoredPetriNetMarking &marking, const bool neg) const override {
            const auto lhs = _lhs.getCount(marking);
            const auto rhs = _rhs.getCount(marking);
            if (neg)
                return lhs == rhs ? 1 : 0;
            return lhs > rhs ? lhs - rhs : rhs - lhs;
        }

    private:
        QueryValue _lhs;
        QueryValue _rhs;
    };

    class GammaQueryNotEqualExpression final : public ExplicitQueryProposition {
    public:
        GammaQueryNotEqualExpression(QueryValue lhs, QueryValue rhs)
            : _lhs(std::move(lhs)), _rhs(std::move(rhs)) {}

        [[nodiscard]] bool eval(const ColoredSuccessorGenerator& successorGenerator, const ColoredPetriNetMarking &marking, size_t id) const override {
            return _lhs.getCount(marking) != _rhs.getCount(marking);
        }

        [[nodiscard]] MarkingCount_t distance(const ColoredPetriNetMarking &marking, const bool neg) const override {
            const auto lhs = _lhs.getCount(marking);
            const auto rhs = _rhs.getCount(marking);
            if (neg)
                return lhs > rhs ? lhs - rhs : rhs - lhs;
            return lhs == rhs ? 1 : 0;
        }

    private:
        QueryValue _lhs;
        QueryValue _rhs;
    };

    class GammaQueryDeadlockExpression final : public ExplicitQueryProposition {
    public:
        [[nodiscard]] bool eval(const ColoredSuccessorGenerator& successorGenerator, const ColoredPetriNetMarking &marking, size_t id) const override {
           return FireabilityChecker::hasDeadlock(successorGenerator, marking, id);
        }

        [[nodiscard]] MarkingCount_t distance(const ColoredPetriNetMarking &marking, const bool neg) const override {
            return 0;
        }
    };

    class GammaQueryFireabilityExpression final : public ExplicitQueryProposition {
    public:
        explicit GammaQueryFireabilityExpression(const Transition_t transitionId)
            : _transitionId(transitionId) {}

        [[nodiscard]] bool eval(const ColoredSuccessorGenerator& successorGenerator, const ColoredPetriNetMarking &marking, size_t id) const override {
            return FireabilityChecker::canFire(successorGenerator, _transitionId, marking, id);
        }

        [[nodiscard]] MarkingCount_t distance(const ColoredPetriNetMarking &marking, bool neg) const override {
            return 0;
        }

    private:
        Transition_t _transitionId;
    };
    
    class GammaQueryCompilerVisitor final : public PQL::Visitor {
        public:
            static std::unique_ptr<ExplicitQueryProposition> compile(
                const PQL::Condition_ptr& expr,
                const ColoredPetriNet& net,
                const std::unordered_map<std::string, uint32_t>& placeNameIndices,
                const std::unordered_map<std::string, uint32_t>& transitionNameIndices,
                const ColoredSuccessorGenerator& successorGenerator
            ) {
                GammaQueryCompilerVisitor visitor{net, placeNameIndices, transitionNameIndices, successorGenerator};
                visit(visitor, expr);

                return std::move(visitor._compiled);
            }
        protected:
            explicit GammaQueryCompilerVisitor(
                const ColoredPetriNet& net,
                const std::unordered_map<std::string, uint32_t>& placeNameIndices,
                const std::unordered_map<std::string, uint32_t>& transitionNameIndices,
                const ColoredSuccessorGenerator& successorGenerator
            )
                : _successorGenerator(successorGenerator), _net(net), _placeNameIndices(placeNameIndices),
                  _transitionNameIndices(transitionNameIndices) { }

            void _accept(const PQL::NotCondition *element) override {
                visit(this, element->getCond().get());
                _compiled = std::make_unique<GammaQueryNotExpression>(std::move(_compiled));
            }

            void _accept(const PQL::AndCondition *expr) override {
                std::vector<std::unique_ptr<ExplicitQueryProposition>> expressions;
                for (const auto& subExpr : *expr) {
                    visit(this, subExpr.get());
                    expressions.emplace_back(std::move(_compiled));
                }
                _compiled = std::make_unique<GammaQueryAndExpression>(std::move(expressions));
            }

            void _accept(const PQL::OrCondition *expr) override {
                std::vector<std::unique_ptr<ExplicitQueryProposition>> expressions;
                for (const auto& subExpr : *expr) {
                    visit(this, subExpr.get());
                    expressions.emplace_back(std::move(_compiled));
                }
                _compiled = std::make_unique<GammaQueryOrExpression>(std::move(expressions));
            }

            void _accept(const PQL::LessThanCondition *element) override {
                _compiled = std::make_unique<GammaQueryLessThanExpression>(
                    QueryValue::fromExpression(*element->getExpr1(), _placeNameIndices, _net),
                    QueryValue::fromExpression(*element->getExpr2(), _placeNameIndices, _net)
                );
            }

            void _accept(const PQL::LessThanOrEqualCondition *element) override {
                _compiled = std::make_unique<GammaQueryLessThanOrEqualExpression>(
                    QueryValue::fromExpression(*element->getExpr1(), _placeNameIndices, _net),
                    QueryValue::fromExpression(*element->getExpr2(), _placeNameIndices, _net)
                );
            }

            void _accept(const PQL::EqualCondition *element) override {
                _compiled = std::make_unique<GammaQueryEqualExpression>(
                    QueryValue::fromExpression(*element->getExpr1(), _placeNameIndices, _net),
                    QueryValue::fromExpression(*element->getExpr2(), _placeNameIndices, _net)
                );
            }

            void _accept(const PQL::NotEqualCondition *element) override {
                _compiled = std::make_unique<GammaQueryNotEqualExpression>(
                    QueryValue::fromExpression(*element->getExpr1(), _placeNameIndices, _net),
                    QueryValue::fromExpression(*element->getExpr2(), _placeNameIndices, _net)
                );
            }

            void _accept(const PQL::DeadlockCondition *element) override {
                _compiled = std::make_unique<GammaQueryDeadlockExpression>();
            }

            void _accept(const PQL::FireableCondition *element) override {
                const auto transitionNameIndex = _transitionNameIndices.find(*element->getName());
                if (transitionNameIndex == _transitionNameIndices.end()) {
                    throw base_error("Unknown transition name '", *element->getName(), "'");
                }
                _compiled = std::make_unique<GammaQueryFireabilityExpression>(transitionNameIndex->second);
            }

            void _accept(const PQL::EFCondition *condition) override {
                notSupported("Does not supported nested quantifiers");
            }

            void _accept(const PQL::AGCondition *condition) override {
                notSupported("Does not supported nested quantifiers");
            }

            void _accept(const PQL::LiteralExpr *element) override {
                invalid();
            }

            void _accept(const PQL::IdentifierExpr *element) override {
                invalid();
            }

            void _accept(const PQL::CompareConjunction *element) override  { notSupported("CompareConjunction"); }
            void _accept(const PQL::UnfoldedUpperBoundsCondition *element) override { notSupported("UnfoldedUpperBoundsCondition"); }
            void _accept(const PQL::CommutativeExpr *element) override  { notSupported("CommutativeExpr"); }
            void _accept(const PQL::SimpleQuantifierCondition *element) override  { notSupported("SimpleQuantifierCondition"); }
            void _accept(const PQL::LogicalCondition *element) override  { notSupported("LogicalCondition"); }
            void _accept(const PQL::CompareCondition *element) override  { notSupported("CompareCondition"); }
            void _accept(const PQL::UntilCondition *element) override  { notSupported("UntilCondition"); }
            void _accept(const PQL::ControlCondition *condition) override  { notSupported("ControlCondition"); }
            void _accept(const PQL::PathQuant *element) override  { notSupported("PathQuant"); }
            void _accept(const PQL::ExistPath *element) override  { notSupported("ExistPath"); }
            void _accept(const PQL::AllPaths *element) override  { notSupported("AllPaths"); }
            void _accept(const PQL::PathSelectCondition *element) override  { notSupported("PathSelectCondition"); }
            void _accept(const PQL::PathSelectExpr *element) override  { notSupported("PathSelectExpr"); }
            void _accept(const PQL::EGCondition *condition) override  { notSupported("EGCondition"); }
            void _accept(const PQL::AFCondition *condition) override  { notSupported("AFCondition"); }
            void _accept(const PQL::EXCondition *condition) override  { notSupported("EXCondition"); }
            void _accept(const PQL::AXCondition *condition) override  { notSupported("AXCondition"); }
            void _accept(const PQL::EUCondition *condition) override  { notSupported("EUCondition"); }
            void _accept(const PQL::AUCondition *condition) override  { notSupported("AUCondition"); }
            void _accept(const PQL::ACondition *condition) override  { notSupported("ACondition"); }
            void _accept(const PQL::ECondition *condition) override  { notSupported("ECondition"); }
            void _accept(const PQL::GCondition *condition) override  { notSupported("GCondition"); }
            void _accept(const PQL::FCondition *condition) override  { notSupported("FCondition"); }
            void _accept(const PQL::XCondition *condition) override  { notSupported("XCondition"); }
            void _accept(const PQL::ShallowCondition *element) override  { notSupported("ShallowCondition"); }
            void _accept(const PQL::UnfoldedFireableCondition *element) override  { notSupported("UnfoldedFireableCondition"); }
            void _accept(const PQL::UpperBoundsCondition *element) override  { notSupported("UpperBoundsCondition"); }
            void _accept(const PQL::LivenessCondition *element) override  { notSupported("LivenessCondition"); }
            void _accept(const PQL::KSafeCondition *element) override  { notSupported("KSafeCondition"); }
            void _accept(const PQL::QuasiLivenessCondition *element) override  { notSupported("QuasiLivenessCondition"); }
            void _accept(const PQL::StableMarkingCondition *element) override  { notSupported("StableMarkingCondition"); }
            void _accept(const PQL::BooleanCondition *element) override  { notSupported("BooleanCondition"); }
            void _accept(const PQL::UnfoldedIdentifierExpr *element) override  { notSupported("UnfoldedIdentifierExpr"); }
            void _accept(const PQL::PlusExpr *element) override  { notSupported("PlusExpr"); }
            void _accept(const PQL::MultiplyExpr *element) override { notSupported("MultiplyExpr"); }
            void _accept(const PQL::MinusExpr *element) override  { notSupported("MinusExpr"); }
            void _accept(const PQL::NaryExpr *element) override  { notSupported("NaryExpr"); }
            void _accept(const PQL::SubtractExpr *element) override { notSupported("SubtractExpr"); }
        private:
            static void notSupported() {
                throw base_error("Not supported");
            }

            static void notSupported(const std::string& type) {
                throw base_error("Not supported ", type);
            }

            static void invalid() {
                throw base_error("Invalid expression");
            }

            const ColoredSuccessorGenerator& _successorGenerator;
            const ColoredPetriNet& _net;
            const std::unordered_map<std::string, uint32_t>& _placeNameIndices;
            const std::unordered_map<std::string, uint32_t>& _transitionNameIndices;
            std::unique_ptr<ExplicitQueryProposition> _compiled;
        };


    ExplicitQueryPropositionCompiler::ExplicitQueryPropositionCompiler(
        const ColoredPetriNet& net,
        const std::unordered_map<std::string, uint32_t>& placeNameIndices,
        const std::unordered_map<std::string, uint32_t>& transitionNameIndices,
        const ColoredSuccessorGenerator& successorGenerator
    ) : _net(net), _placeNameIndices(placeNameIndices),
        _transitionNameIndices(transitionNameIndices), _successorGenerator(successorGenerator) {}

    std::unique_ptr<ExplicitQueryProposition> ExplicitQueryPropositionCompiler::compile(const PQL::Condition_ptr &expression) const {
        return GammaQueryCompilerVisitor::compile(expression, _net, _placeNameIndices, _transitionNameIndices,
                                                   _successorGenerator);
    }
}
