#include "framework/rpc/server_interceptor.h"

#include "common/context/context_store.h"

namespace kd39::framework::rpc {
namespace {
std::string ToString(grpc::string_ref ref) {
    return std::string(ref.data(), ref.size());
}
}  // namespace

MetadataServerInterceptor::MetadataServerInterceptor(grpc::experimental::ServerRpcInfo* /*info*/) {}

void MetadataServerInterceptor::Intercept(grpc::experimental::InterceptorBatchMethods* methods) {
    if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
        common::RequestContext ctx;
        if (auto* metadata = methods->GetRecvInitialMetadata(); metadata != nullptr) {
            for (const auto& [key, value] : *metadata) {
                const auto key_str = ToString(key);
                const auto value_str = ToString(value);
                if (key_str == common::RequestContext::kMetaRequestId) ctx.request_id = value_str;
                else if (key_str == common::RequestContext::kMetaTraceId) ctx.trace_id = value_str;
                else if (key_str == common::RequestContext::kMetaUserId) ctx.user_id = value_str;
                else if (key_str == common::RequestContext::kMetaTrafficTag) ctx.traffic_tag = value_str;
                else if (key_str == common::RequestContext::kMetaClientVersion) ctx.client_version = value_str;
                else if (key_str == common::RequestContext::kMetaZone) ctx.zone = value_str;
            }
        }
        common::SetCurrentRequestContext(ctx);
    }

    if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::PRE_SEND_STATUS)) {
        common::ClearCurrentRequestContext();
    }

    methods->Proceed();
}

grpc::experimental::Interceptor* MetadataServerInterceptorFactory::CreateServerInterceptor(
    grpc::experimental::ServerRpcInfo* info) {
    return new MetadataServerInterceptor(info);
}

}  // namespace kd39::framework::rpc
