#pragma once
#include <optional>
#include <type_traits>
#include <utility>

namespace p11 {
// A fuel-suspended calculation resumes with its old locals. Return a value
// only in the owner round whose observations supplied the inputs. Execution
// still requires fresh action legality and an epoch check after further work.
template<class ReadRound,class Decide>
auto plan_in_round(int observed_round,ReadRound&& current_round,Decide&& decide)
    -> std::optional<std::invoke_result_t<Decide&>> {
    using Choice=std::invoke_result_t<Decide&>;
    if(int(current_round())!=observed_round) return std::nullopt;
    Choice choice=decide();
    if(int(current_round())!=observed_round) return std::nullopt;
    return std::optional<Choice>(std::move(choice));
}
} // namespace p11
