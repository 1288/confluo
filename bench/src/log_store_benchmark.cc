#include "log_store_benchmark.h"

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <thread>
#include <condition_variable>
#include <unistd.h>
#include <sstream>

#ifdef NO_LOG
#define LOG(out, fmt, ...)
#else
#define LOG(out, fmt, ...) fprintf(out, fmt, ##__VA_ARGS__)
#endif

#define QUERY(i, num_keys) {\
  std::vector<int64_t> search_res;\
  std::string get_res;\
  if (query_types[i % kThreadQueryCount] == 0) {\
    client->Get(get_res, keys[i % keys.size()]);\
    num_keys++;\
  } else if (query_types[i % kThreadQueryCount] == 1) {\
    client->Search(search_res, terms[i % terms.size()]);\
    num_keys += search_res.size();\
  } else if (query_types[i % kThreadQueryCount] == 2) {\
    client->Append(cur_key, values[i % values.size()]);\
    num_keys++;\
  } else {\
    client->Delete(keys[i % keys.size()]);\
    num_keys++;\
  }\
}

LogStoreBenchmark::LogStoreBenchmark(std::string& data_path,
                                     std::string& hostname) {

  char resolved_path[100];
  realpath(data_path.c_str(), resolved_path);
  data_path_ = resolved_path;
  hostname_ = hostname;

  BenchmarkConnection cx(hostname_, 11002);
  LOG(stderr, "Loading data on server...\n");
  uint64_t start = GetTimestamp();
  load_keys_ = cx.client->Load(data_path + ".ser");
  uint64_t end = GetTimestamp();
  uint64_t time_taken = (end - start) / 10e6;
  LOG(stderr, "Data load complete: took %llu seconds.\n", time_taken);
}

void LogStoreBenchmark::BenchmarkGetLatency() {
  // Generate queries
  fprintf(stderr, "Generating queries...");
  std::vector<int64_t> keys;

  for (int64_t i = 0; i < kWarmupCount + kMeasureCount; i++) {
    int64_t key = rand() % load_keys_;
    keys.push_back(key);
  }

  fprintf(stderr, "Done.\n");

  TimeStamp t0, t1, tdiff;
  std::ofstream result_stream("latency_get");
  BenchmarkConnection cx(hostname_, 11002);
  auto client = cx.client;

  // Warmup
  fprintf(stderr, "Warming up for %llu queries...\n", kWarmupCount);
  for (uint64_t i = 0; i < kWarmupCount; i++) {
    std::string result;
    client->Get(result, keys[i]);
  }
  fprintf(stderr, "Warmup complete.\n");

  // Measure
  fprintf(stderr, "Measuring for %llu queries...\n", kMeasureCount);
  for (uint64_t i = kWarmupCount; i < kWarmupCount + kMeasureCount; i++) {
    std::string result;
    t0 = GetTimestamp();
    client->Get(result, keys[i]);
    t1 = GetTimestamp();
    tdiff = t1 - t0;
    result_stream << keys[i] << "\t" << tdiff << "\n";
  }
  fprintf(stderr, "Measure complete.\n");
  result_stream.close();
}

void LogStoreBenchmark::BenchmarkSearchLatency() {
  fprintf(stderr, "Reading queries...");
  std::vector<std::string> queries;

  std::ifstream in(data_path_ + ".queries");
  std::string query_line;
  while (queries.size() < kWarmupCount + kMeasureCount
      && std::getline(in, query_line)) {

    uint32_t attr_id;
    std::string attr_val;
    std::stringstream ss(query_line);
    ss >> attr_id >> attr_val;
    attr_val = (char) (kBeginDelim + attr_id) + attr_val
        + (char) (kBeginDelim + attr_id + 1);
    queries.push_back(attr_val);
  }

  size_t warmup_count = queries.size() / 10;
  size_t measure_count = queries.size() - warmup_count;

  fprintf(stderr, "Done.\n");

  TimeStamp t0, t1, tdiff;
  std::ofstream result_stream("latency_search");
  BenchmarkConnection cx(hostname_, 11002);
  auto client = cx.client;

  // Warmup
  fprintf(stderr, "Warming up for %llu queries...\n", kWarmupCount);
  for (uint64_t i = 0; i < warmup_count; i++) {
    std::string query = queries[i % queries.size()];
    std::vector<int64_t> results;
    client->Search(results, query);
  }
  fprintf(stderr, "Warmup complete.\n");

  // Measure
  fprintf(stderr, "Measuring for %llu queries...\n", kMeasureCount);
  for (uint64_t i = warmup_count; i < warmup_count + measure_count; i++) {
    std::string query = queries[i % queries.size()];
    std::vector<int64_t> results;
    t0 = GetTimestamp();
    client->Search(results, query);
    t1 = GetTimestamp();
    tdiff = t1 - t0;
    result_stream << results.size() << "\t" << tdiff << "\n";
  }
  fprintf(stderr, "Measure complete.\n");
  result_stream.close();
}

void LogStoreBenchmark::BenchmarkAppendLatency() {
  // Generate queries
  fprintf(stderr, "Generating queries...");
  std::vector<std::string> values;

  std::ifstream in(data_path_ + ".inserts");
  for (int64_t i = 0; i < kWarmupCount + kMeasureCount; i++) {
    std::string cur_value;
    std::getline(in, cur_value);
    values.push_back(cur_value);
  }
  int64_t cur_key = load_keys_;

  fprintf(stderr, "Done.\n");

  TimeStamp t0, t1, tdiff;
  std::ofstream result_stream("latency_append");
  BenchmarkConnection cx(hostname_, 11002);
  auto client = cx.client;

  // Warmup
  fprintf(stderr, "Warming up for %llu queries...\n", kWarmupCount);
  for (uint64_t i = 0; i < kWarmupCount; i++) {
    std::string cur_value = values[i];
    client->Append(cur_key++, cur_value);
  }
  fprintf(stderr, "Warmup complete.\n");

  // Measure
  fprintf(stderr, "Measuring for %llu queries...\n", kMeasureCount);
  for (uint64_t i = kWarmupCount; i < kWarmupCount + kMeasureCount; i++) {
    std::string cur_value = values[i];
    t0 = GetTimestamp();
    client->Append(cur_key++, cur_value);
    t1 = GetTimestamp();
    tdiff = t1 - t0;
    result_stream << (cur_key - 1) << "\t" << tdiff << "\n";
  }
  fprintf(stderr, "Measure complete.\n");
  result_stream.close();
}

void LogStoreBenchmark::BenchmarkDeleteLatency() {
  // Generate queries
  LOG(stderr, "Generating queries...");
  std::vector<int64_t> keys;

  for (int64_t i = 0; i < kWarmupCount + kMeasureCount; i++) {
    int64_t key = rand() % load_keys_;
    keys.push_back(key);
  }

  LOG(stderr, "Done.\n");

  std::ofstream result_stream("latency_delete");
  BenchmarkConnection cx(hostname_, 11002);
  auto client = cx.client;

  // Warmup
  LOG(stderr, "Warming up for %llu queries...\n", kWarmupCount);
  for (uint64_t i = 0; i < kWarmupCount; i++) {
    client->Delete(keys[i]);
  }
  LOG(stderr, "Warmup complete.\n");

  // Measure
  LOG(stderr, "Measuring for %llu queries...\n", kMeasureCount);
  for (uint64_t i = kWarmupCount; i < kWarmupCount + kMeasureCount; i++) {
    auto t0 = GetTimestamp();
    client->Delete(keys[i]);
    auto t1 = GetTimestamp();
    auto tdiff = t1 - t0;
    result_stream << keys[i] << "\t" << tdiff << "\n";
  }
  LOG(stderr, "Measure complete.\n");
  result_stream.close();
}

void LogStoreBenchmark::BenchmarkThroughput(double get_f, double search_f,
                                            double append_f, double delete_f,
                                            uint32_t num_clients) {

  if (get_f + search_f + append_f + delete_f != 1.0) {
    LOG(stderr, "Query fractions must add up to 1.0. Sum = %lf\n",
        get_f + search_f + append_f + delete_f);
    return;
  }

  const double get_m = get_f, search_m = get_f + search_f, append_m = get_f
      + search_f + append_f, delete_m = get_f + search_f + append_f + delete_f;

  std::vector<std::thread> threads;

  for (uint32_t i = 0; i < num_clients; i++) {
    threads.push_back(
        std::move(
            std::thread(
                [&] {
                  std::vector<int64_t> keys;
                  std::vector<std::string> values, terms;

                  std::ifstream in_s(data_path_ + ".queries");
                  std::ifstream in_a(data_path_ + ".inserts");
                  int64_t cur_key = load_keys_;
                  std::string query_line, value;
                  std::vector<uint32_t> query_types;
                  LOG(stderr, "Generating queries...\n");
                  for (int64_t i = 0; i < kThreadQueryCount; i++) {
                    int64_t key = RandomInteger(0, load_keys_);
                    if (std::getline(in_s, query_line)) {
                      uint32_t attr_id;
                      std::string attr_val;
                      std::stringstream ss(query_line);
                      ss >> attr_id >> attr_val;
                      attr_val = (char) (kBeginDelim + attr_id) + attr_val
                      + (char) (kBeginDelim + attr_id + 1);
                      terms.push_back(attr_val);
                    }
                    if (std::getline(in_a, value)) values.push_back(value);
                    keys.push_back(key);

                    double r = RandomDouble(0, 1);
                    if (r <= get_m) {
                      query_types.push_back(0);
                    } else if (r <= search_m) {
                      query_types.push_back(1);
                    } else if (r <= append_m) {
                      query_types.push_back(2);
                    } else if (r <= delete_m) {
                      query_types.push_back(3);
                    }
                  }

                  fprintf(stderr, "Read %zu keys, %zu search queries and %zu append queries.\n", keys.size(), terms.size(), values.size());

                  std::shuffle(keys.begin(), keys.end(), PRNG());
                  std::shuffle(terms.begin(), terms.end(), PRNG());
                  std::shuffle(values.begin(), values.end(), PRNG());
                  LOG(stderr, "Done.\n");

                  double query_thput = 0;
                  double key_thput = 0;

                  BenchmarkConnection cx(hostname_, 11002);
                  auto client = cx.client;

                  try {
                    // Warmup phase
                    long i = 0;
                    long num_keys = 0;
                    TimeStamp warmup_start = GetTimestamp();
                    while (GetTimestamp() - warmup_start < kWarmupTime) {
                      QUERY(i, num_keys);
                      i++;
                    }

                    // Measure phase
                    i = 0;
                    num_keys = 0;
                    TimeStamp start = GetTimestamp();
                    while (GetTimestamp() - start < kMeasureTime) {
                      QUERY(i, num_keys);
                      i++;
                    }
                    TimeStamp end = GetTimestamp();
                    double totsecs = (double) (end - start) / (1000.0 * 1000.0);
                    query_thput = ((double) i / totsecs);
                    key_thput = ((double) num_keys / totsecs);

                    // Cooldown phase
                    i = 0;
                    num_keys = 0;
                    TimeStamp cooldown_start = GetTimestamp();
                    while (GetTimestamp() - cooldown_start < kCooldownTime) {
                      QUERY(i, num_keys);
                      i++;
                    }

                  } catch (std::exception &e) {
                    LOG(stderr, "Throughput thread ended prematurely.\n");
                  }

                  LOG(stderr, "Throughput: %lf\n", query_thput);

                  std::ofstream ofs;
                  char output_file[100];
                  sprintf(output_file, "throughput_%.2f_%.2f_%.2f_%.2f_%d", get_f, search_f, append_f, delete_f, num_clients);
                  ofs.open(output_file, std::ofstream::out | std::ofstream::app);
                  ofs << query_thput << "\t" << key_thput << "\n";
                  ofs.close();

                })));
  }

  for (auto& th : threads) {
    th.join();
  }

}

void PrintUsage(char *exec) {
  fprintf(
      stderr,
      "Usage: %s [-b bench-type] [-h hostname] [-n num-clients] data-path\n",
      exec);
}

std::vector<std::string> &Split(const std::string &s, char delim,
                                std::vector<std::string> &elems) {
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    elems.push_back(item);
  }
  return elems;
}

std::vector<std::string> Split(const std::string &s, char delim) {
  std::vector<std::string> elems;
  Split(s, delim, elems);
  return elems;
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 8) {
    PrintUsage(argv[0]);
    return -1;
  }

  int c;
  std::string bench_type = "latency-get", hostname = "localhost";
  int num_clients = 1;
  while ((c = getopt(argc, argv, "b:n:h:")) != -1) {
    switch (c) {
      case 'b':
        bench_type = std::string(optarg);
        break;
      case 'n':
        num_clients = atoi(optarg);
        break;
      case 'h':
        hostname = std::string(optarg);
        break;
      default:
        fprintf(stderr, "Could not parse command line arguments.\n");
    }
  }

  if (optind == argc) {
    PrintUsage(argv[0]);
    return -1;
  }

  std::string data_path = std::string(argv[optind]);

  LogStoreBenchmark ls_bench(data_path, hostname);
  if (bench_type == "latency-get") {
    ls_bench.BenchmarkGetLatency();
  } else if (bench_type == "latency-search") {
    ls_bench.BenchmarkSearchLatency();
  } else if (bench_type == "latency-append") {
    ls_bench.BenchmarkAppendLatency();
  } else if (bench_type == "latency-delete") {
    ls_bench.BenchmarkDeleteLatency();
  } else if (bench_type.find("throughput") == 0) {
    std::vector<std::string> tokens = Split(bench_type, '-');
    if (tokens.size() != 5) {
      LOG(stderr, "Error: Incorrect throughput benchmark format.\n");
      return -1;
    }
    double get_f = atof(tokens[1].c_str());
    double search_f = atof(tokens[2].c_str());
    double append_f = atof(tokens[3].c_str());
    double delete_f = atof(tokens[4].c_str());
    LOG(stderr,
        "get_f = %.2lf, search_f = %.2lf, append_f = %.2lf, delete_f = %.2lf, num_clients = %d\n",
        get_f, search_f, append_f, delete_f, num_clients);
    ls_bench.BenchmarkThroughput(get_f, search_f, append_f, delete_f,
                                 num_clients);
  } else {
    fprintf(stderr, "Unknown benchmark type: %s\n", bench_type.c_str());
  }

  return 0;
}
