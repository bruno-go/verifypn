#ifndef NAIVEWORKLIST_H
#define NAIVEWORKLIST_H

#include <PetriEngine/ExplicitColored/ExpressionCompilers/ExplicitQueryPropositionCompiler.h>
#include <PetriEngine/options.h>
#include "PetriEngine/ExplicitColored/ColoredPetriNet.h"
#include "PetriEngine/ExplicitColored/ColoredResultPrinter.h"
#include "PetriEngine/ExplicitColored/Algorithms/SearchStatistics.h"
#include "PetriEngine/ExplicitColored/SuccessorGenerator/ColoredSuccessorGenerator.h"
#include "PetriEngine/ExplicitColored/ColoredEncoder.h"

namespace PetriEngine::ExplicitColored {
    template <typename T>
    class RDFSStructure;
    class ColoredResultPrinter;

    enum class Quantifier {
        EF,
        AG
    };

    struct StateMap {
        std::unordered_map<uint32_t, TraceMapStep> transitions;
    };

    struct InternalTraceStep {
        Binding_t binding;
        Transition_t transition;
    };

    class ExplicitWorklist {
    public:
        ExplicitWorklist(
            const ColoredPetriNet& net,
            const PQL::Condition_ptr& query,
            const std::unordered_map<std::string, uint32_t>& placeNameIndices,
            const std::unordered_map<std::string, Transition_t>& transitionNameIndices,
            size_t seed,
            bool createTrace,
            uint32_t kbound
        );

        Reachability::AbstractHandler::Result check(Strategy searchStrategy, ColoredSuccessorGeneratorOption coloredSuccessorGeneratorOption);
        [[nodiscard]] const SearchStatistics& GetSearchStatistics() const;
        std::optional<uint64_t> getCounterExampleId() const;
        std::optional<std::vector<InternalTraceStep>> getTraceTo(uint64_t counterExampleId) const;
    private:
        std::shared_ptr<ExplicitQueryProposition> _gammaQuery;
        std::optional<uint64_t> _counterExampleId;
        Quantifier _quantifier;
        const ColoredPetriNet& _net;
        const ColoredSuccessorGenerator _successorGenerator;
        const size_t _seed;
        uint32_t _kbound;
        bool _fullStatespace = true;
        bool _createTrace;
        StateMap _stateMap;
        SearchStatistics _searchStatistics;
        template <typename SuccessorGeneratorState>
        [[nodiscard]] Reachability::AbstractHandler::Result _search(Strategy searchStrategy);
        [[nodiscard]] bool _check(const ColoredPetriNetMarking& state, size_t id) const;
        [[nodiscard]] uint64_t _tokenCount(const ColoredPetriNetMarking& marking) const;

        template <typename T>
        [[nodiscard]] Reachability::AbstractHandler::Result _dfs();
        template <typename T>
        [[nodiscard]] Reachability::AbstractHandler::Result _bfs();
        template <typename T>
        [[nodiscard]] Reachability::AbstractHandler::Result _rdfs();
        template <typename T>
        [[nodiscard]] Reachability::AbstractHandler::Result _bestfs();

        template <template <typename> typename WaitingList, typename T>
        [[nodiscard]] Reachability::AbstractHandler::Result _genericSearch(WaitingList<T> waiting);
        [[nodiscard]] Reachability::AbstractHandler::Result _getResult(bool found, bool fullStatespace) const;
    };
}

#endif //NAIVEWORKLIST_H
