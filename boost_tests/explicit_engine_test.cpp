#define BOOST_TEST_MODULE explicit_engine

#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>
#include <set>

#include "utils.h"
#include "PetriEngine/ExplicitColored/ExplicitColoredModelChecker.h"
#include "PetriEngine/ExplicitColored/ExpressionCompilers/ExplicitQueryPropositionCompiler.h"

using namespace PetriEngine;
using namespace PetriEngine::ExplicitColored;
namespace utf = boost::unit_test;

BOOST_AUTO_TEST_CASE(DirectoryTest) {
    BOOST_REQUIRE(getenv("TEST_FILES"));
}

void test_explicit_engine(const char* fn, ExplicitColoredModelChecker::Result expected, size_t quid = 0,
                          uint32_t kbound = 4, const char* expectedOutput = nullptr) {
    std::string model = std::string("/models/explicit-engine/") + fn + ".pnml";
    std::string query = std::string("/models/explicit-engine/") + fn + ".xml";
    std::set<size_t> qnums{quid};
    auto [queries, querynames, sset, options] = load_explicit(model, query, qnums);
    options.kbound = kbound;

    ExplicitColoredModelChecker checker(sset, std::cout);
    std::ostringstream output;
    ColoredResultPrinter printer(quid, output, querynames[0], options.seed(), output);
    auto result = checker.checkQuery(queries[0], options, expectedOutput ? &printer : nullptr);
    
    BOOST_REQUIRE_EQUAL(expected, result);
    if (expectedOutput) {
        BOOST_REQUIRE(output.str().find(expectedOutput) != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(SubtractionWithVars, * utf::timeout(5)) {
    test_explicit_engine("subtraction_with_vars", ExplicitColoredModelChecker::Result::SATISFIED);
}

BOOST_AUTO_TEST_CASE(ReferendumColoredSubtraction, * utf::timeout(5)) {
    test_explicit_engine("referendum_colored_subtraction", ExplicitColoredModelChecker::Result::SATISFIED);
}

BOOST_AUTO_TEST_CASE(KBound, * utf::timeout(5)) {
    test_explicit_engine("referendum_colored_subtraction", ExplicitColoredModelChecker::Result::UNSATISFIED, 1, 9,
                         "FORMULA Ten voting tokens are reachable FALSE");
    test_explicit_engine("referendum_colored_subtraction", ExplicitColoredModelChecker::Result::SATISFIED, 1, 10);
}

BOOST_AUTO_TEST_CASE(ColoredQueryFromXml, * utf::timeout(5)) {
    auto [queries, querynames, sset, options] = load_explicit(
        "/models/explicit-engine/referendum_colored_subtraction.pnml",
        "/models/explicit-engine/referendum_color_query.xml",
        {0}
    );
    ExplicitColoredModelChecker checker(sset, std::cout);
    BOOST_TEST(checker.checkQuery(queries[0], options) == ExplicitColoredModelChecker::Result::SATISFIED);
}

BOOST_AUTO_TEST_CASE(PhilosophersColoredQueryFromXml, * utf::timeout(5)) {
    auto [queries, querynames, sset, options] = load_explicit(
        "/models/explicit-engine/philosophers_color_query.pnml",
        "/models/explicit-engine/philosophers_color_query.xml",
        {0}
    );
    options.trace = TraceLevel::None;
    ExplicitColoredModelChecker checker(sset, std::cout);
    BOOST_TEST(checker.checkQuery(queries[0], options) == ExplicitColoredModelChecker::Result::SATISFIED);
}

BOOST_AUTO_TEST_CASE(TokenRingColoredQueryFromXml, * utf::timeout(5)) {
    auto [queries, querynames, sset, options] = load_explicit(
        "/models/explicit-engine/token_ring_color_query.pnml",
        "/models/explicit-engine/token_ring_color_query.xml",
        {0}
    );
    options.trace = TraceLevel::None;
    ExplicitColoredModelChecker checker(sset, std::cout);
    BOOST_TEST(checker.checkQuery(queries[0], options) == ExplicitColoredModelChecker::Result::SATISFIED);
}

BOOST_AUTO_TEST_CASE(ColoredQueryUsesExplicitTupleEncoding) {
    Colored::ColorType first{"first"};
    first.addColor("a0");
    first.addColor("a1");
    Colored::ColorType second{"second"};
    second.addColor("b0");
    second.addColor("b1");
    second.addColor("b2");
    Colored::ProductType product{"product"};
    product.addType(&first);
    product.addType(&second);

    ExplicitColoredPetriNetBuilder builder;
    builder.addColorType("product", &product);
    Colored::Multiset marking;
    const auto* selected = product.getColor(std::vector<uint32_t>{1, 0});
    marking[selected] = 4;
    builder.addPlace("P", &product, std::move(marking), 0, 0);

    auto net = builder.takeNet();
    ColoredSuccessorGenerator successorGenerator{net};
    ExplicitQueryPropositionCompiler compiler{
        net, builder.getPlaceIndices(), builder.getTransitionIndices(), successorGenerator
    };
    auto query = std::make_shared<PQL::EqualCondition>(
        std::make_shared<PQL::UnfoldedIdentifierExpr>(std::make_shared<const_string>("P_1")),
        std::make_shared<PQL::LiteralExpr>(4)
    );

    const auto proposition = compiler.compile(query);
    BOOST_TEST(proposition->eval(successorGenerator, net.initial(), 0));
}

BOOST_AUTO_TEST_CASE(ColorSensitiveInhibitor, * utf::timeout(5)) {
    test_explicit_engine("color_sensitive_inhibitor", ExplicitColoredModelChecker::Result::SATISFIED);
    test_explicit_engine("color_sensitive_inhibitor", ExplicitColoredModelChecker::Result::UNSATISFIED, 1);
}
