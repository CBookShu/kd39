#include "framework/rpc/client_interceptor.h"

#include "common/context/context_store.h"

namespace kd39::framework::rpc {

MetadataClientInterceptor::MetadataClientInterceptor(grpc::experimental::ClientRpcInfo* /*info*/) {}

void MetadataClientInterceptor::Intercept(grpc::experimental::InterceptorBatchMethods* methods) {
    if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
        const auto ctx = common::GetCurrentRequestContext();
        if (auto* metadata = methods->GetSendInitialMetadata(); metadata != nullptr) {
            if (!ctx.request_id.empty()) metadata->emplace(common::RequestContext::kMetaRequestId, ctx.request_id);
            if (!ctx.trace_id.empty()) metadata->emplace(common::RequestContext::kMetaTraceId, ctx.trace_id);
            if (!ctx.user_id.empty()) metadata->emplace(common::RequestContext::kMetaUserId, ctx.user_id);
            if (!ctx.traffic_tag.empty()) metadata->emplace(common::RequestContext::kMetaTrafficTag, ctx.traffic_tag);
            if (!ctx.client_version.empty()) metadata->emplace(common::RequestContext::kMetaClientVersion, ctx.client_version);
            if (!ctx.zone.empty()) metadata->emplace(common::RequestContext::kMetaZone, ctx.zone);
        }
    }
    methods->Proceed();
}

grpc::experimental::Interceptor* MetadataClientInterceptorFactory::CreateClientInterceptor(
    grpc::experimental::ClientRpcInfo* info) {
    return new MetadataClientInterceptor(info);
}

}  // namespace kd39::framework::rpc
