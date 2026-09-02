#include "PetriEngine/ExplicitColored/ColoredResultPrinter.h"
#include <iomanip>
#include "PetriEngine/Colored/PnmlWriter.h"


namespace PetriEngine::ExplicitColored {
    void ColoredResultPrinter::printResult(
        const SearchStatistics& searchStatistics,
        const Reachability::AbstractHandler::Result result,
        const ExplicitColoredTraceContext* trace
    ) const {
        _printCommon(result, {});
        _stream << "STATS:\n"
                << "	discovered states:     " << searchStatistics.discoveredStates << '\n'
                << "	explored states:       " << searchStatistics.exploredStates << '\n'
                << "	peak waiting states:   " << searchStatistics.peakWaitingStates << '\n'
                << "	end waiting states:    " << searchStatistics.endWaitingStates << '\n'
                << "	max tokens:            " << searchStatistics.maxTokens << '\n'
                << "	biggest encoded state: " << searchStatistics.biggestEncoding << " bytes\n";
        if (trace != nullptr) {
            _printTrace(*trace);
        }
    }

    void ColoredResultPrinter::printNonExplicitResult(const std::vector<std::string> techniques,
        const Reachability::AbstractHandler::Result result) const {
        _printCommon(result, techniques);
    }

    void ColoredResultPrinter::_printCommon(const Reachability::AbstractHandler::Result result, const std::vector<std::string>& extraTechniques) const {
        if (result == Reachability::AbstractHandler::Unknown) {
            return;
        }
        _stream << "FORMULA " << _queryName << " ";
        if (result == Reachability::AbstractHandler::Satisfied) {
            _stream << "TRUE ";
        } else if (result == Reachability::AbstractHandler::NotSatisfied) {
            _stream << "FALSE ";
        }

        _stream << "TECHNIQUES ";
        for (const auto& techniqueFlag : _techniqueFlags) {
            _stream << techniqueFlag << " ";
        }

        for (const auto& techniqueFlag : extraTechniques) {
            _stream << techniqueFlag << " ";
        }

        _stream << '\n';
        if (result == Reachability::AbstractHandler::Satisfied || result == Reachability::AbstractHandler::NotSatisfied) {
            _stream << "Query index " << _queryOffset << " was solved\n";
        }
        _stream << '\n';

        _stream << "Query is ";
        if (result == Reachability::AbstractHandler::NotSatisfied) {
            _stream << "NOT ";
        }

        _stream << "satisfied.\n";
    }

    void ColoredResultPrinter::_printTrace(const ExplicitColoredTraceContext& trace) const {
        _traceStream << "Trace:\n";
        _traceStream << "<trace>\n";
        for (const auto& step : trace.traceSteps) {
            if (!step.isInitial) {
                _traceStream << "\t<transition id=" << std::quoted(step.transitionId) << ">\n";
                _traceStream << "\t\t<bindings>" << '\n';
                for (const auto& [variableId, value] : step.binding) {
                    _traceStream << "\t\t\t<variable id=" << std::quoted(variableId) << ">\n";
                    _traceStream << "\t\t\t\t<color>" << value << "</color>\n";
                    _traceStream << "\t\t\t</variable>\n";
                }
                _traceStream << "\t\t</bindings>\n";
                _traceStream << "\t</transition>\n";
            }
            _traceStream << "\t<marking>\n";
            _printMarkings(trace.cpnBuilder, step);
            _traceStream << "\t</marking>\n";
        }
        _traceStream << "</trace>\n";
    }

    void ColoredResultPrinter::_printMarkings(
        const ExplicitColoredPetriNetBuilder& explicitCpnBuilder, const TraceStep& traceStep) const
    {
        shared_string_set sharedStringSet {};
        ColoredPetriNetBuilder builder(sharedStringSet);
        for (const auto colorType : explicitCpnBuilder.getUnderlyingVariableColorTypes())
        {
            builder.addColorType(colorType->getName(), colorType);
        }

        for (const auto& [place_id, traceTokens] : traceStep.marking) {
            const auto place = explicitCpnBuilder.getPlaceIndices().find(place_id)->second;

            Colored::Multiset tokens;
            const auto& colorType = *explicitCpnBuilder.getPlaceUnderlyingColorType(place);
            for (const auto& [color, count] : traceTokens)
            {
                if (color.size() > 1)
                {
                    const auto productColorType = dynamic_cast<const Colored::ProductType*>(&colorType);
                    if (productColorType == nullptr) {
                        throw std::runtime_error("Trace color is inconsistent with underlying color type");
                    }

                    std::vector<uint32_t> colorIndices;
                    for (size_t colorTypeIndex = 0; colorTypeIndex < color.size(); colorTypeIndex++) {
                        colorIndices.push_back(
                            (*productColorType->getNestedColorType(colorTypeIndex))[color[colorTypeIndex]]->getId());
                    }

                    tokens[productColorType->getColor(colorIndices)] = count;
                }
                else
                {
                    tokens[colorType[color[0]]] = count;
                }
            }

            builder.addPlace(
                explicitCpnBuilder.getPlaceName(place),
                explicitCpnBuilder.getPlaceUnderlyingColorType(place),
                std::move(tokens),
                0,
                0);
        }

        Colored::PnmlWriter writer(builder, _traceStream);
        builder.leak_colors();

        for (const auto& [place_id, traceTokens] : traceStep.marking)
        {
            if (!traceTokens.empty())
            {
                _traceStream << "\t\t<place id=" << std::quoted(place_id) << ">\n";
                writer.writeInitialTokens(place_id);
                _traceStream << "\t\t</place>\n";
            }
        }
    }
}
