// perf_benchmarks.cpp — Reproducible performance measurement for Recent-Project Filtering
// and Saved-Message Search across 300, 3,000, and 10,000 records on isolated test stores.
//
// Hard-refuses any path inside %LOCALAPPDATA%\Mayasaba to protect production storage.

#include "../Mayasaba/Storage.h"
#include "../Mayasaba/ProjectService.h"
#include "../Mayasaba/NativeIds.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
    std::filesystem::path ProductionDirectory()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD const length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            throw std::runtime_error("LOCALAPPDATA is unavailable; refusing to guess a store location");
        }
        return std::filesystem::path{std::wstring{buffer, length}} / L"Mayasaba";
    }

    void RefuseProductionPath(std::filesystem::path const& store)
    {
        auto const production = ProductionDirectory();
        auto const absolute = std::filesystem::absolute(store);
        auto const prefix = std::filesystem::absolute(production);
        auto const rel = std::filesystem::relative(absolute, prefix);
        if (!rel.empty() && rel.native().rfind(L"..", 0) != 0)
        {
            throw std::runtime_error("Refused: store path is inside production directory: " + store.string());
        }
    }

    struct LatencyStats
    {
        double minUs{0.0};
        double meanUs{0.0};
        double medianUs{0.0};
        double p95Us{0.0};
        double maxUs{0.0};
        size_t samples{0};
    };

    LatencyStats ComputeStats(std::vector<double>& samplesUs)
    {
        if (samplesUs.empty()) return {};
        std::sort(samplesUs.begin(), samplesUs.end());
        double const sum = std::accumulate(samplesUs.begin(), samplesUs.end(), 0.0);
        LatencyStats stats;
        stats.samples = samplesUs.size();
        stats.minUs = samplesUs.front();
        stats.maxUs = samplesUs.back();
        stats.meanUs = sum / samplesUs.size();
        stats.medianUs = samplesUs[samplesUs.size() / 2];
        size_t const p95Idx = static_cast<size_t>(samplesUs.size() * 0.95);
        stats.p95Us = samplesUs[std::min(p95Idx, samplesUs.size() - 1)];
        return stats;
    }

    static constexpr double MaxAllowedP95LatencyMs = 100.0;

    void PrintStats(std::string const& label, LatencyStats const& stats, double maxAllowedP95Ms = MaxAllowedP95LatencyMs)
    {
        double const p95Ms = stats.p95Us / 1000.0;
        std::cout << "| " << std::left << std::setw(38) << label
                  << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << stats.minUs / 1000.0 << " ms"
                  << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << stats.meanUs / 1000.0 << " ms"
                  << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << p95Ms << " ms"
                  << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << stats.maxUs / 1000.0 << " ms"
                  << " | " << std::right << std::setw(7) << stats.samples
                  << " |\n";

        if (p95Ms > maxAllowedP95Ms)
        {
            throw std::runtime_error("Latency threshold exceeded for '" + label + "': p95 was " +
                std::to_string(p95Ms) + " ms, exceeding allowable maximum of " + std::to_string(maxAllowedP95Ms) + " ms");
        }
    }

    void BenchmarkRecentFiltering(mayasaba::Storage& store)
    {
        std::cout << "\n### Benchmark Suite 1: Recent Projects Filtering (1,000 Recorded Projects)\n\n";
        std::cout << "| Query Scenario                         | Min Latency | Mean Latency | p95 Latency | Max Latency | Samples |\n";
        std::cout << "|:---------------------------------------|------------:|-------------:|------------:|------------:|--------:|\n";

        int const iterations = 100;

        // 1. Unfiltered first page (pageSize 20, offset 0)
        {
            std::vector<double> samples;
            mayasaba::CrossProjectRecentProjectsQuery q;
            q.pageSize = 20;
            q.offset = 0;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.QueryRecentProjects(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("QueryRecentProjects failed: " + res.errorMessage);
                if (res.items.size() != 20 || res.totalMatching != 1000) throw std::runtime_error("QueryRecentProjects returned unexpected item count");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Unfiltered (pageSize 20, offset 0)", ComputeStats(samples));
        }

        // 2. Status filter: Open only
        {
            std::vector<double> samples;
            mayasaba::CrossProjectRecentProjectsQuery q;
            q.pageSize = 20;
            q.statusFilter = mayasaba::ProjectStatusFilter::Open;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.QueryRecentProjects(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("QueryRecentProjects failed: " + res.errorMessage);
                for (auto const& p : res.items)
                {
                    if (p.closed) throw std::runtime_error("QueryRecentProjects returned closed project for Open filter");
                }
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Status: Open only (pageSize 20)", ComputeStats(samples));
        }

        // 3. Status filter: Closed only
        {
            std::vector<double> samples;
            mayasaba::CrossProjectRecentProjectsQuery q;
            q.pageSize = 20;
            q.statusFilter = mayasaba::ProjectStatusFilter::Closed;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.QueryRecentProjects(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("QueryRecentProjects failed: " + res.errorMessage);
                for (auto const& p : res.items)
                {
                    if (!p.closed) throw std::runtime_error("QueryRecentProjects returned open project for Closed filter");
                }
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Status: Closed only (pageSize 20)", ComputeStats(samples));
        }

        // 4. Substring filter (matching ~20% of projects)
        {
            std::vector<double> samples;
            mayasaba::CrossProjectRecentProjectsQuery q;
            q.pageSize = 20;
            q.filterText = "service_alpha";
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.QueryRecentProjects(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("QueryRecentProjects failed: " + res.errorMessage);
                if (res.items.empty()) throw std::runtime_error("Expected matches for substring filter 'service_alpha'");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Substring match 'service_alpha'", ComputeStats(samples));
        }

        // 5. Literal '%' filter
        {
            std::vector<double> samples;
            mayasaba::CrossProjectRecentProjectsQuery q;
            q.pageSize = 20;
            q.filterText = "special_%";
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.QueryRecentProjects(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("QueryRecentProjects failed: " + res.errorMessage);
                if (res.items.empty()) throw std::runtime_error("Expected matches for literal '%' filter 'special_%'");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Literal '%' match 'special_%'", ComputeStats(samples));
        }

        // 6. Deep paging (pageSize 20, offset 500)
        {
            std::vector<double> samples;
            mayasaba::CrossProjectRecentProjectsQuery q;
            q.pageSize = 20;
            q.offset = 500;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.QueryRecentProjects(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("QueryRecentProjects failed: " + res.errorMessage);
                if (res.items.size() != 20) throw std::runtime_error("Deep paging expected 20 items");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Deep paging (offset 500)", ComputeStats(samples));
        }

        // 7. Full table scan for 0 matches
        {
            std::vector<double> samples;
            mayasaba::CrossProjectRecentProjectsQuery q;
            q.pageSize = 20;
            q.filterText = "nonexistent_term_404";
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.QueryRecentProjects(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("QueryRecentProjects failed: " + res.errorMessage);
                if (!res.items.empty() || res.totalMatching != 0) throw std::runtime_error("Full scan expected 0 matches");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Full scan (0 matches)", ComputeStats(samples));
        }
    }

    void BenchmarkMessageSearchForDataset(mayasaba::Storage& store, std::string const& projectId, int count)
    {
        std::cout << "\n### Benchmark Suite 2: Saved-Message Search (" << count << " Saved Messages)\n\n";
        std::cout << "| Query Scenario                         | Min Latency | Mean Latency | p95 Latency | Max Latency | Samples |\n";
        std::cout << "|:---------------------------------------|------------:|-------------:|------------:|------------:|--------:|\n";

        int const iterations = 100;

        // 1. Cold query (first invocation)
        {
            mayasaba::MessageSearchQuery q;
            q.projectId = projectId;
            q.queryText = "conversational input";
            q.limit = 20;
            auto start = std::chrono::high_resolution_clock::now();
            auto res = store.SearchUserMessages(q);
            auto end = std::chrono::high_resolution_clock::now();
            if (!res.isComplete) throw std::runtime_error("SearchUserMessages cold query failed: " + res.errorMessage);
            if (res.matches.empty()) throw std::runtime_error("Cold query returned 0 matches");
            double const coldUs = std::chrono::duration<double, std::micro>(end - start).count();
            std::cout << "| " << std::left << std::setw(38) << "Cold query (first execution)"
                      << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << coldUs / 1000.0 << " ms"
                      << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << coldUs / 1000.0 << " ms"
                      << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << coldUs / 1000.0 << " ms"
                      << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << coldUs / 1000.0 << " ms"
                      << " | " << std::right << std::setw(7) << 1
                      << " |\n";
        }

        // 2. Warm Substring Search (matching ~10% of records)
        {
            std::vector<double> samples;
            mayasaba::MessageSearchQuery q;
            q.projectId = projectId;
            q.queryText = "algorithm";
            q.limit = 20;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.SearchUserMessages(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("SearchUserMessages failed: " + res.errorMessage);
                if (res.matches.empty() || res.totalMatches == 0) throw std::runtime_error("Expected matches for 'algorithm'");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Substring 'algorithm' (~10% hits)", ComputeStats(samples));
        }

        // 3. Rare Substring Search (matching exactly 1 record)
        {
            std::vector<double> samples;
            mayasaba::MessageSearchQuery q;
            q.projectId = projectId;
            q.queryText = "rare_needle_token";
            q.limit = 20;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.SearchUserMessages(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("SearchUserMessages failed: " + res.errorMessage);
                if (res.matches.size() != 1 || res.totalMatches != 1) throw std::runtime_error("Expected exactly 1 match for rare needle");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Rare needle (exact 1 match)", ComputeStats(samples));
        }

        // 4. Literal '%' and '_' search
        {
            std::vector<double> samples;
            mayasaba::MessageSearchQuery q;
            q.projectId = projectId;
            q.queryText = "100%_verified";
            q.limit = 20;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.SearchUserMessages(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("SearchUserMessages failed: " + res.errorMessage);
                if (res.matches.empty() || res.totalMatches == 0) throw std::runtime_error("Expected matches for '100%_verified'");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Literal wildcard '100%_verified'", ComputeStats(samples));
        }

        // 5. Non-ASCII / Unicode search
        {
            std::vector<double> samples;
            mayasaba::MessageSearchQuery q;
            q.projectId = projectId;
            q.queryText = "日本語テスト";
            q.limit = 20;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.SearchUserMessages(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("SearchUserMessages failed: " + res.errorMessage);
                if (res.matches.empty() || res.totalMatches == 0) throw std::runtime_error("Expected matches for Japanese test string");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Non-ASCII query '日本語テスト'", ComputeStats(samples));
        }

        // 6. Deep Paging (offset = count / 2)
        {
            std::vector<double> samples;
            mayasaba::MessageSearchQuery q;
            q.projectId = projectId;
            q.queryText = "message";
            q.limit = 20;
            q.offset = count / 2;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.SearchUserMessages(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("SearchUserMessages failed: " + res.errorMessage);
                if (res.matches.size() != 20) throw std::runtime_error("Deep paging expected 20 matches");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Deep paging (offset = count/2)", ComputeStats(samples));
        }

        // 7. Worst-case full table scan (0 matches)
        {
            std::vector<double> samples;
            mayasaba::MessageSearchQuery q;
            q.projectId = projectId;
            q.queryText = "term_guaranteed_not_to_exist_404";
            q.limit = 20;
            for (int i = 0; i < iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                auto res = store.SearchUserMessages(q);
                auto end = std::chrono::high_resolution_clock::now();
                if (!res.isComplete) throw std::runtime_error("SearchUserMessages failed: " + res.errorMessage);
                if (!res.matches.empty() || res.totalMatches != 0) throw std::runtime_error("Full scan expected 0 matches");
                samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            }
            PrintStats("Worst-case full scan (0 hits)", ComputeStats(samples));
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        wchar_t tempDir[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempDir);
        std::filesystem::path const baseDir = std::filesystem::path{tempDir} / L"mayasaba_perf_benchmarks";
        std::filesystem::create_directories(baseDir);

        std::filesystem::path const storePath = baseDir / L"perf_test.db";
        if (std::filesystem::exists(storePath))
        {
            std::filesystem::remove(storePath);
        }
        RefuseProductionPath(storePath);

        std::cout << "# Mayasaba Native UX Performance Benchmark\n\n";
        std::cout << "- **Environment:** Windows 11 x64, MSVC v145 Release Build\n";
        std::cout << "- **Database:** Isolated test store (" << storePath.string() << ")\n";
        std::cout << "- **Production database protection:** Verified outside %LOCALAPPDATA%\\Mayasaba\n";
        std::cout << "- **Latency Threshold Enforcement:** Strict p95 <= 100.0 ms checked and enforced per scenario\n\n";
        std::cout << "> [!NOTE]\n";
        std::cout << "> Scope: This benchmark measures SQLite storage engine query and filtering execution time\n";
        std::cout << "> on a background thread under controlled synthetic workloads. This establishes backend latency bounds.\n";
        std::cout << "> It does NOT measure WinUI 3 UI-thread compositor presentation rates, input typing latency, or\n";
        std::cout << "> XAML ListView layout / scrolling frame rates, which require live UI automation or ETW traces.\n";

        mayasaba::Storage store{storePath};

        // Seed 1,000 projects
        std::cout << "\nSeeding 1,000 project records for Screen 2 filtering benchmarks...\n";
        for (int i = 1; i <= 1000; ++i)
        {
            std::string const projId = "proj-" + std::to_string(i);
            std::string name;
            if (i % 5 == 0) name = "service_alpha_" + std::to_string(i);
            else if (i % 7 == 0) name = "special_%_project_" + std::to_string(i);
            else if (i % 11 == 0) name = "backend_core_" + std::to_string(i);
            else name = "project_workspace_" + std::to_string(i);

            std::string const folder = "C:\\Projects\\Workspace_" + std::to_string(i);
            std::string const ident = "ident-" + std::to_string(i);
            store.BindProject(projId, name, folder, ident);

            if (i % 3 == 0)
            {
                store.SetClosure(projId, 1, true);
            }
        }
        std::cout << "Done seeding 1,000 projects.\n";

        BenchmarkRecentFiltering(store);

        // Benchmark Message Search across 300, 3,000, and 10,000 records
        std::vector<int> const datasetCounts = {300, 3000, 10000};
        for (int targetCount : datasetCounts)
        {
            std::filesystem::path const msgStorePath = baseDir / (L"msg_perf_" + std::to_wstring(targetCount) + L".db");
            if (std::filesystem::exists(msgStorePath))
            {
                std::filesystem::remove(msgStorePath);
            }
            RefuseProductionPath(msgStorePath);

            mayasaba::Storage msgStore{msgStorePath};
            std::string const pId = "msg-proj-" + std::to_string(targetCount);
            msgStore.BindProject(pId, "MsgTest", "C:\\Projects\\MsgTest", "ident-msg");

            std::cout << "\nSeeding " << targetCount << " saved messages into isolated store...\n";
            for (int i = 1; i <= targetCount; ++i)
            {
                std::string const subId = "sub-" + std::to_string(i);
                std::string body;
                if (i == targetCount / 2)
                {
                    body = "This is the rare_needle_token specifically planted for exact single match verification.";
                }
                else if (i % 50 == 0)
                {
                    body = "Contains literal wildcard 100%_verified string with symbols and numbers.";
                }
                else if (i % 75 == 0)
                {
                    body = "Unicode content: 日本語テスト message with non-English characters for presentation filtering.";
                }
                else if (i % 10 == 0)
                {
                    body = "Message " + std::to_string(i) + ": Discussing the optimization algorithm for Task DAG graph execution and memory usage constraints.";
                }
                else
                {
                    body = "Message " + std::to_string(i) + ": Regular conversational input text verifying timeline rendering, search indexing and query responsiveness in Mayasaba.";
                }

                auto staged = msgStore.StageSubmission(pId, subId, body);
                msgStore.SaveContribution(pId, staged, body);
            }
            std::cout << "Seeded " << targetCount << " messages. Database size: "
                      << std::filesystem::file_size(msgStorePath) / 1024 << " KiB\n";

            BenchmarkMessageSearchForDataset(msgStore, pId, targetCount);
        }

        std::cout << "\nAll benchmarks completed successfully.\n";
        return 0;
    }
    catch (std::exception const& ex)
    {
        std::cerr << "BENCHMARK ERROR: " << ex.what() << "\n";
        return 1;
    }
}
