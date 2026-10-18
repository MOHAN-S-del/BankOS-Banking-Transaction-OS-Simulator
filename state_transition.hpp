#pragma once

#include "pcb.hpp"
#include <string>
#include <sstream>

namespace bank_sim {

/**
 * State Transition Record
 * Captures lifecycle events of processes for auditing and JSON logging.
 */
struct StateTransition {
    int tick{0};
    int pid{0};
    TransactionType type{TransactionType::UPI};
    ProcessState from_state{ProcessState::NEW};
    ProcessState to_state{ProcessState::READY};
    std::string reason{};

    std::string to_json() const {
        std::ostringstream oss;
        oss << "{"
            << "\"tick\":" << tick << ","
            << "\"pid\":" << pid << ","
            << "\"type\":\"" << transactionTypeToString(type) << "\","
            << "\"from_state\":\"" << processStateToString(from_state) << "\","
            << "\"to_state\":\"" << processStateToString(to_state) << "\","
            << "\"reason\":\"" << reason << "\""
            << "}";
        return oss.str();
    }
};

} // namespace bank_sim
