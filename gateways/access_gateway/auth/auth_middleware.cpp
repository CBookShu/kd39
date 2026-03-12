#include "access_gateway/auth/auth_middleware.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "common/log/logger.h"

namespace kd39::gateways::access {
namespace {

constexpr char kLegacyDevToken[] = "dev-token";
constexpr char kJwtHs256[] = "HS256";

std::string Base64UrlEncode(const unsigned char* data, std::size_t len) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((len * 4 + 2) / 3);
    for (std::size_t i = 0; i < len; i += 3) {
        const std::uint32_t octet_a = data[i];
        const std::uint32_t octet_b = (i + 1 < len) ? data[i + 1] : 0;
        const std::uint32_t octet_c = (i + 2 < len) ? data[i + 2] : 0;
        const std::uint32_t triple = (octet_a << 16U) | (octet_b << 8U) | octet_c;

        out.push_back(kTable[(triple >> 18U) & 0x3FU]);
        out.push_back(kTable[(triple >> 12U) & 0x3FU]);
        if (i + 1 < len) out.push_back(kTable[(triple >> 6U) & 0x3FU]);
        if (i + 2 < len) out.push_back(kTable[triple & 0x3FU]);
    }
    return out;
}

int DecodeBase64UrlChar(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '-' || ch == '+') return 62;
    if (ch == '_' || ch == '/') return 63;
    return -1;
}

std::optional<std::string> Base64UrlDecode(std::string_view input) {
    std::string b64(input);
    while (b64.size() % 4 != 0) {
        b64.push_back('=');
    }

    std::string out;
    out.reserve((b64.size() / 4) * 3);
    for (std::size_t i = 0; i < b64.size(); i += 4) {
        const int a = DecodeBase64UrlChar(b64[i]);
        const int b = DecodeBase64UrlChar(b64[i + 1]);
        const int c = b64[i + 2] == '=' ? -2 : DecodeBase64UrlChar(b64[i + 2]);
        const int d = b64[i + 3] == '=' ? -2 : DecodeBase64UrlChar(b64[i + 3]);
        if (a < 0 || b < 0 || c < -1 || d < -1) {
            return std::nullopt;
        }

        const std::uint32_t triple =
            (static_cast<std::uint32_t>(a) << 18U) |
            (static_cast<std::uint32_t>(b) << 12U) |
            ((c >= 0 ? static_cast<std::uint32_t>(c) : 0U) << 6U) |
            (d >= 0 ? static_cast<std::uint32_t>(d) : 0U);

        out.push_back(static_cast<char>((triple >> 16U) & 0xFFU));
        if (b64[i + 2] != '=') out.push_back(static_cast<char>((triple >> 8U) & 0xFFU));
        if (b64[i + 3] != '=') out.push_back(static_cast<char>(triple & 0xFFU));
    }
    return out;
}

std::optional<nlohmann::json> DecodeJwtJsonPart(std::string_view part) {
    auto decoded = Base64UrlDecode(part);
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    auto json = nlohmann::json::parse(*decoded, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return std::nullopt;
    }
    return json;
}

bool ConstantTimeEqual(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    return CRYPTO_memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}

bool MatchAudience(const nlohmann::json& claims, const std::string& expected_audience) {
    auto aud_it = claims.find("aud");
    if (aud_it == claims.end()) {
        return false;
    }
    if (aud_it->is_string()) {
        return aud_it->get<std::string>() == expected_audience;
    }
    if (aud_it->is_array()) {
        for (const auto& item : *aud_it) {
            if (item.is_string() && item.get<std::string>() == expected_audience) {
                return true;
            }
        }
    }
    return false;
}

std::optional<long long> ParseNumericClaim(const nlohmann::json& claims, const std::string& key) {
    auto it = claims.find(key);
    if (it == claims.end()) {
        return std::nullopt;
    }
    if (it->is_number_integer()) {
        return it->get<long long>();
    }
    if (it->is_string()) {
        try {
            return std::stoll(it->get<std::string>());
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ExtractUserId(const nlohmann::json& claims) {
    if (auto it = claims.find("sub"); it != claims.end() && it->is_string() && !it->get<std::string>().empty()) {
        return it->get<std::string>();
    }
    if (auto it = claims.find("user_id"); it != claims.end() && it->is_string() && !it->get<std::string>().empty()) {
        return it->get<std::string>();
    }
    return std::nullopt;
}

}  // namespace

AuthMiddleware::AuthMiddleware(AuthOptions options)
    : options_(std::move(options)) {}

std::optional<common::RequestContext> AuthMiddleware::Authenticate(const std::string& token) const {
    if (token.empty()) {
        KD39_LOG_WARN("empty auth token");
        return std::nullopt;
    }

    auto normalized = token;
    if (normalized.rfind("Bearer ", 0) == 0) {
        normalized = normalized.substr(7);
    }

    common::RequestContext ctx;
    if (options_.allow_legacy_token && normalized == kLegacyDevToken) {
        ctx.user_id = "dev-user";
        return ctx;
    }

    if (options_.allow_legacy_token && normalized.rfind("user:", 0) == 0) {
        ctx.user_id = normalized.substr(5);
        return ctx;
    }

    const auto first_dot = normalized.find('.');
    const auto second_dot = first_dot == std::string::npos ? std::string::npos : normalized.find('.', first_dot + 1);
    if (first_dot == std::string::npos || second_dot == std::string::npos ||
        normalized.find('.', second_dot + 1) != std::string::npos) {
        KD39_LOG_WARN("invalid JWT token format");
        return std::nullopt;
    }

    if (options_.jwt_secret.empty()) {
        KD39_LOG_WARN("JWT validation disabled due to empty jwt_secret");
        return std::nullopt;
    }

    const auto header_part = std::string_view(normalized).substr(0, first_dot);
    const auto payload_part = std::string_view(normalized).substr(first_dot + 1, second_dot - first_dot - 1);
    const auto signature_part = std::string_view(normalized).substr(second_dot + 1);

    auto header_json = DecodeJwtJsonPart(header_part);
    auto claims_json = DecodeJwtJsonPart(payload_part);
    if (!header_json.has_value() || !claims_json.has_value()) {
        KD39_LOG_WARN("failed to decode JWT payload");
        return std::nullopt;
    }

    if ((*header_json).value("alg", std::string()) != kJwtHs256) {
        KD39_LOG_WARN("unsupported JWT alg: {}", (*header_json).value("alg", std::string("unknown")));
        return std::nullopt;
    }

    const auto signing_input = normalized.substr(0, second_dot);
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(),
         options_.jwt_secret.data(),
         static_cast<int>(options_.jwt_secret.size()),
         reinterpret_cast<const unsigned char*>(signing_input.data()),
         signing_input.size(),
         digest.data(),
         &digest_len);

    const auto expected_signature = Base64UrlEncode(digest.data(), digest_len);
    if (!ConstantTimeEqual(signature_part, expected_signature)) {
        KD39_LOG_WARN("JWT signature mismatch");
        return std::nullopt;
    }

    if (auto exp = ParseNumericClaim(*claims_json, "exp"); exp.has_value()) {
        const auto now = static_cast<long long>(std::time(nullptr));
        if (now >= *exp) {
            KD39_LOG_WARN("JWT token expired");
            return std::nullopt;
        }
    }

    if (!options_.jwt_issuer.empty() &&
        (*claims_json).value("iss", std::string()) != options_.jwt_issuer) {
        KD39_LOG_WARN("JWT issuer mismatch");
        return std::nullopt;
    }

    if (!options_.jwt_audience.empty() && !MatchAudience(*claims_json, options_.jwt_audience)) {
        KD39_LOG_WARN("JWT audience mismatch");
        return std::nullopt;
    }

    const auto user_id = ExtractUserId(*claims_json);
    if (!user_id.has_value()) {
        KD39_LOG_WARN("JWT subject missing");
        return std::nullopt;
    }
    ctx.user_id = *user_id;
    return ctx;
}

}  // namespace kd39::gateways::access
