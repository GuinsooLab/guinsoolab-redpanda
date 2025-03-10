/*
 * Copyright 2022 Redpanda Data, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/redpanda-data/redpanda/blob/master/licenses/rcl.md
 */

#include "http/tests/http_imposter.h"

#include "utils/uuid.h"
#include "vlog.h"

#include <seastar/http/function_handlers.hh>

#include <utility>

static ss::logger http_imposter_log("http_imposter"); // NOLINT

/**
 * Listening on TCP ports from a unit test is not a great practice, but we
 * do it in some places.  To enable these unit tests to run in parallel,
 * each unit test binary must declare its own function to pick a port that
 * no other unit test uses.
 */
extern uint16_t unit_test_httpd_port_number();

http_imposter_fixture::http_imposter_fixture()
  : _server_addr{ss::ipv4_addr{
    httpd_host_name.data(), unit_test_httpd_port_number()}}
  , _address{
      {httpd_host_name.data(), httpd_host_name.size()},
      unit_test_httpd_port_number()} {
    _id = fmt::format("{}", uuid_t::create());
    _server.start().get();
}

http_imposter_fixture::http_imposter_fixture(net::unresolved_address address)
  : _server_addr{ss::ipv4_addr{address.host().data(), address.port()}}
  , _address{address} {
    _id = fmt::format("{}", uuid_t::create());
    _server.start().get();
}

http_imposter_fixture::~http_imposter_fixture() { _server.stop().get(); }

uint16_t http_imposter_fixture::httpd_port_number() {
    return unit_test_httpd_port_number();
}

void http_imposter_fixture::start_request_masking(
  http_test_utils::response canned_response,
  ss::lowres_clock::duration duration) {
    _masking_active = {canned_response, duration, ss::lowres_clock::now()};
}

const std::vector<ss::httpd::request>&
http_imposter_fixture::get_requests() const {
    return _requests;
}

const std::multimap<ss::sstring, ss::httpd::request>&
http_imposter_fixture::get_targets() const {
    return _targets;
}

void http_imposter_fixture::listen() {
    _server.set_routes([this](ss::httpd::routes& r) { set_routes(r); }).get();
    _server.listen(_server_addr).get();
    vlog(http_imposter_log.trace, "HTTP imposter {} started", _id);
}

static ss::sstring remove_query_params(std::string_view url) {
    auto q_pos = url.find('?');
    return q_pos == std::string_view::npos ? ss::sstring{url.data(), url.size()}
                                           : ss::sstring{url.substr(0, q_pos)};
}

void http_imposter_fixture::set_routes(ss::httpd::routes& r) {
    using namespace ss::httpd;
    _handler = std::make_unique<function_handler>(
      [this](const_req req, reply& repl) -> ss::sstring {
          if (_masking_active) {
              if (
                ss::lowres_clock::now() - _masking_active->started
                > _masking_active->duration) {
                  _masking_active.reset();
              } else {
                  repl.set_status(_masking_active->canned_response.status);
                  return _masking_active->canned_response.body;
              }
          }

          _requests.push_back(req);
          _targets.insert(std::make_pair(req._url, req));

          const auto& fp = _fail_requests_when;
          for (size_t i = 0; i < fp.size(); ++i) {
              if (fp[i](req)) {
                  auto response = _fail_responses[i];
                  repl.set_status(response.status);
                  vlog(
                    http_imposter_log.trace,
                    "HTTP imposter id {} failing request {} - {} - {} with "
                    "response: {}",
                    _id,
                    req._url,
                    req.content_length,
                    req._method,
                    response);
                  return response.body;
              }
          }

          vlog(
            http_imposter_log.trace,
            "HTTP imposter id {} request {} - {} - {}",
            _id,
            req._url,
            req.content_length,
            req._method);

          if (req._method == "PUT") {
              when().request(req._url).then_reply_with(req.content);
              repl.set_status(ss::httpd::reply::status_type::ok);
              return "";
          }
          if (req._method == "DELETE") {
              repl.set_status(reply::status_type::no_content);
              return "";
          } else {
              ss::httpd::request lookup_r{req};
              lookup_r._url = remove_query_params(req._url);

              auto response = lookup(lookup_r);
              repl.set_status(response.status);
              return response.body;
          }
      },
      "txt");
    r.add_default_handler(_handler.get());
}

bool http_imposter_fixture::has_call(std::string_view url) const {
    return std::find_if(
             _requests.cbegin(),
             _requests.cend(),
             [&url](const auto& r) { return r._url == url; })
           != _requests.cend();
}

void http_imposter_fixture::fail_request_if(
  http_imposter_fixture::request_predicate predicate,
  http_test_utils::response response) {
    _fail_requests_when.push_back(std::move(predicate));
    _fail_responses[_fail_requests_when.size() - 1] = std::move(response);
}

void http_imposter_fixture::reset_http_call_state() {
    _requests.clear();
    _targets.clear();
}

void http_imposter_fixture::log_requests() const {
    for (const auto& req : _requests) {
        vlog(
          http_imposter_log.info,
          "{}: {} - {} ({} bytes)",
          _id,
          req._method,
          req._url,
          req.content_length);
    }
}
