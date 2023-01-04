/*
 * Copyright 2022 Redpanda Data, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/redpanda-data/redpanda/blob/master/licenses/rcl.md
 */

#include "cloud_storage/segment_meta_cstore.h"
#include "model/fundamental.h"
#include "random/generators.h"
#include "seastarx.h"
#include "utils/delta_for.h"

#include <seastar/testing/perf_tests.hh>

#include <absl/container/btree_map.h>

#include <vector>

using namespace cloud_storage;

using delta_xor_alg = details::delta_xor;
using delta_xor_frame = segment_meta_column_frame<int64_t, delta_xor_alg>;
using delta_delta_alg = details::delta_delta<int64_t>;
using delta_delta_frame = segment_meta_column_frame<int64_t, delta_delta_alg>;
using delta_xor_column = segment_meta_column<int64_t, delta_xor_alg>;
using delta_delta_column = segment_meta_column<int64_t, delta_delta_alg>;

static const delta_xor_alg initial_xor{};
static const delta_delta_alg initial_delta{0};

static const delta_xor_frame xor_frame_4K = []() {
    delta_xor_frame frame(initial_xor);
    int64_t value = random_generators::get_int(1000);
    for (int64_t i = 0; i < 4096; i++) {
        value += random_generators::get_int(1, 100);
        frame.append(value);
    }
    return frame;
}();

static const delta_xor_column xor_column_4K = []() {
    delta_xor_column column(initial_xor);
    int64_t value = random_generators::get_int(1000);
    for (int64_t i = 0; i < 4096; i++) {
        value += random_generators::get_int(1, 100);
        column.append(value);
    }
    return column;
}();

static const delta_xor_column xor_column_4M = []() {
    delta_xor_column column(initial_xor);
    int64_t value = random_generators::get_int(1000);
    for (int64_t i = 0; i < 4096000; i++) {
        value += random_generators::get_int(1, 100);
        column.append(value);
    }
    return column;
}();

static const delta_delta_frame delta_frame_4K = []() {
    delta_delta_frame frame(initial_delta);
    int64_t value = random_generators::get_int(1000);
    for (int64_t i = 0; i < 4096; i++) {
        value += random_generators::get_int(1, 100);
        frame.append(value);
    }
    return frame;
}();

static const delta_delta_column delta_column_4K = []() {
    delta_delta_column column(initial_delta);
    int64_t value = random_generators::get_int(1000);
    for (int64_t i = 0; i < 4096; i++) {
        value += random_generators::get_int(1, 100);
        column.append(value);
    }
    return column;
}();

static const delta_delta_column delta_column_4M = []() {
    delta_delta_column column(initial_delta);
    int64_t value = random_generators::get_int(1000);
    for (int64_t i = 0; i < 4096000; i++) {
        value += random_generators::get_int(1, 100);
        column.append(value);
    }
    return column;
}();

template<class StoreT>
void append_test(StoreT& store, int test_scale) {
    std::vector<int64_t> head;
    int64_t tail;
    int64_t value = 0;
    for (int64_t i = 0; i < test_scale - 1; i++) {
        value += random_generators::get_int(1, 100);
        head.push_back(value);
    }
    tail = value + random_generators::get_int(1, 100);
    for (auto x : head) {
        store.append(x);
    }
    perf_tests::start_measuring_time();
    store.append(tail);
    perf_tests::stop_measuring_time();
}

template<class StoreT>
void append_tx_test(StoreT& store, int test_scale) {
    std::vector<int64_t> head;
    int64_t tail;
    int64_t value = 0;
    for (int64_t i = 0; i < test_scale - 1; i++) {
        value += random_generators::get_int(1, 100);
        head.push_back(value);
    }
    tail = value + random_generators::get_int(1, 100);
    for (auto x : head) {
        store.append(x);
    }
    perf_tests::start_measuring_time();
    auto tx = store.append_tx(tail);
    if (tx) {
        tx->commit();
    } else {
        assert(false);
    }
    perf_tests::stop_measuring_time();
}

template<class StoreT>
void find_test(StoreT& store) {
    perf_tests::start_measuring_time();
    auto it = store.find(*store.last_value());
    perf_tests::do_not_optimize(it);
    perf_tests::stop_measuring_time();
}

template<class StoreT>
void at_test(StoreT& store) {
    perf_tests::start_measuring_time();
    auto it = store.at(store.size() - 1);
    perf_tests::do_not_optimize(it);
    perf_tests::stop_measuring_time();
}

PERF_TEST(cstore_bench, xor_frame_append) {
    delta_xor_frame frame(initial_xor);
    append_test(frame, 4096);
}

PERF_TEST(cstore_bench, xor_frame_append_tx) {
    delta_xor_frame frame(initial_xor);
    append_tx_test(frame, 4096);
}

PERF_TEST(cstore_bench, xor_column_append) {
    delta_xor_column column(initial_xor);
    append_test(column, 4096);
}

PERF_TEST(cstore_bench, xor_column_append_tx) {
    delta_xor_column column(initial_xor);
    append_tx_test(column, 4096);
}

PERF_TEST(cstore_bench, xor_column_append_tx2) {
    // trigger code path that commits by splicing the list
    delta_xor_column column(initial_xor);
    append_tx_test(column, 4097);
}

PERF_TEST(cstore_bench, xor_frame_find_4K) { find_test(xor_frame_4K); }

PERF_TEST(cstore_bench, xor_column_find_4K) { find_test(xor_column_4K); }

PERF_TEST(cstore_bench, xor_frame_at_4K) { at_test(xor_frame_4K); }

PERF_TEST(cstore_bench, xor_column_at_4K) { at_test(xor_column_4K); }

PERF_TEST(cstore_bench, xor_column_at_4M) { at_test(xor_column_4M); }

PERF_TEST(cstore_bench, delta_frame_append) {
    delta_delta_frame frame(initial_delta);
    append_test(frame, 4096);
}

PERF_TEST(cstore_bench, delta_frame_append_tx) {
    delta_delta_frame frame(initial_delta);
    append_tx_test(frame, 4096);
}

PERF_TEST(cstore_bench, delta_column_append) {
    delta_delta_column column(initial_delta);
    append_test(column, 4096);
}

PERF_TEST(cstore_bench, delta_column_append_tx) {
    delta_delta_column column(initial_delta);
    append_tx_test(column, 4096);
}

PERF_TEST(cstore_bench, delta_column_append_tx2) {
    // trigger code path that commits by splicing the list
    delta_delta_column column(initial_delta);
    append_tx_test(column, 4097);
}

PERF_TEST(cstore_bench, delta_frame_find_4K) { find_test(delta_frame_4K); }

PERF_TEST(cstore_bench, delta_column_find_4K) { find_test(delta_column_4K); }

PERF_TEST(cstore_bench, delta_column_find_4M) { at_test(delta_column_4M); }

PERF_TEST(cstore_bench, delta_frame_at_4K) { at_test(delta_frame_4K); }

PERF_TEST(cstore_bench, delta_column_at_4K) { at_test(delta_column_4K); }

PERF_TEST(cstore_bench, delta_column_at_4M) { at_test(delta_column_4M); }
