#pragma once
#include <random>
#include <algorithm>
#include <vector>
#include <autoppl/util/traits.hpp>
#include <autoppl/expression/variable.hpp>
#include <autoppl/distribution/density.hpp>

/*
 * Assumptions:
 * - every variable referenced in model is of type Variable<double>
 */

namespace ppl {
namespace details {

struct MHData
{
    double next;
    // TODO: maybe keep an array for batch sampling?
};

} // namespace details

/*
 * Metropolis-Hastings algorithm to sample from posterior distribution.
 * The posterior distribution is a constant multiple of model.pdf().
 * Any variables that model references which are in state "parameter"
 * is sampled and in state "data" are not.
 * So, model.pdf() is proportional to p(parameters... | data...).
 *
 * User must ensure that they allocated at least as large as n_sample
 * in the storage associated with every parameter referenced in model.
 */
template <class ModelType>
inline void mh_posterior(ModelType& model,
                         double n_sample,
                         double stddev = 1.0,
                         double seed = 0
                         )
{
    using data_t = details::MHData;
    
    // set-up auxiliary tools
    std::mt19937 gen(seed);
    std::uniform_real_distribution unif_sampler(0., 1.);

    // get number of parameters to sample    
    size_t n_params = 0.;
    auto get_n_params = [&](auto& eq_node) {
        auto& var = eq_node.get_variable();
        using var_t = std::decay_t<decltype(var)>;
        using state_t = typename var_traits<var_t>::state_t;
        n_params += (var.get_state() == state_t::parameter);
    };
    model.traverse(get_n_params);

    // vector of parameter-related data with candidate
    std::vector<data_t> params(n_params);
    double curr_log_pdf = model.log_pdf();
    auto params_it = params.begin();

    for (size_t iter = 0; iter < n_sample; ++iter) {

        double log_alpha = -curr_log_pdf;

        // generate next candidates and place them in parameter
        // variables as next values; update log_alpha
        // The old values are temporary stored in the params vector.
        auto get_candidate = [=, &gen](auto& eq_node) mutable {
            auto& var = eq_node.get_variable();
            using var_t = std::decay_t<decltype(var)>;
            using value_t = typename var_traits<var_t>::value_t;
            using state_t = typename var_traits<var_t>::state_t;

            if (var.get_state() == state_t::parameter) {
                auto curr = static_cast<value_t>(var);
                std::normal_distribution norm_sampler(curr, stddev);

                // sample new candidate, place old value in params, 
                // fill next candidate in var, and update log_alpha
                auto cand = norm_sampler(gen); 
                params_it->next = curr;
                var.set_value(cand); 
                log_alpha -= dist::NormalBase::log_pdf(cand, curr, stddev);
                log_alpha += dist::NormalBase::log_pdf(curr, cand, stddev);

                ++params_it;
            }
        };
        model.traverse(get_candidate);

        // compute next candidate log pdf and update log_alpha
        double cand_log_pdf = model.log_pdf();
        log_alpha += cand_log_pdf;

        // accept
        if (std::log(unif_sampler(gen)) <= log_alpha) {
            // "current" sample for next iteration is already in the variables
            // simply append to storage
            auto add_to_storage = [iter](auto& eq_node) {
                auto& var = eq_node.get_variable();
                using var_t = std::decay_t<decltype(var)>;
                using state_t = typename var_traits<var_t>::state_t;
                if (var.get_state() == state_t::parameter) {
                    auto storage = var.get_storage();
                    storage[iter] = var.get_value();
                } 
            };
            model.traverse(add_to_storage);

            // update current log pdf for next iteration
            curr_log_pdf = cand_log_pdf;
        }

        // reject
        else {
            // "current" sample for next iteration must be moved back from 
            // params vector into variables.
            // Then, simply append to storage
            auto add_to_storage = [params_it, iter](auto& eq_node) mutable {
                auto& var = eq_node.get_variable();
                using var_t = std::decay_t<decltype(var)>;
                using state_t = typename var_traits<var_t>::state_t;
                if (var.get_state() == state_t::parameter) {
                    var.set_value(params_it->next);
                    auto storage = var.get_storage();
                    storage[iter] = var.get_value();
                    ++params_it;
                } 
            };
            model.traverse(add_to_storage);
        }

    }
}

} // namespace ppl
