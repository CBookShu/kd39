#pragma once

#include <grpcpp/support/server_interceptor.h>
#include "common/context/request_context.h"

namespace kd39::framework::rpc {

// Extracts metadata from incoming RPCs and populates RequestContext.
class MetadataServerInterceptor
    : public grpc::experimental::Interceptor {
public:
    explicit MetadataServerInterceptor(grpc::experimental::ServerRpcInfo* info);

    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override;
};

class MetadataServerInterceptorFactory
    : public grpc::experimental::ServerInterceptorFactoryInterface {
public:
    grpc::experimental::Interceptor* CreateServerInterceptor(
        grpc::experimental::ServerRpcInfo* info) override;
};

}  // namespace kd39::framework::rpc
