#pragma once

#include <grpcpp/support/client_interceptor.h>
#include "common/context/request_context.h"

namespace kd39::framework::rpc {

// Automatically injects RequestContext metadata into outgoing RPCs.
class MetadataClientInterceptor
    : public grpc::experimental::Interceptor {
public:
    explicit MetadataClientInterceptor(grpc::experimental::ClientRpcInfo* info);

    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override;
};

class MetadataClientInterceptorFactory
    : public grpc::experimental::ClientInterceptorFactoryInterface {
public:
    grpc::experimental::Interceptor* CreateClientInterceptor(
        grpc::experimental::ClientRpcInfo* info) override;
};

}  // namespace kd39::framework::rpc
