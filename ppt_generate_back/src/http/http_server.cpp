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

#include <nlohmann/json.hpp>

#include "logger.h"
#include "utils/string_utils.h"

namespace {
constexpr std::size_t kMaxRequestSize = 1 * 1024 * 1024;  // 1 MB
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

  HttpResponse response;
  try {
    response = router_.Handle(request);
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Unhandled exception while processing request: ") + ex.what());
    response.status_code = 500;
    response.status_message = "Internal Server Error";
    response.body = nlohmann::json{{"message", "Internal server error"}}.dump();
  }

  SendResponse(client_fd, response);
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
          content_length = static_cast<std::size_t>(std::stoul(content_length_it->second));
          if (content_length > kMaxRequestSize) {
            return false;
          }
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

void HttpServer::SendResponse(int client_fd, const HttpResponse& original) {
  HttpResponse response = original;
  response.ApplyCors();

  if (!response.headers.count("content-type")) {
    response.headers["content-type"] = "application/json";
  }
  response.headers["connection"] = "close";
  response.headers["content-length"] = std::to_string(response.body.size());

  const std::string reason = response.status_message.empty() ? "OK" : response.status_message;

  std::ostringstream stream;
  stream << "HTTP/1.1 " << response.status_code << ' ' << reason << "\r\n";
  for (const auto& [key, value] : response.headers) {
    stream << key << ": " << value << "\r\n";
  }
  stream << "\r\n";
  stream << response.body;

  const auto serialized = stream.str();
  ::send(client_fd, serialized.data(), serialized.size(), 0);
}
