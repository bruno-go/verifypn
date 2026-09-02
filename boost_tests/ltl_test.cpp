/* Copyright (C) 2021 Peter G. Jensen <root@petergjoel.dk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define BOOST_TEST_MODULE ltl

#include <boost/test/unit_test.hpp>
#include <string>
#include <fstream>
#include <sstream>

#include "utils.h"
#include "LTL/LTLSearch.h"
#include "CTL/SearchStrategy/HeuristicSearch.h"
#include "PetriEngine/SuccessorGenerator.h"

using namespace PetriEngine;
using namespace PetriEngine::Colored;
namespace utf = boost::unit_test;

BOOST_AUTO_TEST_CASE(DirectoryTest) {
    BOOST_REQUIRE(getenv("TEST_FILES"));
}

static Structures::State replay_trace(
    const PetriNet& net, const std::vector<std::vector<uint32_t>>& trace)
{
    Structures::State state;
    state.setMarking(net.makeInitialMarking());
    SuccessorGenerator successor(net);
    for (const auto& step : trace) {
        BOOST_REQUIRE_EQUAL(step.size(), 1);
        const auto transition = step.front();
        BOOST_REQUIRE_LT(transition, net.numberOfTransitions());
        successor.prepare(state);
        BOOST_REQUIRE(successor.checkPreset(transition));
        successor.consumePreset(state, transition);
        successor.producePostset(state, transition);
    }
    return state;
}

BOOST_AUTO_TEST_CASE(TarjanTraceUsesActualParentsAndTransitions) {
    auto [pn, conditions, qstrings] = load_pn(
        "/models/tarjan_trace.pnml", "/models/tarjan_trace.xml", {0}, TemporalLogic::LTL);
    LTL::LTLSearch search(*pn, conditions[0], LTL::BuchiOptimization::Low, LTL::APCompression::None);

    BOOST_REQUIRE(search.solve(true, 10, LTL::Algorithm::Tarjan, LTL::LTLPartialOrder::Automaton,
                               Strategy::HEUR, LTL::LTLHeuristic::Automaton, true));

    const auto& trace = search.raw_trace();
    BOOST_REQUIRE(!trace.empty());

    size_t buffer = 0;
    while (buffer < pn->numberOfPlaces() && *pn->placeNames()[buffer] != "buffer") {
        ++buffer;
    }
    
    BOOST_REQUIRE_LT(buffer, pn->numberOfPlaces());

    auto state = replay_trace(*pn, trace);
    BOOST_REQUIRE_EQUAL(state.marking()[buffer], 0);
}

BOOST_AUTO_TEST_CASE(ExportedTraceCasesMatchNDFSAndTarjan) {
    struct TraceCase {
        const char* model;
        const char* query;
        uint64_t k_bound;
        Strategy strategy;
    };
    const TraceCase cases[] = {
        {"/models/wolf_goat_cabbage.pnml", "/models/wolf_goat_cabbage.xml", 9, Strategy::DFS},
        {"/models/tarjan_lasso.pnml", "/models/tarjan_lasso.xml", 10, Strategy::DFS},
        {"/models/man_wolf_goat_cabbage.pnml", "/models/man_wolf_goat_cabbage.xml",
            8, Strategy::HEUR},
    };

    for (const auto& test : cases) {
        BOOST_TEST_CONTEXT(test.model) {
            auto [pn, conditions, qstrings] = load_pn(test.model, test.query, {0}, TemporalLogic::LTL);
            for (const auto algorithm : {LTL::Algorithm::NDFS, LTL::Algorithm::Tarjan}) {
                LTL::LTLSearch search(
                    *pn, conditions[0], LTL::BuchiOptimization::Low, LTL::APCompression::None);
                BOOST_REQUIRE(search.solve(true, test.k_bound, algorithm,
                    LTL::LTLPartialOrder::Automaton, test.strategy,
                    LTL::LTLHeuristic::Automaton, true));
                const auto& trace = search.raw_trace();
                BOOST_REQUIRE(!trace.empty());
                replay_trace(*pn, trace);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(AngiogenesisPT01LTLCardinality, * utf::timeout(300)) {

    const std::set<size_t> qnums{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const std::vector<Reachability::ResultPrinter::Result> expected{
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::Satisfied,
        ResultPrinter::Satisfied,
        ResultPrinter::Satisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied};

    auto [pn, conditions, qstrings] = load_pn("/models/Angiogenesis-PT-01/model.pnml",
        "/models/Angiogenesis-PT-01/LTLCardinality.xml", qnums, TemporalLogic::LTL);

    for (auto i : qnums) {
        for (bool trace :{false, true}) {
            for(auto alg : { LTL::Algorithm::NDFS, LTL::Algorithm::Tarjan})
            {
                for(auto por : { LTL::LTLPartialOrder::None, LTL::LTLPartialOrder::Liebke,
                    LTL::LTLPartialOrder::Visible, LTL::LTLPartialOrder::Automaton})
                {
                    if(alg == LTL::Algorithm::NDFS && por != LTL::LTLPartialOrder::None)
                        continue;
                    for(auto heur : { LTL::LTLHeuristic::DFS, LTL::LTLHeuristic::Automaton, LTL::LTLHeuristic::Distance,
                        LTL::LTLHeuristic::FireCount})
                    {
                        std::cerr << "Q[" << i << "] trace=" << std::boolalpha << trace
                            << " por=" << to_underlying(por) << " alg=" << to_underlying(alg) << " heur=" << to_underlying(heur) << std::endl;
                            Strategy strategy = Strategy::HEUR;
                        if(heur == LTL::LTLHeuristic::DFS)
                            strategy = Strategy::HEUR;
                        LTL::LTLSearch search(*pn, conditions[i], LTL::BuchiOptimization::Low, LTL::APCompression::None);
                        auto r = search.solve(trace, 0, alg, por, strategy, heur, true);
                        auto result = r ? ResultPrinter::Satisfied : ResultPrinter::NotSatisfied;
                        BOOST_REQUIRE_EQUAL(expected[i], result);
                    }
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(AngiogenesisPT01LTLFireability, * utf::timeout(300)) {

    const std::set<size_t> qnums{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const std::vector<Reachability::ResultPrinter::Result> expected{
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::Satisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::Satisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::NotSatisfied,
        ResultPrinter::Satisfied};

    auto [pn, conditions, qstrings] = load_pn("/models/Angiogenesis-PT-01/model.pnml",
        "/models/Angiogenesis-PT-01/LTLFireability.xml", qnums, TemporalLogic::LTL);

    for (auto i : qnums) {
        for (bool trace :{false, true}) {
            for(auto alg : { LTL::Algorithm::Tarjan, LTL::Algorithm::NDFS})
            {
                for(auto por : { LTL::LTLPartialOrder::None, LTL::LTLPartialOrder::Liebke,
                    LTL::LTLPartialOrder::Visible, LTL::LTLPartialOrder::Automaton})
                {
                    if(alg == LTL::Algorithm::NDFS && por != LTL::LTLPartialOrder::None)
                        continue;
                    for(auto heur : { LTL::LTLHeuristic::Automaton, LTL::LTLHeuristic::Distance,
                        LTL::LTLHeuristic::FireCount, LTL::LTLHeuristic::DFS, LTL::LTLHeuristic::RDFS})
                    {
                        std::cerr << "Q[" << i << "] trace=" << std::boolalpha << trace
                            << " por=" << to_underlying(por) << " heur=" << to_underlying(heur) << std::endl;
                        Strategy strategy = Strategy::HEUR;
                        if(heur == LTL::LTLHeuristic::DFS)
                            strategy = Strategy::DFS;
                        if(heur == LTL::LTLHeuristic::RDFS)
                            strategy = Strategy::RDFS;
                        LTL::LTLSearch search(*pn, conditions[i], LTL::BuchiOptimization::Low, LTL::APCompression::None);
                        auto r = search.solve(trace, 0, alg, por, strategy, heur, true);
                        auto result = r ? ResultPrinter::Satisfied : ResultPrinter::NotSatisfied;
                        BOOST_REQUIRE_EQUAL(expected[i], result);
                    }
                }
            }
        }
    }
}
