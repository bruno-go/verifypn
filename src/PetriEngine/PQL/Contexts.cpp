/*
 *  Copyright Peter G. Jensen, all rights reserved.
 */

#include "PetriEngine/PQL/Contexts.h"

#include <iostream>

namespace PetriEngine {
    namespace PQL {

        uint32_t AnalysisContext::resolve_trace_name(const std::string& s, bool create)
        {
            uint32_t id = _trace_names.size();
            auto [it, inserted] = _trace_names.emplace(std::make_pair(s,id));
            if (!inserted && create)
                throw base_error("Trace identifier ", s, " already existed.");
            if (inserted && !create)
                throw base_error("Trace identifier ", s, " does not exist, but is used as a prefix in the query.");
            return it->second;
        }

        bool ColoredAnalysisContext::resolvePlace(const shared_const_string& place, std::function<void(const shared_const_string&)>&& fn)
        {
            auto it = _coloredPlaceNames.find(place);
            if (it != _coloredPlaceNames.end()) {
                for (auto& [_, name] : it->second)
                    fn(name);
                return true;
            }
            return false;
        }

        bool ColoredAnalysisContext::resolveTransition(const shared_const_string& transition, std::function<void(const shared_const_string)>&& fn)
        {
            auto it = _coloredTransitionNames.find(transition);
            if (it != _coloredTransitionNames.end()) {
                for (auto& e : it->second)
                    fn(e);
                return true;
            }
            return false;
        }


        AnalysisContext::ResolutionResult AnalysisContext::resolve(const shared_const_string& identifier, bool place)
        {
            ResolutionResult result;
            result.offset = -1;
            result.success = false;
            auto& map = place ? _placeNames : _transitionNames;
            auto it = map.find(identifier);
            if (it != map.end()) {
                result.offset = (int) it->second;
                result.success = true;
                return result;
            }
            return result;
        }

        uint32_t SimplificationContext::getLpTimeout() const
        {
            return _lpTimeout;
        }

        uint32_t SimplificationContext::getPotencyTimeout() const
        {
            return _potencyTimeout;
        }

        double SimplificationContext::getReductionTime()
        {
            // duration in seconds
            auto end = std::chrono::high_resolution_clock::now();
            return (std::chrono::duration_cast<std::chrono::microseconds>(end - _start).count())*0.000001;
        }

        glp_prob* SimplificationContext::makeBaseLP() const
        {
            if (_base_lp == nullptr)
                _base_lp = buildBase();
            if (_base_lp == nullptr)
                return nullptr;
            auto* tmp_lp = glp_create_prob();
            glp_copy_prob(tmp_lp, _base_lp, GLP_OFF);
            return tmp_lp;
        }

        glp_prob* SimplificationContext::buildBase() const
        {
            constexpr auto infty = std::numeric_limits<double>::infinity();
            if (timeout())
                return nullptr;

            auto* lp = glp_create_prob();
            if (lp == nullptr)
                return lp;

            const uint32_t nCol = _net->numberOfTransitions();
            const int nRow = _net->numberOfPlaces();
            std::vector<int32_t> indir(std::max<uint32_t>(nCol, nRow) + 1);

            glp_add_cols(lp, nCol + 1);
            glp_add_rows(lp, nRow + 1);
            {
                std::vector<double> col = std::vector<double>(nRow + 1);
                for (size_t t = 0; t < _net->numberOfTransitions(); ++t) {
                    auto pre = _net->preset(t);
                    auto post = _net->postset(t);
                    size_t l = 1;
                    while (pre.first != pre.second ||
                           post.first != post.second) {
                        if (pre.first == pre.second || (post.first != post.second && post.first->place < pre.first->place)) {
                            col[l] = post.first->tokens;
                            indir[l] = post.first->place + 1;
                            ++post.first;
                        }
                        else if (post.first == post.second || (pre.first != pre.second && pre.first->place < post.first->place)) {
                            if (!pre.first->inhibitor)
                                col[l] = -(double) pre.first->tokens;
                            else
                                col[l] = 0;
                            indir[l] = pre.first->place + 1;
                            ++pre.first;
                        }
                        else {
                            assert(pre.first->place == post.first->place);
                            if (!pre.first->inhibitor)
                                col[l] = (double) post.first->tokens - (double) pre.first->tokens;
                            else
                                col[l] = (double) post.first->tokens;
                            indir[l] = pre.first->place + 1;
                            ++pre.first;
                            ++post.first;
                        }
                        ++l;
                    }
                    glp_set_mat_col(lp, t + 1, l - 1, indir.data(), col.data());
                    if (timeout()) {
                        std::cerr << "glpk: construction timeout" << std::endl;
                        glp_delete_prob(lp);
                        return nullptr;
                    }
                }
            }
            int rowno = 1;
            for (size_t p = 0; p < _net->numberOfPlaces(); p++) {
                glp_set_row_bnds(lp, rowno, GLP_LO, (0.0 - (double) _marking[p]), infty);
                ++rowno;
                if (timeout()) {
                    std::cerr << "glpk: construction timeout" << std::endl;
                    glp_delete_prob(lp);
                    return nullptr;
                }
            }

            glp_add_rows(lp, _net->numberOfPlaces());
            for (size_t p = 0; p < _net->numberOfPlaces(); p++) {
                    std::vector<double> row(nCol, 0);
                    std::vector<int> indices;
                    row.shrink_to_fit();
                
                    for (size_t t = 0; t < _net->numberOfTransitions(); t++) {
                        if(_net->outArc(t, p) - _net->inArc(p, t) != 0){
                            row[t]  = _net->outArc(t, p);
                            row[t] -= _net->inArc(p, t);
                            indices.push_back(t);
                        }
                    }

                    glp_set_mat_row(lp, rowno, indices.size() - 1, indices.data(), row.data());
                    glp_set_row_bnds(lp, rowno, GLP_LO, 0, infty);
                    ++rowno;
            }
            
            return lp;
        }
    }
}