#include "http/http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <variant>

#include <nlohmann/json.hpp>

#include "logger.h"
#include "utils/string_utils.h"

namespace {
constexpr std::size_t kMaxRequestSize = 200 * 1024 * 1024;  // 200 MB (supports large pptx batch uploads)
}

HttpServer::HttpServer(const ServerConfig& config, Router& router)
    : config_(config), router_(router) {}

HttpServer::~HttpServer() { Stop(); }

void HttpServer::Start() {
  if (running_.load()) {
    return;
  }

  thread_pool_ = std::make_unique<ThreadPool>(config_.thread_count);

  server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    throw std::runtime_error("Failed to create socket");
  }

  int opt = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(config_.port);
  if (config_.host == "0.0.0.0" || config_.host == "*") {
    address.sin_addr.s_addr = INADDR_ANY;
  } else {
    if (::inet_pton(AF_INET, config_.host.c_str(), &address.sin_addr) <= 0) {
      ::close(server_fd_);
      throw std::runtime_error("Invalid host address: " + config_.host);
    }
  }

  if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    ::close(server_fd_);
    throw std::runtime_error("Failed to bind socket: " + std::string(strerror(errno)));
  }

  if (::listen(server_fd_, SOMAXCONN) < 0) {
    ::close(server_fd_);
    throw std::runtime_error("Failed to listen on socket");
  }

  running_.store(true);
  accept_thread_ = std::thread(&HttpServer::AcceptLoop, this);
  Logger::Info("HTTP server listening on " + config_.host + ":" + std::to_string(config_.port));
}

void HttpServer::Stop() {
  if (!running_.load()) {
    return;
  }
  running_.store(false);
  if (server_fd_ >= 0) {
    ::shutdown(server_fd_, SHUT_RDWR);
    ::close(server_fd_);
    server_fd_ = -1;
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  thread_pool_.reset();
}

void HttpServer::AcceptLoop() {
  while (running_.load()) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (!running_.load()) {
        break;
      }
      Logger::Warn("Failed to accept connection: " + std::string(strerror(errno)));
      continue;
    }

    thread_pool_->EnqueueDetached([this, client_fd]() { HandleClient(client_fd); });
  }
}

void HttpServer::HandleClient(int client_fd) {
  HttpRequest request;
  if (!ParseRequest(client_fd, request)) {
    HttpResponse response;
    response.status_code = 400;
    response.status_message = "Bad Request";
    response.body = nlohmann::json{{"message", "Invalid HTTP request"}}.dump();
    SendResponse(client_fd, response);
    ::close(client_fd);
    return;
  }

  RouteResult result;
  try {
    result = router_.Handle(request);
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Unhandled exception while processing request: ") + ex.what());
    HttpResponse err;
    err.status_code = 500;
    err.status_message = "Internal Server Error";
    err.body = nlohmann::json{{"message", "Internal server error"}}.dump();
    SendResponse(client_fd, err);
    ::close(client_fd);
    return;
  }

  if (std::holds_alternative<SseResponse>(result)) {
    SendSseResponse(client_fd, std::get<SseResponse>(result));
  } else {
    SendResponse(client_fd, std::get<HttpResponse>(result));
  }
  ::close(client_fd);
}

bool HttpServer::ParseRequest(int client_fd, HttpRequest& request) {
  std::string raw_data;
  raw_data.reserve(4096);
  std::array<char, 4096> buffer{};
  ssize_t bytes_read = 0;
  std::size_t header_end = std::string::npos;
  std::size_t content_length = 0;

  while (true) {
    bytes_read = ::recv(client_fd, buffer.data(), buffer.size(), 0);
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (bytes_read == 0) {
      break;
    }

    raw_data.append(buffer.data(), bytes_read);

    if (raw_data.size() > kMaxRequestSize) {
      return false;
    }

    if (header_end == std::string::npos) {
      static const std::string delimiter = "\r\n\r\n";
      header_end = raw_data.find(delimiter);
      if (header_end != std::string::npos) {
        const auto header_section = raw_data.substr(0, header_end);
        std::istringstream header_stream(header_section);
        std::string request_line;
        if (!std::getline(header_stream, request_line)) {
          return false;
        }
        if (!request_line.empty() && request_line.back() == '\r') {
          request_line.pop_back();
        }

        std::istringstream request_line_stream(request_line);
        request_line_stream >> request.method >> request.target;
        std::string http_version;
        request_line_stream >> http_version;
        if (request.method.empty() || request.target.empty()) {
          return false;
        }
        request.path = request.target;

        const auto query_sep = request.target.find('?');
        if (query_sep != std::string::npos) {
          request.path = request.target.substr(0, query_sep);
          const auto query_string = request.target.substr(query_sep + 1);
          request.query_params = string_utils::ParseQuery(query_string);
        }

        std::string header_line;
        while (std::getline(header_stream, header_line)) {
          if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
          }
          if (header_line.empty()) {
            break;
          }
          const auto colon_pos = header_line.find(':');
          if (colon_pos == std::string::npos) {
            continue;
          }
          const auto key = string_utils::ToLower(string_utils::Trim(header_line.substr(0, colon_pos)));
          const auto value = string_utils::Trim(header_line.substr(colon_pos + 1));
          request.headers[key] = value;
        }

        const auto content_length_it = request.headers.find("content-length");
        if (content_length_it != request.headers.end()) {
          try {
            content_length = static_cast<std::size_t>(std::stoul(content_length_it->second));
          } catch (...) {
            content_length = 0;
          }
          if (content_length > kMaxRequestSize) {
            return false;
          }
        }
        const std::string method_lower = string_utils::ToLower(request.method);
        if (method_lower == "get" || method_lower == "head") {
          content_length = 0;
        }
      }
    }

    if (header_end != std::string::npos) {
      static const std::string delimiter = "\r\n\r\n";
      const auto total_needed = header_end + delimiter.size() + content_length;
      if (raw_data.size() >= total_needed) {
        break;
      }
    }
  }

  if (raw_data.empty()) {
    return false;
  }

  if (header_end == std::string::npos) {
    return false;
  }

  static const std::string delimiter = "\r\n\r\n";
  const auto body_start = header_end + delimiter.size();
  if (raw_data.size() >= body_start) {
    request.body = raw_data.substr(body_start, content_length);
  }

  return true;
}

namespace {
// Send all bytes in buf; returns false if the connection was lost mid-send.
bool SendAll(int fd, const char* buf, std::size_t len) {
  std::size_t sent = 0;
  while (sent < len) {
    const ssize_t n = ::send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
    if (n <= 0) return false;
    sent += static_cast<std::size_t>(n);
  }
  return true;
}
}  // namespace

void HttpServer::SendSseResponse(int client_fd, const SseResponse& sse) {
  // Build SSE-specific HTTP headers (no Content-Length; keep connection alive for streaming)
  std::ostringstream hdr;
  hdr << "HTTP/1.1 " << sse.status_code << " OK\r\n"
      << "content-type: text/event-stream; charset=utf-8\r\n"
      << "cache-control: no-cache\r\n"
      << "connection: close\r\n"
      << "access-control-allow-origin: *\r\n"
      << "access-control-allow-headers: Content-Type, Authorization, ngrok-skip-browser-warning\r\n"
      << "access-control-allow-methods: GET, POST, DELETE, OPTIONS, HEAD\r\n"
      << "access-control-allow-private-network: true\r\n"
      << "x-accel-buffering: no\r\n"   // disable nginx buffering if behind proxy
      << "\r\n";

  const auto hdr_str = hdr.str();
  if (!SendAll(client_fd, hdr_str.data(), hdr_str.size())) return;

  // Provide write callback to stream_fn
  auto write_fn = [client_fd](const std::string& chunk) -> bool {
    return SendAll(client_fd, chunk.data(), chunk.size());
  };

  if (sse.stream_fn) {
    try {
      sse.stream_fn(write_fn);
    } catch (const std::exception& ex) {
      Logger::Warn(std::string("SSE stream_fn threw: ") + ex.what());
    }
  }
}

void HttpServer::SendResponse(int client_fd, const HttpResponse& original) {
  HttpResponse response = original;
  response.ApplyCors();

  if (!response.headers.count("content-type")) {
    response.headers["content-type"] = "application/json";
  }
  response.headers["connection"] = "close";
  if (!response.headers.count("content-length")) {
    response.headers["content-length"] = std::to_string(response.body.size());
  }

  const std::string reason = response.status_message.empty() ? "OK" : response.status_message;

  // Build only the header portion (avoids copying potentially-large body)
  std::ostringstream stream;
  stream << "HTTP/1.1 " << response.status_code << ' ' << reason << "\r\n";
  for (const auto& [key, value] : response.headers) {
    stream << key << ": " << value << "\r\n";
  }
  stream << "\r\n";

  const auto header_str = stream.str();

  // Send header then body separately to handle large binary payloads correctly
  if (!SendAll(client_fd, header_str.data(), header_str.size())) return;
  if (!response.body.empty()) {
    SendAll(client_fd, response.body.data(), response.body.size());
  }
}
