/* Copyright 2023 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */

#include <arcticdb/storage/s3/mock_s3_client.hpp>
#include <arcticdb/storage/s3/s3_client_wrapper.hpp>

#include <arcticdb/util/preconditions.hpp>
#include <arcticdb/util/pb_util.hpp>
#include <arcticdb/log/log.hpp>
#include <arcticdb/util/buffer_pool.hpp>

#include <arcticdb/storage/storage_utils.hpp>

#include <aws/s3/S3Errors.h>

namespace arcticdb::storage{

using namespace object_store_utils;

namespace s3 {

std::string operation_to_string(S3Operation operation){
    switch (operation) {
        case S3Operation::HEAD: return "Head";
        case S3Operation::GET: return "Get";
        case S3Operation::PUT: return "Put";
        case S3Operation::DELETE: return "Delete";
        case S3Operation::DELETE_LOCAL: return "Delete_local";
        case S3Operation::LIST: return "List";
    }
    util::raise_rte("Invalid s3 operation");
}

std::string MockS3Client::get_failure_trigger(
        const std::string& s3_object_name,
        S3Operation operation_to_fail,
        Aws::S3::S3Errors error_to_fail_with) {
    return fmt::format("{}#Failure_{}_{}", s3_object_name, operation_to_string(operation_to_fail), (int)error_to_fail_with);
}

std::optional<Aws::S3::S3Error> has_failure_trigger(const std::string& s3_object_name, S3Operation operation){
    auto failure_string_for_operation = "#Failure_" + operation_to_string(operation) + "_";
    auto position = s3_object_name.rfind(failure_string_for_operation);
    if (position == std::string::npos) return std::nullopt;
    try {
        auto failure_code_string = s3_object_name.substr(position + failure_string_for_operation.size());
        auto failure_code = Aws::S3::S3Errors(std::stoi(failure_code_string));
        return Aws::S3::S3Error(Aws::Client::AWSError<Aws::S3::S3Errors>(failure_code, "Simulated error", "Simulated error message", true));
    } catch (std::exception& e) {
        return std::nullopt;
    }
}

const auto not_found_error = Aws::S3::S3Error(Aws::Client::AWSError<Aws::S3::S3Errors>(Aws::S3::S3Errors::RESOURCE_NOT_FOUND, false));

S3Result<std::monostate> MockS3Client::head_object(
        const std::string& s3_object_name,
        const std::string &bucket_name) const {
    auto maybe_error = has_failure_trigger(s3_object_name, S3Operation::HEAD);
    if (maybe_error.has_value()) {
        return {maybe_error.value()};
    }

    if (s3_contents.find({bucket_name, s3_object_name}) == s3_contents.end()){
        return {not_found_error};
    }
    return {std::monostate()};
}


S3Result<Segment> MockS3Client::get_object(
        const std::string &s3_object_name,
        const std::string &bucket_name) const {
    auto maybe_error = has_failure_trigger(s3_object_name, S3Operation::GET);
    if (maybe_error.has_value()) {
        return {maybe_error.value()};
    }

    auto pos = s3_contents.find({bucket_name, s3_object_name});
    if (pos == s3_contents.end()){
        return {not_found_error};
    }
    return {pos->second};
}

S3Result<std::monostate> MockS3Client::put_object(
        const std::string &s3_object_name,
        Segment &&segment,
        const std::string &bucket_name) {
    auto maybe_error = has_failure_trigger(s3_object_name, S3Operation::PUT);
    if (maybe_error.has_value()) {
        return {maybe_error.value()};
    }

    s3_contents.insert_or_assign({bucket_name, s3_object_name}, std::move(segment));

    return {std::monostate()};
}

S3Result<DeleteOutput> MockS3Client::delete_objects(
        const std::vector<std::string>& s3_object_names,
        const std::string& bucket_name) {
    for (auto& s3_object_name : s3_object_names){
        auto maybe_error = has_failure_trigger(s3_object_name, S3Operation::DELETE);
        if (maybe_error.has_value()) {
            return {maybe_error.value()};
        }
    }

    DeleteOutput output;
    for (auto& s3_object_name : s3_object_names){
        auto maybe_error = has_failure_trigger(s3_object_name, S3Operation::DELETE_LOCAL);
        if (maybe_error.has_value()) {
            output.failed_deletes.emplace_back(s3_object_name);
        }
        else {
            s3_contents.erase({bucket_name, s3_object_name});
        }
    }
    return {output};
}

// Using a fixed page size since it's only being used for simple tests.
// If we ever need to configure it we should move it to the s3 proto config instead.
constexpr auto page_size = 10;
S3Result<ListObjectsOutput> MockS3Client::list_objects(
        const std::string& name_prefix,
        const std::string& bucket_name,
        const std::optional<std::string> continuation_token) const {
    // Terribly inefficient but fine for tests.
    auto matching_names = std::vector<std::string>();
    for (auto& key : s3_contents){
        if (key.first.first == bucket_name && key.first.second.rfind(name_prefix, 0) == 0){
            matching_names.emplace_back(key.first.second);
        }
    }

    auto start_from = 0u;
    if (continuation_token.has_value()) {
        start_from = std::stoi(continuation_token.value());
    }

    ListObjectsOutput output;
    auto end_to = matching_names.size();
    if (start_from + page_size < end_to){
        end_to = start_from + page_size;
        output.next_continuation_token = std::to_string(end_to);
    }

    for (auto i=start_from; i < end_to; ++i){
        auto& s3_object_name = matching_names[i];

        auto maybe_error = has_failure_trigger(s3_object_name, S3Operation::LIST);
        if (maybe_error.has_value()) return {maybe_error.value()};

        output.s3_object_names.emplace_back(s3_object_name);
    }

    return {output};
}

}

}
