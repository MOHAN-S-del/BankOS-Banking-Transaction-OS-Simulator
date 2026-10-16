#pragma once

#include "pcb.hpp"
#include <vector>
#include <random>
#include <algorithm>
#include <iostream>

namespace bank_sim {

/**
 * WorkloadGenerator
 * Generates synthetic banking workloads with randomized, bursty arrival distributions.
 * Simulates 176,000 banking transactions (UPI, ATM, Salary).
 */
class WorkloadGenerator {
public:
    WorkloadGenerator(unsigned int seed = 42)
        : rng_(seed) {}

    /**
     * Generate 176,000 banking transactions with randomized bursty traffic.
     * @param total_requests Number of requests to generate (default 176,000)
     * @param max_ticks Total simulation time horizon for arrivals (default 5000 ticks)
     * @return Sorted vector of ProcessControlBlocks ordered by arrival_time
     */
    std::vector<ProcessControlBlock> generate_workload(size_t total_requests = 176000,
                                                       int max_ticks = 5000) {
        std::vector<ProcessControlBlock> workload;
        workload.reserve(total_requests);

        // Distributions for transaction types:
        // ~60% UPI, ~25% ATM, ~15% Salary
        std::discrete_distribution<int> type_dist({60, 25, 15});

        // Burst time distributions:
        // UPI: 1 to 4 ticks (quick micro-payment)
        std::uniform_int_distribution<int> upi_burst_dist(1, 4);
        // ATM: 2 to 6 ticks
        std::uniform_int_distribution<int> atm_burst_dist(2, 6);
        // Salary: 10 to 30 ticks (batch multi-account processing)
        std::uniform_int_distribution<int> salary_burst_dist(10, 30);

        // Account IDs for locks matching schema.sql
        std::uniform_int_distribution<int> retail_acc_dist(1001, 1008);
        std::uniform_int_distribution<int> corp_acc_dist(2001, 2002);
        std::uniform_int_distribution<int> atm_reserve_dist(5001, 5002);

        // Bursty arrival generation:
        // Create 20 distinct burst peak clusters across the simulation timeline
        // to emulate peak UPI transaction bursts, salary clearing days, and ATM rushes.
        const int num_burst_clusters = 20;
        std::vector<int> cluster_centers;
        cluster_centers.reserve(num_burst_clusters);
        for (int i = 0; i < num_burst_clusters; ++i) {
            cluster_centers.push_back((i + 1) * (max_ticks / (num_burst_clusters + 1)));
        }

        std::uniform_int_distribution<int> cluster_selector(0, num_burst_clusters - 1);
        std::normal_distribution<double> burst_spread(0.0, 25.0); // Standard deviation of 25 ticks
        std::uniform_int_distribution<int> uniform_tick(0, max_ticks);
        std::bernoulli_distribution is_burst_dist(0.75); // 75% traffic in burst waves, 25% background

        std::cout << "[WorkloadGenerator] Generating " << total_requests 
                  << " banking transactions with bursty arrival patterns...\n";

        for (size_t i = 0; i < total_requests; ++i) {
            ProcessControlBlock pcb;
            pcb.pid = static_cast<int>(i + 1);
            pcb.state = ProcessState::NEW;

            // 1. Transaction Type
            int type_val = type_dist(rng_);
            if (type_val == 0) {
                pcb.type = TransactionType::UPI;
                pcb.burst_time = upi_burst_dist(rng_);
                pcb.current_queue = 1; // Queue 1 (SRTF)

                // UPI requires 2 retail account locks (Sender & Receiver)
                int sender = retail_acc_dist(rng_);
                // Intentionally route some transactions to low-balance accounts (1005, 1008) to test rollbacks
                if (pcb.pid % 30 == 0) sender = 1005;
                else if (pcb.pid % 45 == 0) sender = 1008;

                int receiver = retail_acc_dist(rng_);
                while (receiver == sender) receiver = retail_acc_dist(rng_);

                pcb.from_account = sender;
                pcb.to_account = receiver;
                pcb.amount = 100.0 + (pcb.pid % 500); // 100 to 600 INR

                // Strict Deadlock Prevention: always lock smaller Account ID first
                pcb.required_locks = {std::min(sender, receiver), std::max(sender, receiver)};

            } else if (type_val == 1) {
                pcb.type = TransactionType::ATM;
                pcb.burst_time = atm_burst_dist(rng_);
                pcb.current_queue = 1; // Queue 1 (SRTF)

                // ATM requires user account lock + ATM hardware cash vault lock
                int user_acc = retail_acc_dist(rng_);
                if (pcb.pid % 50 == 0) user_acc = 1008;
                int atm_vault = atm_reserve_dist(rng_);

                pcb.from_account = user_acc;
                pcb.to_account = atm_vault;
                pcb.amount = 500.0 + (pcb.pid % 2500); // 500 to 3000 INR

                // Strict Deadlock Prevention: smaller Account ID first
                pcb.required_locks = {std::min(user_acc, atm_vault), std::max(user_acc, atm_vault)};

            } else {
                pcb.type = TransactionType::SALARY;
                pcb.burst_time = salary_burst_dist(rng_);
                pcb.current_queue = 2; // Queue 2 (Round Robin)

                // Salary requires corporate treasury account lock + employee accounts
                int corp = corp_acc_dist(rng_);
                int emp_primary = retail_acc_dist(rng_);

                pcb.from_account = corp;
                pcb.to_account = emp_primary;
                pcb.amount = 25000.0 + (pcb.pid % 15000); // 25,000 to 40,000 INR

                pcb.required_locks.push_back(corp);
                int emp_count = 3 + (pcb.pid % 4); // 3 to 6 employee accounts
                for (int e = 0; e < emp_count; ++e) {
                    int emp = retail_acc_dist(rng_);
                    if (std::find(pcb.required_locks.begin(), pcb.required_locks.end(), emp) == pcb.required_locks.end()) {
                        pcb.required_locks.push_back(emp);
                    }
                }
                // Strict Deadlock Prevention: sort all required locks in ascending order
                std::sort(pcb.required_locks.begin(), pcb.required_locks.end());
            }

            pcb.total_burst_time = pcb.burst_time;

            // 2. Bursty Arrival Time
            int arrival = 0;
            if (is_burst_dist(rng_)) {
                // Clustered in a burst wave
                int center = cluster_centers[cluster_selector(rng_)];
                arrival = static_cast<int>(center + burst_spread(rng_));
            } else {
                // Background uniform traffic
                arrival = uniform_tick(rng_);
            }
            // Clamp arrival to [0, max_ticks]
            pcb.arrival_time = std::clamp(arrival, 0, max_ticks);

            workload.push_back(std::move(pcb));
        }

        // Sort by arrival_time so the scheduler can admit them chronologically
        std::sort(workload.begin(), workload.end(), [](const ProcessControlBlock& a, const ProcessControlBlock& b) {
            if (a.arrival_time != b.arrival_time) {
                return a.arrival_time < b.arrival_time;
            }
            return a.pid < b.pid;
        });

        std::cout << "[WorkloadGenerator] Successfully created " << workload.size() 
                  << " transactions. Earliest arrival: Tick " << workload.front().arrival_time 
                  << ", Latest arrival: Tick " << workload.back().arrival_time << "\n";

        return workload;
    }

private:
    std::mt19937 rng_;
};

} // namespace bank_sim
